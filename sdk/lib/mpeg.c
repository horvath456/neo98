/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* MPEG-1 program streams for programs that play films themselves, decoded by pl_mpeg (Dominic
   Szablewski, MIT; third_party/pl_mpeg, imported by tools/sdk_dependencies.py).
   The library is compiled as it is against the C runtime of the SDK.
   sdk/lib, docs/ARM64-APPS.md. */
#include "movie.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"

typedef struct
{
    plm_t *plm;
    uint32_t *pixels;
    int width, height;
    /* Samples of the sound frame at hand that were not asked for yet. */
    plm_samples_t *samples;
    int sample_at, channels;
    /* The frame a seek found, already as pixels, until it is asked for. */
    int pending;
    double pending_seconds;
} Mpeg;

static void Convert(Mpeg *mpeg, plm_frame_t *frame)
{
    ColorYuv420ToBgrx(frame->y.data, frame->cb.data, frame->cr.data, (int)frame->y.width, (int)frame->cb.width, mpeg->width,
                      mpeg->height, mpeg->pixels, mpeg->width);
}
/* plm_seek hands the frame it lands on to this callback. */
static void SeekFrame(plm_t *plm, plm_frame_t *frame, void *user)
{
    Mpeg *mpeg = (Mpeg *)user;
    (void)plm;
    if (mpeg->pending) return;
    Convert(mpeg, frame);
    mpeg->pending = 1;
    mpeg->pending_seconds = frame->time;
}

static void Close(void *self)
{
    Mpeg *mpeg = (Mpeg *)self;
    if (mpeg->plm) plm_destroy(mpeg->plm);
    free(mpeg->pixels);
    free(mpeg);
}
static int NextFrame(void *self, const uint32_t **pixels, double *seconds)
{
    Mpeg *mpeg = (Mpeg *)self;
    plm_frame_t *frame;
    if (mpeg->pending)
    {
        mpeg->pending = 0;
        *pixels = mpeg->pixels;
        *seconds = mpeg->pending_seconds;
        return 1;
    }
    frame = plm_decode_video(mpeg->plm);
    if (!frame) return 0;
    Convert(mpeg, frame);
    *pixels = mpeg->pixels;
    *seconds = frame->time;
    return 1;
}
static int ReadAudio(void *self, int16_t *samples, int frames)
{
    Mpeg *mpeg = (Mpeg *)self;
    int done = 0;
    while (done < frames)
    {
        if (!mpeg->samples || mpeg->sample_at >= (int)mpeg->samples->count)
        {
            mpeg->samples = plm_decode_audio(mpeg->plm);
            mpeg->sample_at = 0;
            if (!mpeg->samples) break;
        }
        int take = (int)mpeg->samples->count - mpeg->sample_at;
        if (take > frames - done) take = frames - done;
        /* pl_mpeg hands out two channels of floating point between -1 and 1. */
        const float *source = mpeg->samples->interleaved + mpeg->sample_at * 2;
        for (int i = 0; i < take * 2; ++i)
        {
            float value = source[i] * 32767.0f;
            if (value > 32767.0f) value = 32767.0f;
            if (value < -32768.0f) value = -32768.0f;
            samples[done * 2 + i] = (int16_t)value;
        }
        mpeg->sample_at += take;
        done += take;
    }
    return done;
}
static void Seek(void *self, double seconds)
{
    Mpeg *mpeg = (Mpeg *)self;
    mpeg->samples = NULL;
    mpeg->pending = 0;
    /* To the intra frame before the time; the sound follows the picture. The
       start of the film is the start of both streams. */
    if (seconds <= 0 || !plm_seek(mpeg->plm, seconds, 0)) plm_rewind(mpeg->plm);
}
static const MovieFormat mpeg_format = {Close, NextFrame, ReadAudio, Seek};

int MpegOpen(Movie *movie, const char *path, const char **error)
{
    Mpeg *mpeg = (Mpeg *)calloc(1, sizeof(Mpeg));
    *error = "not enough memory";
    if (!mpeg) return 0;
    mpeg->plm = plm_create_with_filename(path);
    *error = "the file cannot be opened";
    if (!mpeg->plm) goto fail;
    *error = "no MPEG-1 program stream with video";
    if (!plm_probe(mpeg->plm, 5000 * 1024) || !plm_get_num_video_streams(mpeg->plm)) goto fail;
    /* The player asks for pictures and sound itself and keeps them in step. */
    plm_set_loop(mpeg->plm, 0);
    plm_set_video_decode_callback(mpeg->plm, SeekFrame, mpeg);
    plm_set_audio_enabled(mpeg->plm, plm_get_num_audio_streams(mpeg->plm) > 0);
    mpeg->width = plm_get_width(mpeg->plm);
    mpeg->height = plm_get_height(mpeg->plm);
    *error = "not enough memory";
    mpeg->pixels = (uint32_t *)malloc((size_t)mpeg->width * (size_t)mpeg->height * 4);
    if (!mpeg->pixels) goto fail;
    movie->format = &mpeg_format;
    movie->self = mpeg;
    movie->info.width = mpeg->width;
    movie->info.height = mpeg->height;
    movie->info.stride = mpeg->width;
    movie->info.frames_per_second = plm_get_framerate(mpeg->plm);
    movie->info.seconds = plm_get_duration(mpeg->plm);
    movie->info.frames = (int)(movie->info.seconds * movie->info.frames_per_second + 0.5);
    movie->info.video_codec = "MPEG-1";
    if (plm_get_num_audio_streams(mpeg->plm) > 0)
    {
        movie->info.audio_rate = plm_get_samplerate(mpeg->plm);
        movie->info.audio_channels = 2;
        movie->info.audio_codec = "MPEG layer II";
    }
    return 1;
fail:
    Close(mpeg);
    return 0;
}
