#include "page_conf.h"
#include "esp_log.h"
#include "esp_system.h"
#include "k10_ui/data/device_data.h"
#include <stdio.h>
#include "font.h"

static const char *TAG = "page_notice";

static void switch_to_local_page(lv_timer_t *timer) {
    ESP_LOGI(TAG, "Switching to local page");
    stop_page_switch_timer();
    delete_current_page(NULL);
    page_local_init();
}

void page_notice_init(void) {
    ESP_LOGI(TAG, "Initializing notice page, free heap: %u bytes", esp_get_free_heap_size());

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *cont = lv_obj_create(scr);
    lv_obj_set_size(cont, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *notice_label = init_icon_container(cont, ICON_TYPE_NOTICE, NULL);
    lv_obj_align(notice_label, LV_ALIGN_TOP_MID, 0, 15);

    int notice_count = device_data_get_notice_count();
    notice_message_t *notice_messages = device_data_get_notices();

    if (notice_count > 0 && notice_messages != NULL) {
        const int message_spacing = 6;
        lv_obj_t *prev_label = NULL;

        for (int i = 0; i < notice_count && i < MAX_NOTICE_MESSAGES; i++) {
            lv_obj_t *text_label = lv_label_create(cont);
            lv_label_set_text(text_label, notice_messages[i].message);
            lv_obj_set_style_text_font(text_label, &dfrobot_font_16, 0);
            lv_obj_set_style_text_color(text_label, lv_color_hex(0x000000), 0);
            lv_obj_set_style_text_align(text_label, LV_TEXT_ALIGN_LEFT, 0);
            lv_obj_set_width(text_label, 180);
            lv_label_set_long_mode(text_label, LV_LABEL_LONG_WRAP);

            if (prev_label == NULL) {
                lv_obj_align_to(text_label, notice_label, LV_ALIGN_OUT_BOTTOM_LEFT, -42, 8);
            } else {
                lv_obj_align_to(text_label, prev_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, message_spacing);
            }

            prev_label = text_label;
        }
    } else {
        lv_obj_t *text_label = lv_label_create(cont);
        lv_label_set_text(text_label, "暂无消息");
        lv_obj_set_style_text_font(text_label, &dfrobot_font_16, 0);
        lv_obj_set_style_text_color(text_label, lv_color_hex(0x000000), 0);
        lv_obj_align_to(text_label, notice_label, LV_ALIGN_OUT_BOTTOM_LEFT, -42, 6);
    }

    stop_page_switch_timer();
    page_switch_timer = lv_timer_create(switch_to_local_page, 5000, NULL);
    ESP_LOGI(TAG, "Page switch timer created");
}
