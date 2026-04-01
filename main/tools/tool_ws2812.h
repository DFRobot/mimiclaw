#ifndef _TOOL_WS2812_H_
#define _TOOL_WS2812_H_

#include "esp_err.h"
#include <stddef.h>

esp_err_t tool_ws2812_init(void);

esp_err_t tool_ws2812_init_execute(const char *input_json, char *output, size_t output_size);

esp_err_t tool_ws2812_set_pixel_execute(const char *input_json, char *output, size_t output_size);

esp_err_t tool_ws2812_set_all_execute(const char *input_json, char *output, size_t output_size);

esp_err_t tool_ws2812_flush_execute(const char *input_json, char *output, size_t output_size);

esp_err_t tool_ws2812_clear_execute(const char *input_json, char *output, size_t output_size);

#endif
