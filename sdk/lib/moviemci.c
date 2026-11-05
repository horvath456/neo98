/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* Films through MCI command strings, decoded in the program: see moviemci.h.
   Own code. docs/ARM64-APPS.md. */
#include "moviemci.h"
#include "movie.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The numbers of mmsystem.h for the errors this device answers. */
enum { ErrInvalidDevice = 263, ErrUnrecognized = 261, ErrMissingParameter = 273, ErrBadInteger = 270, ErrOutOfRange = 282,
       ErrFileNotFound = 275, ErrInternal = 277, ErrUnsupported = 274, ErrBufferTooSmall = 276 };
enum { Stopped, Playing, Paused };
#define DEVICES 4
#define BLOCKS 8

typedef struct
{
    int used, state, frames_format, volume;
    char alias[32];
    Movie *movie;
    const MovieInfo *info;
    HWND window;
    RECT destination;
    const uint32_t *picture;
    double shown, position; /* seconds: the frame on the screen, where the film stands */
    double base, stop_at, sound_time;
    DWORD base_ticks;
    HWAVEOUT wave;
    WAVEHDR header[BLOCKS];
    int16_t *data[BLOCKS];
    int block_frames, sound_ended, sound_clock;
} Device;
static Device devices[DEVICES];
static const char window_class[] = "Neo98Movie";

static Device *Find(const char *alias)
{
    for (int i = 0; i < DEVICES; ++i)
        if (devices[i].used && !lstrcmpiA(devices[i].alias, alias)) return &devices[i];
    return NULL;
}
static Device *OfWindow(HWND window)
{
    for (int i = 0; i < DEVICES; ++i)
        if (devices[i].used && devices[i].window == window) return &devices[i];
    return NULL;
}

