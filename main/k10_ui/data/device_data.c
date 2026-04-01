#include "device_data.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "page_conf.h"

static const char *TAG = "device_data";

static device_data_t s_device_data = {0};
static SemaphoreHandle_t s_data_mutex = NULL;

static const char* state_messages[] = {
    [TYPE_WORKING] = "工作中...",
    [TYPE_WAITING] = "等待中...",
    [TYPE_RESEARCHING] = "调研中...",
    [TYPE_ERROR] = "异常",
    [TYPE_IDLE] = "空闲中...",
    [TYPE_OFFLINE] = "离线!"
};

static const char* state_image_paths[] = {
    [TYPE_WORKING] = "/sdcard/img/work.jpg",
    [TYPE_WAITING] = "/sdcard/img/wait.jpg",
    [TYPE_RESEARCHING] = "/sdcard/img/research.jpg",
    [TYPE_ERROR] = "/sdcard/img/error.jpg",
    [TYPE_IDLE] = "/sdcard/img/idle.jpg",
    [TYPE_OFFLINE] = "/sdcard/img/offline.jpg",
};

static const char* channel_state_messages[] = {
    [CHANNEL_STATE_CONNECTED] = "已连接",
    [CHANNEL_STATE_OFFLINE] = "离线",
};

static const char* channel_state_image_paths[] = {
    [CHANNEL_STATE_CONNECTED] = "/sdcard/img/connect.jpg",
    [CHANNEL_STATE_OFFLINE] = "/sdcard/img/offline.jpg",
};

static const char* get_state_message(AI_STATE_E state)
{
    if (state >= 0 && state < sizeof(state_messages) / sizeof(state_messages[0])) {
        return state_messages[state];
    }
    return state_messages[TYPE_ERROR];
}

static const char* get_state_image_path(AI_STATE_E state)
{
    if (state >= 0 && state < sizeof(state_image_paths) / sizeof(state_image_paths[0])) {
        return state_image_paths[state];
    }
    return state_image_paths[TYPE_ERROR];
}

void device_data_init(void)
{
    memset(&s_device_data, 0, sizeof(s_device_data));

    s_data_mutex = xSemaphoreCreateMutex();
    if (s_data_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return;
    }

    s_device_data.agent_count = 0;

    s_device_data.channel_count = 0;

    s_device_data.notice_count = 0;
    for (int i = 0; i < MAX_NOTICE_MESSAGES; i++) {
        s_device_data.notice_messages[i].id = 0;
        s_device_data.notice_messages[i].message[0] = '\0';
    }
}

agent_info_t* device_data_get_agent_by_index(int index)
{
    if (index < 0 || index >= s_device_data.agent_count) {
        return NULL;
    }
    return &s_device_data.agents[index];
}

int device_data_get_agent_count(void)
{
    return s_device_data.agent_count;
}

int device_data_find_agent(const char *name)
{
    if (name == NULL) return -1;

    for (int i = 0; i < s_device_data.agent_count; i++) {
        if (strcmp(s_device_data.agents[i].name, name) == 0) {
            return i;
        }
    }

    return -1;
}

int device_data_add_agent(AGENT_E id, const char *name, uint16_t belongs, AI_STATE_E state)
{
    if (name == NULL) return -1;

    int existing = device_data_find_agent(name);
    if (existing >= 0) return existing;

    if (s_device_data.agent_count >= MAX_AGENTS) {
        ESP_LOGE(TAG, "Agent list full, cannot add: %s", name);
        return -1;
    }

    device_data_lock();
    int idx = s_device_data.agent_count;
    agent_info_t *agent = &s_device_data.agents[idx];
    agent->id = id;
    agent->belongs = belongs;
    strncpy(agent->name, name, MAX_AGENT_NAME - 1);
    agent->name[MAX_AGENT_NAME - 1] = '\0';
    agent->state = state;
    agent->has_custom_message = false;
    agent->custom_message[0] = '\0';
    s_device_data.agent_count++;
    device_data_unlock();

    ESP_LOGI(TAG, "Added agent: %s (id=%d) at index %d", name, id, idx);
    return idx;
}

