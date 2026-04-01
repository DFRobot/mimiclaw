#include "page_conf.h"
#include "esp_log.h"
#include "esp_system.h"
#include "k10_ui/data/device_data.h"
#include <stdio.h>
#include "font.h"

static void switch_to_notice_page(lv_timer_t *timer) {
    ESP_LOGI("page_online", "Switching to notice page");
    stop_page_switch_timer();
    delete_current_page(NULL);
    page_notice_init();
}

void page_online_init(void){
    ESP_LOGI("page_online", "Initializing online page, free heap: %u bytes", esp_get_free_heap_size());
    
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
    
    lv_obj_t *cont = lv_obj_create(scr);
    lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);

    const char *post_ip = device_data_get_last_post_ip();
    const char *ip_text = (post_ip != NULL && post_ip[0] != '\0') ? post_ip : "无请求到达";
    lv_obj_t *online_label = init_icon_container(cont, ICON_TYPE_ONLINE, ip_text);
    lv_obj_align(cont, LV_ALIGN_TOP_LEFT, 15, 15);

    lv_obj_t *ai_label = lv_label_create(cont);
    lv_label_set_text(ai_label, "智能体");
    lv_obj_set_style_text_font(ai_label, &dfrobot_font_16, 0);
    lv_obj_set_style_text_color(ai_label, lv_color_hex(0x000000), 0);
    lv_obj_align_to(ai_label, online_label, LV_ALIGN_BOTTOM_LEFT, 10, 24);

    lv_obj_t *agent_last = create_agent_list(cont, ai_label, ALONG_TO_ONLINE);

    lv_obj_t *channel_label = lv_label_create(cont);
    lv_label_set_text(channel_label, "Channels");
    lv_obj_set_style_text_font(channel_label, &dfrobot_font_16, 0);
    lv_obj_set_style_text_color(channel_label, lv_color_hex(0x666666), 0);
    if (agent_last != NULL) {
        lv_obj_align_to(channel_label, agent_last, LV_ALIGN_BOTTOM_LEFT, -15, 30);
    } else {
        lv_obj_align_to(channel_label, ai_label, LV_ALIGN_BOTTOM_LEFT, 0, 80);
    }

    create_channel_list(cont, channel_label, ALONG_TO_ONLINE);
    
    stop_page_switch_timer();
    page_switch_timer = lv_timer_create(switch_to_notice_page, 5000, NULL);
    ESP_LOGI("page_online", "Page switch timer created");
}
