#include <stdio.h>
#include <string.h>
#include "page_conf.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "pic_decoder/jpg/jpeg_to_image.h"
#include "k10_ui/data/device_data.h"
#include "font.h"

static const char *TAG = "page_conf";

typedef struct {
    lv_img_dsc_t* img_dsc;
    uint8_t* image_data;
} image_resource_t;

typedef struct {
    ICON_TYPE_E type;
    const char *image_path;
    const char *message;
} SET_ICON_T;

static SET_ICON_T icon_array[] = {
    {ICON_TYPE_LOCAL, "/sdcard/img/local.jpg", "本机"},
    {ICON_TYPE_ONLINE, "/sdcard/img/online.jpg", "192.168.1.29"},
    {ICON_TYPE_FEISHU, "/sdcard/img/feishu.jpg", "Feishu"},
    {ICON_TYPE_QQ, "/sdcard/img/qq.jpg", "QQ"},
    {ICON_TYPE_NOTICE, "/sdcard/img/notice.jpg", "留言板"}
};

lv_timer_t *page_switch_timer = NULL;

void stop_page_switch_timer(void) {
    if (page_switch_timer != NULL) {
        lv_timer_del(page_switch_timer);
        page_switch_timer = NULL;
        ESP_LOGI(TAG, "Page switch timer stopped");
    }
}

static void delete_obj_with_event(lv_obj_t *obj) {
    if (obj == NULL) {
        return;
    }
    
    uint32_t child_cnt = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_t *child = lv_obj_get_child(obj, 0);
        if (child != NULL) {
            delete_obj_with_event(child);
        }
    }
    
    lv_obj_delete(obj);
}

