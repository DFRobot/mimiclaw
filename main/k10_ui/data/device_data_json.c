#include "device_data_json.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "device_data_json";

static device_data_update_callback_t s_update_callback = NULL;

void device_data_json_init(void)
{
    ESP_LOGI(TAG, "Device data JSON parser initialized");
}

void device_data_json_register_update_callback(device_data_update_callback_t callback)
{
    s_update_callback = callback;
}

esp_err_t device_data_json_parse_and_update(const char *json_str)
{
    if (json_str == NULL) {
        ESP_LOGE(TAG, "JSON string is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        return ESP_FAIL;
    }

    esp_err_t ret = device_data_json_parse_and_update_cJSON(root);
    cJSON_Delete(root);

    return ret;
}

static esp_err_t process_single_agent(cJSON *agent_obj)
{
    if (agent_obj == NULL || !cJSON_IsObject(agent_obj)) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *name = cJSON_GetObjectItem(agent_obj, "name");
    if (name == NULL || !cJSON_IsString(name)) {
        name = cJSON_GetObjectItem(agent_obj, "agent");
    }

    cJSON *status = cJSON_GetObjectItem(agent_obj, "status");
    cJSON *message = cJSON_GetObjectItem(agent_obj, "message");

    if (name == NULL || !cJSON_IsString(name)) {
        ESP_LOGW(TAG, "Missing or invalid 'name'/'agent' field");
        return ESP_ERR_INVALID_ARG;
    }

    const char *name_str = name->valuestring;
    int agent_idx = device_data_find_agent(name_str);

    if (agent_idx < 0) {
        agent_idx = device_data_add_agent(AGENT_MAX, name_str, ALONG_TO_ONLINE, TYPE_IDLE);
    }

    if (agent_idx < 0) {
        ESP_LOGW(TAG, "Cannot find or add agent: %s", name_str);
        return ESP_ERR_NOT_FOUND;
    }

    AI_STATE_E state = TYPE_WORKING;
    const char *custom_message = NULL;

    if (status != NULL && cJSON_IsString(status)) {
        state = device_data_map_state(status->valuestring);
    }

    if (message != NULL && cJSON_IsString(message)) {
        custom_message = message->valuestring;
    }

    agent_info_t *agent = device_data_get_agent_by_index(agent_idx);
    if (agent == NULL) {
        ESP_LOGE(TAG, "Agent index %d is NULL", agent_idx);
        return ESP_FAIL;
    }

    device_data_lock();
    agent->state = state;
    if (custom_message != NULL && strlen(custom_message) > 0) {
        strncpy(agent->custom_message, custom_message, MAX_CUSTOM_MESSAGE - 1);
        agent->custom_message[MAX_CUSTOM_MESSAGE - 1] = '\0';
        agent->has_custom_message = true;
    } else {
        agent->has_custom_message = false;
        agent->custom_message[0] = '\0';
    }
    device_data_unlock();

    ESP_LOGI(TAG, "Updated agent %s: state=%d, message=%s",
             name_str, state, custom_message ? custom_message : "default");

    if (s_update_callback != NULL) {
        s_update_callback(agent->id);
    }

    return ESP_OK;
}

static esp_err_t process_single_channel(cJSON *channel_obj)
{
    if (channel_obj == NULL || !cJSON_IsObject(channel_obj)) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *name = cJSON_GetObjectItem(channel_obj, "name");
    cJSON *status = cJSON_GetObjectItem(channel_obj, "status");

    if (name == NULL || !cJSON_IsString(name)) {
        ESP_LOGW(TAG, "Missing or invalid 'name' field for channel");
        return ESP_ERR_INVALID_ARG;
    }

    const char *name_str = name->valuestring;
    int ch_idx = device_data_find_channel(name_str);

    if (ch_idx < 0) {
        ch_idx = device_data_add_channel(CHANNEL_MAX, name_str, ALONG_TO_ONLINE);
    }

    if (ch_idx < 0) {
        ESP_LOGW(TAG, "Cannot find or add channel: %s", name_str);
        return ESP_ERR_NOT_FOUND;
    }

    CHANNEL_STATE_E state = CHANNEL_STATE_OFFLINE;

    if (status != NULL && cJSON_IsString(status)) {
        state = device_data_map_channel_state(status->valuestring);
    }

    device_data_update_channel_state(ch_idx, state);

    ESP_LOGI(TAG, "Updated channel %s: state=%d", name_str, state);

    return ESP_OK;
}

esp_err_t device_data_json_parse_and_update_cJSON(cJSON *root)
{
    if (root == NULL) {
        ESP_LOGE(TAG, "cJSON root is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    bool processed = false;

    cJSON *agents_array = cJSON_GetObjectItem(root, "agents");
    if (agents_array != NULL && cJSON_IsArray(agents_array)) {
        int agent_count = cJSON_GetArraySize(agents_array);
        ESP_LOGI(TAG, "Processing %d agents (array mode)", agent_count);

        for (int i = 0; i < agent_count; i++) {
            cJSON *agent_obj = cJSON_GetArrayItem(agents_array, i);
            process_single_agent(agent_obj);
        }

        processed = true;
    }

    cJSON *channels_array = cJSON_GetObjectItem(root, "channels");
    if (channels_array != NULL && cJSON_IsArray(channels_array)) {
        int channel_count = cJSON_GetArraySize(channels_array);
        ESP_LOGI(TAG, "Processing %d channels (array mode)", channel_count);

        for (int i = 0; i < channel_count; i++) {
            cJSON *channel_obj = cJSON_GetArrayItem(channels_array, i);
            process_single_channel(channel_obj);
        }

        processed = true;
    }

    if (processed) {
        return ESP_OK;
    }

    cJSON *name = cJSON_GetObjectItem(root, "name");
    cJSON *agent = cJSON_GetObjectItem(root, "agent");

    if (name != NULL || agent != NULL) {
        ESP_LOGI(TAG, "Processing single agent (object mode)");
        return process_single_agent(root);
    }

    ESP_LOGE(TAG, "Invalid JSON: expected 'agents'/'channels' array or single agent object");
    return ESP_ERR_INVALID_ARG;
}
