/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* A Cinepak decoder: own code after the description of the format.

   A frame is a header (flags, length, width, height, number of strips) and
   strips. A strip covers full-width rows and holds chunks: two codebooks of
   up to 256 entries (V4 and V1; an entry is four luma values and, in colour
   films, one pair of chroma values) and the vectors. The picture is made of
   blocks of 4 x 4 pixels: a V1 block is one entry of the V1 codebook, each
   luma value doubled to 2 x 2; a V4 block is four entries of the V4 codebook,
   one per quadrant. In a frame that depends on the one before, codebooks are
   updated entry by entry and blocks may be skipped.

   The codebooks are kept as pixels, so drawing a block is copying.
   sdk/lib, docs/ARM64-APPS.md. */
#include "cinepak.h"
#include <stdlib.h>
#include <string.h>
#if defined(__ARM_NEON) && !defined(PLAYER_NO_NEON)
#include <arm_neon.h>
#define CINEPAK_NEON 1
#endif

static unsigned Big16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }
static unsigned Big24(const uint8_t *p) { return (unsigned)p[0] << 16 | (unsigned)p[1] << 8 | p[2]; }
static unsigned Big32(const uint8_t *p) { return (unsigned)p[0] << 24 | (unsigned)p[1] << 16 | (unsigned)p[2] << 8 | p[3]; }

int CinepakCreate(Cinepak *decoder, int width, int height)
{
    memset(decoder, 0, sizeof *decoder);
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096) return 0;
    decoder->width = width;
    decoder->height = height;
    decoder->stride = (width + 3) & ~3;
    decoder->pixels = (uint32_t *)calloc((size_t)decoder->stride * (size_t)((height + 3) & ~3), 4);
    return decoder->pixels != NULL;
}
void CinepakDestroy(Cinepak *decoder)
{
    free(decoder->pixels);
    decoder->pixels = NULL;
}

/* The colour space of Cinepak: red = y + 2v, green = y - u/2 - v,
   blue = y + 2u, each clamped to a byte. */
static void EntryPixels(uint32_t *out, const uint8_t *luma, int u, int v)
{
#ifdef CINEPAK_NEON
    uint8_t four[8] = {luma[0], luma[1], luma[2], luma[3], 0, 0, 0, 0};
    const int16x4_t y = vget_low_s16(vreinterpretq_s16_u16(vmovl_u8(vld1_u8(four))));
    const int16x4_t red = vadd_s16(y, vdup_n_s16((int16_t)(2 * v)));
    const int16x4_t green = vsub_s16(y, vdup_n_s16((int16_t)(u / 2 + v)));
    const int16x4_t blue = vadd_s16(y, vdup_n_s16((int16_t)(2 * u)));
    const int16x8_t zero = vdupq_n_s16(0);
    uint8x8x4_t pixel;
    pixel.val[0] = vqmovun_s16(vcombine_s16(blue, vget_low_s16(zero)));
    pixel.val[1] = vqmovun_s16(vcombine_s16(green, vget_low_s16(zero)));
    pixel.val[2] = vqmovun_s16(vcombine_s16(red, vget_low_s16(zero)));
    pixel.val[3] = vdup_n_u8(0);
    uint8_t bytes[32];
    vst4_u8(bytes, pixel);
    memcpy(out, bytes, 16);
#else
    for (int k = 0; k < 4; ++k)
    {
        int red = luma[k] + 2 * v, green = luma[k] - u / 2 - v, blue = luma[k] + 2 * u;
        if (red < 0) red = 0; else if (red > 255) red = 255;
        if (green < 0) green = 0; else if (green > 255) green = 255;
        if (blue < 0) blue = 0; else if (blue > 255) blue = 255;
        out[k] = (uint32_t)blue | (uint32_t)green << 8 | (uint32_t)red << 16;
    }
#endif
}

/* A codebook chunk. Bit 0 of the id: only the entries a bit mask names
   follow. Bit 2: luma only. */
static void Codebook(uint32_t book[256][4], unsigned id, const uint8_t *data, const uint8_t *end)
{
    const unsigned bytes = id & 4 ? 4 : 6;
    uint32_t flags = 0, mask = 0;
    for (unsigned i = 0; i < 256; ++i)
    {
        if ((id & 1) && !(mask >>= 1))
        {
            if (data + 4 > end) return;
            flags = Big32(data);
            data += 4;
            mask = 0x80000000u;
        }
        if (!(id & 1) || (flags & mask))
        {
            if (data + bytes > end) return;
            EntryPixels(book[i], data, bytes == 6 ? (int8_t)data[4] : 0, bytes == 6 ? (int8_t)data[5] : 0);
            data += bytes;
        }
    }
}

