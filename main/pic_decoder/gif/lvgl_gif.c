#include "lvgl_gif.h"
#include <esp_log.h>
#include <string.h>
#include <stdlib.h>

#define TAG "LvglGif"

struct lvgl_gif_t {
    gd_GIF* gif;
    lv_img_dsc_t img_dsc;
    lv_timer_t* timer;
    uint32_t last_call;
    bool playing;
    bool loaded;
    uint32_t loop_delay_ms;
    bool loop_waiting;
    uint32_t loop_wait_start;
    lvgl_gif_frame_callback_t frame_callback;
    lv_obj_t* obj;  // LVGL object reference for invalidation
};

static void next_frame(lv_timer_t* timer) {
    lvgl_gif_t* gif = (lvgl_gif_t*)lv_timer_get_user_data(timer);
    
    if (!gif->loaded || !gif->gif || !gif->playing) {
        ESP_LOGI(TAG, "next_frame: not loaded/playing, returning");
        return;
    }

    if (gif->loop_waiting) {
        uint32_t wait_elapsed = lv_tick_elaps(gif->loop_wait_start);
        if (wait_elapsed < gif->loop_delay_ms) {
            return;
        }
        gif->loop_waiting = false;
        ESP_LOGI(TAG, "Loop delay completed, continuing GIF");
    }

    uint32_t elapsed = lv_tick_elaps(gif->last_call);
    ESP_LOGI(TAG, "next_frame: elapsed=%lu, delay=%d", elapsed, gif->gif->gce.delay * 10);
    if (elapsed < gif->gif->gce.delay * 10) {
        return;
    }

    gif->last_call = lv_tick_get();

    ESP_LOGI(TAG, "next_frame: calling gd_get_frame");
    uint32_t pos_before = gif->gif->f_rw_p;
    int has_next = gd_get_frame(gif->gif);
    ESP_LOGI(TAG, "next_frame: has_next=%d", has_next);
    
    if (has_next == 0) {
        gif->playing = false;
        if (gif->timer) {
            lv_timer_pause(gif->timer);
        }
        ESP_LOGI(TAG, "GIF animation completed");
        return;
    }

    if (gif->loop_delay_ms > 0 && gif->gif->f_rw_p < pos_before) {
        gif->loop_waiting = true;
        gif->loop_wait_start = lv_tick_get();
        ESP_LOGI(TAG, "GIF completed one cycle, waiting %lu ms before next loop", gif->loop_delay_ms);
        return;
    }

    ESP_LOGI(TAG, "next_frame: calling gd_render_frame");
    if (gif->gif->canvas) {
        gd_render_frame(gif->gif, gif->gif->canvas);
        
        // Invalidate LVGL object to trigger redraw
        if (gif->obj) {
            lv_obj_invalidate(gif->obj);
        }
        
        if (gif->frame_callback) {
            gif->frame_callback();
        }
    }
}

lvgl_gif_t* lvgl_gif_create(const lv_img_dsc_t* img_dsc) {
    if (!img_dsc || !img_dsc->data) {
        ESP_LOGE(TAG, "Invalid image descriptor");
        return NULL;
    }

    lvgl_gif_t* gif = (lvgl_gif_t*)lv_malloc(sizeof(lvgl_gif_t));
    if (!gif) {
        ESP_LOGE(TAG, "Failed to allocate GIF object");
        return NULL;
    }

    memset(gif, 0, sizeof(lvgl_gif_t));

    gif->gif = gd_open_gif_data(img_dsc->data);
    if (!gif->gif) {
        ESP_LOGE(TAG, "Failed to open GIF from image descriptor");
        lv_free(gif);
        return NULL;
    }

    memset(&gif->img_dsc, 0, sizeof(gif->img_dsc));
    gif->img_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    gif->img_dsc.header.flags = LV_IMAGE_FLAGS_MODIFIABLE;
    gif->img_dsc.header.cf = LV_COLOR_FORMAT_ARGB8888;
    gif->img_dsc.header.w = gif->gif->width;
    gif->img_dsc.header.h = gif->gif->height;
    gif->img_dsc.header.stride = gif->gif->width * 4;
    gif->img_dsc.data = gif->gif->canvas;
    gif->img_dsc.data_size = gif->gif->width * gif->gif->height * 4;

    if (gif->gif->canvas) {
        gd_render_frame(gif->gif, gif->gif->canvas);
    }

    gif->loaded = true;
    ESP_LOGD(TAG, "GIF loaded from image descriptor: %dx%d", gif->gif->width, gif->gif->height);
    
    return gif;
}

void lvgl_gif_destroy(lvgl_gif_t* gif) {
    if (!gif) {
        return;
    }

    if (gif->timer) {
        lv_timer_delete(gif->timer);
        gif->timer = NULL;
    }

    if (gif->gif) {
        gd_close_gif(gif->gif);
        gif->gif = NULL;
    }

    gif->playing = false;
    gif->loaded = false;
    
    memset(&gif->img_dsc, 0, sizeof(gif->img_dsc));
    
    lv_free(gif);
}

