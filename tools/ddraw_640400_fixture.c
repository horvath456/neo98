/* SPDX-License-Identifier: GPL-2.0-or-later
 * legacy 640x400 indexed mode through original IDirectDraw v1. */
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>

void *memset(void *destination, int value, size_t count)
{
    volatile BYTE *out = destination;
    for (size_t i = 0; i < count; ++i) out[i] = (BYTE)value;
    return destination;
}

static unsigned found;
static HRESULT WINAPI mode(DDSURFACEDESC *d, void *unused)
{
    (void)unused;
    if (d->dwWidth == 640 && d->dwHeight == 400 && d->ddpfPixelFormat.dwRGBBitCount == 8) ++found;
    return DDENUMRET_OK;
}

void mainCRTStartup(void)
{
    WNDCLASSA c = {0};
    IDirectDraw *dd = NULL;
    IDirectDrawSurface *front = NULL, *back = NULL;
    IDirectDrawPalette *palette = NULL;
    PALETTEENTRY colors[256] = {{0}};
    DDSURFACEDESC d = {0};
    DDSCAPS caps = {DDSCAPS_BACKBUFFER};
    c.lpfnWndProc = DefWindowProcA;
    c.lpszClassName = "LegacyIndexedMode";
    RegisterClassA(&c);
    HWND w = CreateWindowExA(0, c.lpszClassName, "640x400 indexed", WS_POPUP | WS_VISIBLE,
                             0, 0, 640, 400, NULL, NULL, NULL, NULL);
    if (!w || DirectDrawCreate(NULL, &dd, NULL)) ExitProcess(1);
    if (IDirectDraw_SetCooperativeLevel(dd, w, DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN | DDSCL_ALLOWMODEX)) ExitProcess(2);
    if (IDirectDraw_EnumDisplayModes(dd, 0, NULL, NULL, mode)) ExitProcess(3);
    if (IDirectDraw_SetDisplayMode(dd, 640, 400, 8)) ExitProcess(100);
    if (!found) ExitProcess(4);
    d.dwSize = sizeof d;
    if (IDirectDraw_GetDisplayMode(dd, &d) || d.dwWidth != 640 || d.dwHeight != 400 ||
        d.ddpfPixelFormat.dwRGBBitCount != 8) ExitProcess(5);
    d = (DDSURFACEDESC){0}; d.dwSize = sizeof d;
    d.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
    d.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP | DDSCAPS_COMPLEX;
    d.dwBackBufferCount = 1;
    if (IDirectDraw_CreateSurface(dd, &d, &front, NULL) ||
        IDirectDrawSurface_GetAttachedSurface(front, &caps, &back)) ExitProcess(6);
    colors[1].peRed = 255; colors[2].peGreen = 255; colors[3].peBlue = 255;
    colors[4].peRed = colors[4].peGreen = 255;
    if (IDirectDraw_CreatePalette(dd, DDPCAPS_8BIT | DDPCAPS_ALLOW256, colors, &palette, NULL) ||
        IDirectDrawSurface_SetPalette(front, palette)) ExitProcess(7);
    d = (DDSURFACEDESC){0}; d.dwSize = sizeof d;
    if (IDirectDrawSurface_Lock(back, NULL, &d, DDLOCK_WAIT, NULL) || !d.lpSurface ||
        d.dwWidth != 640 || d.dwHeight != 400 || d.lPitch < 640 ||
        d.ddpfPixelFormat.dwRGBBitCount != 8) ExitProcess(8);
    for (unsigned y = 0; y < 400; ++y)
        for (unsigned x = 0; x < 640; ++x)
            ((BYTE *)d.lpSurface)[y * d.lPitch + x] = 1 + (x >= 320) + 2 * (y >= 200);
    if (IDirectDrawSurface_Unlock(back, d.lpSurface) || IDirectDrawSurface_Flip(front, NULL, DDFLIP_WAIT)) ExitProcess(9);
    Sleep(5000);
    IDirectDrawSurface_Release(back);
    IDirectDrawSurface_Release(front);
    IDirectDrawPalette_Release(palette);
    if (IDirectDraw_RestoreDisplayMode(dd)) ExitProcess(10);
    IDirectDraw_Release(dd);
    ExitProcess(0);
}