AI_STATE_E device_data_map_state(const char *state_str)
{
    if (state_str == NULL) return TYPE_ERROR;

    if (strcmp(state_str, "idle") == 0 || strcmp(state_str, "空闲") == 0) {
        return TYPE_IDLE;
    } else if (strcmp(state_str, "researching") == 0 || strcmp(state_str, "调研") == 0 || strcmp(state_str, "调研中") == 0) {
        return TYPE_RESEARCHING;
    } else if (strcmp(state_str, "waiting") == 0 || strcmp(state_str, "等待") == 0 || strcmp(state_str, "等待中") == 0) {
        return TYPE_WAITING;
    } else if (strcmp(state_str, "working") == 0 || strcmp(state_str, "工作") == 0 || strcmp(state_str, "工作中") == 0) {
        return TYPE_WORKING;
    } else if (strcmp(state_str, "error") == 0 || strcmp(state_str, "错误") == 0) {
        return TYPE_ERROR;
    } else if (strcmp(state_str, "offline") == 0 || strcmp(state_str, "离线") == 0) {
        return TYPE_OFFLINE;
    }

    return TYPE_ERROR;
}

const char* device_data_get_message(int idx)
{
    if (idx < 0 || idx >= s_device_data.agent_count) {
        return "Unknown";
    }

    device_data_lock();
    agent_info_t *agent = &s_device_data.agents[idx];
    const char *message = NULL;

    if (agent->has_custom_message) {
        message = agent->custom_message;
    } else {
        message = get_state_message(agent->state);
    }

    device_data_unlock();

    return message;
}

const char* device_data_get_image_path(int idx)
{
    if (idx < 0 || idx >= s_device_data.agent_count) {
        return state_image_paths[TYPE_ERROR];
    }

    device_data_lock();
    AI_STATE_E state = s_device_data.agents[idx].state;
    device_data_unlock();

    return get_state_image_path(state);
}

channel_info_t* device_data_get_channel_by_index(int index)
{
    if (index < 0 || index >= s_device_data.channel_count) {
        return NULL;
    }
    return &s_device_data.channels[index];
}

int device_data_get_channel_count(void)
{
    return s_device_data.channel_count;
}

int device_data_find_channel(const char *name)
{
    if (name == NULL) return -1;

    for (int i = 0; i < s_device_data.channel_count; i++) {
        if (strcmp(s_device_data.channels[i].name, name) == 0) {
            return i;
        }
    }

    return -1;
}

int device_data_add_channel(CHANNEL_E id, const char *name, uint16_t belongs)
{
    if (name == NULL) return -1;

    int existing = device_data_find_channel(name);
    if (existing >= 0) return existing;

    if (s_device_data.channel_count >= MAX_CHANNELS) {
        ESP_LOGE(TAG, "Channel list full, cannot add: %s", name);
        return -1;
    }

    if (id == CHANNEL_MAX) {
        if (strcmp(name, "feishu") == 0) {
            id = CHANNEL_FEISHU;
        } else if (strcmp(name, "QQ") == 0 ) {
            id = CHANNEL_QQ;
        } else if (strcmp(name, "Telegram") == 0) {
            id = CHANNEL_TELEGRAM;
        }
    }

    device_data_lock();
    int idx = s_device_data.channel_count;
    channel_info_t *ch = &s_device_data.channels[idx];
    ch->id = id;
    ch->belongs = belongs;
    strncpy(ch->name, name, MAX_AGENT_NAME - 1);
    ch->name[MAX_AGENT_NAME - 1] = '\0';
    ch->state = CHANNEL_STATE_OFFLINE;
    s_device_data.channel_count++;
    device_data_unlock();

    ESP_LOGI(TAG, "Added channel: %s (id=%d) at index %d", name, id, idx);
    return idx;
}

void device_data_update_channel_state(int idx, CHANNEL_STATE_E state)
{
    if (idx < 0 || idx >= s_device_data.channel_count) return;

    device_data_lock();
    s_device_data.channels[idx].state = state;
    device_data_unlock();
}

CHANNEL_STATE_E device_data_map_channel_state(const char *state_str)
{
    if (state_str == NULL) return CHANNEL_STATE_OFFLINE;

    if (strcmp(state_str, "working") == 0 || strcmp(state_str, "connected") == 0 ||
        strcmp(state_str, "online") == 0 ||
        strcmp(state_str, "工作") == 0 || strcmp(state_str, "工作中") == 0 ||
        strcmp(state_str, "连接") == 0 || strcmp(state_str, "已连接") == 0) {
        return CHANNEL_STATE_CONNECTED;
    }

    if (strcmp(state_str, "offline") == 0 || strcmp(state_str, "离线") == 0) {
        return CHANNEL_STATE_OFFLINE;
    }

    return CHANNEL_STATE_OFFLINE;
}