void delete_current_page(lv_style_t *style){
    lv_obj_t * act_scr = lv_screen_active();
    
    uint32_t child_cnt = lv_obj_get_child_count(act_scr);
    ESP_LOGI(TAG, "Deleting page with %u children", child_cnt);
    ESP_LOGI(TAG, "Free RAM before: %u bytes", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    
    for (uint32_t i = 0; i < child_cnt; i++) {
        lv_obj_t *child = lv_obj_get_child(act_scr, 0);
        if (child != NULL) {
            delete_obj_with_event(child);
        }
    }
    
    ESP_LOGI(TAG, "Free RAM after: %u bytes", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    
    if (style != NULL) {
        lv_style_reset(style);
    }
}

static void image_delete_event_cb(lv_event_t * e) {
    lv_obj_t * img = lv_event_get_target(e);
    image_resource_t* res = (image_resource_t*)lv_obj_get_user_data(img);
    if (res != NULL) {
        if (res->img_dsc) {
            free(res->img_dsc);
        }
        if (res->image_data) {
            heap_caps_free(res->image_data);
        }
        free(res);
        lv_obj_set_user_data(img, NULL);
    }
}

static lv_obj_t* init_ai_state_container(lv_obj_t *parent, int agent_idx) {
    if (parent == NULL) {
        ESP_LOGE(TAG, "Parent object is NULL");
        return NULL;
    }

    const char *image_path = device_data_get_image_path(agent_idx);
    const char *message = device_data_get_message(agent_idx);

    if (image_path == NULL || message == NULL) {
        ESP_LOGE(TAG, "Failed to get agent data for index=%d", agent_idx);
        return NULL;
    }

    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(cont, lv_color_hex(0x000000), 0);

    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    FILE* jpg_f = fopen(image_path, "rb");
    if (!jpg_f) {
        ESP_LOGE(TAG, "Failed to open JPEG file: %s", image_path);
    } else {
        fseek(jpg_f, 0, SEEK_END);
        long jpg_size = ftell(jpg_f);
        fseek(jpg_f, 0, SEEK_SET);

        uint8_t* jpeg_data = (uint8_t*)heap_caps_malloc(jpg_size, MALLOC_CAP_SPIRAM);
        if (!jpeg_data) {
            ESP_LOGE(TAG, "Failed to allocate PSRAM memory for JPEG");
            fclose(jpg_f);
        } else {
            size_t read_size = fread(jpeg_data, 1, jpg_size, jpg_f);
            fclose(jpg_f);

            uint8_t* image_data = NULL;
            size_t image_len, width, height, stride;
            esp_err_t ret = jpeg_to_image(jpeg_data, read_size,
                                          &image_data, &image_len,
                                          &width, &height, &stride);

            heap_caps_free(jpeg_data);

            if (ret == ESP_OK) {
                lv_img_dsc_t* img_dsc = (lv_img_dsc_t*)malloc(sizeof(lv_img_dsc_t));
                if (img_dsc == NULL) {
                    ESP_LOGE(TAG, "Failed to allocate memory for image descriptor");
                    heap_caps_free(image_data);
                } else {
                    memset(img_dsc, 0, sizeof(lv_img_dsc_t));
                    img_dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
                    img_dsc->data = image_data;
                    img_dsc->data_size = image_len;
                    img_dsc->header.w = width;
                    img_dsc->header.h = height;
                    img_dsc->header.cf = LV_COLOR_FORMAT_RGB565;

                    image_resource_t* res = (image_resource_t*)malloc(sizeof(image_resource_t));
                    if (res == NULL) {
                        ESP_LOGE(TAG, "Failed to allocate resource manager");
                        free(img_dsc);
                        heap_caps_free(image_data);
                    } else {
                        memset(res, 0, sizeof(image_resource_t));
                        res->img_dsc = img_dsc;
                        res->image_data = image_data;

                        lv_obj_t *img = lv_image_create(cont);
                        lv_image_set_src(img, img_dsc);

                        lv_obj_set_user_data(img, res);
                        lv_obj_add_event_cb(img, image_delete_event_cb, LV_EVENT_DELETE, NULL);
                    }
                }
            } else {
                ESP_LOGE(TAG, "Failed to decode JPEG: %s", esp_err_to_name(ret));
            }
        }
    }

    lv_obj_t *label = lv_label_create(cont);
    if (message != NULL) {
        int char_count = 0;
        int byte_pos = 0;
        while (message[byte_pos] != '\0') {
            if ((message[byte_pos] & 0xC0) != 0x80) char_count++;
            byte_pos++;
        }
        if (char_count > 4) {
            int cut_pos = 0;
            int cut_count = 0;
            while (message[cut_pos] != '\0') {
                if ((message[cut_pos] & 0xC0) != 0x80) {
                    cut_count++;
                    if (cut_count > 3) break;
                }
                cut_pos++;
            }
            char buf[16] = {0};
            memcpy(buf, message, cut_pos);
            strcat(buf, "...");
            lv_label_set_text(label, buf);
        } else {
            lv_label_set_text(label, message);
        }
    } else {
        lv_label_set_text(label, "");
    }
    lv_obj_set_style_text_font(label, &dfrobot_font_16, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x000000), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);

    return cont;
}

lv_obj_t* create_agent_list(lv_obj_t *parent, lv_obj_t *ref, uint16_t belongs) {
    if (parent == NULL) {
        ESP_LOGE(TAG, "create_agent_list: parent is NULL");
        return NULL;
    }

    lv_obj_t *prev_label = NULL;
    int agent_count = device_data_get_agent_count();

    for (int i = 0; i < agent_count; i++) {
        agent_info_t *agent = device_data_get_agent_by_index(i);
        if (agent == NULL) continue;

        if (!(agent->belongs & belongs)) continue;

        if (agent->state == TYPE_NONE) continue;

        const char *display_name = agent->name;
        if (display_name == NULL || display_name[0] == '\0') continue;

        lv_obj_t *name_label = lv_label_create(parent);
        lv_label_set_text(name_label, display_name);
        lv_obj_set_style_text_font(name_label, &dfrobot_font_16, 0);
        lv_obj_set_style_text_color(name_label, lv_color_hex(0x000000), 0);

        if (prev_label == NULL) {
            if (ref != NULL) {
                lv_obj_align_to(name_label, ref, LV_ALIGN_BOTTOM_LEFT, 15, 20);
            } else {
                lv_obj_align(name_label, LV_ALIGN_TOP_LEFT, 15, 0);
            }
        } else {
            lv_obj_align_to(name_label, prev_label, LV_ALIGN_BOTTOM_LEFT, 0, 20);
        }

        lv_obj_t *state_cont = init_ai_state_container(parent, i);
        if (state_cont != NULL) {
            lv_obj_align_to(state_cont, name_label, LV_ALIGN_LEFT_MID, 90, 0);
        }

        prev_label = name_label;
    }

    if (prev_label == NULL) {
        lv_obj_t *empty_label = lv_label_create(parent);
        lv_label_set_text(empty_label, "NO--AGENTS!");
        lv_obj_set_style_text_font(empty_label, &dfrobot_font_16, 0);
        lv_obj_set_style_text_color(empty_label, lv_color_hex(0x000000), 0);
        if (ref != NULL) {
            lv_obj_align_to(empty_label, ref, LV_ALIGN_BOTTOM_LEFT, 15, 20);
        } else {
            lv_obj_align(empty_label, LV_ALIGN_TOP_LEFT, 15, 0);
        }
        prev_label = empty_label;
    }

    return prev_label;
}

static const int icon_count = sizeof(icon_array) / sizeof(icon_array[0]);

lv_obj_t* init_icon_container(lv_obj_t *parent, ICON_TYPE_E type, const char *message) {
    if (parent == NULL) {
        ESP_LOGE(TAG, "Parent object is NULL");
        return NULL;
    }

    SET_ICON_T *icon_data = NULL;
    for (int i = 0; i < icon_count; i++) {
        if (icon_array[i].type == type) {
            icon_data = &icon_array[i];
            break;
        }
    }

    if (icon_data == NULL) {
        ESP_LOGE(TAG, "Invalid icon type: %d", type);
        return NULL;
    }

    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(cont, lv_color_hex(0x000000), 0);

    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    FILE* jpg_f = fopen(icon_data->image_path, "rb");
    if (!jpg_f) {
        ESP_LOGE(TAG, "Failed to open JPEG file: %s", icon_data->image_path);
    } else {
        fseek(jpg_f, 0, SEEK_END);
        long jpg_size = ftell(jpg_f);
        fseek(jpg_f, 0, SEEK_SET);

        uint8_t* jpeg_data = (uint8_t*)heap_caps_malloc(jpg_size, MALLOC_CAP_SPIRAM);
        if (!jpeg_data) {
            ESP_LOGE(TAG, "Failed to allocate PSRAM memory for JPEG");
            fclose(jpg_f);
        } else {
            size_t read_size = fread(jpeg_data, 1, jpg_size, jpg_f);
            fclose(jpg_f);

            uint8_t* image_data = NULL;
            size_t image_len, width, height, stride;
            esp_err_t ret = jpeg_to_image(jpeg_data, read_size,
                                          &image_data, &image_len,
                                          &width, &height, &stride);

            heap_caps_free(jpeg_data);

            if (ret == ESP_OK) {
                lv_img_dsc_t* img_dsc = (lv_img_dsc_t*)malloc(sizeof(lv_img_dsc_t));
                if (img_dsc == NULL) {
                    ESP_LOGE(TAG, "Failed to allocate memory for image descriptor");
                    heap_caps_free(image_data);
                } else {
                    memset(img_dsc, 0, sizeof(lv_img_dsc_t));
                    img_dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
                    img_dsc->data = image_data;
                    img_dsc->data_size = image_len;
                    img_dsc->header.w = width;
                    img_dsc->header.h = height;
                    img_dsc->header.cf = LV_COLOR_FORMAT_RGB565;

                    image_resource_t* res = (image_resource_t*)malloc(sizeof(image_resource_t));
                    if (res == NULL) {
                        ESP_LOGE(TAG, "Failed to allocate resource manager");
                        free(img_dsc);
                        heap_caps_free(image_data);
                    } else {
                        memset(res, 0, sizeof(image_resource_t));
                        res->img_dsc = img_dsc;
                        res->image_data = image_data;

                        lv_obj_t *img = lv_image_create(cont);
                        lv_image_set_src(img, img_dsc);

                        lv_obj_set_user_data(img, res);
                        lv_obj_add_event_cb(img, image_delete_event_cb, LV_EVENT_DELETE, NULL);
                    }
                }
            } else {
                ESP_LOGE(TAG, "Failed to decode JPEG: %s", esp_err_to_name(ret));
            }
        }
    }

    lv_obj_t *label = lv_label_create(cont);
    lv_label_set_text(label, message != NULL ? message : icon_data->message);
    lv_obj_set_style_text_font(label, &dfrobot_font_16, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x000000), 0);

    return cont;
}

