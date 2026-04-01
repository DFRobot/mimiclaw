#ifndef _CIRCULAR_STRIP_H_
#define _CIRCULAR_STRIP_H_

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "stdint.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <driver/gpio.h>
#include <esp_timer.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum
{
    kDeviceStateUnknown = 0,
    kDeviceStateStarting,
    kDeviceStateWifiConfiguring,
    kDeviceStateIdle,
    kDeviceStateConnecting,
    kDeviceStateListening,
    kDeviceStateSpeaking,
    kDeviceStateUpgrading,
    kDeviceStateActivating,
    kDeviceStateAudioTesting,
    kDeviceStateFatalError
} DeviceState;

#define DEFAULT_BRIGHTNESS 32
#define LOW_BRIGHTNESS 4
#define WS2812_PIN GPIO_NUM_46

typedef struct
{
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} strip_color_t;

/* 不透明句柄 */
typedef struct circular_strip_s circular_strip_t;

circular_strip_t *circular_strip_create(gpio_num_t gpio, uint16_t max_leds);
void circular_strip_destroy(circular_strip_t *strip);

void circular_strip_set_brightness(circular_strip_t *strip, uint8_t default_brightness, uint8_t low_brightness);
void circular_strip_set_all_color(circular_strip_t *strip, strip_color_t color);
void circular_strip_set_single_color(circular_strip_t *strip, uint8_t index, strip_color_t color);
void circular_strip_set_multi_colors(circular_strip_t *strip, const strip_color_t *colors, int count);
void circular_strip_blink(circular_strip_t *strip, strip_color_t color, int interval_ms);
void circular_strip_breathe(circular_strip_t *strip, strip_color_t low, strip_color_t high, int interval_ms);
void circular_strip_scroll(circular_strip_t *strip, strip_color_t low, strip_color_t high, int length, int interval_ms);
void circular_strip_fade_out(circular_strip_t *strip, int interval_ms);

/*
 * device_state 取值与 DeviceState 枚举一一对应：
 *   0=Unknown  1=Starting     2=WifiConfiguring  3=Idle
 *   4=Connecting  5=Listening  6=Speaking  7=Upgrading
 *   8=Activating  9=AudioTesting
 */
void circular_strip_on_state_changed(circular_strip_t *strip, int device_state);

#endif /* _CIRCULAR_STRIP_H_ */
