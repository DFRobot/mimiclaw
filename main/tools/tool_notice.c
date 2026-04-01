#include "tool_notice.h"
#include "k10_ui/data/device_data.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "tool_notice";

esp_err_t tool_notice_init(void)
{
    ESP_LOGI(TAG, "Notice tool initialized");
    return ESP_OK;
}

esp_err_t tool_notice_add_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *message_obj = cJSON_GetObjectItem(root, "message");
    if (!cJSON_IsString(message_obj)) {
        snprintf(output, output_size, "Error: 'message' required (string)");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    const char *message = message_obj->valuestring;
    int id = device_data_add_notice(message);
    
    if (id < 0) {
        snprintf(output, output_size, "Error: failed to add notice message");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    snprintf(output, output_size, "OK: added notice message (id: %d)", id);
    ESP_LOGI(TAG, "Added notice message: %s (id: %d)", message, id);
    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t tool_notice_clear_execute(const char *input_json, char *output, size_t output_size)
{
    (void)input_json;
    
    device_data_clear_notices();
    
    snprintf(output, output_size, "OK: cleared all notice messages");
    ESP_LOGI(TAG, "Cleared all notice messages");
    return ESP_OK;
}

esp_err_t tool_notice_del_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *index_obj = cJSON_GetObjectItem(root, "index");
    if (!cJSON_IsNumber(index_obj)) {
        snprintf(output, output_size, "Error: 'index' required (integer)");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    int index = (int)index_obj->valuedouble;
    
    if (index < 1 || index > MAX_NOTICE_MESSAGES) {
        snprintf(output, output_size, "Error: invalid position %d (must be 1-%d)", index, MAX_NOTICE_MESSAGES);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    
    int array_index = index - 1;
    esp_err_t ret = device_data_delete_notice(array_index);
    
    if (ret != ESP_OK) {
        snprintf(output, output_size, "Error: failed to delete notice message at position %d", index);
        cJSON_Delete(root);
        return ret;
    }

    snprintf(output, output_size, "OK: deleted notice message at position %d", index);
    ESP_LOGI(TAG, "Deleted notice message at position %d", index);
    cJSON_Delete(root);
    return ESP_OK;
}
