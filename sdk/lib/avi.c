/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* AVI (RIFF) with Cinepak video and PCM sound for programs that play films themselves: the
   headers, the index (or a scan of the movie list when a file has none), one
   video stream and one sound stream. sdk/lib, docs/ARM64-APPS.md. */
#include "cinepak.h"
#include "movie.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    uint32_t offset, size; /* of the data of the chunk in the file */
    int key;
} Chunk;
typedef struct
{
    FILE *file;
    Cinepak cinepak;
    Chunk *video, *audio;
    int video_count, audio_count, video_capacity, audio_capacity;
    int next_frame;
    double frames_per_second;
    /* Sound: where the next sample comes from. */
    int audio_chunk;
    uint32_t audio_offset;
    int block_align, bits, channels, rate;
    uint64_t audio_bytes;
    uint8_t *buffer;
    uint32_t buffer_size;
    int video_stream, audio_stream;
} Avi;

static uint32_t Little32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t Tag(const char *text) { return Little32((const uint8_t *)text); }

static int Add(Chunk **list, int *count, int *capacity, uint32_t offset, uint32_t size, int key)
{
    if (*count == *capacity)
    {
        const int grown = *capacity ? *capacity * 2 : 256;
        Chunk *moved = (Chunk *)realloc(*list, sizeof(Chunk) * (size_t)grown);
        if (!moved) return 0;
        *list = moved;
        *capacity = grown;
    }
    (*list)[*count].offset = offset;
    (*list)[*count].size = size;
    (*list)[*count].key = key;
    ++*count;
    return 1;
}
/* A chunk id of the movie list: two digits of the stream, two letters of the kind. */
static int StreamOf(uint32_t id)
{
    const int a = (int)(id & 0xff) - '0', b = (int)(id >> 8 & 0xff) - '0';
    return a >= 0 && a <= 9 && b >= 0 && b <= 9 ? a * 10 + b : -1;
}
static void Take(Avi *avi, uint32_t id, uint32_t offset, uint32_t size, int key)
{
    const int stream = StreamOf(id);
    if (stream < 0) return;
    if (stream == avi->video_stream) Add(&avi->video, &avi->video_count, &avi->video_capacity, offset, size, key);
    else if (stream == avi->audio_stream && size)
        Add(&avi->audio, &avi->audio_count, &avi->audio_capacity, offset, size, 1);
}
/* Without an index: walk the movie list. */
static void Scan(Avi *avi, uint32_t begin, uint32_t end)
{
    uint8_t header[12];
    for (uint32_t at = begin; at + 8 <= end;)
    {
        if (fseek(avi->file, (long)at, SEEK_SET) || fread(header, 1, 8, avi->file) != 8) return;
        const uint32_t id = Little32(header), size = Little32(header + 4);
        if (id == Tag("LIST"))
        {
            at += 12; /* a "rec " list: its chunks follow */
            continue;
        }
        Take(avi, id, at + 8, size, 0);
        at += 8 + size + (size & 1);
    }
    /* Cinepak says which frames stand alone: bit 0 of the first byte is clear. */
    for (int i = 0; i < avi->video_count; ++i)
    {
        uint8_t first = 1;
        if (avi->video[i].size && !fseek(avi->file, (long)avi->video[i].offset, SEEK_SET) && fread(&first, 1, 1, avi->file) == 1)
            avi->video[i].key = !(first & 1);
    }
}

