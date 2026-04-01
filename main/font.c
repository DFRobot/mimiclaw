#include "font.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "font";

/* ── lv_font_fmt_txt 常量（与 lvgl_font_glyph_tool.py / LVGL 一致）── */
#define CMAP_FORMAT0_FULL   0
#define CMAP_SPARSE_FULL   1
#define CMAP_FORMAT0_TINY  2
#define CMAP_SPARSE_TINY   3

#define FMT_PLAIN       0
#define FMT_COMPRESSED  1

#define RLE_SINGLE    0
#define RLE_REPEATED  1
#define RLE_COUNTER   2

static const uint8_t OPA4[16] = {0, 17, 34, 51, 68, 85, 102, 119, 136, 153, 170, 187, 204, 221, 238, 255};
static const uint8_t OPA3[8] = {0, 36, 73, 109, 146, 182, 218, 255};
static const uint8_t OPA2[4] = {0, 85, 170, 255};

typedef struct {
    uint32_t range_start;
    uint16_t range_length;
    uint16_t glyph_id_start;
    uint16_t list_length;
    uint8_t ctype;
    uint16_t *sparse_keys;   /* list_length，rcp 键（SPARSE_TINY / SPARSE_FULL） */
    uint16_t *sparse_ofs;    /* list_length，SPARSE_FULL 时每键对应 ofs */
    uint8_t *format0_full;  /* FORMAT0_FULL：每码点 1 字节 delta，长 range_length */
} bin_cmap_t;

typedef struct {
    FILE *fp;
    uint32_t file_data_skip; /* 文件开头跳过字节（如 0x5A5A） */
    size_t size;             /* 逻辑长度（去掉魔数后的 payload） */
    uint32_t dsc_off;
    uint32_t glyph_bitmap_off;
    uint32_t glyph_dsc_off;
    uint32_t cmaps_base;
    bin_cmap_t *cmaps;
    uint16_t cmap_num;
    uint8_t bpp;
    uint8_t bitmap_format;
    uint8_t glyph_stride;
    uint32_t glyph_count;
    int32_t line_height;
    int32_t base_line;
} bin_font_ctx_t;

static bin_font_ctx_t s_ctx20;
static bin_font_ctx_t s_ctx16;

lv_font_t dfrobot_font_20;
lv_font_t dfrobot_font_16;

#define SCRATCH_PX (128 * 128)
static uint8_t *s_glyph_a8;
static uint8_t *_pBits;
#define PBITS_SIZE 1024


static uint32_t read_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int32_t read_i32_le(const uint8_t *p)
{
    uint32_t u = read_u32_le(p);
    return (int32_t)u;
}

static uint16_t read_u16_le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static int16_t read_i16_le(const uint8_t *p)
{
    return (int16_t)read_u16_le(p);
}

static uint32_t get_bits(const uint8_t *buf, size_t len, uint32_t bit_pos, int nbits)
{
    uint32_t mask = (nbits >= 32) ? 0xFFFFFFFFu : ((1u << nbits) - 1u);
    uint32_t byte_pos = bit_pos >> 3;
    uint32_t bi = bit_pos & 7;
    if (byte_pos >= len) {
        return 0;
    }
    if (bi + (uint32_t)nbits >= 8) {
        uint32_t in16;
        if (byte_pos + 1 >= len) {
            in16 = (uint32_t)buf[byte_pos] << 8;
        } else {
            in16 = ((uint32_t)buf[byte_pos] << 8) + buf[byte_pos + 1];
        }
        return (in16 >> (16u - bi - (uint32_t)nbits)) & mask;
    }
    return (uint32_t)(buf[byte_pos] >> (8 - (int)bi - nbits)) & mask;
}

typedef struct {
    const uint8_t *buf;
    size_t len;
    int bpp;
    int state;
    uint32_t rdp;
    uint32_t prev_v;
    uint32_t count;
} RleDecoder;

static void rle_init(RleDecoder *d254, const uint8_t *buf, size_t len, int bpp)
{
    d254->buf = buf;
    d254->len = len;
    d254->bpp = bpp;
    d254->state = RLE_SINGLE;
    d254->rdp = 0;
    d254->prev_v = 0;
    d254->count = 0;
}

static uint32_t rle_next_px(RleDecoder *d254)
{
    int bpp = d254->bpp;
    const uint8_t *buf = d254->buf;
    size_t len = d254->len;
    if (d254->state == RLE_SINGLE) {
        uint32_t ret = get_bits(buf, len, d254->rdp, bpp);
        if (d254->rdp != 0 && d254->prev_v == ret) {
            d254->count = 0;
            d254->state = RLE_REPEATED;
        }
        d254->prev_v = ret;
        d254->rdp += (uint32_t)bpp;
        return ret;
    }
    if (d254->state == RLE_REPEATED) {
        uint32_t v = get_bits(buf, len, d254->rdp, 1);
        d254->rdp += 1;
        d254->count++;
        if (v == 1) {
            uint32_t ret = d254->prev_v;
            if (d254->count == 11u) {
                d254->count = get_bits(buf, len, d254->rdp, 6);
                d254->rdp += 6;
                if (d254->count != 0) {
                    d254->state = RLE_COUNTER;
                } else {
                    ret = get_bits(buf, len, d254->rdp, bpp);
                    d254->prev_v = ret;
                    d254->rdp += (uint32_t)bpp;
                    d254->state = RLE_SINGLE;
                }
            }
            return ret;
        }
        uint32_t ret = get_bits(buf, len, d254->rdp, bpp);
        d254->prev_v = ret;
        d254->rdp += (uint32_t)bpp;
        d254->state = RLE_SINGLE;
        return ret;
    }
    /* RLE_COUNTER */
    {
        uint32_t ret = d254->prev_v;
        d254->count--;
        if (d254->count == 0) {
            ret = get_bits(buf, len, d254->rdp, bpp);
            d254->prev_v = ret;
            d254->rdp += (uint32_t)bpp;
            d254->state = RLE_SINGLE;
        }
        return ret;
    }
}

