#ifndef __TOOL_NOTICE_H__
#define __TOOL_NOTICE_H__

#include "esp_err.h"

esp_err_t tool_notice_init(void);

esp_err_t tool_notice_add_execute(const char *input_json, char *output, size_t output_size);

esp_err_t tool_notice_clear_execute(const char *input_json, char *output, size_t output_size);

esp_err_t tool_notice_del_execute(const char *input_json, char *output, size_t output_size);

#endif // __TOOL_NOTICE_H__
