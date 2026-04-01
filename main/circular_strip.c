#include "circular_strip.h"
#include "led_strip.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_log.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define TAG "CircularStrip"
typedef enum
{
    ANIM_NONE = 0,
    ANIM_BLINK,
    ANIM_FADE_OUT,
    ANIM_BREATHE,
    ANIM_SCROLL,
} anim_type_t;

struct circular_strip_s
{
    led_strip_handle_t led_strip;
    SemaphoreHandle_t mutex;
    esp_timer_handle_t timer;
    uint16_t max_leds;
    strip_color_t *colors;
    uint8_t default_brightness;
    uint8_t low_brightness;
    anim_type_t anim_type;

    /* Blink 动画状态 */
    bool blink_on;

    /* Breathe 动画状态 */
    bool breathe_increase;
    strip_color_t breathe_color;
    strip_color_t breathe_low;
    strip_color_t breathe_high;

    /* Scroll 动画状态 */
    int scroll_offset;
    strip_color_t scroll_low;
    strip_color_t scroll_high;
    int scroll_length;
};

/* ------------------------------------------------------------------ */
/* 动画执行函数（调用者必须持有 mutex）                               */
/* ------------------------------------------------------------------ */

static void do_blink(circular_strip_t *s)
{
    if (s->blink_on)
    {
        for (int i = 0; i < s->max_leds; i++)
        {
            led_strip_set_pixel(s->led_strip, i,
                                s->colors[i].red, s->colors[i].green, s->colors[i].blue);
        }
        led_strip_refresh(s->led_strip);
    }
    else
    {
        led_strip_clear(s->led_strip);
    }
    s->blink_on = !s->blink_on;
}

static void do_fade_out(circular_strip_t *s)
{
    bool all_off = true;
    for (int i = 0; i < s->max_leds; i++)
    {
        s->colors[i].red /= 2;
        s->colors[i].green /= 2;
        s->colors[i].blue /= 2;
        if (s->colors[i].red != 0 || s->colors[i].green != 0 || s->colors[i].blue != 0)
        {
            all_off = false;
        }
        led_strip_set_pixel(s->led_strip, i,
                            s->colors[i].red, s->colors[i].green, s->colors[i].blue);
    }
    if (all_off)
    {
        led_strip_clear(s->led_strip);
        esp_timer_stop(s->timer);
    }
    else
    {
        led_strip_refresh(s->led_strip);
    }
}

static void do_breathe(circular_strip_t *s)
{
    strip_color_t *c = &s->breathe_color;
    strip_color_t *lo = &s->breathe_low;
    strip_color_t *hi = &s->breathe_high;

    if (s->breathe_increase)
    {
        if (c->red < hi->red)
            c->red++;
        if (c->green < hi->green)
            c->green++;
        if (c->blue < hi->blue)
            c->blue++;
        if (c->red == hi->red && c->green == hi->green && c->blue == hi->blue)
        {
            s->breathe_increase = false;
        }
    }
    else
    {
        if (c->red > lo->red)
            c->red--;
        if (c->green > lo->green)
            c->green--;
        if (c->blue > lo->blue)
            c->blue--;
        if (c->red == lo->red && c->green == lo->green && c->blue == lo->blue)
        {
            s->breathe_increase = true;
        }
    }
    for (int i = 0; i < s->max_leds; i++)
    {
        led_strip_set_pixel(s->led_strip, i, c->red, c->green, c->blue);
    }
    led_strip_refresh(s->led_strip);
}

static void do_scroll(circular_strip_t *s)
{
    for (int i = 0; i < s->max_leds; i++)
    {
        s->colors[i] = s->scroll_low;
    }
    for (int j = 0; j < s->scroll_length; j++)
    {
        int i = (s->scroll_offset + j) % s->max_leds;
        s->colors[i] = s->scroll_high;
    }
    for (int i = 0; i < s->max_leds; i++)
    {
        led_strip_set_pixel(s->led_strip, i,
                            s->colors[i].red, s->colors[i].green, s->colors[i].blue);
    }
    led_strip_refresh(s->led_strip);
    s->scroll_offset = (s->scroll_offset + 1) % s->max_leds;
}