/* A vector chunk over the rows [top, bottom) of the strip. Bit 0 of the id:
   a mask says which blocks are coded at all. Bit 1: every block is V1. */
static int Vectors(Cinepak *decoder, const CinepakStrip *strip, unsigned id, int top, int bottom, const uint8_t *data,
                   const uint8_t *end)
{
    uint32_t flags = 0, mask = 0;
    const int stride = decoder->stride;
    for (int y = top; y < bottom; y += 4)
    {
        uint32_t *row = decoder->pixels + (size_t)y * (size_t)stride;
        for (int x = 0; x < stride; x += 4)
        {
            if ((id & 1) && !(mask >>= 1))
            {
                if (data + 4 > end) return 0;
                flags = Big32(data);
                data += 4;
                mask = 0x80000000u;
            }
            if ((id & 1) && !(flags & mask)) continue; /* skipped: the block of the frame before stays */
            if (!(id & 2) && !(mask >>= 1))
            {
                if (data + 4 > end) return 0;
                flags = Big32(data);
                data += 4;
                mask = 0x80000000u;
            }
            uint32_t *p = row + x;
            if ((id & 2) || !(flags & mask))
            {
                if (data + 1 > end) return 0;
                const uint32_t *entry = strip->v1[*data++];
                p[0] = p[1] = p[stride] = p[stride + 1] = entry[0];
                p[2] = p[3] = p[stride + 2] = p[stride + 3] = entry[1];
                p += 2 * stride;
                p[0] = p[1] = p[stride] = p[stride + 1] = entry[2];
                p[2] = p[3] = p[stride + 2] = p[stride + 3] = entry[3];
            }
            else
            {
                if (data + 4 > end) return 0;
                const uint32_t *a = strip->v4[data[0]], *b = strip->v4[data[1]];
                const uint32_t *c = strip->v4[data[2]], *d = strip->v4[data[3]];
                data += 4;
                p[0] = a[0]; p[1] = a[1]; p[2] = b[0]; p[3] = b[1];
                p += stride;
                p[0] = a[2]; p[1] = a[3]; p[2] = b[2]; p[3] = b[3];
                p += stride;
                p[0] = c[0]; p[1] = c[1]; p[2] = d[0]; p[3] = d[1];
                p += stride;
                p[0] = c[2]; p[1] = c[3]; p[2] = d[2]; p[3] = d[3];
            }
        }
    }
    return 1;
}

int CinepakDecode(Cinepak *decoder, const uint8_t *data, unsigned size)
{
    if (size < 10) return 0;
    const unsigned frame_flags = data[0];
    unsigned strips = Big16(data + 8);
    const uint8_t *end = data + size;
    const int rows = (decoder->height + 3) & ~3;
    int top = 0;
    if (strips > CINEPAK_STRIPS) strips = CINEPAK_STRIPS;
    data += 10;
    for (unsigned s = 0; s < strips && data + 12 <= end; ++s)
    {
        CinepakStrip *strip = &decoder->strips[s];
        const unsigned strip_size = Big24(data + 1);
        /* The strip names its height; strips follow each other downward. */
        int bottom = top + (int)Big16(data + 8);
        const uint8_t *strip_end = data + strip_size;
        if (strip_size < 12) break;
        if (strip_end > end) strip_end = end;
        if (bottom > rows) bottom = rows;
        /* Unless the frame says otherwise, a strip starts from the codebooks
           of the strip above it. */
        if (s > 0 && !(frame_flags & 1)) memcpy(strip, strip - 1, sizeof *strip);
        const uint8_t *chunk = data + 12;
        while (chunk + 4 <= strip_end)
        {
            const unsigned id = chunk[0];
            unsigned chunk_size = Big24(chunk + 1);
            const uint8_t *body = chunk + 4, *chunk_end;
            if (chunk_size < 4) break;
            chunk_end = chunk + chunk_size;
            if (chunk_end > strip_end) chunk_end = strip_end;
            if (id >= 0x20 && id <= 0x27) Codebook(id & 2 ? strip->v1 : strip->v4, id, body, chunk_end);
            else if (id >= 0x30 && id <= 0x32)
            {
                Vectors(decoder, strip, id, top, bottom, body, chunk_end);
                break;
            }
            chunk = chunk_end;
        }
        data += strip_size;
        top = bottom;
    }
    return 1;
}
