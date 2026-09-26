#include "lvgl_bmp_encoder.h"

#include <stdlib.h>
#include <string.h>

#define LVGL_BMP_HEADER_BYTES 54U

static uint8_t lvgl_bmp_expand5(uint16_t value)
{
    value &= 0x1F;
    return (uint8_t)((value << 3) | (value >> 2));
}

static uint8_t lvgl_bmp_expand6(uint16_t value)
{
    value &= 0x3F;
    return (uint8_t)((value << 2) | (value >> 4));
}

static void lvgl_bmp_encode_row_bgr(const uint16_t *src, uint8_t *dst,
                                    uint32_t width)
{
    for (uint32_t x = 0; x < width; ++x) {
        uint16_t color = src[x];
        dst[x * 3U + 0U] = lvgl_bmp_expand5(color);
        dst[x * 3U + 1U] = lvgl_bmp_expand6(color >> 5);
        dst[x * 3U + 2U] = lvgl_bmp_expand5(color >> 11);
    }
}

static void lvgl_bmp_build_header(uint8_t *header, uint32_t width,
                                  uint32_t height, uint32_t pixel_bytes)
{
    uint32_t total = LVGL_BMP_HEADER_BYTES + pixel_bytes;
    memset(header, 0, LVGL_BMP_HEADER_BYTES);
    header[0] = 'B';
    header[1] = 'M';
    header[2] = (uint8_t)total;
    header[3] = (uint8_t)(total >> 8);
    header[4] = (uint8_t)(total >> 16);
    header[5] = (uint8_t)(total >> 24);
    header[10] = LVGL_BMP_HEADER_BYTES;
    header[14] = 40U;
    header[18] = (uint8_t)width;
    header[19] = (uint8_t)(width >> 8);
    header[20] = (uint8_t)(width >> 16);
    header[21] = (uint8_t)(width >> 24);
    header[22] = (uint8_t)height;
    header[23] = (uint8_t)(height >> 8);
    header[24] = (uint8_t)(height >> 16);
    header[25] = (uint8_t)(height >> 24);
    header[26] = 1U;
    header[28] = 24U;
    header[34] = (uint8_t)pixel_bytes;
    header[35] = (uint8_t)(pixel_bytes >> 8);
    header[36] = (uint8_t)(pixel_bytes >> 16);
    header[37] = (uint8_t)(pixel_bytes >> 24);
}

bool lvgl_bmp_encode_rgb565(const uint8_t *framebuffer, uint32_t width,
                            uint32_t height, uint32_t stride,
                            uint8_t **bmp_buf, size_t *bmp_len,
                            lvgl_bmp_alloc_fn_t alloc_fn)
{
    if (framebuffer == NULL || width == 0U || height == 0U ||
        stride < width * 2U || bmp_buf == NULL || bmp_len == NULL) {
        return false;
    }

    *bmp_buf = NULL;
    *bmp_len = 0;
    uint32_t row_bytes = width * 3U;
    uint32_t row_stride = (row_bytes + 3U) & ~3U;
    uint32_t pixel_bytes = row_stride * height;
    size_t total = LVGL_BMP_HEADER_BYTES + (size_t)pixel_bytes;
    uint8_t *bmp =
        (uint8_t *)(alloc_fn != NULL ? alloc_fn(total) : malloc(total));
    if (bmp == NULL) {
        return false;
    }

    lvgl_bmp_build_header(bmp, width, height, pixel_bytes);
    uint8_t *pixel_start = bmp + LVGL_BMP_HEADER_BYTES;
    for (uint32_t y = 0; y < height; ++y) {
        uint32_t source_y = height - 1U - y;
        const uint16_t *source_row =
            (const uint16_t *)(framebuffer + source_y * stride);
        uint8_t *destination_row = pixel_start + y * row_stride;
        lvgl_bmp_encode_row_bgr(source_row, destination_row, width);
        if (row_stride > row_bytes) {
            memset(destination_row + row_bytes, 0, row_stride - row_bytes);
        }
    }

    *bmp_buf = bmp;
    *bmp_len = total;
    return true;
}