/* ------------------------------------------------------------------ */
/* 定时器回调                                                         */
/* ------------------------------------------------------------------ */

static void strip_timer_callback(void *arg)
{
    circular_strip_t *s = (circular_strip_t *)arg;
    xSemaphoreTake(s->mutex, portMAX_DELAY);
    switch (s->anim_type)
    {
    case ANIM_BLINK:
        do_blink(s);
        break;
    case ANIM_FADE_OUT:
        do_fade_out(s);
        break;
    case ANIM_BREATHE:
        do_breathe(s);
        break;
    case ANIM_SCROLL:
        do_scroll(s);
        break;
    default:
        break;
    }
    xSemaphoreGive(s->mutex);
}

/* 启动定时动画（调用者必须持有 mutex）*/
static void start_animation_locked(circular_strip_t *s, anim_type_t type, int interval_ms)
{
    esp_timer_stop(s->timer);
    s->anim_type = type;
    esp_timer_start_periodic(s->timer, (uint64_t)interval_ms * 1000);
}

/* ------------------------------------------------------------------ */
/* 公开 API                                                           */
/* ------------------------------------------------------------------ */

circular_strip_t *circular_strip_create(gpio_num_t gpio, uint16_t max_leds)
{
    assert(gpio != GPIO_NUM_NC);

    circular_strip_t *s = (circular_strip_t *)calloc(1, sizeof(circular_strip_t));
    if (!s)
        return NULL;

    s->max_leds = max_leds;
    s->default_brightness = DEFAULT_BRIGHTNESS;
    s->low_brightness = LOW_BRIGHTNESS;
    s->blink_on = true;
    s->breathe_increase = true;

    s->colors = (strip_color_t *)calloc(max_leds, sizeof(strip_color_t));
    if (!s->colors)
        goto err_colors;

    s->mutex = xSemaphoreCreateMutex();
    if (!s->mutex)
        goto err_mutex;

    led_strip_config_t strip_cfg = {
        .strip_gpio_num = gpio,
        .max_leds = max_leds,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .led_model = LED_MODEL_WS2812,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .resolution_hz = 10 * 1000 * 1000,
    };
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s->led_strip));
    led_strip_clear(s->led_strip);

    esp_timer_create_args_t timer_args = {
        .callback = strip_timer_callback,
        .arg = s,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "strip_timer",
        .skip_unhandled_events = false,
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s->timer));

    return s;

err_mutex:
    free(s->colors);
err_colors:
    free(s);
    return NULL;
}

void circular_strip_destroy(circular_strip_t *s)
{
    if (!s)
        return;
    esp_timer_stop(s->timer);
    esp_timer_delete(s->timer);
    if (s->led_strip)
    {
        led_strip_del(s->led_strip);
    }
    vSemaphoreDelete(s->mutex);
    free(s->colors);
    free(s);
}

void circular_strip_set_brightness(circular_strip_t *s, uint8_t default_brightness, uint8_t low_brightness)
{
    s->default_brightness = default_brightness;
    s->low_brightness = low_brightness;
    /* 调用方负责在设置亮度后调用 circular_strip_on_state_changed 以刷新显示 */
}

void circular_strip_set_all_color(circular_strip_t *s, strip_color_t color)
{
    xSemaphoreTake(s->mutex, portMAX_DELAY);
    esp_timer_stop(s->timer);
    s->anim_type = ANIM_NONE;
    for (int i = 0; i < s->max_leds; i++)
    {
        s->colors[i] = color;
        led_strip_set_pixel(s->led_strip, i, color.red, color.green, color.blue);
    }
    led_strip_refresh(s->led_strip);
    xSemaphoreGive(s->mutex);
}

void circular_strip_set_single_color(circular_strip_t *s, uint8_t index, strip_color_t color)
{
    xSemaphoreTake(s->mutex, portMAX_DELAY);
    esp_timer_stop(s->timer);
    s->anim_type = ANIM_NONE;
    s->colors[index] = color;
    led_strip_set_pixel(s->led_strip, index, color.red, color.green, color.blue);
    led_strip_refresh(s->led_strip);
    xSemaphoreGive(s->mutex);
}