static void Close(void *self)
{
    Avi *avi = (Avi *)self;
    if (avi->file) fclose(avi->file);
    CinepakDestroy(&avi->cinepak);
    free(avi->video);
    free(avi->audio);
    free(avi->buffer);
    free(avi);
}
static int NextFrame(void *self, const uint32_t **pixels, double *seconds)
{
    Avi *avi = (Avi *)self;
    if (avi->next_frame >= avi->video_count) return 0;
    const Chunk *chunk = &avi->video[avi->next_frame];
    /* A chunk without data repeats the frame before. */
    if (chunk->size)
    {
        if (chunk->size > avi->buffer_size)
        {
            uint8_t *grown = (uint8_t *)realloc(avi->buffer, chunk->size + 4096);
            if (!grown) return 0;
            avi->buffer = grown;
            avi->buffer_size = chunk->size + 4096;
        }
        if (fseek(avi->file, (long)chunk->offset, SEEK_SET) || fread(avi->buffer, 1, chunk->size, avi->file) != chunk->size)
            return 0;
        CinepakDecode(&avi->cinepak, avi->buffer, chunk->size);
    }
    *pixels = avi->cinepak.pixels;
    *seconds = avi->next_frame / avi->frames_per_second;
    ++avi->next_frame;
    return 1;
}
static int ReadAudio(void *self, int16_t *samples, int frames)
{
    Avi *avi = (Avi *)self;
    const int frame_bytes = avi->block_align;
    int done = 0;
    if (!avi->audio_count || !frame_bytes) return 0;
    while (done < frames && avi->audio_chunk < avi->audio_count)
    {
        const Chunk *chunk = &avi->audio[avi->audio_chunk];
        uint32_t left = chunk->size - avi->audio_offset;
        left -= left % (uint32_t)frame_bytes;
        if (!left)
        {
            ++avi->audio_chunk;
            avi->audio_offset = 0;
            continue;
        }
        uint32_t want = (uint32_t)(frames - done) * (uint32_t)frame_bytes;
        if (want > left) want = left;
        uint8_t *target = (uint8_t *)(samples + (size_t)done * (size_t)avi->channels);
        if (fseek(avi->file, (long)(chunk->offset + avi->audio_offset), SEEK_SET)) break;
        if (avi->bits == 16)
        {
            if (fread(target, 1, want, avi->file) != want) break;
        }
        else
        {
            /* Eight bits without a sign become sixteen with one; read into
               the upper half of the room and widen from the front. */
            const uint32_t count = want;
            uint8_t *bytes = target + count;
            if (fread(bytes, 1, count, avi->file) != count) break;
            for (uint32_t i = 0; i < count; ++i) ((int16_t *)target)[i] = (int16_t)((bytes[i] - 128) << 8);
        }
        avi->audio_offset += want;
        done += (int)(want / (uint32_t)frame_bytes);
    }
    return done;
}
static void Seek(void *self, double seconds)
{
    Avi *avi = (Avi *)self;
    int frame = (int)(seconds * avi->frames_per_second);
    if (frame < 0) frame = 0;
    if (frame >= avi->video_count) frame = avi->video_count ? avi->video_count - 1 : 0;
    while (frame > 0 && !avi->video[frame].key) --frame;
    avi->next_frame = frame;
    /* The sound of the same moment. */
    if (avi->audio_count)
    {
        uint64_t position = (uint64_t)((double)frame / avi->frames_per_second * avi->rate) * (uint64_t)avi->block_align;
        avi->audio_chunk = 0;
        avi->audio_offset = 0;
        while (avi->audio_chunk < avi->audio_count && position >= avi->audio[avi->audio_chunk].size)
            position -= avi->audio[avi->audio_chunk++].size;
        if (avi->audio_chunk < avi->audio_count) avi->audio_offset = (uint32_t)position;
    }
}
static const MovieFormat avi_format = {Close, NextFrame, ReadAudio, Seek};

