/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* Planar YCbCr 4:2:0 to 32-bit pixels, BT.601 with studio range, the colour
   space of MPEG-1: with NEON sixteen pixels at a time, in C otherwise. Both
   use the same fixed-point numbers and give the same pixels.
   sdk/lib, docs/ARM64-APPS.md. */
#include "movie.h"
#if defined(__ARM_NEON) && !defined(PLAYER_NO_NEON)
#include <arm_neon.h>
#define COLOR_NEON 1
#endif

/* In units of 1/64: 1.164 = 74.5, 1.596 = 102, 0.813 = 52, 0.392 = 25,
   2.017 = 129. */
static inline uint32_t Pixel(int y, int cb, int cr)
{
    /* y from 0 to 255 as it is stored; cb and cr less 128. */
    const int luma = ((y * 149) >> 1) - 1192; /* (y - 16) * 74.5 */
    int red = (luma + 102 * cr + 32) >> 6, green = (luma - 52 * cr - 25 * cb + 32) >> 6, blue = (luma + 129 * cb + 32) >> 6;
    if (red < 0) red = 0; else if (red > 255) red = 255;
    if (green < 0) green = 0; else if (green > 255) green = 255;
    if (blue < 0) blue = 0; else if (blue > 255) blue = 255;
    return (uint32_t)blue | (uint32_t)green << 8 | (uint32_t)red << 16;
}

void ColorYuv420ToBgrx(const uint8_t *y, const uint8_t *cb, const uint8_t *cr, int y_stride, int c_stride, int width,
                       int height, uint32_t *out, int out_stride)
{
    for (int row = 0; row < height; ++row)
    {
        const uint8_t *luma = y + (size_t)row * (size_t)y_stride;
        const uint8_t *blue_row = cb + (size_t)(row / 2) * (size_t)c_stride, *red_row = cr + (size_t)(row / 2) * (size_t)c_stride;
        uint32_t *pixel = out + (size_t)row * (size_t)out_stride;
        int x = 0;
#ifdef COLOR_NEON
        for (; x + 16 <= width; x += 16)
        {
            const uint8x16_t y16 = vld1q_u8(luma + x);
            const uint8x8_t cb8 = vld1_u8(blue_row + x / 2), cr8 = vld1_u8(red_row + x / 2);
            /* Each chroma sample belongs to two pixels. */
            const int16x8_t cb_s = vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(cb8)), vdupq_n_s16(128));
            const int16x8_t cr_s = vsubq_s16(vreinterpretq_s16_u16(vmovl_u8(cr8)), vdupq_n_s16(128));
            const int16x8_t red_c = vmulq_n_s16(cr_s, 102);
            const int16x8_t green_c = vaddq_s16(vmulq_n_s16(cr_s, 52), vmulq_n_s16(cb_s, 25));
            const int16x8_t blue_c = vmulq_n_s16(cb_s, 129);
            uint8x16x4_t result;
            uint8x8_t parts[3][2];
            for (int half = 0; half < 2; ++half)
            {
                const uint8x8_t y8 = half ? vget_high_u8(y16) : vget_low_u8(y16);
                /* y * 149 fits sixteen bits without a sign, half of it with one. */
                const uint16x8_t product = vmull_u8(y8, vdup_n_u8(149));
                const int16x8_t luma_s = vsubq_s16(vreinterpretq_s16_u16(vshrq_n_u16(product, 1)), vdupq_n_s16(1192));
                const int16x8x2_t red2 = vzipq_s16(red_c, red_c), green2 = vzipq_s16(green_c, green_c), blue2 = vzipq_s16(blue_c, blue_c);
                const int16x8_t bias = vdupq_n_s16(32);
                parts[0][half] = vqmovun_s16(vshrq_n_s16(vqaddq_s16(vqaddq_s16(luma_s, blue2.val[half]), bias), 6));
                parts[1][half] = vqmovun_s16(vshrq_n_s16(vqaddq_s16(vqsubq_s16(luma_s, green2.val[half]), bias), 6));
                parts[2][half] = vqmovun_s16(vshrq_n_s16(vqaddq_s16(vqaddq_s16(luma_s, red2.val[half]), bias), 6));
            }
            result.val[0] = vcombine_u8(parts[0][0], parts[0][1]);
            result.val[1] = vcombine_u8(parts[1][0], parts[1][1]);
            result.val[2] = vcombine_u8(parts[2][0], parts[2][1]);
            result.val[3] = vdupq_n_u8(0);
            vst4q_u8((uint8_t *)(pixel + x), result);
        }
#endif
        for (; x < width; ++x) pixel[x] = Pixel(luma[x], blue_row[x / 2] - 128, red_row[x / 2] - 128);
    }
}