/* --- sound and clock --------------------------------------------------------- */
static void SetVolume(Device *d)
{
    const DWORD level = (DWORD)(d->volume * 0xffff / 1000);
    if (d->wave) waveOutSetVolume(d->wave, level | level << 16);
}
static void OpenSound(Device *d)
{
    WAVEFORMATEX format;
    if (!d->info->audio_rate) return;
    memset(&format, 0, sizeof format);
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = (WORD)d->info->audio_channels;
    format.nSamplesPerSec = (DWORD)d->info->audio_rate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = (WORD)(2 * d->info->audio_channels);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    if (waveOutOpen(&d->wave, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
    {
        d->wave = NULL;
        return;
    }
    d->block_frames = d->info->audio_rate / 10;
    for (int i = 0; i < BLOCKS; ++i)
    {
        d->data[i] = (int16_t *)malloc((size_t)d->block_frames * format.nBlockAlign);
        memset(&d->header[i], 0, sizeof d->header[i]);
        d->header[i].lpData = (LPSTR)d->data[i];
        d->header[i].dwBufferLength = (DWORD)d->block_frames * format.nBlockAlign;
        waveOutPrepareHeader(d->wave, &d->header[i], sizeof d->header[i]);
    }
    SetVolume(d);
}
static void CloseSound(Device *d)
{
    if (!d->wave) return;
    waveOutReset(d->wave);
    for (int i = 0; i < BLOCKS; ++i)
    {
        waveOutUnprepareHeader(d->wave, &d->header[i], sizeof d->header[i]);
        free(d->data[i]);
    }
    waveOutClose(d->wave);
    d->wave = NULL;
}
static void Feed(Device *d)
{
    if (!d->wave || d->sound_ended) return;
    for (int i = 0; i < BLOCKS; ++i)
    {
        if (d->header[i].dwFlags & WHDR_INQUEUE) continue;
        const int frames = MovieReadAudio(d->movie, d->data[i], d->block_frames);
        if (frames <= 0)
        {
            d->sound_ended = 1;
            return;
        }
        d->header[i].dwBufferLength = (DWORD)frames * 2 * (DWORD)d->info->audio_channels;
        waveOutWrite(d->wave, &d->header[i], sizeof d->header[i]);
    }
}
/* The time of the film while it plays: what the sound device has played, or
   the timer of the system for a silent film and after the sound has ended. */
static double Clock(Device *d)
{
    if (d->state != Playing) return d->position;
    if (d->wave && d->sound_clock)
    {
        MMTIME at;
        int queued = 0;
        for (int i = 0; i < BLOCKS; ++i) queued |= (d->header[i].dwFlags & WHDR_INQUEUE) != 0;
        at.wType = TIME_SAMPLES;
        if ((queued || !d->sound_ended) && waveOutGetPosition(d->wave, &at, sizeof at) == MMSYSERR_NOERROR && at.wType == TIME_SAMPLES)
            return d->sound_time = d->base + (double)at.u.sample / d->info->audio_rate;
        d->sound_clock = 0;
        d->base = d->sound_time;
        d->base_ticks = timeGetTime();
    }
    return d->base + (double)(timeGetTime() - d->base_ticks) / 1000.0;
}

/* --- picture ------------------------------------------------------------------ */
static void Present(Device *d, HDC dc)
{
    BITMAPINFO header;
    RECT area = d->destination;
    if (area.right <= area.left || area.bottom <= area.top) GetClientRect(d->window, &area);
    if (!d->picture)
    {
        FillRect(dc, &area, (HBRUSH)GetStockObject(BLACK_BRUSH));
        return;
    }
    memset(&header, 0, sizeof header);
    header.bmiHeader.biSize = sizeof header.bmiHeader;
    header.bmiHeader.biWidth = d->info->stride;
    header.bmiHeader.biHeight = -d->info->height;
    header.bmiHeader.biPlanes = 1;
    header.bmiHeader.biBitCount = 32;
    header.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(dc, COLORONCOLOR);
    StretchDIBits(dc, area.left, area.top, area.right - area.left, area.bottom - area.top, 0, 0, d->info->width, d->info->height,
                  d->picture, &header, DIB_RGB_COLORS, SRCCOPY);
}
static int Decode(Device *d)
{
    double seconds = 0;
    const uint32_t *pixels = NULL;
    if (!MovieNextFrame(d->movie, &pixels, &seconds)) return 0;
    d->picture = pixels;
    d->shown = seconds;
    return 1;
}
static void Show(Device *d)
{
    HDC dc = GetDC(d->window);
    Present(d, dc);
    ReleaseDC(d->window, dc);
}
/* Both streams continue at this time, with its picture on the screen. */
static void Stand(Device *d, double seconds)
{
    if (seconds < 0) seconds = 0;
    if (seconds > d->info->seconds) seconds = d->info->seconds;
    if (d->wave) waveOutReset(d->wave);
    KillTimer(d->window, 1);
    d->state = Stopped;
    MovieSeek(d->movie, seconds);
    d->shown = -1;
    if (Decode(d)) Show(d);
    /* A key frame may lie before the time asked for; the position is the
       one asked for, as MCI reports it. */
    d->position = seconds;
    MovieSeek(d->movie, d->shown < 0 ? seconds : d->shown);
    d->shown = -1;
}
static void Start(Device *d, double to)
{
    if (d->position >= d->info->seconds) return; /* at the end: nothing to play */
    if (d->wave) waveOutReset(d->wave);
    d->sound_ended = 0;
    d->sound_clock = d->wave != NULL;
    d->sound_time = d->base = d->position;
    d->base_ticks = timeGetTime();
    d->stop_at = to;
    d->state = Playing;
    Feed(d);
    SetTimer(d->window, 1, 10, NULL);
}
static void Halt(Device *d, int state)
{
    if (d->state != Playing) { d->state = state; return; }
    d->position = Clock(d);
    if (d->position > d->info->seconds) d->position = d->info->seconds;
    d->state = state;
    KillTimer(d->window, 1);
    if (d->wave) waveOutReset(d->wave);
    MovieSeek(d->movie, d->position);
    d->shown = -1;
}
static void Tick(Device *d)
{
    const double frame = 1.0 / d->info->frames_per_second;
    int decoded = 0;
    double now;
    if (d->state != Playing) return;
    Feed(d);
    now = Clock(d);
    if (d->stop_at >= 0 && now >= d->stop_at)
    {
        Halt(d, Stopped);
        d->position = d->stop_at;
        return;
    }
    while (d->shown < 0 || now >= d->shown + frame)
    {
        if (!Decode(d))
        {
            /* The end of the film: it stands there, stopped. */
            KillTimer(d->window, 1);
            if (d->wave) waveOutReset(d->wave);
            d->state = Stopped;
            d->position = d->info->seconds;
            return;
        }
        if (++decoded > 30) break;
    }
    if (decoded) Show(d);
}
static LRESULT CALLBACK MovieProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    Device *d = OfWindow(window);
    switch (message)
    {
    case WM_TIMER:
        if (d) Tick(d);
        return 0;
    case WM_PAINT:
    {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        if (d) Present(d, dc);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProcA(window, message, wParam, lParam);
}

/* --- the command strings ---------------------------------------------------------- */
/* The next word of the command, or a quoted string; empty at the end. */
static const char *Word(const char *p, char *out, int room)
{
    int n = 0;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p == '"')
    {
        for (++p; *p && *p != '"'; ++p)
            if (n < room - 1) out[n++] = *p;
        if (*p == '"') ++p;
    }
    else
        for (; *p && *p != ' ' && *p != '\t'; ++p)
            if (n < room - 1) out[n++] = *p;
    out[n] = 0;
    return p;
}
static int Number(const char *text, long *value)
{
    char *end = NULL;
    *value = strtol(text, &end, 10);
    return end != text && !*end;
}
static MCIERROR Answer(LPSTR result, UINT size, const char *text)
{
    if (!result || !size) return 0;
    if ((UINT)lstrlenA(text) >= size) return ErrBufferTooSmall;
    lstrcpyA(result, text);
    return 0;
}
/* A position of a command, in the time format of the device, as seconds. */
static double Seconds(const Device *d, long value)
{
    return d->frames_format ? value / d->info->frames_per_second : value / 1000.0;
}
static long Units(const Device *d, double seconds)
{
    return (long)(d->frames_format ? seconds * d->info->frames_per_second + 0.5 : seconds * 1000.0 + 0.5);
}
static void Close(Device *d)
{
    KillTimer(d->window, 1);
    CloseSound(d);
    if (d->window) DestroyWindow(d->window);
    MovieClose(d->movie);
    memset(d, 0, sizeof *d);
}
/* `open`: ours when one of the decoders reads the file. */
static int Open(const char *rest, MCIERROR *error)
{
    char file[MAX_PATH], word[64], alias[32] = "", value[32];
    HWND parent = NULL;
    DWORD style = WS_POPUP;
    const char *problem = "";
    Device *d = NULL;
    Movie *movie;
    WNDCLASSA wc;
    rest = Word(rest, file, sizeof file);
    for (;;)
    {
        rest = Word(rest, word, sizeof word);
        if (!word[0]) break;
        if (!lstrcmpiA(word, "alias")) rest = Word(rest, alias, sizeof alias);
        else if (!lstrcmpiA(word, "parent"))
        {
            long handle = 0;
            rest = Word(rest, value, sizeof value);
            if (!Number(value, &handle)) { *error = ErrBadInteger; return 1; }
            parent = (HWND)(INT_PTR)handle;
        }
        else if (!lstrcmpiA(word, "style"))
        {
            rest = Word(rest, value, sizeof value);
            style = !lstrcmpiA(value, "child") ? WS_CHILD : !lstrcmpiA(value, "overlapped") ? WS_OVERLAPPEDWINDOW : WS_POPUP;
        }
        else if (!lstrcmpiA(word, "type"))
        {
            rest = Word(rest, value, sizeof value);
            if (lstrcmpiA(value, "avivideo") && lstrcmpiA(value, "mpegvideo")) return 0; /* another device */
        }
    }
    if (!alias[0]) lstrcpynA(alias, file, sizeof alias);
    if (GetFileAttributesA(file) == 0xffffffffu) return 0; /* a device name or a missing file: WINMM says which */
    movie = MovieOpen(file, &problem);
    if (!movie) return 0; /* no film these decoders read */
    for (int i = 0; i < DEVICES && !d; ++i)
        if (!devices[i].used) d = &devices[i];
    if (!d || Find(alias))
    {
        MovieClose(movie);
        *error = ErrInternal;
        return 1;
    }
    memset(d, 0, sizeof *d);
    d->used = 1;
    d->movie = movie;
    d->info = MovieGetInfo(movie);
    d->frames_format = 1; /* the default of MCIAVI */
    d->volume = 1000;
    d->shown = -1;
    d->stop_at = -1;
    lstrcpyA(d->alias, alias);
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = MovieProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.lpszClassName = window_class;
    RegisterClassA(&wc); /* fails the second time, harmlessly */
    if (style == WS_CHILD && !parent) style = WS_POPUP;
    d->window = CreateWindowExA(0, window_class, file, style, 0, 0, d->info->width, d->info->height, parent, NULL, wc.hInstance, NULL);
    if (!d->window)
    {
        MovieClose(movie);
        memset(d, 0, sizeof *d);
        *error = ErrInternal;
        return 1;
    }
    OpenSound(d);
    /* The first picture stands in the window. */
    if (Decode(d)) MovieSeek(d->movie, 0);
    d->shown = -1;
    *error = 0;
    return 1;
}
static MCIERROR Command(Device *d, const char *verb, const char *rest, LPSTR result, UINT size)
{
    char word[64], text[96];
    long value = 0;
    if (!lstrcmpiA(verb, "close"))
    {
        Close(d);
        return 0;
    }
    if (!lstrcmpiA(verb, "set"))
    {
        rest = Word(rest, word, sizeof word);
        if (!lstrcmpiA(word, "time"))
        {
            rest = Word(rest, word, sizeof word); /* format */
            rest = Word(rest, word, sizeof word);
            if (!lstrcmpiA(word, "milliseconds") || !lstrcmpiA(word, "ms")) d->frames_format = 0;
            else if (!lstrcmpiA(word, "frames")) d->frames_format = 1;
            else return ErrUnsupported;
            return 0;
        }
        if (!lstrcmpiA(word, "audio"))
        {
            rest = Word(rest, word, sizeof word); /* all, left, right */
            rest = Word(rest, word, sizeof word);
            d->volume = !lstrcmpiA(word, "off") ? 0 : 1000;
            SetVolume(d);
            return 0;
        }
        return ErrUnsupported;
    }
    if (!lstrcmpiA(verb, "setaudio"))
    {
        rest = Word(rest, word, sizeof word);
        if (lstrcmpiA(word, "volume")) return ErrUnsupported;
        rest = Word(rest, word, sizeof word); /* to */
        rest = Word(rest, word, sizeof word);
        if (!Number(word, &value)) return ErrBadInteger;
        d->volume = value < 0 ? 0 : value > 1000 ? 1000 : (int)value;
        SetVolume(d);
        return 0;
    }
    if (!lstrcmpiA(verb, "status"))
    {
        rest = Word(rest, word, sizeof word);
        if (!lstrcmpiA(word, "length")) wsprintfA(text, "%ld", Units(d, d->info->seconds));
        else if (!lstrcmpiA(word, "position"))
        {
            double now = Clock(d);
            if (now > d->info->seconds) now = d->info->seconds;
            wsprintfA(text, "%ld", Units(d, now));
        }
        else if (!lstrcmpiA(word, "mode")) lstrcpyA(text, d->state == Playing ? "playing" : d->state == Paused ? "paused" : "stopped");
        else if (!lstrcmpiA(word, "ready")) lstrcpyA(text, "true");
        else if (!lstrcmpiA(word, "window")) wsprintfA(text, "%ld", (long)(INT_PTR)d->window);
        else return ErrUnsupported;
        return Answer(result, size, text);
    }
    if (!lstrcmpiA(verb, "where"))
    {
        RECT area = {0, 0, d->info->width, d->info->height};
        rest = Word(rest, word, sizeof word);
        if (!lstrcmpiA(word, "destination"))
        {
            area = d->destination;
            if (area.right <= area.left) GetClientRect(d->window, &area);
        }
        else if (!lstrcmpiA(word, "window"))
        {
            POINT corner = {0, 0};
            GetWindowRect(d->window, &area);
            corner.x = area.left;
            corner.y = area.top;
            if (GetParent(d->window)) ScreenToClient(GetParent(d->window), &corner);
            OffsetRect(&area, corner.x - area.left, corner.y - area.top);
        }
        else if (lstrcmpiA(word, "source")) return ErrUnsupported;
        wsprintfA(text, "%ld %ld %ld %ld", area.left, area.top, area.right - area.left, area.bottom - area.top);
        return Answer(result, size, text);
    }
    if (!lstrcmpiA(verb, "put"))
    {
        char what[32];
        long box[4];
        rest = Word(rest, what, sizeof what);
        rest = Word(rest, word, sizeof word);
        if (lstrcmpiA(word, "at")) return ErrMissingParameter;
        for (int i = 0; i < 4; ++i)
        {
            rest = Word(rest, word, sizeof word);
            if (!Number(word, &box[i])) return ErrBadInteger;
        }
        if (!lstrcmpiA(what, "window")) SetWindowPos(d->window, NULL, (int)box[0], (int)box[1], (int)box[2], (int)box[3], SWP_NOZORDER | SWP_NOACTIVATE);
        else if (!lstrcmpiA(what, "destination")) SetRect(&d->destination, (int)box[0], (int)box[1], (int)(box[0] + box[2]), (int)(box[1] + box[3]));
        else return ErrUnsupported;
        InvalidateRect(d->window, NULL, FALSE);
        return 0;
    }
    if (!lstrcmpiA(verb, "window"))
    {
        rest = Word(rest, word, sizeof word);
        if (lstrcmpiA(word, "state")) return ErrUnsupported;
        rest = Word(rest, word, sizeof word);
        ShowWindow(d->window, !lstrcmpiA(word, "hide") ? SW_HIDE : SW_SHOWNA);
        return 0;
    }
    if (!lstrcmpiA(verb, "play"))
    {
        double to = -1;
        for (;;)
        {
            rest = Word(rest, word, sizeof word);
            if (!word[0]) break;
            if (!lstrcmpiA(word, "from") || !lstrcmpiA(word, "to"))
            {
                const int from = !lstrcmpiA(word, "from");
                rest = Word(rest, word, sizeof word);
                if (!Number(word, &value)) return ErrBadInteger;
                if (Seconds(d, value) > d->info->seconds + 0.001) return ErrOutOfRange;
                if (from) { Halt(d, Stopped); Stand(d, Seconds(d, value)); }
                else to = Seconds(d, value);
            }
            else if (!lstrcmpiA(word, "wait") || !lstrcmpiA(word, "notify")) return ErrUnsupported;
            /* "window", "fullscreen" and the rest: the film plays in its window. */
        }
        if (d->state == Playing) Halt(d, Stopped);
        ShowWindow(d->window, SW_SHOWNA);
        Start(d, to);
        return 0;
    }
    if (!lstrcmpiA(verb, "pause"))
    {
        Halt(d, Paused);
        return 0;
    }
    if (!lstrcmpiA(verb, "resume"))
    {
        if (d->state == Paused) Start(d, d->stop_at);
        return 0;
    }
    if (!lstrcmpiA(verb, "stop"))
    {
        Halt(d, Stopped);
        return 0;
    }
    if (!lstrcmpiA(verb, "seek"))
    {
        double target;
        rest = Word(rest, word, sizeof word);
        if (lstrcmpiA(word, "to")) return ErrMissingParameter;
        rest = Word(rest, word, sizeof word);
        if (!lstrcmpiA(word, "start")) target = 0;
        else if (!lstrcmpiA(word, "end")) target = d->info->seconds;
        else if (!Number(word, &value)) return ErrBadInteger;
        else target = Seconds(d, value);
        if (target > d->info->seconds + 0.001) return ErrOutOfRange;
        Halt(d, Stopped);
        Stand(d, target);
        return 0;
    }
    return ErrUnrecognized;
}
MCIERROR MovieMciSendString(LPCSTR command, LPSTR result, UINT size, HWND callback)
{
    char verb[32], alias[MAX_PATH];
    const char *rest;
    Device *d;
    if (result && size) result[0] = 0;
    if (!command) return ErrMissingParameter;
    rest = Word(command, verb, sizeof verb);
    if (!lstrcmpiA(verb, "open"))
    {
        MCIERROR error = 0;
        if (Open(rest, &error)) return error;
        return mciSendStringA(command, result, size, callback);
    }
    rest = Word(rest, alias, sizeof alias);
    d = Find(alias);
    if (!d) return mciSendStringA(command, result, size, callback);
    return Command(d, verb, rest, result, size);
}
BOOL MovieMciGetErrorString(MCIERROR error, LPSTR text, UINT size)
{
    const char *message = NULL;
    switch (error)
    {
    case ErrUnsupported: message = "The film device of this program does not serve this command."; break;
    case ErrOutOfRange: message = "The position lies beyond the end of the film."; break;
    case ErrInternal: message = "The film could not be opened in a window."; break;
    }
    if (!message) return mciGetErrorStringA(error, text, size);
    lstrcpynA(text, message, (int)size);
    return TRUE;
}
