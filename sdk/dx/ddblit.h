/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The blitter of DirectDraw for ARM64 programs: one source for the emulation
   layer inside DDRAW.DLL (surfaces in system memory, sdk/dx/ddraw) and for
   the driver in the host (src/x86_dx_driver.cpp).

   Colour fill, copies with and without stretching, mirroring, source and
   destination colour keys and a clip list, between surfaces whose pixels are
   equally wide; no format is converted. The mapping of a stretched copy is
   the one of the x86 HAL (src/x86_ddhal.cpp, HalBlt): destination column x of
   the rect takes source column left + (x - left) * source width / destination
   width, rows alike, so both ways put the same pixels on the screen.

   Plain C without a library beyond memcpy and memmove. The includer may
   define DDBLIT_ALLOC and DDBLIT_FREE; they default to malloc and free. */
#ifndef NEO98_DDBLIT_H
#define NEO98_DDBLIT_H

#include "neo98dx_abi.h"

#ifndef DDBLIT_ALLOC
#define DDBLIT_ALLOC(bytes) malloc(bytes)
#define DDBLIT_FREE(pointer) free(pointer)
#endif

typedef struct DDBLIT_PLANE
{
    unsigned char *bits;
    int pitch, width, height;
    int bytes;                 /* bytes of one pixel, 1 to 4 */
} DDBLIT_PLANE;

typedef struct DDBLIT_RECT
{
    int left, top, right, bottom;
} DDBLIT_RECT;

typedef struct DDBLIT_JOB
{
    DDBLIT_PLANE dst, src;     /* src.bits null for a fill */
    DDBLIT_RECT drect, srect;
    unsigned flags;            /* NEO98DX_BLT_ */
    unsigned fill;
    unsigned src_key_low, src_key_high, dst_key_low, dst_key_high;
    const DDBLIT_RECT *clips;  /* in destination coordinates; null: no clip list */
    unsigned clip_count;
} DDBLIT_JOB;

enum { DDBLIT_OK = 0, DDBLIT_MEMORY = 1, DDBLIT_PARAMS = 2 };
/* Columns of one clip rect whose source columns are worked out in advance. */
#define DDBLIT_MAP 2048

static inline unsigned DdBlitLoad(const unsigned char *p, int bytes)
{
    switch (bytes)
    {
    case 1: return p[0];
    case 2: return (unsigned)p[0] | (unsigned)p[1] << 8;
    case 3: return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16;
    default: return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
    }
}

static inline void DdBlitStore(unsigned char *p, int bytes, unsigned v)
{
    p[0] = (unsigned char)v;
    if (bytes > 1) p[1] = (unsigned char)(v >> 8);
    if (bytes > 2) p[2] = (unsigned char)(v >> 16);
    if (bytes > 3) p[3] = (unsigned char)(v >> 24);
}

static inline int DdBlitIntersect(DDBLIT_RECT *a, const DDBLIT_RECT *b)
{
    if (a->left < b->left) a->left = b->left;
    if (a->top < b->top) a->top = b->top;
    if (a->right > b->right) a->right = b->right;
    if (a->bottom > b->bottom) a->bottom = b->bottom;
    return a->right > a->left && a->bottom > a->top;
}

/* One clip rect of a fill. */
static inline void DdBlitFillRect(const DDBLIT_JOB *job, const DDBLIT_RECT *c, unsigned value, unsigned mask)
{
    const DDBLIT_PLANE *d = &job->dst;
    const int bytes = d->bytes, width = c->right - c->left;
    const int keyed = (job->flags & NEO98DX_BLT_KEYDEST) != 0;
    int x, y;
    for (y = c->top; y < c->bottom; ++y)
    {
        unsigned char *p = d->bits + (long)y * d->pitch + (long)c->left * bytes;
        if (keyed)
        {
            for (x = 0; x < width; ++x, p += bytes)
            {
                const unsigned was = DdBlitLoad(p, bytes) & mask;
                if (was >= job->dst_key_low && was <= job->dst_key_high) DdBlitStore(p, bytes, value);
            }
        }
        else if (bytes == 1) memset(p, (int)value, (unsigned long)width);
        else if (bytes == 2)
        {
            /* The surface may sit at any address; the bytes are written one by one. */
            const unsigned char low = (unsigned char)value, high = (unsigned char)(value >> 8);
            if (low == high) memset(p, low, (unsigned long)width * 2);
            else for (x = 0; x < width; ++x, p += 2) { p[0] = low; p[1] = high; }
        }
        else if (y > c->top)
            memcpy(p, d->bits + (long)c->top * d->pitch + (long)c->left * bytes, (unsigned long)width * (unsigned long)bytes);
        else
            for (x = 0; x < width; ++x, p += bytes) DdBlitStore(p, bytes, value);
    }
}

