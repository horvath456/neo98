/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* A film as frames of 32-bit pixels and sound as
   16-bit PCM, whatever the file holds. sdk/lib, docs/ARM64-APPS.md. */
#ifndef PLAYER_MOVIE_H
#define PLAYER_MOVIE_H
#include <stddef.h>
#include <stdint.h>

typedef struct Movie Movie;
typedef struct
{
    int width, height;
    /* The row length of a frame in pixels; at least the width. */
    int stride;
    double frames_per_second, seconds;
    int frames;        /* zero when the container does not say */
    int audio_rate, audio_channels; /* zero without sound */
    const char *video_codec, *audio_codec;
} MovieInfo;

/* Opens an AVI with Cinepak video or an MPEG-1 program stream; NULL and a
   text in *error otherwise. */
Movie *MovieOpen(const char *path, const char **error);
void MovieClose(Movie *movie);
const MovieInfo *MovieGetInfo(const Movie *movie);
/* Decodes the next frame. *pixels points at stride * height pixels, top row
   first, blue in the low byte, valid until the next call; *seconds is the
   time the frame belongs to. Zero at the end of the film. */
int MovieNextFrame(Movie *movie, const uint32_t **pixels, double *seconds);
/* The next `frames` sample frames of sound (interleaved, 16 bits); returns
   how many there were. */
int MovieReadAudio(Movie *movie, int16_t *samples, int frames);
/* Both streams continue at this time; the picture at the key frame before it. */
void MovieSeek(Movie *movie, double seconds);

/* The two containers (avi.c, mpeg.c) behind the functions above. */
typedef struct
{
    void (*close)(void *self);
    int (*next_frame)(void *self, const uint32_t **pixels, double *seconds);
    int (*read_audio)(void *self, int16_t *samples, int frames);
    void (*seek)(void *self, double seconds);
} MovieFormat;
struct Movie
{
    const MovieFormat *format;
    void *self;
    MovieInfo info;
};
int AviOpen(Movie *movie, const char *path, const char **error);
int MpegOpen(Movie *movie, const char *path, const char **error);

/* Planar YCbCr 4:2:0 (BT.601, studio range) to 32-bit pixels: NEON where the
   compiler has it, C otherwise (color.c). */
void ColorYuv420ToBgrx(const uint8_t *y, const uint8_t *cb, const uint8_t *cr, int y_stride, int c_stride, int width,
                       int height, uint32_t *out, int out_stride);
#endif