static void decompress_line(RleDecoder *dec, int w, uint8_t *line_out)
{
    for (int i = 0; i < w; i++) {
        line_out[i] = (uint8_t)rle_next_px(dec);
    }
}

static void decompress_glyph_to_a8(const uint8_t *comp, size_t comp_len, int w, int h, int bpp, int bitmap_format,
                                   uint8_t *out_a8)
{
    const uint8_t *opa = (bpp == 2) ? OPA2 : ((bpp == 3) ? OPA3 : OPA4);
    int prefilter = (bitmap_format == FMT_COMPRESSED);
    RleDecoder dec;
    rle_init(&dec, comp, comp_len, bpp);

    uint8_t line1[256];
    uint8_t line2[256];
    if (w > 256) {
        return; /* 不应出现 */
    }
    decompress_line(&dec, w, line1);
    int row = 0;
    for (int x = 0; x < w; x++) {
        out_a8[row * w + x] = opa[line1[x] & 0xFF];
    }
    row++;

    if (prefilter) {
        for (int y = 1; y < h; y++) {
            decompress_line(&dec, w, line2);
            for (int x = 0; x < w; x++) {
                line1[x] = (uint8_t)(line2[x] ^ line1[x]);
                out_a8[row * w + x] = opa[line1[x] & 0xFF];
            }
            row++;
        }
    } else {
        for (int y = 1; y < h; y++) {
            decompress_line(&dec, w, line1);
            for (int x = 0; x < w; x++) {
                out_a8[row * w + x] = opa[line1[x] & 0xFF];
            }
            row++;
        }
    }
}

static void decode_plain_a8(const uint8_t *glyph_bitmap, size_t glyph_len, uint32_t bitmap_index, int box_w, int box_h,
                            int bpp, uint8_t *out_a8)
{
    const uint8_t *bitmap_in = glyph_bitmap + bitmap_index;
    (void)glyph_len;
    if (bpp == 1) {
        int i = 0, bi = 0;
        for (int y = 0; y < box_h; y++) {
            for (int x = 0; x < box_w; x++) {
                int bit_in_byte = i & 7;
                uint8_t b;
                if (bit_in_byte == 0) {
                    b = bitmap_in[bi++];
                } else {
                    b = bitmap_in[bi - 1];
                }
                uint8_t bit = (uint8_t)((b >> (7 - bit_in_byte)) & 1);
                out_a8[y * box_w + x] = bit ? 0xFF : 0x00;
                i++;
            }
        }
    } else if (bpp == 2) {
        int i = 0, bi = 0;
        for (int y = 0; y < box_h; y++) {
            for (int x = 0; x < box_w; x++) {
                int bit_in = i & 3;
                uint8_t b;
                if (bit_in == 0) {
                    b = bitmap_in[bi++];
                } else {
                    b = bitmap_in[bi - 1];
                }
                int shift = 6 - 2 * bit_in;
                uint8_t v = (uint8_t)((b >> shift) & 3);
                out_a8[y * box_w + x] = OPA2[v];
                i++;
            }
        }
    } else if (bpp == 4) {
        int i = 0, bi = 0;
        for (int y = 0; y < box_h; y++) {
            for (int x = 0; x < box_w; x++) {
                uint8_t b, v;
                if ((i & 1) == 0) {
                    b = bitmap_in[bi++];
                    v = (uint8_t)((b >> 4) & 0xF);
                } else {
                    b = bitmap_in[bi - 1];
                    v = (uint8_t)(b & 0xF);
                }
                out_a8[y * box_w + x] = OPA4[v];
                i++;
            }
        }
    }
}

