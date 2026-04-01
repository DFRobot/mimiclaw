#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// V4L2 pixel format definitions
#define V4L2_PIX_FMT_RGB565 0x50424752  // 'RGBP'
#define V4L2_PIX_FMT_RGB565X 0x52474250 // 'PRGB'
#define V4L2_PIX_FMT_RGB24 0x33424752   // 'RGB3'
#define V4L2_PIX_FMT_YUYV 0x56595559    // 'YUYV'
#define V4L2_PIX_FMT_YUV422P 0x36315559 // 'YU16'
#define V4L2_PIX_FMT_YUV420 0x32315559  // 'YU12'
#define V4L2_PIX_FMT_GREY 0x59455247    // 'GREY'
#define V4L2_PIX_FMT_UYVY 0x59565955    // 'UYVY'
#define V4L2_PIX_FMT_JPEG 0x4745504A    // 'JPEG'

typedef uint32_t v4l2_pix_fmt_t;

#ifdef __cplusplus
extern "C"
{
#endif

    // JPEG output callback function type
    // arg: user defined parameter, index: current data index, data: JPEG data block, len: data block length
    // return: actual processed bytes
    typedef size_t (*jpg_out_cb)(void *arg, size_t index, const void *data, size_t len);

    /**
     * @brief Convert image format to JPEG
     *
     * This function uses optimized JPEG encoder for encoding, main features:
     * - Save about 8KB SRAM (static variables changed to heap allocation)
     * - Support multiple image format inputs
     * - High quality JPEG output
     *
     * @param src       Source image data
     * @param src_len   Source image data length
     * @param width     Image width
     * @param height    Image height
     * @param format    Image format (PIXFORMAT_RGB565, PIXFORMAT_RGB888, etc.)
     * @param quality   JPEG quality (1-100)
     * @param out       Output JPEG data pointer (needs to be freed by caller)
     * @param out_len   Output JPEG data length
     *
     * @return true success, false failure
     */
    bool image_to_jpeg(uint8_t *src, size_t src_len, uint16_t width, uint16_t height,
                       v4l2_pix_fmt_t format, uint8_t quality, uint8_t **out, size_t *out_len);

    /**
     * @brief Convert image format to JPEG (callback version)
     *
     * Use callback function to process JPEG output data, suitable for streaming or chunk processing:
     * - Save about 8KB SRAM (static variables changed to heap allocation)
     * - Support streaming output, no need to pre-allocate large buffer
     * - Process JPEG data chunk by chunk through callback function
     *
     * @param src       Source image data
     * @param src_len   Source image data length
     * @param width     Image width
     * @param height    Image height
     * @param format    Image format
     * @param quality   JPEG quality (1-100)
     * @param cb        Output callback function
     * @param arg       User parameter passed to callback function
     *
     * @return true success, false failure
     */
    bool image_to_jpeg_cb(uint8_t *src, size_t src_len, uint16_t width, uint16_t height,
                          v4l2_pix_fmt_t format, uint8_t quality, jpg_out_cb cb, void *arg);

#ifdef __cplusplus
}
#endif
