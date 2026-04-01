#ifndef __PAGE_CONF_H__
#define __PAGE_CONF_H__

#include "lvgl.h"

typedef enum {
    TYPE_WORKING = 0,
    TYPE_WAITING,
    TYPE_RESEARCHING,
    TYPE_ERROR,
    TYPE_IDLE,
    TYPE_OFFLINE,
    TYPE_NONE,
    TYPE_MAX,
}AI_STATE_E;

typedef enum {
    CHANNEL_STATE_CONNECTED = 0,
    CHANNEL_STATE_OFFLINE,
    CHANNEL_STATE_MAX,
} CHANNEL_STATE_E;

typedef enum {
    ICON_TYPE_LOCAL = 0,
    ICON_TYPE_ONLINE,
    ICON_TYPE_FEISHU,
    ICON_TYPE_QQ,
    ICON_TYPE_NOTICE,
}ICON_TYPE_E;

void delete_current_page(lv_style_t *style);

lv_obj_t* create_agent_list(lv_obj_t *parent, lv_obj_t *ref, uint16_t belongs);

lv_obj_t* create_channel_list(lv_obj_t *parent, lv_obj_t *ref, uint16_t belongs);

lv_obj_t* init_icon_container(lv_obj_t *parent, ICON_TYPE_E type, const char *message);

void page_local_init(void);
void page_online_init(void);
void page_notice_init(void);

void stop_page_switch_timer(void);
extern lv_timer_t *page_switch_timer;

#endif // __PAGE_CONF_H__
