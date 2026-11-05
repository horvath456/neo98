/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* A film player as an ARM64 application: the decoders are part of the program
   (Cinepak in AVI, MPEG-1 with layer II sound), the picture goes to the
   window with StretchDIBits, the sound to waveOut, and the sound is the
   clock the picture follows. The window draws its own controls.

       PLAYER.EXE film play
       PLAYER.EXE /bench film result decode every frame as fast as it
                                           goes, without a window, and write
                                           what was measured into `result`
       PLAYER.EXE /play film result play once to the end, write what
                                           was measured, and leave

   Keys: space plays and pauses, left and right move by five seconds, up and
   down change the volume, F or Alt+Enter switches the full screen, Esc
   leaves it. sdk/samples/player, docs/ARM64-APPS.md. */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "movie.h"

#define BAR_HEIGHT 44
#define BAR_WIDTH 360
#define WAVE_BLOCKS 8

static Movie *movie;
static const MovieInfo *info;
static HWND window;
static const uint32_t *picture; /* the frame on the screen, owned by the decoder */

/* --- sound ------------------------------------------------------------------ */
static HWAVEOUT wave;
static WAVEHDR wave_header[WAVE_BLOCKS];
static int16_t *wave_data[WAVE_BLOCKS];
static int wave_block_frames, sound_ended;
/* The sound device is the clock, and the last time it named. */
static int sound_clock;
static double sound_seconds;
static int volume = 80; /* percent */

/* --- the clock ---------------------------------------------------------------- */
static int playing, ended;
static double base_seconds;  /* the film time the clock started from */
static DWORD base_ticks;     /* without sound: timeGetTime at that moment */
static double shown_seconds = -1, pause_seconds;
static int fullscreen;
static WINDOWPLACEMENT windowed_placement;
static LONG windowed_style;

/* --- what a run measures -------------------------------------------------------- */
static struct
{
    unsigned decoded, presented, late;
    double decode_ms, present_ms;
    unsigned sound_blocks;
} measured;
static LARGE_INTEGER counter_frequency;
static double Milliseconds(void)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart * 1000.0 / (double)counter_frequency.QuadPart;
}

