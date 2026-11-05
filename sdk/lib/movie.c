/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* The film behind the player: the container is told by its first bytes.
   sdk/lib, docs/ARM64-APPS.md. */
#include "movie.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Movie *MovieOpen(const char *path, const char **error)
{
    unsigned char head[12] = {0};
    Movie *movie = (Movie *)calloc(1, sizeof(Movie));
    FILE *file = fopen(path, "rb");
    size_t got = 0;
    *error = "not enough memory";
    if (!movie) { if (file) fclose(file); return NULL; }
    *error = "the file cannot be opened";
    if (file)
    {
        got = fread(head, 1, sizeof head, file);
        fclose(file);
        const int riff = got == 12 && !memcmp(head, "RIFF", 4);
        if (riff ? AviOpen(movie, path, error) : MpegOpen(movie, path, error)) return movie;
    }
    free(movie);
    return NULL;
}
void MovieClose(Movie *movie)
{
    if (!movie) return;
    movie->format->close(movie->self);
    free(movie);
}
const MovieInfo *MovieGetInfo(const Movie *movie) { return &movie->info; }
int MovieNextFrame(Movie *movie, const uint32_t **pixels, double *seconds) { return movie->format->next_frame(movie->self, pixels, seconds); }
int MovieReadAudio(Movie *movie, int16_t *samples, int frames) { return movie->format->read_audio(movie->self, samples, frames); }
void MovieSeek(Movie *movie, double seconds) { movie->format->seek(movie->self, seconds); }