const lv_img_dsc_t* lvgl_gif_get_image_dsc(const lvgl_gif_t* gif) {
    if (!gif || !gif->loaded) {
        return NULL;
    }
    return &gif->img_dsc;
}

void lvgl_gif_start(lvgl_gif_t* gif) {
    ESP_LOGI(TAG, "lvgl_gif_start called");
    if (!gif || !gif->loaded || !gif->gif) {
        ESP_LOGW(TAG, "GIF not loaded, cannot start");
        return;
    }

    ESP_LOGI(TAG, "GIF state: loaded=%d, playing=%d, timer=%p", gif->loaded, gif->playing, (void*)gif->timer);

    if (!gif->timer) {
        ESP_LOGI(TAG, "Creating timer");
        gif->timer = lv_timer_create(next_frame, 10, gif);
        ESP_LOGI(TAG, "Timer created: %p", (void*)gif->timer);
    }

    if (gif->timer) {
        gif->playing = true;
        gif->loop_waiting = false;
        gif->last_call = lv_tick_get();
        lv_timer_resume(gif->timer);
        lv_timer_reset(gif->timer);
        
        ESP_LOGI(TAG, "Calling next_frame manually");
        next_frame(gif->timer);
        
        ESP_LOGI(TAG, "GIF animation started");
    } else {
        ESP_LOGE(TAG, "Failed to create timer");
    }
}

void lvgl_gif_pause(lvgl_gif_t* gif) {
    if (!gif || !gif->timer) {
        return;
    }
    
    gif->playing = false;
    lv_timer_pause(gif->timer);
    ESP_LOGD(TAG, "GIF animation paused");
}

void lvgl_gif_resume(lvgl_gif_t* gif) {
    if (!gif || !gif->loaded || !gif->gif) {
        ESP_LOGW(TAG, "GIF not loaded, cannot resume");
        return;
    }

    if (gif->timer) {
        gif->playing = true;
        lv_timer_resume(gif->timer);
        ESP_LOGD(TAG, "GIF animation resumed");
    }
}

void lvgl_gif_stop(lvgl_gif_t* gif) {
    if (!gif) {
        return;
    }

    if (gif->timer) {
        gif->playing = false;
        lv_timer_pause(gif->timer);
    }

    gif->loop_waiting = false;

    if (gif->gif) {
        gd_rewind(gif->gif);
        if (gif->gif->canvas) {
            gd_render_frame(gif->gif, gif->gif->canvas);
        }
        ESP_LOGD(TAG, "GIF animation stopped and rewound");
    }
}

bool lvgl_gif_is_playing(const lvgl_gif_t* gif) {
    if (!gif) {
        return false;
    }
    return gif->playing;
}

bool lvgl_gif_is_loaded(const lvgl_gif_t* gif) {
    if (!gif) {
        return false;
    }
    return gif->loaded;
}

int32_t lvgl_gif_get_loop_count(const lvgl_gif_t* gif) {
    if (!gif || !gif->loaded || !gif->gif) {
        return -1;
    }
    return gif->gif->loop_count;
}

void lvgl_gif_set_loop_count(lvgl_gif_t* gif, int32_t count) {
    if (!gif || !gif->loaded || !gif->gif) {
        ESP_LOGW(TAG, "GIF not loaded, cannot set loop count");
        return;
    }
    gif->gif->loop_count = count;
}

uint32_t lvgl_gif_get_loop_delay(const lvgl_gif_t* gif) {
    if (!gif) {
        return 0;
    }
    return gif->loop_delay_ms;
}

void lvgl_gif_set_loop_delay(lvgl_gif_t* gif, uint32_t delay_ms) {
    if (!gif) {
        return;
    }
    gif->loop_delay_ms = delay_ms;
    ESP_LOGD(TAG, "Loop delay set to %lu ms", delay_ms);
}

uint16_t lvgl_gif_get_width(const lvgl_gif_t* gif) {
    if (!gif || !gif->loaded || !gif->gif) {
        return 0;
    }
    return gif->gif->width;
}

uint16_t lvgl_gif_get_height(const lvgl_gif_t* gif) {
    if (!gif || !gif->loaded || !gif->gif) {
        return 0;
    }
    return gif->gif->height;
}

void lvgl_gif_set_frame_callback(lvgl_gif_t* gif, lvgl_gif_frame_callback_t callback) {
    if (!gif) {
        return;
    }
    gif->frame_callback = callback;
}

void lvgl_gif_set_obj(lvgl_gif_t* gif, lv_obj_t* obj) {
    if (!gif) {
        return;
    }
    gif->obj = obj;
}