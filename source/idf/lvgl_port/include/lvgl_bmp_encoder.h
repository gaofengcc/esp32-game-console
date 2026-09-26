#ifndef LVGL_BMP_ENCODER_H
#define LVGL_BMP_ENCODER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void *(*lvgl_bmp_alloc_fn_t)(size_t size);

bool lvgl_bmp_encode_rgb565(const uint8_t *framebuffer, uint32_t width,
                            uint32_t height, uint32_t stride,
                            uint8_t **bmp_buf, size_t *bmp_len,
                            lvgl_bmp_alloc_fn_t alloc_fn);

#endif /* LVGL_BMP_ENCODER_H */
