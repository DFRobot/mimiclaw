#include "tools/tool_ws2812.h"
#include "circular_strip.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "tool_ws2812";

static circular_strip_t *s_strip = NULL;

esp_err_t tool_ws2812_init(void)
{
    ESP_LOGI(TAG, "WS2812 tool initialized");
    return ESP_OK;
}

esp_err_t tool_ws2812_init_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *gpio_obj = cJSON_GetObjectItem(root, "gpio");
    cJSON *led_count_obj = cJSON_GetObjectItem(root, "led_count");

    int gpio = 46;
    int led_count = 3;

    if (gpio_obj && cJSON_IsNumber(gpio_obj)) {
        gpio = (int)gpio_obj->valuedouble;
    }
    if (led_count_obj && cJSON_IsNumber(led_count_obj)) {
        led_count = (int)led_count_obj->valuedouble;
    }

    if (s_strip != NULL) {
        circular_strip_destroy(s_strip);
        s_strip = NULL;
    }

    s_strip = circular_strip_create((gpio_num_t)gpio, (uint16_t)led_count);
    if (!s_strip) {
        snprintf(output, output_size, "Error: failed to create WS2812 strip");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    snprintf(output, output_size, "OK: WS2812 initialized on GPIO %d with %d LEDs", gpio, led_count);
    ESP_LOGI(TAG, "ws2812_init: GPIO %d, LED count %d", gpio, led_count);
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t tool_ws2812_set_pixel_execute(const char *input_json, char *output, size_t output_size)
{
    if (s_strip == NULL) {
        snprintf(output, output_size, "Error: WS2812 not initialized. Call ws2812_init first");
        return ESP_ERR_INVALID_STATE;
    }

    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *index_obj = cJSON_GetObjectItem(root, "index");
    cJSON *r_obj = cJSON_GetObjectItem(root, "r");
    cJSON *g_obj = cJSON_GetObjectItem(root, "g");
    cJSON *b_obj = cJSON_GetObjectItem(root, "b");

    if (!cJSON_IsNumber(index_obj)) {
        snprintf(output, output_size, "Error: 'index' required (integer)");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    if (!cJSON_IsNumber(r_obj) || !cJSON_IsNumber(g_obj) || !cJSON_IsNumber(b_obj)) {
        snprintf(output, output_size, "Error: 'r', 'g', 'b' required (integers 0-255)");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    int index = (int)index_obj->valuedouble;
    uint8_t r = (uint8_t)r_obj->valuedouble;
    uint8_t g = (uint8_t)g_obj->valuedouble;
    uint8_t b = (uint8_t)b_obj->valuedouble;

    strip_color_t color = {r, g, b};
    circular_strip_set_single_color(s_strip, (uint8_t)index, color);

    snprintf(output, output_size, "OK: set pixel %d to RGB(%d, %d, %d)", index, r, g, b);
    ESP_LOGI(TAG, "ws2812_set_pixel: index=%d, RGB(%d, %d, %d)", index, r, g, b);
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t tool_ws2812_set_all_execute(const char *input_json, char *output, size_t output_size)
{
    if (s_strip == NULL) {
        snprintf(output, output_size, "Error: WS2812 not initialized. Call ws2812_init first");
        return ESP_ERR_INVALID_STATE;
    }

    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *r_obj = cJSON_GetObjectItem(root, "r");
    cJSON *g_obj = cJSON_GetObjectItem(root, "g");
    cJSON *b_obj = cJSON_GetObjectItem(root, "b");

    if (!cJSON_IsNumber(r_obj) || !cJSON_IsNumber(g_obj) || !cJSON_IsNumber(b_obj)) {
        snprintf(output, output_size, "Error: 'r', 'g', 'b' required (integers 0-255)");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t r = (uint8_t)r_obj->valuedouble;
    uint8_t g = (uint8_t)g_obj->valuedouble;
    uint8_t b = (uint8_t)b_obj->valuedouble;

    strip_color_t color = {r, g, b};
    circular_strip_set_all_color(s_strip, color);

    snprintf(output, output_size, "OK: set all pixels to RGB(%d, %d, %d)", r, g, b);
    ESP_LOGI(TAG, "ws2812_set_all: RGB(%d, %d, %d)", r, g, b);
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t tool_ws2812_flush_execute(const char *input_json, char *output, size_t output_size)
{
    (void)input_json;

    if (s_strip == NULL) {
        snprintf(output, output_size, "Error: WS2812 not initialized. Call ws2812_init first");
        return ESP_ERR_INVALID_STATE;
    }

    snprintf(output, output_size, "OK: flush called (note: colors are applied immediately in this implementation)");
    ESP_LOGI(TAG, "ws2812_flush called");
    return ESP_OK;
}

esp_err_t tool_ws2812_clear_execute(const char *input_json, char *output, size_t output_size)
{
    (void)input_json;

    if (s_strip == NULL) {
        snprintf(output, output_size, "Error: WS2812 not initialized. Call ws2812_init first");
        return ESP_ERR_INVALID_STATE;
    }

    strip_color_t black = {0, 0, 0};
    circular_strip_set_all_color(s_strip, black);

    snprintf(output, output_size, "OK: cleared all LEDs");
    ESP_LOGI(TAG, "ws2812_clear called");
    return ESP_OK;
}