static ICON_TYPE_E channel_to_icon_type(CHANNEL_E channel_id)
{
    switch (channel_id) {
        case CHANNEL_FEISHU:   return ICON_TYPE_FEISHU;
        case CHANNEL_QQ:       return ICON_TYPE_QQ;
        default:               return ICON_TYPE_LOCAL;
    }
}

static bool channel_has_icon(CHANNEL_E channel_id)
{
    return channel_id == CHANNEL_FEISHU || channel_id == CHANNEL_QQ;
}

static lv_obj_t* init_channel_state_container(lv_obj_t *parent, CHANNEL_STATE_E state) {
    if (parent == NULL) {
        ESP_LOGE(TAG, "Parent object is NULL");
        return NULL;
    }

    const char *image_path = device_data_get_channel_state_image_path(state);
    const char *message = device_data_get_channel_state_message(state);

    if (image_path == NULL || message == NULL) {
        ESP_LOGE(TAG, "Failed to get channel state data");
        return NULL;
    }

    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(cont, lv_color_hex(0x000000), 0);

    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    FILE* jpg_f = fopen(image_path, "rb");
    if (!jpg_f) {
        ESP_LOGE(TAG, "Failed to open JPEG file: %s", image_path);
    } else {
        fseek(jpg_f, 0, SEEK_END);
        long jpg_size = ftell(jpg_f);
        fseek(jpg_f, 0, SEEK_SET);

        uint8_t* jpeg_data = (uint8_t*)heap_caps_malloc(jpg_size, MALLOC_CAP_SPIRAM);
        if (!jpeg_data) {
            ESP_LOGE(TAG, "Failed to allocate PSRAM memory for JPEG");
            fclose(jpg_f);
        } else {
            size_t read_size = fread(jpeg_data, 1, jpg_size, jpg_f);
            fclose(jpg_f);

            uint8_t* image_data = NULL;
            size_t image_len, width, height, stride;
            esp_err_t ret = jpeg_to_image(jpeg_data, read_size,
                                          &image_data, &image_len,
                                          &width, &height, &stride);

            heap_caps_free(jpeg_data);

            if (ret == ESP_OK) {
                lv_img_dsc_t* img_dsc = (lv_img_dsc_t*)malloc(sizeof(lv_img_dsc_t));
                if (img_dsc == NULL) {
                    ESP_LOGE(TAG, "Failed to allocate memory for image descriptor");
                    heap_caps_free(image_data);
                } else {
                    memset(img_dsc, 0, sizeof(lv_img_dsc_t));
                    img_dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
                    img_dsc->data = image_data;
                    img_dsc->data_size = image_len;
                    img_dsc->header.w = width;
                    img_dsc->header.h = height;
                    img_dsc->header.cf = LV_COLOR_FORMAT_RGB565;

                    image_resource_t* res = (image_resource_t*)malloc(sizeof(image_resource_t));
                    if (res == NULL) {
                        ESP_LOGE(TAG, "Failed to allocate resource manager");
                        free(img_dsc);
                        heap_caps_free(image_data);
                    } else {
                        memset(res, 0, sizeof(image_resource_t));
                        res->img_dsc = img_dsc;
                        res->image_data = image_data;

                        lv_obj_t *img = lv_image_create(cont);
                        lv_image_set_src(img, img_dsc);

                        lv_obj_set_user_data(img, res);
                        lv_obj_add_event_cb(img, image_delete_event_cb, LV_EVENT_DELETE, NULL);
                    }
                }
            } else {
                ESP_LOGE(TAG, "Failed to decode JPEG: %s", esp_err_to_name(ret));
            }
        }
    }

    lv_obj_t *label = lv_label_create(cont);
    lv_label_set_text(label, message);
    lv_obj_set_style_text_font(label, &dfrobot_font_16, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(0x000000), 0);

    return cont;
}