void circular_strip_set_multi_colors(circular_strip_t *s, const strip_color_t *colors, int count)
{
    xSemaphoreTake(s->mutex, portMAX_DELAY);
    esp_timer_stop(s->timer);
    s->anim_type = ANIM_NONE;
    int n = count < (int)s->max_leds ? count : (int)s->max_leds;
    for (int i = 0; i < n; i++)
    {
        s->colors[i] = colors[i];
        led_strip_set_pixel(s->led_strip, i, colors[i].red, colors[i].green, colors[i].blue);
    }
    led_strip_refresh(s->led_strip);
    xSemaphoreGive(s->mutex);
}

void circular_strip_blink(circular_strip_t *s, strip_color_t color, int interval_ms)
{
    xSemaphoreTake(s->mutex, portMAX_DELAY);
    for (int i = 0; i < s->max_leds; i++)
    {
        s->colors[i] = color;
    }
    s->blink_on = true;
    start_animation_locked(s, ANIM_BLINK, interval_ms);
    xSemaphoreGive(s->mutex);
}

void circular_strip_fade_out(circular_strip_t *s, int interval_ms)
{
    xSemaphoreTake(s->mutex, portMAX_DELAY);
    start_animation_locked(s, ANIM_FADE_OUT, interval_ms);
    xSemaphoreGive(s->mutex);
}

void circular_strip_breathe(circular_strip_t *s, strip_color_t low, strip_color_t high, int interval_ms)
{
    xSemaphoreTake(s->mutex, portMAX_DELAY);
    s->breathe_low = low;
    s->breathe_high = high;
    s->breathe_color = low;
    s->breathe_increase = true;
    start_animation_locked(s, ANIM_BREATHE, interval_ms);
    xSemaphoreGive(s->mutex);
}

void circular_strip_scroll(circular_strip_t *s, strip_color_t low, strip_color_t high, int length, int interval_ms)
{
    xSemaphoreTake(s->mutex, portMAX_DELAY);
    s->scroll_low = low;
    s->scroll_high = high;
    s->scroll_length = length;
    s->scroll_offset = 0;
    for (int i = 0; i < s->max_leds; i++)
    {
        s->colors[i] = low;
    }
    start_animation_locked(s, ANIM_SCROLL, interval_ms);
    xSemaphoreGive(s->mutex);
}

void circular_strip_on_state_changed(circular_strip_t *s, int device_state)
{
    uint8_t def = s->default_brightness;
    uint8_t low = s->low_brightness;

    switch ((DeviceState)device_state)
    {
    case kDeviceStateStarting:
    {
        strip_color_t lo = {0, 0, 0};
        strip_color_t hi = {low, low, def};
        circular_strip_scroll(s, lo, hi, 3, 100);
        break;
    }
    case kDeviceStateWifiConfiguring:
    {
        strip_color_t color = {low, low, def};
        circular_strip_blink(s, color, 500);
        break;
    }
    case kDeviceStateIdle:
        circular_strip_fade_out(s, 50);
        break;
    case kDeviceStateConnecting:
    {
        strip_color_t color = {low, low, def};
        circular_strip_set_all_color(s, color);
        break;
    }
    case kDeviceStateListening:
    case kDeviceStateAudioTesting:
    {
        strip_color_t color = {def, low, low};
        circular_strip_set_all_color(s, color);
        break;
    }
    case kDeviceStateSpeaking:
    {
        strip_color_t color = {low, def, low};
        circular_strip_set_all_color(s, color);
        break;
    }
    case kDeviceStateUpgrading:
    {
        strip_color_t color = {low, def, low};
        circular_strip_blink(s, color, 100);
        break;
    }
    case kDeviceStateActivating:
    {
        strip_color_t color = {low, def, low};
        circular_strip_blink(s, color, 500);
        break;
    }
    default:
        ESP_LOGW(TAG, "Unknown led strip state: %d", device_state);
        break;
    }
}
