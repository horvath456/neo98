/* SPDX-License-Identifier: GPL-2.0-or-later
 * A flip chain flipped as fast as the program can, with DDFLIP_NOVSYNC, the
 * way Expendable flips without looking at the caps. Writes what it measured
 * to C:\FLIPPACE.TXT: the dwCaps2 of the driver, the frames and the
 * milliseconds they took. */
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>

void *memset(void *destination, int value, size_t count)
{
    volatile BYTE *out = destination;
    for (size_t i = 0; i < count; ++i) out[i] = (BYTE)value;
    return destination;
}

enum { Frames = 120 };

void mainCRTStartup(void)
{
    WNDCLASSA c = {0};
    IDirectDraw *dd = NULL;
    IDirectDrawSurface *front = NULL;
    DDSURFACEDESC d = {0};
    DDCAPS caps = {0};
    char text[96];
    DWORD written = 0;
    c.lpfnWndProc = DefWindowProcA;
    c.lpszClassName = "FlipPace";
    RegisterClassA(&c);
    HWND w = CreateWindowExA(0, c.lpszClassName, "flip pace", WS_POPUP | WS_VISIBLE, 0, 0, 640, 480, NULL, NULL, NULL, NULL);
    if (!w || DirectDrawCreate(NULL, &dd, NULL)) ExitProcess(1);
    if (IDirectDraw_SetCooperativeLevel(dd, w, DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN)) ExitProcess(2);
    if (IDirectDraw_SetDisplayMode(dd, 640, 480, 16)) ExitProcess(3);
    caps.dwSize = sizeof caps;
    if (IDirectDraw_GetCaps(dd, &caps, NULL)) ExitProcess(4);
    d.dwSize = sizeof d;
    d.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
    d.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP | DDSCAPS_COMPLEX;
    d.dwBackBufferCount = 1;
    if (IDirectDraw_CreateSurface(dd, &d, &front, NULL)) ExitProcess(5);
    /* The first flip sets the phase; the time of the following ones counts. */
    if (IDirectDrawSurface_Flip(front, NULL, DDFLIP_WAIT | DDFLIP_NOVSYNC)) ExitProcess(6);
    const DWORD start = GetTickCount();
    for (unsigned i = 0; i < Frames; ++i)
        if (IDirectDrawSurface_Flip(front, NULL, DDFLIP_WAIT | DDFLIP_NOVSYNC)) ExitProcess(7);
    const DWORD elapsed = GetTickCount() - start;
    IDirectDrawSurface_Release(front);
    IDirectDraw_RestoreDisplayMode(dd);
    IDirectDraw_Release(dd);
    wsprintfA(text, "caps2=%08lx frames=%u ms=%lu\r\n", caps.dwCaps2, (unsigned)Frames, elapsed);
    HANDLE file = CreateFileA("C:\\FLIPPACE.TXT", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) ExitProcess(8);
    WriteFile(file, text, lstrlenA(text), &written, NULL);
    CloseHandle(file);
    ExitProcess(0);
}