lv_obj_t* create_channel_list(lv_obj_t *parent, lv_obj_t *ref, uint16_t belongs) {
    if (parent == NULL) {
        ESP_LOGE(TAG, "create_channel_list: parent is NULL");
        return NULL;
    }

    lv_obj_t *prev_obj = NULL;
    bool prev_has_icon = false;
    int channel_count = device_data_get_channel_count();

    for (int i = 0; i < channel_count; i++) {
        channel_info_t *channel = device_data_get_channel_by_index(i);
        if (channel == NULL) continue;

        if (!(channel->belongs & belongs)) continue;

        bool has_icon = channel_has_icon(channel->id);

        if (has_icon) {
            lv_obj_t *icon_cont = init_icon_container(parent, channel_to_icon_type(channel->id), NULL);
            if (icon_cont == NULL) continue;

            if (prev_obj == NULL) {
                if (ref != NULL) {
                    lv_obj_align_to(icon_cont, ref, LV_ALIGN_BOTTOM_LEFT, -12, 20);
                } else {
                    lv_obj_align(icon_cont, LV_ALIGN_TOP_LEFT, -12, 0);
                }
            } else {
                int x_offset = prev_has_icon ? 0 : -27;
                lv_obj_align_to(icon_cont, prev_obj, LV_ALIGN_BOTTOM_LEFT, x_offset, 24);
            }

            lv_obj_t *state_cont = init_channel_state_container(parent, channel->state);
            if (state_cont != NULL) {
                lv_obj_align_to(state_cont, icon_cont, LV_ALIGN_LEFT_MID, 106, 0);
            }

            prev_obj = icon_cont;
            prev_has_icon = true;
        } else {
            const char *display_name = channel->name;
            if (display_name == NULL || display_name[0] == '\0') continue;

            lv_obj_t *name_label = lv_label_create(parent);
            lv_label_set_text(name_label, display_name);
            lv_obj_set_style_text_font(name_label, &dfrobot_font_16, 0);
            lv_obj_set_style_text_color(name_label, lv_color_hex(0x000000), 0);

            if (prev_obj == NULL) {
                if (ref != NULL) {
                    lv_obj_align_to(name_label, ref, LV_ALIGN_BOTTOM_LEFT, 15, 20);
                } else {
                    lv_obj_align(name_label, LV_ALIGN_TOP_LEFT, 15, 0);
                }
            } else {
                int x_offset = prev_has_icon ? 27 : 0;
                lv_obj_align_to(name_label, prev_obj, LV_ALIGN_BOTTOM_LEFT, x_offset, 20);
            }

            lv_obj_t *state_cont = init_channel_state_container(parent, channel->state);
            if (state_cont != NULL) {
                lv_obj_align_to(state_cont, name_label, LV_ALIGN_LEFT_MID, 100, 0);
            }

            prev_obj = name_label;
            prev_has_icon = false;
        }
    }

    if (prev_obj == NULL) {
        lv_obj_t *empty_label = lv_label_create(parent);
        lv_label_set_text(empty_label, "No--Channels!");
        lv_obj_set_style_text_font(empty_label, &dfrobot_font_16, 0);
        lv_obj_set_style_text_color(empty_label, lv_color_hex(0x000000), 0);
        if (ref != NULL) {
            lv_obj_align_to(empty_label, ref, LV_ALIGN_BOTTOM_LEFT, 15, 20);
        } else {
            lv_obj_align(empty_label, LV_ALIGN_TOP_LEFT, 15, 0);
        }
        prev_obj = empty_label;
    }

    return prev_obj;
}