/* Runs the job. `touched`, when given, receives the bounding rect of what
   was written (empty when nothing was). */
static inline int DdBlit(const DDBLIT_JOB *job, DDBLIT_RECT *touched)
{
    const DDBLIT_PLANE *d = &job->dst, *s = &job->src;
    const int bytes = d->bytes;
    const unsigned mask = bytes >= 4 ? 0xffffffffu : (1u << (bytes * 8)) - 1;
    const int fill = (job->flags & NEO98DX_BLT_FILL) != 0;
    DDBLIT_RECT dbounds, single, bound;
    const DDBLIT_RECT *clips = job->clips;
    unsigned clip_count = job->clip_count, c;
    unsigned char *snapshot = 0;
    int result = DDBLIT_OK;

    if (touched) { touched->left = touched->top = touched->right = touched->bottom = 0; }
    if (!d->bits || d->width <= 0 || d->height <= 0 || bytes < 1 || bytes > 4) return DDBLIT_PARAMS;
    if (!fill && (!s->bits || s->bytes != bytes || s->width <= 0 || s->height <= 0)) return DDBLIT_PARAMS;
    dbounds.left = 0; dbounds.top = 0; dbounds.right = d->width; dbounds.bottom = d->height;
    if (!clips)
    {
        single = job->drect;
        clips = &single;
        clip_count = 1;
    }
    bound.left = bound.top = 0x7fffffff;
    bound.right = bound.bottom = -0x7fffffff;

    if (fill)
    {
        const unsigned value = job->fill & mask;
        for (c = 0; c < clip_count; ++c)
        {
            DDBLIT_RECT r = clips[c];
            if (!DdBlitIntersect(&r, &job->drect) || !DdBlitIntersect(&r, &dbounds)) continue;
            DdBlitFillRect(job, &r, value, mask);
            if (r.left < bound.left) bound.left = r.left;
            if (r.top < bound.top) bound.top = r.top;
            if (r.right > bound.right) bound.right = r.right;
            if (r.bottom > bound.bottom) bound.bottom = r.bottom;
        }
    }
    else
    {
        const DDBLIT_RECT drect = job->drect, srect = job->srect;
        const int dw = drect.right - drect.left, dh = drect.bottom - drect.top;
        const int sw = srect.right - srect.left, sh = srect.bottom - srect.top;
        const int mirror_x = (job->flags & NEO98DX_BLT_MIRRORX) != 0, mirror_y = (job->flags & NEO98DX_BLT_MIRRORY) != 0;
        const int src_key = (job->flags & NEO98DX_BLT_KEYSRC) != 0, dst_key = (job->flags & NEO98DX_BLT_KEYDEST) != 0;
        const int plain = !src_key && !dst_key;
        const int straight = dw == sw && dh == sh && !mirror_x && !mirror_y && plain;
        DDBLIT_RECT sread, sbounds;
        const unsigned char *sbits = s->bits;
        int spitch = s->pitch, sorigin_x = 0, sorigin_y = 0;
        unsigned key_high = job->src_key_high < job->src_key_low ? job->src_key_low : job->src_key_high;
        unsigned dkey_high = job->dst_key_high < job->dst_key_low ? job->dst_key_low : job->dst_key_high;

        if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return DDBLIT_OK;
        sbounds.left = 0; sbounds.top = 0; sbounds.right = s->width; sbounds.bottom = s->height;
        sread = srect;
        if (!DdBlitIntersect(&sread, &sbounds)) return DDBLIT_OK;
        /* A copy inside one surface reads a snapshot of the source rect, so
           an overlapping move is right in every direction. */
        if (s->bits == d->bits)
        {
            const int rw = sread.right - sread.left, rh = sread.bottom - sread.top;
            int y;
            snapshot = (unsigned char *)DDBLIT_ALLOC((unsigned long)rw * (unsigned long)bytes * (unsigned long)rh);
            if (!snapshot) return DDBLIT_MEMORY;
            for (y = 0; y < rh; ++y)
                memcpy(snapshot + (long)y * rw * bytes, s->bits + (long)(sread.top + y) * s->pitch + (long)sread.left * bytes,
                       (unsigned long)rw * (unsigned long)bytes);
            sbits = snapshot;
            spitch = rw * bytes;
            sorigin_x = sread.left;
            sorigin_y = sread.top;
        }
        for (c = 0; c < clip_count; ++c)
        {
            DDBLIT_RECT r = clips[c];
            int x, y, last_sy = -1, mapped = 0;
            unsigned char *last_row = 0;
            int map[DDBLIT_MAP];
            if (!DdBlitIntersect(&r, &drect) || !DdBlitIntersect(&r, &dbounds)) continue;
            /* The source column of every destination column is the same in
               every row: worked out once, -1 for a column outside the source. */
            if (!straight && plain && r.right - r.left <= DDBLIT_MAP)
            {
                for (x = r.left; x < r.right; ++x)
                {
                    int rx = x - drect.left, sx;
                    if (mirror_x) rx = dw - 1 - rx;
                    sx = srect.left + (int)((long long)rx * sw / dw);
                    map[x - r.left] = sx < sread.left || sx >= sread.right ? -1 : sx;
                }
                mapped = 1;
            }
            if (r.left < bound.left) bound.left = r.left;
            if (r.top < bound.top) bound.top = r.top;
            if (r.right > bound.right) bound.right = r.right;
            if (r.bottom > bound.bottom) bound.bottom = r.bottom;
            for (y = r.top; y < r.bottom; ++y)
            {
                int ry = y - drect.top, sy;
                const unsigned char *srow;
                unsigned char *out = d->bits + (long)y * d->pitch + (long)r.left * bytes;
                if (mirror_y) ry = dh - 1 - ry;
                sy = srect.top + (int)((long long)ry * sh / dh);
                if (sy < sread.top || sy >= sread.bottom) { last_row = 0; continue; }
                srow = sbits + (long)(sy - sorigin_y) * spitch - (long)sorigin_x * bytes;
                if (straight)
                {
                    const int from = srect.left + (r.left - drect.left), to = from + (r.right - r.left);
                    if (from >= sread.left && to <= sread.right)
                    {
                        memcpy(out, srow + (long)from * bytes, (unsigned long)(to - from) * (unsigned long)bytes);
                        continue;
                    }
                }
                /* A stretched row that takes the source row of the one above
                   is that row again. */
                if (plain && last_row && sy == last_sy)
                {
                    memcpy(out, last_row, (unsigned long)(r.right - r.left) * (unsigned long)bytes);
                    continue;
                }
                if (mapped)
                {
                    const int count = r.right - r.left;
                    int i;
                    /* The copies of one pixel are the compiler's own, whatever
                       the includer says about library calls: a call for
                       every pixel would cost more than the pixel. */
                    if (bytes == 4)
                        for (i = 0; i < count; ++i) { if (map[i] >= 0) __builtin_memcpy(out + (long)i * 4, srow + (long)map[i] * 4, 4); }
                    else if (bytes == 2)
                        for (i = 0; i < count; ++i) { if (map[i] >= 0) __builtin_memcpy(out + (long)i * 2, srow + (long)map[i] * 2, 2); }
                    else if (bytes == 1)
                        for (i = 0; i < count; ++i) { if (map[i] >= 0) out[i] = srow[map[i]]; }
                    else
                        for (i = 0; i < count; ++i) { if (map[i] >= 0) __builtin_memcpy(out + (long)i * 3, srow + (long)map[i] * 3, 3); }
                }
                else for (x = r.left; x < r.right; ++x, out += bytes)
                {
                    int rx = x - drect.left, sx;
                    unsigned v;
                    if (mirror_x) rx = dw - 1 - rx;
                    sx = srect.left + (int)((long long)rx * sw / dw);
                    if (sx < sread.left || sx >= sread.right) continue;
                    v = DdBlitLoad(srow + (long)sx * bytes, bytes) & mask;
                    if (src_key && v >= job->src_key_low && v <= key_high) continue;
                    if (dst_key)
                    {
                        const unsigned was = DdBlitLoad(out, bytes) & mask;
                        if (was < job->dst_key_low || was > dkey_high) continue;
                    }
                    DdBlitStore(out, bytes, v);
                }
                /* Only a row that lies wholly inside the source can stand in
                   for the next one. */
                if (plain && srect.left >= sread.left && srect.right <= sread.right)
                {
                    last_row = d->bits + (long)y * d->pitch + (long)r.left * bytes;
                    last_sy = sy;
                }
                else last_row = 0;
            }
        }
        if (snapshot) DDBLIT_FREE(snapshot);
    }
    if (touched && bound.right > bound.left && bound.bottom > bound.top) *touched = bound;
    return result;
}

#endif