static void SetVolume(void)
{
    const DWORD level = (DWORD)(volume * 0xffff / 100);
    if (wave) waveOutSetVolume(wave, level | level << 16);
}
static void OpenSound(void)
{
    WAVEFORMATEX format;
    if (!info->audio_rate) return;
    memset(&format, 0, sizeof format);
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = (WORD)info->audio_channels;
    format.nSamplesPerSec = (DWORD)info->audio_rate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = (WORD)(2 * info->audio_channels);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    if (waveOutOpen(&wave, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR)
    {
        wave = NULL;
        return;
    }
    /* A tenth of a second per block. */
    wave_block_frames = info->audio_rate / 10;
    for (int i = 0; i < WAVE_BLOCKS; ++i)
    {
        wave_data[i] = (int16_t *)malloc((size_t)wave_block_frames * format.nBlockAlign);
        memset(&wave_header[i], 0, sizeof wave_header[i]);
        wave_header[i].lpData = (LPSTR)wave_data[i];
        wave_header[i].dwBufferLength = (DWORD)wave_block_frames * format.nBlockAlign;
        waveOutPrepareHeader(wave, &wave_header[i], sizeof wave_header[i]);
    }
    SetVolume();
}
static void CloseSound(void)
{
    if (!wave) return;
    waveOutReset(wave);
    for (int i = 0; i < WAVE_BLOCKS; ++i)
    {
        waveOutUnprepareHeader(wave, &wave_header[i], sizeof wave_header[i]);
        free(wave_data[i]);
    }
    waveOutClose(wave);
    wave = NULL;
}
/* Every block the device is not holding gets the next sound of the film. */
static void FeedSound(void)
{
    if (!wave || sound_ended) return;
    for (int i = 0; i < WAVE_BLOCKS; ++i)
    {
        if (wave_header[i].dwFlags & WHDR_INQUEUE) continue;
        const int frames = MovieReadAudio(movie, wave_data[i], wave_block_frames);
        if (frames <= 0)
        {
            sound_ended = 1;
            return;
        }
        wave_header[i].dwBufferLength = (DWORD)frames * 2 * (DWORD)info->audio_channels;
        if (waveOutWrite(wave, &wave_header[i], sizeof wave_header[i]) == MMSYSERR_NOERROR) ++measured.sound_blocks;
    }
}
/* The time of the film: what the sound device has played since the clock
   started, or the timer of the system for a silent film. */
static double Clock(void)
{
    if (!playing) return pause_seconds;
    if (wave && sound_clock)
    {
        MMTIME position;
        int queued = 0;
        for (int i = 0; i < WAVE_BLOCKS; ++i) queued |= (wave_header[i].dwFlags & WHDR_INQUEUE) != 0;
        position.wType = TIME_SAMPLES;
        if ((queued || !sound_ended) && waveOutGetPosition(wave, &position, sizeof position) == MMSYSERR_NOERROR &&
            position.wType == TIME_SAMPLES)
            return sound_seconds = base_seconds + (double)position.u.sample / info->audio_rate;
        /* The sound is over before the picture: the timer of the system
           takes over from where the sound stopped. */
        sound_clock = 0;
        base_seconds = sound_seconds;
        base_ticks = timeGetTime();
    }
    return base_seconds + (double)(timeGetTime() - base_ticks) / 1000.0;
}
/* The clock starts at this film time; both streams continue there. */
static void StartClock(double seconds)
{
    if (wave) waveOutReset(wave); /* the position of the device is zero again */
    sound_ended = 0;
    sound_clock = wave != NULL;
    sound_seconds = seconds;
    base_seconds = seconds;
    base_ticks = timeGetTime();
}

/* --- the picture ---------------------------------------------------------------- */
static void VideoRect(RECT *area)
{
    RECT client;
    GetClientRect(window, &client);
    client.bottom -= fullscreen ? 0 : BAR_HEIGHT;
    if (client.bottom < 1) client.bottom = 1;
    /* The largest rectangle of the film's shape. */
    int width = client.right, height = MulDiv(client.right, info->height, info->width);
    if (height > client.bottom)
    {
        height = client.bottom;
        width = MulDiv(client.bottom, info->width, info->height);
    }
    area->left = (client.right - width) / 2;
    area->top = (client.bottom - height) / 2;
    area->right = area->left + width;
    area->bottom = area->top + height;
}
static void Present(HDC dc)
{
    BITMAPINFO header;
    RECT area;
    if (!picture) return;
    VideoRect(&area);
    memset(&header, 0, sizeof header);
    header.bmiHeader.biSize = sizeof header.bmiHeader;
    header.bmiHeader.biWidth = info->stride;
    header.bmiHeader.biHeight = -info->height;
    header.bmiHeader.biPlanes = 1;
    header.bmiHeader.biBitCount = 32;
    header.bmiHeader.biCompression = BI_RGB;
    SetStretchBltMode(dc, COLORONCOLOR);
    if (area.right - area.left == info->width && area.bottom - area.top == info->height)
        SetDIBitsToDevice(dc, area.left, area.top, (DWORD)info->width, (DWORD)info->height, 0, 0, 0, (UINT)info->height, picture,
                          &header, DIB_RGB_COLORS);
    else
        StretchDIBits(dc, area.left, area.top, area.right - area.left, area.bottom - area.top, 0, 0, info->width, info->height,
                      picture, &header, DIB_RGB_COLORS, SRCCOPY);
}

/* --- the bar ---------------------------------------------------------------------- */
enum { PartNone, PartPlay, PartSeek, PartVolume, PartFull };
static int captured;
static void BarRects(RECT *play, RECT *seek, RECT *text, RECT *level, RECT *full)
{
    RECT client;
    GetClientRect(window, &client);
    const int top = client.bottom - BAR_HEIGHT + 6, bottom = client.bottom - 6;
    SetRect(play, 6, top, 6 + 40, bottom);
    SetRect(full, client.right - 6 - 40, top, client.right - 6, bottom);
    SetRect(level, full->left - 8 - 56, top, full->left - 8, bottom);
    SetRect(text, level->left - 8 - 84, top, level->left - 8, bottom);
    SetRect(seek, play->right + 10, top, text->left - 8, bottom);
    if (seek->right < seek->left + 20) seek->right = seek->left + 20;
}
static void Slider(HDC dc, const RECT *area, double part)
{
    RECT track = *area, thumb;
    const int middle = (area->top + area->bottom) / 2;
    track.top = middle - 2;
    track.bottom = middle + 2;
    DrawEdge(dc, &track, EDGE_SUNKEN, BF_RECT);
    if (part < 0) part = 0;
    if (part > 1) part = 1;
    thumb.left = area->left + (int)((area->right - area->left - 10) * part);
    thumb.right = thumb.left + 10;
    thumb.top = area->top + 4;
    thumb.bottom = area->bottom - 4;
    FillRect(dc, &thumb, (HBRUSH)GetStockObject(LTGRAY_BRUSH));
    DrawEdge(dc, &thumb, EDGE_RAISED, BF_RECT);
}
static void DrawBar(HDC dc)
{
    RECT client, bar, play, seek, text, level, full;
    char label[48];
    const double now = ended ? info->seconds : (shown_seconds < 0 ? 0 : shown_seconds);
    HGDIOBJ old_font, old_brush, old_pen;
    if (fullscreen) return;
    GetClientRect(window, &client);
    bar = client;
    bar.top = client.bottom - BAR_HEIGHT;
    FillRect(dc, &bar, (HBRUSH)GetStockObject(LTGRAY_BRUSH));
    DrawEdge(dc, &bar, EDGE_RAISED, BF_TOP);
    BarRects(&play, &seek, &text, &level, &full);
    DrawFrameControl(dc, &play, DFC_BUTTON, DFCS_BUTTONPUSH);
    DrawFrameControl(dc, &full, DFC_BUTTON, DFCS_BUTTONPUSH);
    old_brush = SelectObject(dc, GetStockObject(BLACK_BRUSH));
    old_pen = SelectObject(dc, GetStockObject(BLACK_PEN));
    {
        const int cx = (play.left + play.right) / 2, cy = (play.top + play.bottom) / 2;
        if (playing)
        {
            Rectangle(dc, cx - 6, cy - 7, cx - 2, cy + 7);
            Rectangle(dc, cx + 2, cy - 7, cx + 6, cy + 7);
        }
        else
        {
            const POINT triangle[3] = {{cx - 5, cy - 8}, {cx - 5, cy + 8}, {cx + 7, cy}};
            Polygon(dc, triangle, 3);
        }
    }
    {
        /* A frame with a filled corner: the full screen. */
        RECT frame = {full.left + 11, full.top + 8, full.right - 11, full.bottom - 8}, corner;
        FrameRect(dc, &frame, (HBRUSH)GetStockObject(BLACK_BRUSH));
        SetRect(&corner, frame.left, frame.top, frame.left + 7, frame.top + 5);
        FillRect(dc, &corner, (HBRUSH)GetStockObject(BLACK_BRUSH));
    }
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    Slider(dc, &seek, info->seconds > 0 ? now / info->seconds : 0);
    Slider(dc, &level, volume / 100.0);
    old_font = SelectObject(dc, GetStockObject(DEFAULT_GUI_FONT));
    SetBkMode(dc, TRANSPARENT);
    snprintf(label, sizeof label, "%d:%02d / %d:%02d", (int)now / 60, (int)now % 60, (int)info->seconds / 60,
             (int)info->seconds % 60);
    DrawTextA(dc, label, -1, &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, old_font);
}
static void Paint(HDC dc)
{
    RECT client, area, side;
    GetClientRect(window, &client);
    if (!fullscreen) client.bottom -= BAR_HEIGHT;
    VideoRect(&area);
    /* The black beside the picture, without touching the picture itself. */
    SetRect(&side, 0, 0, client.right, area.top);
    FillRect(dc, &side, (HBRUSH)GetStockObject(BLACK_BRUSH));
    SetRect(&side, 0, area.bottom, client.right, client.bottom);
    FillRect(dc, &side, (HBRUSH)GetStockObject(BLACK_BRUSH));
    SetRect(&side, 0, area.top, area.left, area.bottom);
    FillRect(dc, &side, (HBRUSH)GetStockObject(BLACK_BRUSH));
    SetRect(&side, area.right, area.top, client.right, area.bottom);
    FillRect(dc, &side, (HBRUSH)GetStockObject(BLACK_BRUSH));
    if (picture) Present(dc);
    else FillRect(dc, &area, (HBRUSH)GetStockObject(BLACK_BRUSH));
    DrawBar(dc);
}
static void RedrawBar(void)
{
    HDC dc = GetDC(window);
    DrawBar(dc);
    ReleaseDC(window, dc);
}

/* --- playing ------------------------------------------------------------------------ */
/* Decodes the next frame; true unless the film is over. */
static int Decode(void)
{
    const double before = Milliseconds();
    double seconds = 0;
    const uint32_t *pixels = NULL;
    if (!MovieNextFrame(movie, &pixels, &seconds)) return 0;
    measured.decode_ms += Milliseconds() - before;
    ++measured.decoded;
    picture = pixels;
    shown_seconds = seconds;
    return 1;
}
static void Show(void)
{
    const double before = Milliseconds();
    HDC dc = GetDC(window);
    Present(dc);
    ReleaseDC(window, dc);
    measured.present_ms += Milliseconds() - before;
    ++measured.presented;
}
static void Play(void)
{
    if (playing) return;
    if (ended)
    {
        MovieSeek(movie, 0);
        shown_seconds = -1;
        pause_seconds = 0;
        ended = 0;
    }
    StartClock(pause_seconds);
    playing = 1;
    FeedSound();
    RedrawBar();
}
static void Pause(void)
{
    if (!playing) return;
    pause_seconds = Clock();
    playing = 0;
    /* What the device still holds is dropped; the film continues from the
       picture that is on the screen. */
    if (wave) waveOutReset(wave);
    MovieSeek(movie, pause_seconds);
    shown_seconds = -1;
    RedrawBar();
}
static void SeekTo(double seconds)
{
    const int was_playing = playing;
    if (seconds < 0) seconds = 0;
    if (seconds > info->seconds) seconds = info->seconds;
    playing = 0;
    if (wave) waveOutReset(wave);
    MovieSeek(movie, seconds);
    ended = 0;
    shown_seconds = -1;
    /* The picture of the new place, also while the film stands. */
    if (Decode()) Show();
    pause_seconds = shown_seconds < 0 ? seconds : shown_seconds;
    /* The sound follows the picture that was found. */
    if (was_playing)
    {
        StartClock(pause_seconds);
        playing = 1;
        FeedSound();
    }
    RedrawBar();
}
/* One round while the film plays: sound ahead, the frame that is due. Returns
   the milliseconds until the next frame is due. */
static DWORD Tick(void)
{
    const double frame_seconds = 1.0 / info->frames_per_second;
    int decoded = 0;
    if (!playing) return 50;
    FeedSound();
    double now = Clock();
    /* Every frame that is due is decoded; only the last one is shown. */
    while (shown_seconds < 0 || now >= shown_seconds + frame_seconds)
    {
        if (!Decode())
        {
            playing = 0;
            ended = 1;
            pause_seconds = info->seconds;
            RedrawBar();
            PostMessageA(window, WM_APP, 0, 0);
            return 50;
        }
        if (decoded++) ++measured.late;
        if (decoded > 30) break; /* far behind: show something */
    }
    if (decoded)
    {
        static int rounds;
        Show();
        if (!(++rounds % 5)) RedrawBar();
    }
    now = Clock();
    const double wait = shown_seconds + frame_seconds - now;
    return wait <= 0 ? 0 : wait > 0.02 ? 20 : (DWORD)(wait * 1000);
}

static void ToggleFullscreen(void)
{
    if (!fullscreen)
    {
        windowed_placement.length = sizeof windowed_placement;
        GetWindowPlacement(window, &windowed_placement);
        windowed_style = GetWindowLongA(window, GWL_STYLE);
        fullscreen = 1;
        SetWindowLongA(window, GWL_STYLE, (LONG)(WS_POPUP | WS_VISIBLE));
        SetWindowPos(window, HWND_TOP, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
    else
    {
        fullscreen = 0;
        SetWindowLongA(window, GWL_STYLE, windowed_style);
        SetWindowPlacement(window, &windowed_placement);
        SetWindowPos(window, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    }
    InvalidateRect(window, NULL, FALSE);
}
static double PartOf(const RECT *area, int x)
{
    const double part = (double)(x - area->left - 5) / (double)(area->right - area->left - 10);
    return part < 0 ? 0 : part > 1 ? 1 : part;
}
static void Drag(int part, int x)
{
    RECT play, seek, text, level, full;
    BarRects(&play, &seek, &text, &level, &full);
    if (part == PartSeek) SeekTo(PartOf(&seek, x) * info->seconds);
    else if (part == PartVolume)
    {
        volume = (int)(PartOf(&level, x) * 100 + 0.5);
        SetVolume();
        RedrawBar();
    }
}

static const char *result_path;
static void WriteResult(const char *mode, double wall_ms);

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(hwnd, &paint);
        Paint(dc);
        EndPaint(hwnd, &paint);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
    {
        RECT play, seek, text, level, full;
        const POINT point = {(short)LOWORD(lParam), (short)HIWORD(lParam)};
        if (fullscreen) { if (playing) Pause(); else Play(); return 0; }
        BarRects(&play, &seek, &text, &level, &full);
        if (PtInRect(&play, point)) { if (playing) Pause(); else Play(); }
        else if (PtInRect(&full, point)) ToggleFullscreen();
        else if (PtInRect(&seek, point)) { captured = PartSeek; SetCapture(hwnd); Drag(captured, point.x); }
        else if (PtInRect(&level, point)) { captured = PartVolume; SetCapture(hwnd); Drag(captured, point.x); }
        else if (point.y < play.top - 6) { if (playing) Pause(); else Play(); }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (captured) Drag(captured, (short)LOWORD(lParam));
        return 0;
    case WM_LBUTTONUP:
        if (captured) { captured = PartNone; ReleaseCapture(); }
        return 0;
    case WM_LBUTTONDBLCLK:
        ToggleFullscreen();
        return 0;
    case WM_SYSKEYDOWN:
        if (wParam == VK_RETURN) { ToggleFullscreen(); return 0; }
        break;
    case WM_KEYDOWN:
        switch (wParam)
        {
        case VK_SPACE: if (playing) Pause(); else Play(); break;
        case VK_LEFT: SeekTo((playing ? Clock() : pause_seconds) - 5); break;
        case VK_RIGHT: SeekTo((playing ? Clock() : pause_seconds) + 5); break;
        case VK_UP: volume = volume > 90 ? 100 : volume + 10; SetVolume(); RedrawBar(); break;
        case VK_DOWN: volume = volume < 10 ? 0 : volume - 10; SetVolume(); RedrawBar(); break;
        case 'F': ToggleFullscreen(); break;
        case VK_ESCAPE: if (fullscreen) ToggleFullscreen(); break;
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, message, wParam, lParam);
}

/* --- measuring ---------------------------------------------------------------------- */
static uint32_t Crc(uint32_t crc, const void *data, size_t size)
{
    static uint32_t table[256];
    const uint8_t *bytes = (const uint8_t *)data;
    if (!table[1])
        for (uint32_t i = 0; i < 256; ++i)
        {
            uint32_t value = i;
            for (int bit = 0; bit < 8; ++bit) value = value & 1 ? value >> 1 ^ 0xedb88320u : value >> 1;
            table[i] = value;
        }
    crc = ~crc;
    while (size--) crc = table[(crc ^ *bytes++) & 0xff] ^ crc >> 8;
    return ~crc;
}
static FILE *result_file;
static void WriteResult(const char *mode, double wall_ms)
{
    if (!result_file) result_file = fopen(result_path, "w");
    if (!result_file) return;
    fprintf(result_file, "mode=%s\nvideo=%s\naudio=%s\nwidth=%d\nheight=%d\nframes=%d\nfps=%.3f\nseconds=%.3f\n", mode,
            info->video_codec, info->audio_codec ? info->audio_codec : "none", info->width, info->height, info->frames,
            info->frames_per_second, info->seconds);
    fprintf(result_file, "audio_rate=%d\naudio_channels=%d\n", info->audio_rate, info->audio_channels);
    fprintf(result_file, "decoded=%u\npresented=%u\nlate=%u\ndecode_ms=%.3f\npresent_ms=%.3f\nwall_ms=%.3f\nsound_blocks=%u\n",
            measured.decoded, measured.presented, measured.late, measured.decode_ms, measured.present_ms, wall_ms,
            measured.sound_blocks);
}
/* Decodes the film twice without a window: once for the time, once for what
   came out (a checksum and the mean colour of every frame, a checksum of the
   sound, and the frame a seek to the middle finds). */
static int Bench(void)
{
    const double start = Milliseconds();
    const uint32_t *pixels;
    double seconds;
    uint32_t all = 0, sound = 0;
    unsigned frames = 0;
    unsigned long long sound_frames = 0;
    static int16_t samples[4096 * 2];
    while (MovieNextFrame(movie, &pixels, &seconds)) ++frames;
    const double decode_ms = Milliseconds() - start;
    measured.decoded = frames;
    measured.decode_ms = decode_ms;
    WriteResult("bench", decode_ms);
    MovieSeek(movie, 0);
    for (unsigned frame = 0; MovieNextFrame(movie, &pixels, &seconds); ++frame)
    {
        uint32_t crc = 0;
        unsigned long long blue = 0, green = 0, red = 0;
        for (int y = 0; y < info->height; ++y)
        {
            const uint32_t *row = pixels + (size_t)y * (size_t)info->stride;
            crc = Crc(crc, row, (size_t)info->width * 4);
            for (int x = 0; x < info->width; ++x)
            {
                blue += row[x] & 0xff;
                green += row[x] >> 8 & 0xff;
                red += row[x] >> 16 & 0xff;
            }
        }
        all = Crc(all, &crc, 4);
        const unsigned long long count = (unsigned long long)info->width * (unsigned long long)info->height;
        fprintf(result_file, "frame %u %08x %u %u %u %.3f\n", frame, crc, (unsigned)(blue / count), (unsigned)(green / count),
                (unsigned)(red / count), seconds);
    }
    for (int got; (got = MovieReadAudio(movie, samples, 4096)) > 0;)
    {
        sound = Crc(sound, samples, (size_t)got * 2 * (size_t)info->audio_channels);
        sound_frames += (unsigned long long)got;
    }
    fprintf(result_file, "frames_crc=%08x\nsound_frames=%llu\nsound_crc=%08x\n", all, sound_frames, sound);
    MovieSeek(movie, info->seconds / 2);
    if (MovieNextFrame(movie, &pixels, &seconds)) fprintf(result_file, "seek_half=%.3f\n", seconds);
    fprintf(result_file, "done=1\n");
    fclose(result_file);
    return 0;
}

static int Run(int argc, char **argv)
{
    WNDCLASSA wc;
    MSG message;
    RECT frame;
    const char *path = NULL, *error = "";
    char title[300];
    int bench = 0, once = 0;
    double started;

    QueryPerformanceFrequency(&counter_frequency);
    for (int i = 1; i < argc; ++i)
    {
        if (!_stricmp(argv[i], "/bench")) bench = 1;
        else if (!_stricmp(argv[i], "/play")) once = 1;
        else if (!path) path = argv[i];
        else if (!result_path) result_path = argv[i];
    }
    if (!path)
    {
        MessageBoxA(NULL, "PLAYER.EXE film\n\nAVI with Cinepak video, or MPEG-1.", "Player", MB_OK | MB_ICONINFORMATION);
        return 2;
    }
    movie = MovieOpen(path, &error);
    if (!movie)
    {
        snprintf(title, sizeof title, "%s\n\n%s", path, error);
        if ((bench || once) && result_path)
        {
            FILE *file = fopen(result_path, "w");
            if (file) { fprintf(file, "error=%s\n", error); fclose(file); }
        }
        else MessageBoxA(NULL, title, "Player", MB_OK | MB_ICONERROR);
        return 1;
    }
    info = MovieGetInfo(movie);
    if (bench && result_path) return Bench();

    memset(&wc, 0, sizeof wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.lpszClassName = "Neo98Player";
    RegisterClassA(&wc);
    /* A small film still gets a window wide enough for the bar. */
    SetRect(&frame, 0, 0, info->width < BAR_WIDTH ? BAR_WIDTH : info->width, info->height + BAR_HEIGHT);
    AdjustWindowRect(&frame, WS_OVERLAPPEDWINDOW, FALSE);
    snprintf(title, sizeof title, "%s - Player", strrchr(path, '\\') ? strrchr(path, '\\') + 1 : path);
    window = CreateWindowExA(0, "Neo98Player", title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                             frame.right - frame.left, frame.bottom - frame.top, NULL, NULL, wc.hInstance, NULL);
    if (!window) return 1;
    timeBeginPeriod(1);
    OpenSound();
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    started = Milliseconds();
    Play();
    for (;;)
    {
        /* While the film plays the loop does not wait for messages longer
           than until the next frame; while it stands, it sleeps in GetMessage. */
        if (playing)
        {
            int quit = 0;
            while (PeekMessageA(&message, NULL, 0, 0, PM_REMOVE))
            {
                if (message.message == WM_QUIT) { quit = 1; break; }
                TranslateMessage(&message);
                DispatchMessageA(&message);
            }
            if (quit) break;
            const DWORD wait = Tick();
            if (wait) Sleep(wait > 5 ? 5 : wait);
            continue;
        }
        if (once && ended)
        {
            if (result_path)
            {
                WriteResult("play", Milliseconds() - started);
                fprintf(result_file, "done=1\n");
                fclose(result_file);
            }
            DestroyWindow(window);
            once = 0;
        }
        if (GetMessageA(&message, NULL, 0, 0) <= 0) break;
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    CloseSound();
    timeEndPeriod(1);
    MovieClose(movie);
    return 0;
}

/* A program of the Windows subsystem: no console window beside the film. */
int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command_line, int show)
{
    (void)instance; (void)previous; (void)command_line; (void)show;
    return Run(__argc, __argv);
}
