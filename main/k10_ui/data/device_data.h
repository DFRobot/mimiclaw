#ifndef __DEVICE_DATA_H__
#define __DEVICE_DATA_H__

#include "page_conf.h"
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define MAX_AGENTS 12
#define MAX_CHANNELS 6
#define MAX_AGENT_NAME 32
#define MAX_CUSTOM_MESSAGE 128
#define MAX_NOTICE_MESSAGES 3
#define MAX_NOTICE_MESSAGE_LENGTH 256

#define ALONG_TO_LOCAL 1 << 0
#define ALONG_TO_ONLINE 1 << 1

typedef enum {
    AGENT_SECRETARY = 0,
    AGENT_MIMICLAW,
    AGENT_MAX
} AGENT_E;

typedef struct {
    uint16_t belongs;
    AGENT_E id;
    char name[MAX_AGENT_NAME];
    AI_STATE_E state;
    bool has_custom_message;
    char custom_message[MAX_CUSTOM_MESSAGE];
} agent_info_t;

typedef enum {
    CHANNEL_FEISHU = 0,
    CHANNEL_QQ,
    CHANNEL_TELEGRAM,
    CHANNEL_MAX
} CHANNEL_E;

typedef struct {
    uint16_t belongs;
    CHANNEL_E id;
    char name[MAX_AGENT_NAME];
    CHANNEL_STATE_E state;
} channel_info_t;

typedef struct {
    int id;
    char message[MAX_NOTICE_MESSAGE_LENGTH];
} notice_message_t;

#define MAX_POST_IP_LEN 16

typedef struct {
    agent_info_t agents[MAX_AGENTS];
    int agent_count;
    channel_info_t channels[MAX_CHANNELS];
    int channel_count;
    notice_message_t notice_messages[MAX_NOTICE_MESSAGES];
    int notice_count;
    char last_post_ip[MAX_POST_IP_LEN];
} device_data_t;

void device_data_init(void);

int device_data_add_channel(CHANNEL_E id, const char *name, uint16_t belongs);

agent_info_t* device_data_get_agent_by_index(int index);

int device_data_get_agent_count(void);

int device_data_find_agent(const char *name);

int device_data_add_agent(AGENT_E id, const char *name, uint16_t belongs, AI_STATE_E state);

AI_STATE_E device_data_map_state(const char *state_str);

const char* device_data_get_message(int idx);

const char* device_data_get_image_path(int idx);

channel_info_t* device_data_get_channel_by_index(int index);

int device_data_get_channel_count(void);

int device_data_find_channel(const char *name);

void device_data_update_channel_state(int idx, CHANNEL_STATE_E state);

CHANNEL_STATE_E device_data_map_channel_state(const char *state_str);

const char* device_data_get_channel_state_message(CHANNEL_STATE_E state);

const char* device_data_get_channel_state_image_path(CHANNEL_STATE_E state);

int device_data_add_notice(const char *message);

notice_message_t* device_data_get_notices(void);

int device_data_get_notice_count(void);

void device_data_clear_notices(void);

esp_err_t device_data_delete_notice(int index);

void device_data_lock(void);

void device_data_unlock(void);

void device_data_set_last_post_ip(const char *ip);

const char* device_data_get_last_post_ip(void);

#endif // __DEVICE_DATA_H__
