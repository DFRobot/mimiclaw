#include <esp_heap_caps.h>
#include <esp_log.h>
#include <stddef.h>
#include <string.h>

#include "esp_jpeg_common.h"
#include "esp_jpeg_enc.h"
#include "esp_imgfx_color_convert.h"

#include "image_to_jpeg.h"

#define TAG "image_to_jpeg"

static void* malloc_psram(size_t size) {
    void* p = malloc(size);
    if (p)
        return p;
#if (CONFIG_SPIRAM_SUPPORT && (CONFIG_SPIRAM_USE_CAPS_ALLOC || CONFIG_SPIRAM_USE_MALLOC))
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return NULL;
#endif
}

static inline uint8_t expand_5_to_8(uint8_t v) {
    return (uint8_t)((v << 3) | (v >> 2));
}

static inline uint8_t expand_6_to_8(uint8_t v) {
    return (uint8_t)((v << 2) | (v >> 4));
}

static uint8_t* convert_input_to_encoder_buf(const uint8_t* src, uint16_t width, uint16_t height, v4l2_pix_fmt_t format,
                                             jpeg_pixel_format_t* out_fmt, int* out_size) {
    // GRAY directly as JPEG_PIXEL_FORMAT_GRAY input
    if (format == V4L2_PIX_FMT_GREY) {
        int sz = (int)width * (int)height;
        uint8_t* buf = (uint8_t*)jpeg_calloc_align(sz, 16);
        if (!buf)
            return NULL;
        memcpy(buf, src, sz);
        if (out_fmt)
            *out_fmt = JPEG_PIXEL_FORMAT_GRAY;
        if (out_size)
            *out_size = sz;
        return buf;
    }

    // RGB565
    if (format == V4L2_PIX_FMT_RGB565) {
        int sz = (int)width * (int)height * 3;
        uint8_t* buf = (uint8_t*)jpeg_calloc_align(sz, 16);
        if (!buf)
            return NULL;
        
        const uint16_t* src16 = (const uint16_t*)src;
        uint8_t* dst = buf;
        int pixel_count = width * height;
        
        for (int i = 0; i < pixel_count; i++) {
            uint16_t pixel = src16[i];
            uint8_t r5 = (pixel >> 11) & 0x1F;
            uint8_t g6 = (pixel >> 5) & 0x3F;
            uint8_t b5 = pixel & 0x1F;
            
            *dst++ = expand_5_to_8(r5);
            *dst++ = expand_6_to_8(g6);
            *dst++ = expand_5_to_8(b5);
        }
        
        if (out_fmt)
            *out_fmt = JPEG_PIXEL_FORMAT_RGB888;
        if (out_size)
            *out_size = sz;
        return buf;
    }

    // RGB24
    if (format == V4L2_PIX_FMT_RGB24) {
        int sz = (int)width * (int)height * 3;
        uint8_t* buf = (uint8_t*)jpeg_calloc_align(sz, 16);
        if (!buf)
            return NULL;
        memcpy(buf, src, sz);
        if (out_fmt)
            *out_fmt = JPEG_PIXEL_FORMAT_RGB888;
        if (out_size)
            *out_size = sz;
        return buf;
    }

    ESP_LOGE(TAG, "Unsupported format: 0x%08X", format);
    return NULL;
}

bool image_to_jpeg(uint8_t *src, size_t src_len, uint16_t width, uint16_t height,
                   v4l2_pix_fmt_t format, uint8_t quality, uint8_t **out, size_t *out_len) {
    if (!src || !out || !out_len) {
        ESP_LOGE(TAG, "Invalid parameters");
        return false;
    }

    jpeg_pixel_format_t pix_fmt = JPEG_PIXEL_FORMAT_RGB888;
    int input_size = 0;
    uint8_t* input_buf = convert_input_to_encoder_buf(src, width, height, format, &pix_fmt, &input_size);
    if (!input_buf) {
        ESP_LOGE(TAG, "Failed to convert input format");
        return false;
    }

    jpeg_enc_handle_t jpeg_enc = NULL;
    jpeg_enc_config_t config = DEFAULT_JPEG_ENC_CONFIG();
    config.width = width;
    config.height = height;
    config.src_type = pix_fmt;
    config.subsampling = (pix_fmt == JPEG_PIXEL_FORMAT_GRAY) ? JPEG_SUBSAMPLE_GRAY : JPEG_SUBSAMPLE_420;
    config.quality = quality;

    jpeg_error_t ret = jpeg_enc_open(&config, &jpeg_enc);
    if (ret != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "Failed to open JPEG encoder");
        jpeg_free_align(input_buf);
        return false;
    }

    size_t out_buf_size = width * height * 3 / 2;
    uint8_t* out_buf = (uint8_t*)malloc_psram(out_buf_size);
    if (!out_buf) {
        ESP_LOGE(TAG, "Failed to allocate output buffer");
        jpeg_enc_close(jpeg_enc);
        jpeg_free_align(input_buf);
        return false;
    }

    int encoded_len = 0;
    ret = jpeg_enc_process(jpeg_enc, input_buf, input_size, out_buf, (int)out_buf_size, &encoded_len);
    jpeg_enc_close(jpeg_enc);
    jpeg_free_align(input_buf);

    if (ret != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "Failed to encode JPEG");
        free(out_buf);
        return false;
    }

    *out = out_buf;
    *out_len = (size_t)encoded_len;

    ESP_LOGD(TAG, "JPEG encoded: %dx%d, size=%zu bytes", width, height, *out_len);
    return true;
}

bool image_to_jpeg_cb(uint8_t *src, size_t src_len, uint16_t width, uint16_t height,
                      v4l2_pix_fmt_t format, uint8_t quality, jpg_out_cb cb, void *arg) {
    if (!src || !cb) {
        ESP_LOGE(TAG, "Invalid parameters");
        return false;
    }

    uint8_t* out_buf = NULL;
    size_t out_len = 0;

    bool ret = image_to_jpeg(src, src_len, width, height, format, quality, &out_buf, &out_len);
    if (!ret) {
        return false;
    }

    if (cb) {
        cb(arg, 0, out_buf, out_len);
    }

    free(out_buf);
    return true;
}
