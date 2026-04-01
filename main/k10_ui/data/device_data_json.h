#ifndef __DEVICE_DATA_JSON_H__
#define __DEVICE_DATA_JSON_H__

#include "device_data.h"
#include <stdbool.h>
#include "cJSON.h"



typedef void (*device_data_update_callback_t)(AGENT_E agent_id);

void device_data_json_init(void);

esp_err_t device_data_json_parse_and_update(const char *json_str);

esp_err_t device_data_json_parse_and_update_cJSON(cJSON *root);

void device_data_json_register_update_callback(device_data_update_callback_t callback);

#endif // __DEVICE_DATA_JSON_H__