int AviOpen(Movie *movie, const char *path, const char **error)
{
    uint8_t header[12], *list = NULL;
    uint32_t movi_begin = 0, movi_end = 0, index_at = 0, index_size = 0, handler = 0, scale = 1, rate = 15;
    int width = 0, height = 0, streams = 0, audio_format = 0;
    Avi *avi = (Avi *)calloc(1, sizeof(Avi));
    *error = "not enough memory";
    if (!avi) return 0;
    avi->video_stream = avi->audio_stream = -1;
    avi->file = fopen(path, "rb");
    *error = "the file cannot be opened";
    if (!avi->file) goto fail;
    *error = "no AVI file";
    if (fread(header, 1, 12, avi->file) != 12 || Little32(header) != Tag("RIFF") || Little32(header + 8) != Tag("AVI ")) goto fail;
    /* The lists of the file, one after the other. */
    for (uint32_t at = 12;;)
    {
        if (fseek(avi->file, (long)at, SEEK_SET) || fread(header, 1, 8, avi->file) != 8) break;
        const uint32_t id = Little32(header), size = Little32(header + 4);
        if (id == Tag("LIST"))
        {
            if (fread(header + 8, 1, 4, avi->file) != 4) break;
            const uint32_t kind = Little32(header + 8);
            if (kind == Tag("movi"))
            {
                movi_begin = at + 12;
                movi_end = at + 8 + size;
            }
            else if (kind == Tag("hdrl") && size > 4 && size < 1 << 20)
            {
                /* The stream headers: for each stream its header and its format. */
                list = (uint8_t *)malloc(size);
                if (!list || fread(list, 1, size - 4, avi->file) != size - 4) goto fail;
                uint32_t kind_of_stream = 0;
                for (uint32_t p = 0; p + 8 <= size - 4;)
                {
                    const uint32_t part = Little32(list + p), part_size = Little32(list + p + 4);
                    if (part == Tag("LIST")) { p += 12; continue; }
                    if (p + 8 + part_size > size - 4) break;
                    const uint8_t *body = list + p + 8;
                    if (part == Tag("strh") && part_size >= 36)
                    {
                        kind_of_stream = Little32(body);
                        if (kind_of_stream == Tag("vids") && avi->video_stream < 0)
                        {
                            avi->video_stream = streams;
                            handler = Little32(body + 4);
                            scale = Little32(body + 20);
                            rate = Little32(body + 24);
                        }
                        else if (kind_of_stream == Tag("auds") && avi->audio_stream < 0) avi->audio_stream = streams;
                        else kind_of_stream = 0;
                        ++streams;
                    }
                    else if (part == Tag("strf") && kind_of_stream == Tag("vids") && part_size >= 40)
                    {
                        width = (int)Little32(body + 4);
                        height = (int)Little32(body + 8);
                        if (height < 0) height = -height;
                        if (Little32(body + 16)) handler = Little32(body + 16);
                        kind_of_stream = 0;
                    }
                    else if (part == Tag("strf") && kind_of_stream == Tag("auds") && part_size >= 16)
                    {
                        audio_format = body[0] | body[1] << 8;
                        avi->channels = body[2] | body[3] << 8;
                        avi->rate = (int)Little32(body + 4);
                        avi->block_align = body[12] | body[13] << 8;
                        avi->bits = body[14] | body[15] << 8;
                        kind_of_stream = 0;
                    }
                    p += 8 + part_size + (part_size & 1);
                }
                free(list);
                list = NULL;
            }
        }
        else if (id == Tag("idx1"))
        {
            index_at = at + 8;
            index_size = size;
        }
        at += 8 + size + (size & 1);
    }
    *error = "the file has no video stream";
    if (avi->video_stream < 0 || !movi_begin || !scale || !rate) goto fail;
    *error = "the video is not Cinepak";
    if (handler != Tag("cvid") && handler != Tag("CVID")) goto fail;
    *error = "not enough memory";
    if (!CinepakCreate(&avi->cinepak, width, height)) goto fail;
    /* Sound this player does not decode stays silent. */
    if (audio_format != 1 || (avi->bits != 8 && avi->bits != 16) || avi->channels < 1 || avi->channels > 2 ||
        avi->block_align != avi->channels * avi->bits / 8)
        avi->audio_stream = -1;
    if (index_size >= 16)
    {
        uint8_t *index = (uint8_t *)malloc(index_size);
        if (index && !fseek(avi->file, (long)index_at, SEEK_SET) && fread(index, 1, index_size, avi->file) == index_size)
        {
            /* Offsets count from the list id of the movie list, or, in some
               files, from the start of the file: the first entry tells. */
            uint32_t base = movi_begin - 4;
            uint8_t probe[4];
            const uint32_t first = Little32(index + 8);
            if (!fseek(avi->file, (long)(base + first), SEEK_SET) && fread(probe, 1, 4, avi->file) == 4 &&
                Little32(probe) != Little32(index))
                base = 0;
            for (uint32_t p = 0; p + 16 <= index_size; p += 16)
                Take(avi, Little32(index + p), base + Little32(index + p + 8) + 8, Little32(index + p + 12),
                     (Little32(index + p + 4) & 0x10) != 0);
        }
        free(index);
    }
    if (!avi->video_count)
    {
        avi->audio_count = 0;
        Scan(avi, movi_begin, movi_end);
    }
    *error = "the file has no frames";
    if (!avi->video_count) goto fail;
    avi->video[0].key = 1;
    avi->frames_per_second = (double)rate / (double)scale;
    movie->format = &avi_format;
    movie->self = avi;
    movie->info.width = width;
    movie->info.height = height;
    movie->info.stride = avi->cinepak.stride;
    movie->info.frames = avi->video_count;
    movie->info.frames_per_second = avi->frames_per_second;
    movie->info.seconds = avi->video_count / avi->frames_per_second;
    movie->info.video_codec = "Cinepak";
    if (avi->audio_stream >= 0 && avi->audio_count)
    {
        movie->info.audio_rate = avi->rate;
        movie->info.audio_channels = avi->channels;
        movie->info.audio_codec = "PCM";
    }
    else avi->audio_count = 0;
    return 1;
fail:
    free(list);
    Close(avi);
    return 0;
}