const char* device_data_get_channel_state_message(CHANNEL_STATE_E state)
{
    if (state >= 0 && state < sizeof(channel_state_messages) / sizeof(channel_state_messages[0])) {
        return channel_state_messages[state];
    }
    return "未知";
}

const char* device_data_get_channel_state_image_path(CHANNEL_STATE_E state)
{
    if (state >= 0 && state < sizeof(channel_state_image_paths) / sizeof(channel_state_image_paths[0])) {
        return channel_state_image_paths[state];
    }
    return channel_state_image_paths[CHANNEL_STATE_OFFLINE];
}

void device_data_lock(void)
{
    if (s_data_mutex != NULL) {
        xSemaphoreTake(s_data_mutex, portMAX_DELAY);
    }
}

void device_data_unlock(void)
{
    if (s_data_mutex != NULL) {
        xSemaphoreGive(s_data_mutex);
    }
}

void device_data_set_last_post_ip(const char *ip)
{
    if (ip == NULL) return;
    device_data_lock();
    strncpy(s_device_data.last_post_ip, ip, MAX_POST_IP_LEN - 1);
    s_device_data.last_post_ip[MAX_POST_IP_LEN - 1] = '\0';
    device_data_unlock();
}

const char* device_data_get_last_post_ip(void)
{
    return s_device_data.last_post_ip;
}

int device_data_add_notice(const char *message)
{
    if (message == NULL || strlen(message) == 0) {
        return -1;
    }

    if (s_data_mutex == NULL) {
        return -1;
    }

    if (xSemaphoreTake(s_data_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take mutex");
        return -1;
    }

    static int next_id = 1;

    if (s_device_data.notice_count >= MAX_NOTICE_MESSAGES) {
        for (int i = 0; i < MAX_NOTICE_MESSAGES - 1; i++) {
            s_device_data.notice_messages[i] = s_device_data.notice_messages[i + 1];
        }
        s_device_data.notice_count = MAX_NOTICE_MESSAGES - 1;
    }

    int index = s_device_data.notice_count;
    s_device_data.notice_messages[index].id = next_id++;
    strncpy(s_device_data.notice_messages[index].message, message, MAX_NOTICE_MESSAGE_LENGTH - 1);
    s_device_data.notice_messages[index].message[MAX_NOTICE_MESSAGE_LENGTH - 1] = '\0';
    s_device_data.notice_count++;

    xSemaphoreGive(s_data_mutex);

    ESP_LOGI(TAG, "Added notice: %s", message);
    return s_device_data.notice_messages[index].id;
}

notice_message_t* device_data_get_notices(void)
{
    if (s_data_mutex == NULL) {
        return NULL;
    }

    if (xSemaphoreTake(s_data_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take mutex");
        return NULL;
    }

    notice_message_t *messages = s_device_data.notice_messages;

    xSemaphoreGive(s_data_mutex);

    return messages;
}

int device_data_get_notice_count(void)
{
    if (s_data_mutex == NULL) {
        return 0;
    }

    if (xSemaphoreTake(s_data_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take mutex");
        return 0;
    }

    int count = s_device_data.notice_count;

    xSemaphoreGive(s_data_mutex);

    return count;
}

void device_data_clear_notices(void)
{
    if (s_data_mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(s_data_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take mutex");
        return;
    }

    s_device_data.notice_count = 0;
    for (int i = 0; i < MAX_NOTICE_MESSAGES; i++) {
        s_device_data.notice_messages[i].id = 0;
        s_device_data.notice_messages[i].message[0] = '\0';
    }

    xSemaphoreGive(s_data_mutex);

    ESP_LOGI(TAG, "Cleared all notices");
}

esp_err_t device_data_delete_notice(int index)
{
    if (index < 0 || index >= s_device_data.notice_count) {
        ESP_LOGE(TAG, "Invalid index %d, notice count is %d", index, s_device_data.notice_count);
        return ESP_ERR_INVALID_ARG;
    }

    if (s_data_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_data_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to take mutex");
        return ESP_ERR_INVALID_STATE;
    }

    for (int i = index; i < s_device_data.notice_count - 1; i++) {
        s_device_data.notice_messages[i] = s_device_data.notice_messages[i + 1];
    }

    s_device_data.notice_count--;
    s_device_data.notice_messages[s_device_data.notice_count].id = 0;
    s_device_data.notice_messages[s_device_data.notice_count].message[0] = '\0';

    xSemaphoreGive(s_data_mutex);

    ESP_LOGI(TAG, "Deleted notice at index %d, remaining: %d", index, s_device_data.notice_count);
    return ESP_OK;
}