static int bisect_left_u16(const uint16_t *arr, int n, uint16_t key)
{
    int lo = 0, hi = n;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (arr[mid] < key) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

static bool try_parse_dsc(const uint8_t *data, size_t len, uint32_t dsc_off)
{
    if (dsc_off + 20 > len) {
        return false;
    }
    uint32_t o_bm = read_u32_le(data + dsc_off);
    uint32_t o_gd = read_u32_le(data + dsc_off + 4);
    uint32_t o_cm = read_u32_le(data + dsc_off + 8);
    uint32_t abs_bm = dsc_off + o_bm;
    uint32_t abs_gd = dsc_off + o_gd;
    uint32_t abs_cm = dsc_off + o_cm;
    if (abs_bm >= len || abs_gd >= len || abs_cm >= len) {
        return false;
    }
    if (abs_bm + 4 > len || abs_gd + 8 > len) {
        return false;
    }
    uint16_t pack = (uint16_t)data[dsc_off + 18] | ((uint16_t)data[dsc_off + 19] << 8);
    uint32_t cmap_num = pack & 0x1FFu;
    uint32_t bpp = (pack >> 9) & 0xFu;
    uint32_t bitmap_format = (pack >> 14) & 3u;
    if (cmap_num == 0 || cmap_num > 511 || bpp < 1 || bpp > 8 || bitmap_format > 2) {
        return false;
    }
    if (abs_gd + 16 <= len) {
        uint16_t bw16 = (uint16_t)data[abs_gd + 8] | ((uint16_t)data[abs_gd + 9] << 8);
        uint16_t bh16 = (uint16_t)data[abs_gd + 10] | ((uint16_t)data[abs_gd + 11] << 8);
        if (bw16 <= 512 && bh16 <= 512) {
            return true;
        }
    }
    uint32_t w0 = read_u32_le(data + abs_gd);
    uint8_t box_w = data[abs_gd + 4];
    uint8_t box_h = data[abs_gd + 5];
    (void)w0;
    if (box_w > 200 || box_h > 200) {
        return false;
    }
    return true;
}

static uint32_t find_dsc_offset(const uint8_t *data, size_t len)
{
    const uint32_t min_dsc = 28;
    for (uint32_t hdr_off = 0; hdr_off + 4 <= len && hdr_off < 256; hdr_off += 4) {
        uint32_t rel = read_u32_le(data + hdr_off);
        if (rel < min_dsc || rel >= len - 32) {
            continue;
        }
        if (try_parse_dsc(data, len, rel)) {
            return rel;
        }
    }
    return 0;
}

static void detect_glyph_stride_from_blob(const uint8_t *blob, size_t blob_len, uint32_t glyph_dsc_off,
                                         bin_font_ctx_t *ctx)
{
    size_t avail = (glyph_dsc_off < blob_len) ? (blob_len - glyph_dsc_off) : 0;
    if (avail < 16) {
        ctx->glyph_stride = 8;
        return;
    }
    int score8 = 0, score16 = 0;
    int last = (int)(avail / 16);
    if (last > 96) {
        last = 96;
    }
    for (int gid = 0; gid < last; gid++) {
        uint32_t off16 = glyph_dsc_off + (uint32_t)gid * 16;
        uint32_t off8 = glyph_dsc_off + (uint32_t)gid * 8;
        if (off16 + 16 > blob_len || off8 + 8 > blob_len) {
            break;
        }
        uint16_t bw16 = (uint16_t)blob[off16 + 8] | ((uint16_t)blob[off16 + 9] << 8);
        uint16_t bh16 = (uint16_t)blob[off16 + 10] | ((uint16_t)blob[off16 + 11] << 8);
        uint8_t bw8 = blob[off8 + 4];
        uint8_t bh8 = blob[off8 + 5];
        if (bw16 >= 1 && bw16 <= 256 && bh16 >= 1 && bh16 <= 256) {
            score16++;
        }
        if (bw8 >= 1 && bw8 <= 255 && bh8 >= 1 && bh8 <= 255) {
            score8++;
        }
    }
    ctx->glyph_stride = (score16 > score8) ? 16 : 8;
}

static uint32_t estimate_glyph_count_from_blob(const uint8_t *blob, size_t blob_len, bin_font_ctx_t *ctx)
{
    uint32_t off = ctx->glyph_dsc_off;
    uint32_t stride = ctx->glyph_stride;
    if (off > blob_len - stride) {
        return 1;
    }
    uint32_t n = (uint32_t)((blob_len - off) / stride);
    if (n > 65535u) {
        n = 65535u;
    }
    if (n < 1) {
        n = 1;
    }
    if (ctx->bitmap_format == FMT_PLAIN && n > 1) {
        uint32_t last_off = ctx->glyph_dsc_off + (n - 1) * stride;
        if (stride == 8 && last_off + 4 <= blob_len) {
            uint32_t w0_last = read_u32_le(blob + last_off);
            uint32_t bi_last = w0_last & ((1u << 20) - 1u);
            while (n > 1 && ctx->glyph_bitmap_off + bi_last > blob_len) {
                n--;
                last_off = ctx->glyph_dsc_off + (n - 1) * stride;
                w0_last = read_u32_le(blob + last_off);
                bi_last = w0_last & ((1u << 20) - 1u);
            }
        } else if (stride == 16 && last_off + 4 <= blob_len) {
            uint32_t bi_last = read_u32_le(blob + last_off);
            while (n > 1 && ctx->glyph_bitmap_off + bi_last > blob_len) {
                n--;
                last_off = ctx->glyph_dsc_off + (n - 1) * stride;
                bi_last = read_u32_le(blob + last_off);
            }
        }
    }
    return n;
}

static long file_payload_offset(const bin_font_ctx_t *ctx, size_t logical_off)
{
    return (long)ctx->file_data_skip + (long)logical_off;
}

static bool file_read_at(FILE *fp, long abs_pos, void *buf, size_t len)
{
    if (!fp || !buf || len == 0) {
        return false;
    }
    if (fseek(fp, abs_pos, SEEK_SET) != 0) {
        return false;
    }
    return fread(buf, 1, len, fp) == len;
}

static void bin_cmaps_free(bin_font_ctx_t *ctx)
{
    if (!ctx->cmaps) {
        return;
    }
    for (int i = 0; i < ctx->cmap_num; i++) {
        heap_caps_free(ctx->cmaps[i].sparse_keys);
        heap_caps_free(ctx->cmaps[i].sparse_ofs);
        heap_caps_free(ctx->cmaps[i].format0_full);
    }
    heap_caps_free(ctx->cmaps);
    ctx->cmaps = NULL;
}

/* 不 fclose(fp)：文件由调用方长期持有 */
static void bin_font_free(bin_font_ctx_t *ctx)
{
    bin_cmaps_free(ctx);
    ctx->fp = NULL;
    memset(ctx, 0, sizeof(*ctx));
}

static uint32_t unicode_to_gid(bin_font_ctx_t *ctx, uint32_t letter)
{
    if (letter == 0) {
        return 0;
    }
    for (int idx = 0; idx < ctx->cmap_num; idx++) {
        bin_cmap_t *cm = &ctx->cmaps[idx];
        if (letter < cm->range_start) {
            continue;
        }
        uint32_t rcp = letter - cm->range_start;
        if (rcp >= cm->range_length) {
            continue;
        }
        if (cm->ctype == CMAP_FORMAT0_TINY) {
            return (uint32_t)cm->glyph_id_start + rcp;
        }
        if (cm->ctype == CMAP_FORMAT0_FULL) {
            if (!cm->format0_full || rcp >= cm->range_length) {
                continue;
            }
            uint8_t delta = cm->format0_full[rcp];
            return (uint32_t)cm->glyph_id_start + delta;
        }
        if (cm->ctype == CMAP_SPARSE_TINY) {
            if (!cm->sparse_keys || cm->list_length == 0) {
                continue;
            }
            int sp = bisect_left_u16(cm->sparse_keys, cm->list_length, (uint16_t)rcp);
            if (sp < cm->list_length && cm->sparse_keys[sp] == (uint16_t)rcp) {
                return (uint32_t)cm->glyph_id_start + (uint32_t)sp;
            }
        }
        if (cm->ctype == CMAP_SPARSE_FULL) {
            if (!cm->sparse_keys || !cm->sparse_ofs || cm->list_length == 0) {
                continue;
            }
            int sp = bisect_left_u16(cm->sparse_keys, cm->list_length, (uint16_t)rcp);
            if (sp < cm->list_length && cm->sparse_keys[sp] == (uint16_t)rcp) {
                return (uint32_t)cm->glyph_id_start + cm->sparse_ofs[sp];
            }
        }
    }
    return 0;
}

/* 读一条 glyph 描述记录；box 为 0 的空槽也返回 true（仅 IO/越界失败时返回 false）。 */
static bool bin_read_glyph_dsc_any(bin_font_ctx_t *ctx, uint32_t gid, uint16_t *adv_w, uint8_t *box_w, uint8_t *box_h,
                                   int16_t *ofs_x, int16_t *ofs_y, uint32_t *bitmap_index)
{
    if (!ctx->fp || gid >= ctx->glyph_count) {
        return false;
    }
    uint32_t off = ctx->glyph_dsc_off + gid * ctx->glyph_stride;
    if (off + ctx->glyph_stride > ctx->size) {
        return false;
    }
    uint8_t dsc[16];
    if (!file_read_at(ctx->fp, file_payload_offset(ctx, off), dsc, ctx->glyph_stride)) {
        return false;
    }
    if (ctx->glyph_stride == 16) {
        /* LVGL LVFONT_LARGE / lv_font_fmt_txt: <IIHHhh> bitmap_index, adv_w, box_w, box_h, ofs_x, ofs_y */
        *bitmap_index = read_u32_le(dsc);
        uint32_t aw = read_u32_le(dsc + 4);
        *adv_w = (aw > 65535u) ? 65535u : (uint16_t)aw;
        uint16_t bw16 = read_u16_le(dsc + 8);
        uint16_t bh16 = read_u16_le(dsc + 10);
        if (bw16 > 255 || bh16 > 255) {
            ESP_LOGW(TAG, "gid=%u box %ux%u >255, clamp for lvgl dsc", (unsigned)gid, (unsigned)bw16,
                     (unsigned)bh16);
        }
        *box_w = (uint8_t)(bw16 > 255 ? 255 : bw16);
        *box_h = (uint8_t)(bh16 > 255 ? 255 : bh16);
        *ofs_x = read_i16_le(dsc + 12);
        *ofs_y = read_i16_le(dsc + 14);
    } else {
        uint32_t w0 = read_u32_le(dsc);
        *bitmap_index = w0 & ((1u << 20) - 1u);
        *adv_w = (uint16_t)(w0 >> 20);
        *box_w = dsc[4];
        *box_h = dsc[5];
        *ofs_x = (int16_t)(int8_t)dsc[6];
        *ofs_y = (int16_t)(int8_t)dsc[7];
    }
    return true;
}

static bool bin_read_glyph_dsc(bin_font_ctx_t *ctx, uint32_t gid, uint16_t *adv_w, uint8_t *box_w, uint8_t *box_h,
                               int16_t *ofs_x, int16_t *ofs_y, uint32_t *bitmap_index)
{
    if (!bin_read_glyph_dsc_any(ctx, gid, adv_w, box_w, box_h, ofs_x, ofs_y, bitmap_index)) {
        return false;
    }
    return (*box_w > 0 && *box_h > 0);
}

static bool bin_read_glyph_bitmap_index(bin_font_ctx_t *ctx, uint32_t gid, uint32_t *bitmap_index_out)
{
    if (!ctx->fp || gid >= ctx->glyph_count) {
        return false;
    }
    uint32_t off = ctx->glyph_dsc_off + gid * ctx->glyph_stride;
    if (off + ctx->glyph_stride > ctx->size) {
        return false;
    }
    uint8_t dsc[16];
    if (!file_read_at(ctx->fp, file_payload_offset(ctx, off), dsc, ctx->glyph_stride)) {
        return false;
    }
    if (ctx->glyph_stride == 16) {
        *bitmap_index_out = read_u32_le(dsc);
    } else {
        uint32_t w0 = read_u32_le(dsc);
        *bitmap_index_out = w0 & ((1u << 20) - 1u);
    }
    return true;
}

#define FONT_BITMAP_READ_MAX (48 * 1024)

static bool bin_raster_glyph(bin_font_ctx_t *ctx, uint32_t gid, uint8_t *out_a8, int *out_w, int *out_h)
{
    uint16_t adv_w;
    uint8_t box_w, box_h;
    int16_t ox, oy;
    uint32_t bitmap_index;
    if (!bin_read_glyph_dsc(ctx, gid, &adv_w, &box_w, &box_h, &ox, &oy, &bitmap_index)) {
        (void)adv_w;
        (void)ox;
        (void)oy;
        return false;
    }
    if ((int)box_w * (int)box_h > SCRATCH_PX) {
        ESP_LOGW(TAG, "glyph too large %dx%d", box_w, box_h);
        return false;
    }

    size_t bm_base = (size_t)ctx->glyph_bitmap_off + (size_t)bitmap_index;
    if (bm_base >= ctx->size) {
        return false;
    }

    size_t raw_len = ctx->size - bm_base;
    if (gid + 1u < ctx->glyph_count) {
        uint32_t next_bi = 0;
        if (bin_read_glyph_bitmap_index(ctx, gid + 1u, &next_bi)) {
            size_t next_abs = (size_t)ctx->glyph_bitmap_off + (size_t)next_bi;
            if (next_abs > bm_base && next_abs <= ctx->size) {
                raw_len = next_abs - bm_base;
            }
        }
    }

    if (ctx->bitmap_format != FMT_PLAIN && raw_len > FONT_BITMAP_READ_MAX) {
        raw_len = FONT_BITMAP_READ_MAX;
    }

    uint8_t *raw = (uint8_t *)heap_caps_malloc(raw_len, MALLOC_CAP_INTERNAL);
    if (!raw) {
        raw = (uint8_t *)heap_caps_malloc(raw_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!raw) {
        return false;
    }

    bool ok = file_read_at(ctx->fp, file_payload_offset(ctx, bm_base), raw, raw_len);
    if (!ok) {
        heap_caps_free(raw);
        return false;
    }

    if (ctx->bitmap_format == FMT_PLAIN) {
        decode_plain_a8(raw, raw_len, 0, box_w, box_h, ctx->bpp, out_a8);
    } else {
        decompress_glyph_to_a8(raw, raw_len, box_w, box_h, ctx->bpp, ctx->bitmap_format, out_a8);
    }
    heap_caps_free(raw);
    *out_w = box_w;
    *out_h = box_h;
    return true;
}

static esp_err_t cmap_tables_copy(bin_font_ctx_t *ctx, const uint8_t *data, size_t size, uint32_t cmaps_base)
{
    ctx->cmaps = (bin_cmap_t *)heap_caps_calloc(ctx->cmap_num, sizeof(bin_cmap_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ctx->cmaps) {
        return ESP_ERR_NO_MEM;
    }

    uint32_t ptr = cmaps_base;
    for (int i = 0; i < ctx->cmap_num; i++) {
        if (ptr + 20 > size) {
            ESP_LOGE(TAG, "cbin: cmap truncated");
            return ESP_FAIL;
        }
        bin_cmap_t *cm = &ctx->cmaps[i];
        cm->range_start = read_u32_le(data + ptr);
        cm->range_length = (uint16_t)data[ptr + 4] | ((uint16_t)data[ptr + 5] << 8);
        cm->glyph_id_start = (uint16_t)data[ptr + 6] | ((uint16_t)data[ptr + 7] << 8);
        uint32_t o_ul = read_u32_le(data + ptr + 8);
        uint32_t o_gio = read_u32_le(data + ptr + 12);
        cm->list_length = (uint16_t)data[ptr + 16] | ((uint16_t)data[ptr + 17] << 8);
        cm->ctype = data[ptr + 18];
        ptr += 20;
        cm->sparse_keys = NULL;
        cm->sparse_ofs = NULL;
        cm->format0_full = NULL;

        uint32_t ul_abs = o_ul ? (cmaps_base + o_ul) : 0;
        uint32_t gio_abs = o_gio ? (cmaps_base + o_gio) : 0;

        if (cm->ctype == CMAP_SPARSE_TINY || cm->ctype == CMAP_SPARSE_FULL) {
            if (o_ul && cm->list_length > 0) {
                size_t nbytes = (size_t)cm->list_length * 2u;
                if (ul_abs + nbytes > size) {
                    return ESP_FAIL;
                }
                cm->sparse_keys = (uint16_t *)heap_caps_malloc(nbytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (!cm->sparse_keys) {
                    return ESP_ERR_NO_MEM;
                }
                memcpy(cm->sparse_keys, data + ul_abs, nbytes);
            }
            if (cm->ctype == CMAP_SPARSE_FULL && o_gio && cm->list_length > 0) {
                size_t nbytes = (size_t)cm->list_length * 2u;
                if (gio_abs + nbytes > size) {
                    return ESP_FAIL;
                }
                cm->sparse_ofs = (uint16_t *)heap_caps_malloc(nbytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (!cm->sparse_ofs) {
                    return ESP_ERR_NO_MEM;
                }
                memcpy(cm->sparse_ofs, data + gio_abs, nbytes);
            }
        } else if (cm->ctype == CMAP_FORMAT0_FULL && o_gio && cm->range_length > 0) {
            if (gio_abs + cm->range_length > size) {
                return ESP_FAIL;
            }
            cm->format0_full = (uint8_t *)heap_caps_malloc(cm->range_length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!cm->format0_full) {
                return ESP_ERR_NO_MEM;
            }
            memcpy(cm->format0_full, data + gio_abs, cm->range_length);
        }
    }
    return ESP_OK;
}

/* 启动时整文件读入 tmp 只做解析；cmap 拷贝到内部堆后释放 tmp。之后字模只通过 ctx->fp 随机读。 */
static esp_err_t font_bind_file(FILE *fp, bin_font_ctx_t *ctx)
{
    if (!fp || !ctx) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->fp = fp;

    if (fseek(fp, 0, SEEK_END) != 0) {
        ctx->fp = NULL;
        return ESP_FAIL;
    }
    long f_sz = ftell(fp);
    if (f_sz < 32) {
        ctx->fp = NULL;
        return ESP_FAIL;
    }
    rewind(fp);

    uint8_t *tmp = (uint8_t *)heap_caps_malloc((size_t)f_sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!tmp) {
        tmp = (uint8_t *)heap_caps_malloc((size_t)f_sz, MALLOC_CAP_INTERNAL);
    }
    if (!tmp) {
        ctx->fp = NULL;
        return ESP_ERR_NO_MEM;
    }
    if (fread(tmp, 1, (size_t)f_sz, fp) != (size_t)f_sz) {
        heap_caps_free(tmp);
        ctx->fp = NULL;
        return ESP_FAIL;
    }

    size_t logical = (size_t)f_sz;
    ctx->file_data_skip = 0;
    if (logical >= 2 && tmp[0] == 0x5A && tmp[1] == 0x5A) {
        memmove(tmp, tmp + 2, logical - 2);
        logical -= 2;
        ctx->file_data_skip = 2;
    }
    if (logical < 32) {
        heap_caps_free(tmp);
        ctx->fp = NULL;
        return ESP_FAIL;
    }

    ctx->size = logical;

    uint32_t dsc_off = find_dsc_offset(tmp, logical);
    if (dsc_off == 0) {
        ESP_LOGE(TAG, "cbin: dsc not found");
        heap_caps_free(tmp);
        ctx->fp = NULL;
        return ESP_FAIL;
    }
    ctx->dsc_off = dsc_off;

    uint32_t o_bm = read_u32_le(tmp + dsc_off);
    uint32_t o_gd = read_u32_le(tmp + dsc_off + 4);
    uint32_t o_cm = read_u32_le(tmp + dsc_off + 8);
    uint16_t pack = (uint16_t)tmp[dsc_off + 18] | ((uint16_t)tmp[dsc_off + 19] << 8);
    ctx->cmap_num = (uint16_t)(pack & 0x1FFu);
    ctx->bpp = (uint8_t)((pack >> 9) & 0xFu);
    ctx->bitmap_format = (uint8_t)((pack >> 14) & 3u);

    ctx->glyph_bitmap_off = dsc_off + o_bm;
    ctx->glyph_dsc_off = dsc_off + o_gd;
    ctx->cmaps_base = dsc_off + o_cm;

    if (dsc_off >= 32 && logical >= 20) {
        ctx->line_height = read_i32_le(tmp + 12);
        ctx->base_line = read_i32_le(tmp + 16);
    }

    detect_glyph_stride_from_blob(tmp, logical, ctx->glyph_dsc_off, ctx);
    ctx->glyph_count = estimate_glyph_count_from_blob(tmp, logical, ctx);

    esp_err_t err = cmap_tables_copy(ctx, tmp, logical, ctx->cmaps_base);
    heap_caps_free(tmp);
    if (err != ESP_OK) {
        bin_cmaps_free(ctx);
        ctx->fp = NULL;
        return err;
    }

    rewind(fp);
    ESP_LOGI(TAG, "cbin: FILE=%p skip=%u bpp=%u fmt=%u stride=%u glyphs≈%u lh=%ld base=%ld",
             (void *)fp, (unsigned)ctx->file_data_skip, (unsigned)ctx->bpp, (unsigned)ctx->bitmap_format,
             (unsigned)ctx->glyph_stride, (unsigned)ctx->glyph_count, (long)ctx->line_height,
             (long)ctx->base_line);
    return ESP_OK;
}

static bool a8_to_a1_packed_msb(const uint8_t *a8, int w, int h, uint8_t *out, size_t out_cap)
{
    uint32_t row_b = (uint32_t)((w + 7) / 8);
    if ((size_t)row_b * (size_t)h > out_cap) {
        return false;
    }
    memset(out, 0, (size_t)row_b * (size_t)h);
    for (int y = 0; y < h; y++) {
        uint8_t *row = out + (size_t)y * row_b;
        for (int x = 0; x < w; x++) {
            if (a8[y * w + x] >= 128) {
                row[x / 8] |= (uint8_t)(1u << (7 - (x % 8)));
            }
        }
    }
    return true;
}

static bool bin_glyph_dsc_cb_a8(const lv_font_t *font, lv_font_glyph_dsc_t *dsc_out, uint32_t unicode_letter,
                                uint32_t letter_next, bin_font_ctx_t *ctx)
{
    (void)font;
    (void)letter_next;
    if (!ctx->fp) {
        return false;
    }
    uint32_t gid = unicode_to_gid(ctx, unicode_letter);
    uint16_t adv_w;
    uint8_t box_w, box_h;
    int16_t ofs_x, ofs_y;
    uint32_t bitmap_index;
    if (!bin_read_glyph_dsc(ctx, gid, &adv_w, &box_w, &box_h, &ofs_x, &ofs_y, &bitmap_index)) {
        return false;
    }

    uint16_t adv_px = (uint16_t)((uint32_t)adv_w + 8u) >> 4;
    if (adv_px == 0u) {
        adv_px = box_w ? box_w : 1u;
    }

    dsc_out->box_w = box_w;
    dsc_out->box_h = box_h;
    dsc_out->ofs_x = ofs_x;
    dsc_out->ofs_y = ofs_y;
    dsc_out->format = LV_FONT_GLYPH_FORMAT_A8;
    dsc_out->stride = lv_draw_buf_width_to_stride(box_w, LV_COLOR_FORMAT_A8);
    dsc_out->gid.index = unicode_letter;
    dsc_out->adv_w = adv_px;
    dsc_out->is_placeholder = false;
    return true;
}

static bool myGetGlyphDscCb_bin20(const lv_font_t *font, lv_font_glyph_dsc_t *dsc_out, uint32_t unicode_letter,
                                  uint32_t letter_next)
{
    return bin_glyph_dsc_cb_a8(font, dsc_out, unicode_letter, letter_next, &s_ctx20);
}

static bool myGetGlyphDscCb_bin16(const lv_font_t *font, lv_font_glyph_dsc_t *dsc_out, uint32_t unicode_letter,
                                  uint32_t letter_next)
{
    return bin_glyph_dsc_cb_a8(font, dsc_out, unicode_letter, letter_next, &s_ctx16);
}

static const void *myGetGlyphBitmapCb_bin(const lv_font_glyph_dsc_t *dsc, lv_draw_buf_t *draw_buf,
                                          bin_font_ctx_t *ctx)
{
    if (!dsc || !draw_buf || !draw_buf->data || !ctx->fp) {
        return NULL;
    }
    uint32_t unicode_letter = dsc->gid.index;
    uint32_t gid = unicode_to_gid(ctx, unicode_letter);

    int bw = 0, bh = 0;
    if (!bin_raster_glyph(ctx, gid, s_glyph_a8, &bw, &bh)) {
        return NULL;
    }
    if (bw != (int)dsc->box_w || bh != (int)dsc->box_h) {
        return NULL;
    }

    uint32_t dst_stride = lv_draw_buf_width_to_stride(bw, LV_COLOR_FORMAT_A8);
    uint8_t *bitmap_out = (uint8_t *)draw_buf->data;

    for (int y = 0; y < bh; y++) {
        const uint8_t *src_row = s_glyph_a8 + y * bw;
        uint8_t *dst_row = bitmap_out + y * dst_stride;
        memcpy(dst_row, src_row, bw);
        if ((int)dst_stride > bw) {
            memset(dst_row + bw, 0, dst_stride - bw);
        }
    }

    return draw_buf;
}

static const void *myGetGlyphBitmapCb_bin20(lv_font_glyph_dsc_t *dsc, lv_draw_buf_t *draw_buf)
{
    return myGetGlyphBitmapCb_bin(dsc, draw_buf, &s_ctx20);
}

static const void *myGetGlyphBitmapCb_bin16(lv_font_glyph_dsc_t *dsc, lv_draw_buf_t *draw_buf)
{
    return myGetGlyphBitmapCb_bin(dsc, draw_buf, &s_ctx16);
}

/* 启动调试用：从 cbin 导出字母 'A'（U+0041）的字模关键信息 */
static void font_debug_dump_glyph_A(bin_font_ctx_t *ctx, const char *label)
{
    if (!ctx || !ctx->fp) {
        ESP_LOGW(TAG, "debug[%s]: no ctx/fp", label ? label : "?");
        return;
    }

    const uint32_t u = (uint32_t)'A';
    uint32_t gid = unicode_to_gid(ctx, u);
    ESP_LOGI(TAG, "debug[%s]: 'A' U+0041 -> gid=%u (glyph_count=%u)", label, (unsigned)gid,
             (unsigned)ctx->glyph_count);

    uint16_t adv_w;
    uint8_t box_w, box_h;
    int16_t ofs_x, ofs_y;
    uint32_t bitmap_index;
    if (!bin_read_glyph_dsc(ctx, gid, &adv_w, &box_w, &box_h, &ofs_x, &ofs_y, &bitmap_index)) {
        ESP_LOGW(TAG, "debug[%s]: bin_read_glyph_dsc FAILED (缺字或 gid 无效?)", label);
        return;
    }

    ESP_LOGI(TAG,
             "debug[%s]: dsc adv_w=%u box=%ux%u ofs=(%d,%d) bitmap_index=%u fmt=bpp%u %s", label,
             (unsigned)adv_w, (unsigned)box_w, (unsigned)box_h, (int)ofs_x, (int)ofs_y,
             (unsigned)bitmap_index, (unsigned)ctx->bpp,
             ctx->bitmap_format == FMT_PLAIN ? "PLAIN" : "COMPRESSED");

    size_t bm_base = (size_t)ctx->glyph_bitmap_off + (size_t)bitmap_index;
    if (bm_base < ctx->size) {
        size_t raw_len = ctx->size - bm_base;
        if (gid + 1u < ctx->glyph_count) {
            uint32_t next_bi = 0;
            if (bin_read_glyph_bitmap_index(ctx, gid + 1u, &next_bi)) {
                size_t next_abs = (size_t)ctx->glyph_bitmap_off + (size_t)next_bi;
                if (next_abs > bm_base && next_abs <= ctx->size) {
                    raw_len = next_abs - bm_base;
                }
            }
        }
        size_t dump_n = raw_len > 32 ? 32 : raw_len;
        uint8_t raw_head[32];
        if (dump_n && file_read_at(ctx->fp, file_payload_offset(ctx, bm_base), raw_head, dump_n)) {
            ESP_LOGI(TAG, "debug[%s]: bitmap file off=%ld raw_len=%u dump first %u bytes:", label,
                     (long)file_payload_offset(ctx, bm_base), (unsigned)raw_len, (unsigned)dump_n);
            ESP_LOG_BUFFER_HEX(TAG, raw_head, dump_n);
        } else {
            ESP_LOGW(TAG, "debug[%s]: read bitmap head failed", label);
        }
    }

    int rw = 0, rh = 0;
    if (!bin_raster_glyph(ctx, gid, s_glyph_a8, &rw, &rh)) {
        ESP_LOGW(TAG, "debug[%s]: bin_raster_glyph FAILED", label);
        return;
    }

    int amin = 255, amax = 0, asum = 0;
    for (int i = 0; i < rw * rh; i++) {
        int v = s_glyph_a8[i];
        if (v < amin) {
            amin = v;
        }
        if (v > amax) {
            amax = v;
        }
        asum += v;
    }
    ESP_LOGI(TAG, "debug[%s]: A8 raster %dx%d min=%d max=%d sum=%d", label, rw, rh, amin, amax, asum);

    if (a8_to_a1_packed_msb(s_glyph_a8, rw, rh, _pBits, PBITS_SIZE)) {
        size_t a1bytes = (size_t)((rw + 7) / 8) * (size_t)rh;
        size_t show = a1bytes > 24 ? 24 : a1bytes;
        ESP_LOGI(TAG, "debug[%s]: A1 packed first %u/%u bytes:", label, (unsigned)show, (unsigned)a1bytes);
        ESP_LOG_BUFFER_HEX(TAG, _pBits, show);
    } else {
        ESP_LOGW(TAG, "debug[%s]: a8_to_a1_packed_msb failed (_pBits 太小?)", label);
    }
}

esp_err_t font_bins_init(FILE *f_puhui_20, FILE *f_puhui_16)
{
    if (!f_puhui_20 || !f_puhui_16) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_glyph_a8) {
        s_glyph_a8 = (uint8_t *)heap_caps_malloc(SCRATCH_PX, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_glyph_a8) {
            ESP_LOGE(TAG, "PSRAM alloc s_glyph_a8 failed (%d bytes)", SCRATCH_PX);
            return ESP_ERR_NO_MEM;
        }
    }
    if (!_pBits) {
        _pBits = (uint8_t *)heap_caps_aligned_alloc(4, PBITS_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!_pBits) {
            ESP_LOGE(TAG, "PSRAM alloc _pBits failed (%d bytes)", PBITS_SIZE);
            return ESP_ERR_NO_MEM;
        }
    }

    bin_font_free(&s_ctx20);
    bin_font_free(&s_ctx16);

    esp_err_t e = font_bind_file(f_puhui_20, &s_ctx20);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "bind font20 bin failed: %s", esp_err_to_name(e));
        return e;
    }
    e = font_bind_file(f_puhui_16, &s_ctx16);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "bind font16 bin failed: %s", esp_err_to_name(e));
        bin_font_free(&s_ctx20);
        return e;
    }

    memset(&dfrobot_font_20, 0, sizeof(dfrobot_font_20));
    dfrobot_font_20.get_glyph_dsc = myGetGlyphDscCb_bin20;
    dfrobot_font_20.get_glyph_bitmap = myGetGlyphBitmapCb_bin20;
    dfrobot_font_20.line_height = (s_ctx20.line_height > 0) ? (uint16_t)s_ctx20.line_height : 20;
    dfrobot_font_20.base_line = (s_ctx20.base_line > 0) ? (uint16_t)s_ctx20.base_line : 4;
    dfrobot_font_20.subpx = LV_FONT_SUBPX_NONE;

    memset(&dfrobot_font_16, 0, sizeof(dfrobot_font_16));
    dfrobot_font_16.get_glyph_dsc = myGetGlyphDscCb_bin16;
    dfrobot_font_16.get_glyph_bitmap = myGetGlyphBitmapCb_bin16;
    dfrobot_font_16.line_height = (s_ctx16.line_height > 0) ? (uint16_t)s_ctx16.line_height : 16;
    dfrobot_font_16.base_line = (s_ctx16.base_line > 0) ? (uint16_t)s_ctx16.base_line : 3;
    dfrobot_font_16.subpx = LV_FONT_SUBPX_NONE;

    font_debug_dump_glyph_A(&s_ctx20, "font20.bin");
    font_debug_dump_glyph_A(&s_ctx16, "font16.bin");

    ESP_LOGI(TAG, "puhui bin fonts OK (dfrobot_font_20 / dfrobot_font_16), FILE* kept open");
    return ESP_OK;
}
