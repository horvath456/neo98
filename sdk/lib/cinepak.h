/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* A Cinepak decoder: own code after the description of the format.
   sdk/lib, docs/ARM64-APPS.md. */
#ifndef PLAYER_CINEPAK_H
#define PLAYER_CINEPAK_H
#include <stdint.h>

#define CINEPAK_STRIPS 32
typedef struct
{
    /* Each codebook entry as the four pixels it stands for. */
    uint32_t v4[256][4], v1[256][4];
} CinepakStrip;
typedef struct
{
    int width, height, stride; /* stride and the rows are multiples of four */
    uint32_t *pixels;          /* stride * rows, top row first */
    CinepakStrip strips[CINEPAK_STRIPS];
} Cinepak;

int CinepakCreate(Cinepak *decoder, int width, int height);
void CinepakDestroy(Cinepak *decoder);
/* Decodes one frame into decoder->pixels; a frame that is cut short keeps
   what the frame before left. Zero for data that is no Cinepak frame. */
int CinepakDecode(Cinepak *decoder, const uint8_t *data, unsigned size);
#endif
