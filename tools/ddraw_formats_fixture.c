/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The same surface operations against the original x86 DDRAW and ARM64 SDK. */
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>

static const DWORD formats[][5] = {
    {16, 0x7c00, 0x03e0, 0x001f, 0},
    {16, 0xf800, 0x07e0, 0x001f, 0},
    {16, 0x7c00, 0x03e0, 0x001f, 0x8000},
    {16, 0x0f00, 0x00f0, 0x000f, 0xf000},
    {32, 0xff0000, 0x00ff00, 0x0000ff, 0},
    {32, 0x0000ff, 0x00ff00, 0xff0000, 0},
    {24, 0xff0000, 0x00ff00, 0x0000ff, 0},
};
enum { Count = sizeof formats / sizeof formats[0] };
static HANDLE report;

static void Row(DWORD operation, DWORD source, DWORD dest, HRESULT result, DWORD value)
{
    DWORD row[5] = {operation, source, dest, (DWORD)result, value}, written;
    if (!WriteFile(report, row, sizeof row, &written, NULL) || written != sizeof row) ExitProcess(4);
}

static HRESULT Pixel(IDirectDrawSurface *surface, DWORD *value, BOOL write)
{
    DDSURFACEDESC desc = {0};
    HRESULT hr;
    unsigned i;
    desc.dwSize = sizeof desc;
    hr = IDirectDrawSurface_Lock(surface, NULL, &desc, DDLOCK_WAIT, NULL);
    if (hr != DD_OK) return hr;
    if (!write) *value = 0;
    for (i = 0; i < desc.ddpfPixelFormat.dwRGBBitCount / 8; ++i) {
        if (write) ((BYTE *)desc.lpSurface)[i] = (BYTE)(*value >> (i * 8));
        else *value |= (DWORD)((BYTE *)desc.lpSurface)[i] << (i * 8);
    }
    return IDirectDrawSurface_Unlock(surface, desc.lpSurface);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show)
{
    IDirectDraw *dd = NULL;
    IDirectDrawSurface *surfaces[Count] = {0};
    WNDCLASSA klass = {0};
    HWND window;
    unsigned i, j;
    (void)previous; (void)command; (void)show;
    klass.lpfnWndProc = DefWindowProcA;
    klass.hInstance = instance;
    klass.lpszClassName = "DdFormats";
    RegisterClassA(&klass);
    window = CreateWindowExA(0, klass.lpszClassName, "DdFormats", WS_POPUP,
                             0, 0, 100, 100, NULL, NULL, instance, NULL);
    if (!window || DirectDrawCreate(NULL, &dd, NULL) != DD_OK) return 1;
    if (IDirectDraw_SetCooperativeLevel(dd, window, DDSCL_NORMAL) != DD_OK) return 2;
    report = CreateFileA("E:\\FORMATS.BIN", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (report == INVALID_HANDLE_VALUE) return 3;
    for (i = 0; i < Count; ++i) {
        DDSURFACEDESC desc = {0};
        HDC dc = NULL;
        DWORD pixel = 0;
        HRESULT hr;
        desc.dwSize = sizeof desc;
        desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
        desc.dwWidth = desc.dwHeight = 4;
        desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
        desc.ddpfPixelFormat.dwSize = sizeof desc.ddpfPixelFormat;
        desc.ddpfPixelFormat.dwFlags = DDPF_RGB | (formats[i][4] ? DDPF_ALPHAPIXELS : 0);
        desc.ddpfPixelFormat.dwRGBBitCount = formats[i][0];
        desc.ddpfPixelFormat.dwRBitMask = formats[i][1];
        desc.ddpfPixelFormat.dwGBitMask = formats[i][2];
        desc.ddpfPixelFormat.dwBBitMask = formats[i][3];
        desc.ddpfPixelFormat.dwRGBAlphaBitMask = formats[i][4];
        hr = IDirectDraw_CreateSurface(dd, &desc, &surfaces[i], NULL);
        Row(0, i, i, hr, 0);
        if (hr != DD_OK) continue;
        hr = IDirectDrawSurface_GetDC(surfaces[i], &dc);
        Row(1, i, i, hr, 0);
        if (hr == DD_OK) {
            pixel = SetPixel(dc, 0, 0, RGB(248, 128, 56));
            Row(2, i, i, IDirectDrawSurface_ReleaseDC(surfaces[i], dc), pixel);
            hr = Pixel(surfaces[i], &pixel, FALSE);
            Row(3, i, i, hr, pixel);
        }
        if (i == 1) {
            /* DDRAW16 reuses the 565 DC with a differently sized surface. */
            IDirectDrawSurface *larger = NULL;
            DDSURFACEDESC locked = {0};
            BYTE *at;
            desc.dwWidth = desc.dwHeight = 8;
            if (IDirectDraw_CreateSurface(dd, &desc, &larger, NULL) != DD_OK) return 11;
            if (IDirectDrawSurface_GetDC(larger, &dc) != DD_OK) return 12;
            SetPixel(dc, 7, 7, RGB(0, 255, 0));
            if (IDirectDrawSurface_ReleaseDC(larger, dc) != DD_OK) return 13;
            locked.dwSize = sizeof locked;
            if (IDirectDrawSurface_Lock(larger, NULL, &locked, DDLOCK_WAIT, NULL) != DD_OK) return 14;
            at = (BYTE *)locked.lpSurface + 7 * locked.lPitch + 14;
            pixel = at[0] | (DWORD)at[1] << 8;
            hr = IDirectDrawSurface_Unlock(larger, locked.lpSurface);
            Row(6, i, i, hr, pixel);
            IDirectDrawSurface_Release(larger);
        }
    }
    for (i = 0; i < Count; ++i) for (j = 0; j < Count; ++j) {
        DWORD pixel = 0;
        RECT rect = {0, 0, 1, 1};
        HRESULT hr;
        if (!surfaces[i] || !surfaces[j]) continue;
        if (Pixel(surfaces[j], &pixel, TRUE) != DD_OK) return 5;
        pixel = formats[i][1] | formats[i][3];
        if (Pixel(surfaces[i], &pixel, TRUE) != DD_OK) return 6;
        hr = IDirectDrawSurface_Blt(surfaces[j], &rect, surfaces[i], &rect, DDBLT_WAIT, NULL);
        if (Pixel(surfaces[j], &pixel, FALSE) != DD_OK) return 7;
        Row(4, i, j, hr, pixel);
        pixel = 0;
        if (Pixel(surfaces[j], &pixel, TRUE) != DD_OK) return 8;
        pixel = formats[i][1] | formats[i][3];
        if (Pixel(surfaces[i], &pixel, TRUE) != DD_OK) return 9;
        hr = IDirectDrawSurface_BltFast(surfaces[j], 0, 0, surfaces[i], &rect, DDBLTFAST_WAIT);
        if (Pixel(surfaces[j], &pixel, FALSE) != DD_OK) return 10;
        Row(5, i, j, hr, pixel);
    }
    for (i = 0; i < Count; ++i) if (surfaces[i]) IDirectDrawSurface_Release(surfaces[i]);
    IDirectDraw_Release(dd);
    CloseHandle(report);
    DestroyWindow(window);
    return 0;
}

#ifndef _WIN64
void mainCRTStartup(void) { ExitProcess(WinMain(GetModuleHandleA(NULL), NULL, NULL, 0)); }
#endif
