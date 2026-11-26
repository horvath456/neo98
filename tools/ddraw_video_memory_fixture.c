/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Video memory comes back when a surface goes: more surfaces than the heap
 * holds are made and released one after the other, and at the end the free
 * count is what it was at the start.
 *
 * Video memory is the display driver's. Without a GPU backend the host's
 * driver gives DirectDraw no driver of its own, as a card without one did:
 * DirectDraw emulates everything (DDCAPS_NOHARDWARE) and has no video
 * memory at all. The fixture says so instead of failing. */
#define CINTERFACE
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <ddraw.h>

static void require(int okay, UINT code)
{
    if (!okay)
        ExitProcess(code);
}

static DWORD available(LPDIRECTDRAW2 dd, DWORD *total)
{
    DDSCAPS caps;
    DWORD free_bytes = 0;
    caps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;
    require(IDirectDraw2_GetAvailableVidMem(dd, &caps, total, &free_bytes) == DD_OK, 4);
    return free_bytes;
}

void mainCRTStartup(void)
{
    LPDIRECTDRAW first = 0;
    LPDIRECTDRAW2 dd = 0;
    DWORD total = 0, before, made = 0;
    require(DirectDrawCreate(0, &first, 0) == DD_OK, 2);
    require(IDirectDraw_QueryInterface(first, &IID_IDirectDraw2, (void **)&dd) == DD_OK, 3);
    require(IDirectDraw2_SetCooperativeLevel(dd, 0, DDSCL_NORMAL) == DD_OK, 5);
    {
        DDCAPS driver, emulation;
        ZeroMemory(&driver, sizeof driver);
        ZeroMemory(&emulation, sizeof emulation);
        driver.dwSize = emulation.dwSize = sizeof driver;
        require(IDirectDraw2_GetCaps(dd, &driver, &emulation) == DD_OK, 10);
        if (driver.dwCaps & DDCAPS_NOHARDWARE)
        {
            OutputDebugStringA("DDRAW VIDEO MEMORY none: DirectDraw has no driver of its own");
            IDirectDraw2_Release(dd);
            IDirectDraw_Release(first);
            ExitProcess(0);
        }
    }
    before = available(dd, &total);
    require(total && before, 6);
    while (made < 2 * total)
    {
        DDSURFACEDESC d;
        LPDIRECTDRAWSURFACE surface = 0;
        ZeroMemory(&d, sizeof d);
        d.dwSize = sizeof d;
        d.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
        d.dwWidth = d.dwHeight = 512;
        d.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;
        require(IDirectDraw2_CreateSurface(dd, &d, &surface, 0) == DD_OK, 7);
        require(IDirectDrawSurface_GetSurfaceDesc(surface, &d) == DD_OK, 8);
        made += d.lPitch * d.dwHeight;
        IDirectDrawSurface_Release(surface);
    }
    require(available(dd, &total) == before, 9);
    OutputDebugStringA("DDRAW VIDEO MEMORY returned");
    IDirectDraw2_Release(dd);
    IDirectDraw_Release(first);
    ExitProcess(0);
}
