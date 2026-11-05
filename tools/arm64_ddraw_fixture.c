/* SPDX-License-Identifier: GPL-2.0-or-later */
/* DirectDraw for ARM64 programs, checked from inside one: the interfaces of
   DDRAW.DLL (sdk/dx/ddraw) over the driver NEO98DX.DLL. Every check reads
   pixels back through Lock or compares an answer with what DirectDraw
   documents; the rows "SHOT name" ask the runner for a picture of the host
   image, which tools/test_arm64_dx_native.py compares.
    */
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include <stdio.h>
#include <string.h>

static int checks, failures;
/* A failed check is kept by its line and, for a result, its value, and all
   of them are printed in a few rows at the end ("FAILS line:value..."): the
   console holds twenty-five rows, and the rows "SHOT" must stay readable. The
   test finds the text of a check in this file by its line. */
static int failed_lines[96];
static unsigned long failed_values[96];
static void Fail(int line, unsigned long value)
{
    if (failures < 96)
    {
        failed_lines[failures] = line;
        failed_values[failures] = value;
    }
    ++failures;
}
/* With a file name as its argument the fixture writes the same rows there:
   the host shows no console to a test. The row "INFO" carries what the
   device says about itself. */
static const char *report_file;
static char info[160], timing[200];
static void Report(const char *name)
{
    FILE *file = report_file ? fopen(report_file, "w") : NULL;
    int i;
    for (i = 0; i < failures && i < 96; ++i)
    {
        const char *head = i % 8 ? " " : "FAILS ", *tail = i % 8 == 7 || i + 1 == failures ? "\n" : "";
        printf("%s%d:%lx%s", head, failed_lines[i], failed_values[i], tail);
        if (file) fprintf(file, "%s%d:%lx%s", head, failed_lines[i], failed_values[i], tail);
    }
    printf("INFO %s\nMEASURE %s\n%s checks=%d failures=%d\n", info, timing, name, checks, failures);
    if (file)
    {
        fprintf(file, "INFO %s\nMEASURE %s\n%s checks=%d failures=%d\n", info, timing, name, checks, failures);
        fclose(file);
    }
}
#define CHECK(condition, text) \
    do { ++checks; if (!(condition)) Fail(__LINE__, 0); } while (0)
#define CHECK_HR(expression, expected, text) \
    do { const HRESULT hr_ = (expression); ++checks; if (hr_ != (HRESULT)(expected)) Fail(__LINE__, (unsigned long)hr_); } while (0)

static HWND window;
static const int WindowX = 100, WindowY = 80, WindowWidth = 320, WindowHeight = 240;

static LRESULT CALLBACK Procedure(HWND h, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_ERASEBKGND) return 1;
    return DefWindowProcA(h, message, wparam, lparam);
}

static void Pump(void)
{
    MSG message;
    while (PeekMessageA(&message, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
}

/* A picture of the host image, taken by the runner while this sleeps. */
static void Shot(const char *name)
{
    Pump();
    printf("SHOT %s\n", name);
    Sleep(150);
}

static DWORD Pixel32(const DDSURFACEDESC *d, int x, int y)
{
    DWORD value;
    memcpy(&value, (const BYTE *)d->lpSurface + (size_t)y * d->lPitch + (size_t)x * 4, 4);
    return value & 0xffffff;
}

static void Put32(const DDSURFACEDESC *d, int x, int y, DWORD value)
{
    memcpy((BYTE *)d->lpSurface + (size_t)y * d->lPitch + (size_t)x * 4, &value, 4);
}

static IDirectDrawSurface *MakeSurface(IDirectDraw *dd, DWORD caps, DWORD width, DWORD height)
{
    DDSURFACEDESC d;
    IDirectDrawSurface *s = NULL;
    memset(&d, 0, sizeof d);
    d.dwSize = sizeof d;
    d.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    d.ddsCaps.dwCaps = caps;
    d.dwWidth = width;
    d.dwHeight = height;
    return IDirectDraw_CreateSurface(dd, &d, &s, NULL) == DD_OK ? s : NULL;
}

/* A pattern every pixel of which says where it is. */
static DWORD Pattern(int x, int y) { return (DWORD)(0x010000 * (x * 3 + 16) + 0x000100 * (y * 5 + 8) + 0x40); }

static void FillPattern(IDirectDrawSurface *s, int width, int height)
{
    DDSURFACEDESC d;
    int x, y;
    memset(&d, 0, sizeof d);
    d.dwSize = sizeof d;
    if (IDirectDrawSurface_Lock(s, NULL, &d, DDLOCK_WAIT, NULL) != DD_OK) return;
    for (y = 0; y < height; ++y)
        for (x = 0; x < width; ++x) Put32(&d, x, y, Pattern(x, y));
    IDirectDrawSurface_Unlock(s, NULL);
}

static HRESULT CALLBACK CountModes(LPDDSURFACEDESC d, LPVOID context)
{
    int *counts = (int *)context;
    ++counts[0];
    if (d->dwWidth == 640 && d->dwHeight == 480 && d->ddpfPixelFormat.dwRGBBitCount == 16) ++counts[1];
    if (d->dwSize != sizeof *d || !(d->dwFlags & DDSD_PIXELFORMAT)) ++counts[2];
    return DDENUMRET_OK;
}

static HRESULT CALLBACK CountAttached(LPDIRECTDRAWSURFACE s, LPDDSURFACEDESC d, LPVOID context)
{
    int *count = (int *)context;
    ++*count;
    (void)d;
    IDirectDrawSurface_Release(s);
    return DDENUMRET_OK;
}

static BOOL CALLBACK CountDevices(GUID *guid, LPSTR description, LPSTR name, LPVOID context)
{
    int *count = (int *)context;
    if (!guid && description && name && !strcmp(name, "display")) ++*count;
    return TRUE;
}

/* ------------------------------------------------------------------ objects */
static void Objects(IDirectDraw *dd)
{
    IDirectDraw2 *dd2 = NULL;
    IDirectDraw4 *dd4 = NULL;
    IDirectDraw7 *dd7 = NULL;
    IUnknown *unknown = NULL;
    void *nothing = (void *)1;
    static const GUID stranger = {0x12345678, 1, 2, {3, 4, 5, 6, 7, 8, 9, 10}};
    DDCAPS caps, hel;
    DDCAPS_DX3 old;
    DDSURFACEDESC mode;
    DDDEVICEIDENTIFIER id;
    DWORD value = 0, total = 0, free_bytes = 0;
    BOOL blank = 7;
    int counts[3] = {0, 0, 0}, devices = 0;
    DDSCAPS scaps = {DDSCAPS_OFFSCREENPLAIN};

    CHECK_HR(IDirectDraw_QueryInterface(dd, &IID_IDirectDraw2, (void **)&dd2), S_OK, "QueryInterface IDirectDraw2");
    CHECK_HR(IDirectDraw_QueryInterface(dd, &IID_IDirectDraw4, (void **)&dd4), S_OK, "QueryInterface IDirectDraw4");
    CHECK_HR(IDirectDraw_QueryInterface(dd, &IID_IDirectDraw7, (void **)&dd7), S_OK, "QueryInterface IDirectDraw7");
    CHECK_HR(IDirectDraw_QueryInterface(dd, &IID_IUnknown, (void **)&unknown), S_OK, "QueryInterface IUnknown");
    CHECK_HR(IDirectDraw_QueryInterface(dd, &stranger, &nothing), E_NOINTERFACE, "QueryInterface of a stranger");
    CHECK(nothing == NULL, "a refused QueryInterface clears the pointer");
    CHECK(dd2 && dd4 && dd7 && (void *)dd2 != (void *)dd && (void *)dd4 != (void *)dd7, "each version has its own pointer");
    if (!dd2 || !dd4 || !dd7 || !unknown) return;
    {
        IDirectDraw *back = NULL;
        CHECK_HR(IDirectDraw7_QueryInterface(dd7, &IID_IDirectDraw, (void **)&back), S_OK, "IDirectDraw7 leads back to IDirectDraw");
        CHECK(back == dd, "the same IDirectDraw pointer");
        if (back) IDirectDraw_Release(back);
    }
    /* Each interface of DirectDraw counts its own references. */
    /* Each interface of DirectDraw counts its own references; IUnknown is
       the first IDirectDraw. Held here: DirectDrawCreate and IUnknown. */
    CHECK_HR((HRESULT)IDirectDraw_AddRef(dd), 3, "a third reference of IDirectDraw");
    CHECK_HR((HRESULT)IDirectDraw_Release(dd), 2, "Release answers the count that is left");
    IUnknown_Release(unknown);

    memset(&caps, 0, sizeof caps);
    memset(&hel, 0, sizeof hel);
    caps.dwSize = sizeof caps;
    hel.dwSize = sizeof hel;
    CHECK_HR(IDirectDraw_GetCaps(dd, &caps, &hel), DD_OK, "GetCaps");
    CHECK((caps.dwCaps & (DDCAPS_BLT | DDCAPS_BLTSTRETCH | DDCAPS_BLTCOLORFILL | DDCAPS_COLORKEY)) ==
          (DDCAPS_BLT | DDCAPS_BLTSTRETCH | DDCAPS_BLTCOLORFILL | DDCAPS_COLORKEY), "the driver blits, stretches, fills and keys");
    CHECK(!(caps.dwCaps & DDCAPS_OVERLAY) && !(caps.dwCaps & DDCAPS_ALPHA), "no overlays and no alpha blits are claimed");
    CHECK(caps.dwVidMemTotal >= caps.dwVidMemFree && caps.dwVidMemTotal > 0, "video memory");
    CHECK(!(hel.dwCaps & DDCAPS_NOHARDWARE) && !(caps.dwCaps & DDCAPS_NOHARDWARE), "there is hardware, and the emulation layer does not say otherwise");
    CHECK((hel.dwCaps & (DDCAPS_BLT | DDCAPS_BLTSTRETCH | DDCAPS_BLTCOLORFILL)) == (DDCAPS_BLT | DDCAPS_BLTSTRETCH | DDCAPS_BLTCOLORFILL),
          "the emulation layer blits, stretches and fills");
    CHECK(caps.ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE, "surface caps");
    memset(&old, 0, sizeof old);
    old.dwSize = sizeof old;
    CHECK_HR(IDirectDraw_GetCaps(dd, (LPDDCAPS)&old, NULL), DD_OK, "GetCaps with the structure of DirectX 3");
    CHECK(old.dwCaps == caps.dwCaps && old.dwSize == sizeof old, "the short structure holds the same caps");
    caps.dwSize = 17;
    CHECK_HR(IDirectDraw_GetCaps(dd, &caps, NULL), DDERR_INVALIDPARAMS, "GetCaps with a size of no version");

    memset(&mode, 0, sizeof mode);
    mode.dwSize = sizeof mode;
    CHECK_HR(IDirectDraw_GetDisplayMode(dd, &mode), DD_OK, "GetDisplayMode");
    CHECK(mode.dwWidth == 800 && mode.dwHeight == 600 && mode.ddpfPixelFormat.dwRGBBitCount == 32, "the desktop is 800x600x32");
    CHECK(mode.ddpfPixelFormat.dwRBitMask == 0xff0000 && mode.ddpfPixelFormat.dwBBitMask == 0xff && mode.lPitch == 3200,
          "masks and pitch of the mode");
    CHECK_HR(IDirectDraw_EnumDisplayModes(dd, 0, NULL, counts, CountModes), DD_OK, "EnumDisplayModes");
    CHECK(counts[0] >= 12 && counts[1] == 1 && counts[2] == 0, "the mode list holds 640x480x16 once");
    CHECK_HR(IDirectDraw_GetMonitorFrequency(dd, &value), DD_OK, "GetMonitorFrequency");
    CHECK(value == 60, "sixty pictures a second");
    CHECK_HR(IDirectDraw2_GetAvailableVidMem(dd2, &scaps, &total, &free_bytes), DD_OK, "GetAvailableVidMem");
    CHECK(total == caps.dwVidMemTotal && free_bytes <= total, "the same video memory as in the caps");
    CHECK_HR(IDirectDraw_GetVerticalBlankStatus(dd, &blank), DD_OK, "GetVerticalBlankStatus");
    CHECK(blank == TRUE || blank == FALSE, "in or out of the blank");
    /* The raster: a wait for the begin of the blank ends inside it, a wait
       for its end outside. */
    CHECK_HR(IDirectDraw_WaitForVerticalBlank(dd, DDWAITVB_BLOCKBEGIN, NULL), DD_OK, "WaitForVerticalBlank for the begin");
    CHECK_HR(IDirectDraw_WaitForVerticalBlank(dd, DDWAITVB_BLOCKEND, NULL), DD_OK, "WaitForVerticalBlank for the end");
    IDirectDraw_GetVerticalBlankStatus(dd, &blank);
    CHECK(blank == FALSE, "the blank is over");
    {
        const DWORD before = GetTickCount();
        int i;
        for (i = 0; i < 6; ++i) IDirectDraw_WaitForVerticalBlank(dd, DDWAITVB_BLOCKEND, NULL);
        value = GetTickCount() - before;
        CHECK(value >= 60 && value < 400, "six ends of the blank take about a tenth of a second");
    }
    CHECK_HR(IDirectDraw_WaitForVerticalBlank(dd, 0x40, NULL), DDERR_INVALIDPARAMS, "WaitForVerticalBlank with no known flag");
    {
        const HRESULT hr = IDirectDraw_GetScanLine(dd, &value);
        CHECK((hr == DD_OK && value < 600) || hr == DDERR_VERTICALBLANKINPROGRESS, "GetScanLine");
    }
    memset(&id, 0, sizeof id);
    CHECK_HR(IDirectDraw4_GetDeviceIdentifier(dd4, &id, 0), DD_OK, "GetDeviceIdentifier");
    CHECK(id.szDescription[0], "the device describes itself");
    snprintf(info, sizeof info, "driver=%.40s description=%.60s", id.szDriver, id.szDescription);
    CHECK_HR(IDirectDraw4_TestCooperativeLevel(dd4), DD_OK, "TestCooperativeLevel in a window");
    CHECK_HR(IDirectDraw_Initialize(dd, NULL), DDERR_ALREADYINITIALIZED, "Initialize of a created object");
    CHECK_HR(IDirectDraw_Compact(dd), DDERR_NOEXCLUSIVEMODE, "Compact in a window");
    value = 99;
    CHECK_HR(IDirectDraw_GetFourCCCodes(dd, &value, NULL), DD_OK, "GetFourCCCodes");
    CHECK(value == 0 || value == 5, "FourCC codes: none, or DXT1 to DXT5 with a rasterizer");
    CHECK_HR(DirectDrawEnumerateA(CountDevices, &devices), DD_OK, "DirectDrawEnumerate");
    CHECK(devices == 1, "one device, the display");
    IDirectDraw2_Release(dd2);
    IDirectDraw4_Release(dd4);
    CHECK_HR((HRESULT)IDirectDraw7_Release(dd7), 0, "the one reference of IDirectDraw7 is gone");
}

/* --------------------------------------------- the primary surface in a window */
static void Windowed(IDirectDraw *dd)
{
    IDirectDrawSurface *primary = NULL, *second = NULL, *back, *sprite, *video;
    IDirectDrawClipper *clipper = NULL, *got = NULL;
    DDSURFACEDESC d;
    DDPIXELFORMAT format;
    DDBLTFX fx;
    DDCOLORKEY key;
    RECT target, source;
    HWND clip_window = NULL;
    HDC dc = NULL;
    struct { RGNDATAHEADER header; RECT rects[8]; } region;
    DWORD size;
    int x, y, wrong;

    memset(&d, 0, sizeof d);
    d.dwSize = sizeof d;
    d.dwFlags = DDSD_CAPS;
    d.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    CHECK_HR(IDirectDraw_CreateSurface(dd, &d, &primary, NULL), DD_OK, "the primary surface");
    if (!primary) return;
    CHECK_HR(IDirectDraw_CreateSurface(dd, &d, &second, NULL), DDERR_PRIMARYSURFACEALREADYEXISTS, "a second primary surface");
    d.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    d.dwWidth = d.dwHeight = 64;
    CHECK_HR(IDirectDraw_CreateSurface(dd, &d, &second, NULL), DDERR_INVALIDPARAMS, "a primary surface with a size of its own");
    memset(&d, 0, sizeof d);
    d.dwSize = sizeof d;
    CHECK_HR(IDirectDrawSurface_GetSurfaceDesc(primary, &d), DD_OK, "GetSurfaceDesc of the primary surface");
    CHECK(d.dwWidth == 800 && d.dwHeight == 600 && d.lPitch == 3200, "the primary surface is the screen");
    CHECK((d.ddsCaps.dwCaps & (DDSCAPS_PRIMARYSURFACE | DDSCAPS_VISIBLE | DDSCAPS_VIDEOMEMORY)) ==
          (DDSCAPS_PRIMARYSURFACE | DDSCAPS_VISIBLE | DDSCAPS_VIDEOMEMORY), "caps of the primary surface");
    memset(&format, 0, sizeof format);
    format.dwSize = sizeof format;
    CHECK_HR(IDirectDrawSurface_GetPixelFormat(primary, &format), DD_OK, "GetPixelFormat");
    CHECK((format.dwFlags & DDPF_RGB) && format.dwRGBBitCount == 32 && format.dwGBitMask == 0xff00, "the format of the screen");
    format.dwSize = 8;
    CHECK_HR(IDirectDrawSurface_GetPixelFormat(primary, &format), DDERR_INVALIDPARAMS, "GetPixelFormat with a wrong size");

    /* The clipper names the window; its clip list is the client area. */
    CHECK_HR(IDirectDrawSurface_GetClipper(primary, &got), DDERR_NOCLIPPERATTACHED, "GetClipper without one");
    CHECK_HR(IDirectDraw_CreateClipper(dd, 0, &clipper, NULL), DD_OK, "CreateClipper");
    if (!clipper) return;
    size = sizeof region;
    CHECK_HR(IDirectDrawClipper_GetClipList(clipper, NULL, (LPRGNDATA)&region, &size), DDERR_NOCLIPLIST, "a clipper without a list");
    CHECK_HR(IDirectDrawClipper_SetHWnd(clipper, 0, window), DD_OK, "SetHWnd");
    CHECK_HR(IDirectDrawClipper_GetHWnd(clipper, &clip_window), DD_OK, "GetHWnd");
    CHECK(clip_window == window, "the window of the clipper");
    CHECK_HR(IDirectDrawSurface_SetClipper(primary, clipper), DD_OK, "SetClipper");
    CHECK_HR(IDirectDrawSurface_GetClipper(primary, &got), DD_OK, "GetClipper");
    CHECK(got == clipper, "the clipper that was set");
    if (got) IDirectDrawClipper_Release(got);
    size = 0;
    CHECK_HR(IDirectDrawClipper_GetClipList(clipper, NULL, NULL, &size), DD_OK, "the size of the clip list");
    CHECK(size == sizeof(RGNDATAHEADER) + sizeof(RECT), "one rectangle");
    size = sizeof region;
    memset(&region, 0, sizeof region);
    CHECK_HR(IDirectDrawClipper_GetClipList(clipper, NULL, (LPRGNDATA)&region, &size), DD_OK, "GetClipList");
    CHECK(region.header.nCount == 1 && region.rects[0].left == WindowX && region.rects[0].top == WindowY &&
          region.rects[0].right == WindowX + WindowWidth && region.rects[0].bottom == WindowY + WindowHeight,
          "the clip list is the client area on the screen");

    /* A back surface in system memory, stretched into the window. */
    back = MakeSurface(dd, DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY, 64, 48);
    CHECK(back != NULL, "an off-screen surface in system memory");
    if (!back) return;
    memset(&d, 0, sizeof d);
    d.dwSize = sizeof d;
    IDirectDrawSurface_GetSurfaceDesc(back, &d);
    CHECK((d.ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY) && !(d.ddsCaps.dwCaps & DDSCAPS_VIDEOMEMORY) && d.dwWidth == 64 &&
          d.ddpfPixelFormat.dwRGBBitCount == 32, "it has the format of the screen");
    FillPattern(back, 64, 48);
    /* The whole screen is the target: the clipper leaves the window of it. */
    SetRect(&target, 0, 0, 800, 600);
    {
        /* What stands on the screen outside the window must stay. */
        DWORD before[4], after[4];
        const int probes[4][2] = {{WindowX - 3, WindowY + 10}, {WindowX + WindowWidth + 2, WindowY + 10}, {50, 500}, {700, 30}};
        int i;
        memset(&d, 0, sizeof d);
        d.dwSize = sizeof d;
        CHECK_HR(IDirectDrawSurface_Lock(primary, NULL, &d, DDLOCK_WAIT | DDLOCK_READONLY, NULL), DD_OK, "Lock of the primary surface");
        for (i = 0; i < 4; ++i) before[i] = Pixel32(&d, probes[i][0], probes[i][1]);
        CHECK_HR(IDirectDrawSurface_Blt(primary, &target, back, NULL, DDBLT_WAIT, NULL), DDERR_SURFACEBUSY, "Blt into a locked surface");
        CHECK_HR(IDirectDrawSurface_Unlock(primary, NULL), DD_OK, "Unlock");
        CHECK_HR(IDirectDrawSurface_Unlock(primary, NULL), DDERR_NOTLOCKED, "a second Unlock");
        CHECK_HR(IDirectDrawSurface_Blt(primary, &target, back, NULL, DDBLT_WAIT, NULL), DD_OK, "Blt to the whole screen through the clipper");
        memset(&d, 0, sizeof d);
        d.dwSize = sizeof d;
        IDirectDrawSurface_Lock(primary, NULL, &d, DDLOCK_WAIT | DDLOCK_READONLY, NULL);
        for (i = 0; i < 4; ++i) after[i] = Pixel32(&d, probes[i][0], probes[i][1]);
        CHECK(!memcmp(before, after, sizeof before), "nothing outside the window changed");
        /* Inside: column x of the screen shows column x * 64 / 800 of the source. */
        wrong = 0;
        for (y = WindowY; y < WindowY + WindowHeight; y += 7)
            for (x = WindowX; x < WindowX + WindowWidth; x += 5)
                if (Pixel32(&d, x, y) != Pattern(x * 64 / 800, y * 48 / 600)) ++wrong;
        CHECK(wrong == 0, "the window shows its part of the stretched picture");
        IDirectDrawSurface_Unlock(primary, NULL);
    }
    /* The picture a game presents: the back surface onto the client area. */
    SetRect(&target, WindowX, WindowY, WindowX + WindowWidth, WindowY + WindowHeight);
    CHECK_HR(IDirectDrawSurface_Blt(primary, &target, back, NULL, DDBLT_WAIT, NULL), DD_OK, "Blt onto the client area");
    memset(&d, 0, sizeof d);
    d.dwSize = sizeof d;
    IDirectDrawSurface_Lock(primary, NULL, &d, DDLOCK_WAIT | DDLOCK_READONLY, NULL);
    wrong = 0;
    for (y = 0; y < WindowHeight; y += 3)
        for (x = 0; x < WindowWidth; x += 3)
            if (Pixel32(&d, WindowX + x, WindowY + y) != Pattern(x * 64 / WindowWidth, y * 48 / WindowHeight)) ++wrong;
    CHECK(wrong == 0, "five times stretched, pixel for pixel");
    IDirectDrawSurface_Unlock(primary, NULL);
    Shot("window");

    /* Fill, colour key and mirror between surfaces in system memory. */
    sprite = MakeSurface(dd, DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY, 16, 16);
    CHECK(sprite != NULL, "a second off-screen surface");
    if (!sprite) return;
    memset(&fx, 0, sizeof fx);
    fx.dwSize = sizeof fx;
    fx.dwFillColor = 0x00ff00ff;
    CHECK_HR(IDirectDrawSurface_Blt(sprite, NULL, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &fx), DD_OK, "colour fill");
    SetRect(&target, 4, 4, 12, 12);
    fx.dwFillColor = 0x00102030;
    CHECK_HR(IDirectDrawSurface_Blt(sprite, &target, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &fx), DD_OK, "colour fill of a rectangle");
    SetRect(&target, 4, 4, 20, 12);
    CHECK_HR(IDirectDrawSurface_Blt(sprite, &target, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &fx), DDERR_INVALIDRECT,
             "a rectangle beyond the surface without a clipper");
    key.dwColorSpaceLowValue = key.dwColorSpaceHighValue = 0x00ff00ff;
    CHECK_HR(IDirectDrawSurface_BltFast(back, 8, 8, sprite, NULL, DDBLTFAST_SRCCOLORKEY | DDBLTFAST_WAIT), DDERR_INVALIDPARAMS,
             "a keyed BltFast without a key");
    SetRect(&target, 8, 8, 24, 24);
    CHECK_HR(IDirectDrawSurface_Blt(back, &target, sprite, NULL, DDBLT_KEYSRC | DDBLT_WAIT, NULL), DDERR_INVALIDPARAMS,
             "a keyed Blt without a key");
    CHECK_HR(IDirectDrawSurface_Blt(back, &target, sprite, NULL, DDBLT_KEYDEST | DDBLT_WAIT, NULL), DDERR_INVALIDPARAMS,
             "a Blt through a destination key without a key");
    CHECK_HR(IDirectDrawSurface_SetColorKey(sprite, DDCKEY_SRCBLT, &key), DD_OK, "SetColorKey");
    memset(&key, 0, sizeof key);
    CHECK_HR(IDirectDrawSurface_GetColorKey(sprite, DDCKEY_SRCBLT, &key), DD_OK, "GetColorKey");
    CHECK(key.dwColorSpaceLowValue == 0x00ff00ff, "the key that was set");
    CHECK_HR(IDirectDrawSurface_GetColorKey(sprite, DDCKEY_DESTBLT, &key), DDERR_NOCOLORKEY, "a key that was not set");
    CHECK_HR(IDirectDrawSurface_BltFast(back, 8, 8, sprite, NULL, DDBLTFAST_SRCCOLORKEY | DDBLTFAST_WAIT), DD_OK, "BltFast with a source key");
    CHECK_HR(IDirectDrawSurface_BltFast(back, 56, 40, sprite, NULL, DDBLTFAST_WAIT), DDERR_INVALIDRECT, "BltFast beyond the surface");
    memset(&d, 0, sizeof d);
    d.dwSize = sizeof d;
    IDirectDrawSurface_Lock(back, NULL, &d, DDLOCK_WAIT, NULL);
    CHECK(Pixel32(&d, 9, 9) == Pattern(9, 9) && Pixel32(&d, 8 + 15, 8 + 15) == Pattern(23, 23), "the keyed pixels stayed out");
    CHECK(Pixel32(&d, 12, 12) == 0x102030 && Pixel32(&d, 19, 19) == 0x102030 && Pixel32(&d, 20, 20) == Pattern(20, 20),
          "the others came over");
    IDirectDrawSurface_Unlock(back, NULL);
    /* Mirrored and stretched: 16x16 onto 32x32 at 32,8. */
    SetRect(&source, 0, 0, 8, 16);
    SetRect(&target, 32, 8, 48, 40);
    memset(&fx, 0, sizeof fx);
    fx.dwSize = sizeof fx;
    fx.dwDDFX = DDBLTFX_MIRRORLEFTRIGHT;
    CHECK_HR(IDirectDrawSurface_Blt(back, &target, sprite, &source, DDBLT_DDFX | DDBLT_WAIT, &fx), DD_OK, "a mirrored, stretched Blt");
    memset(&d, 0, sizeof d);
    d.dwSize = sizeof d;
    IDirectDrawSurface_Lock(back, NULL, &d, DDLOCK_WAIT, NULL);
    /* Source columns 0..7 mirrored: the inner square (columns 4..7) lies left. */
    CHECK(Pixel32(&d, 32, 20) == 0x102030 && Pixel32(&d, 39, 20) == 0x102030 && Pixel32(&d, 40, 20) == 0xff00ff &&
          Pixel32(&d, 47, 20) == 0xff00ff && Pixel32(&d, 36, 9) == 0xff00ff, "left and right are exchanged");
    IDirectDrawSurface_Unlock(back, NULL);
    /* A destination key: only the magenta of the sprite takes the fill. */
    key.dwColorSpaceLowValue = key.dwColorSpaceHighValue = 0x00ff00ff;
    memset(&fx, 0, sizeof fx);
    fx.dwSize = sizeof fx;
    fx.dwFillColor = 0x00000080;
    fx.ddckDestColorkey = key;
    CHECK_HR(IDirectDrawSurface_Blt(sprite, NULL, NULL, NULL, DDBLT_COLORFILL | DDBLT_KEYDESTOVERRIDE | DDBLT_WAIT, &fx), DD_OK,
             "a fill through a destination key");
    memset(&d, 0, sizeof d);
    d.dwSize = sizeof d;
    IDirectDrawSurface_Lock(sprite, NULL, &d, DDLOCK_WAIT, NULL);
    CHECK(Pixel32(&d, 0, 0) == 0x80 && Pixel32(&d, 15, 15) == 0x80 && Pixel32(&d, 8, 8) == 0x102030, "only the keyed pixels are filled");
    IDirectDrawSurface_Unlock(sprite, NULL);
    /* A copy inside one surface, overlapping. */
    SetRect(&source, 0, 0, 12, 12);
    CHECK_HR(IDirectDrawSurface_BltFast(sprite, 4, 4, sprite, &source, DDBLTFAST_WAIT), DD_OK, "an overlapping copy inside one surface");
    memset(&d, 0, sizeof d);
    d.dwSize = sizeof d;
    IDirectDrawSurface_Lock(sprite, NULL, &d, DDLOCK_WAIT, NULL);
    CHECK(Pixel32(&d, 4, 4) == 0x80 && Pixel32(&d, 8, 8) == 0x102030 && Pixel32(&d, 15, 15) == 0x102030 && Pixel32(&d, 7, 7) == 0x80,
          "it reads the pixels as they were");
    IDirectDrawSurface_Unlock(sprite, NULL);
    memset(&fx, 0, sizeof fx);
    fx.dwSize = sizeof fx;
    SetRect(&target, 48, 0, 64, 16);
    CHECK_HR(IDirectDrawSurface_Blt(back, &target, sprite, NULL, DDBLT_ALPHASRC | DDBLT_WAIT, &fx), DD_OK, "the alpha flags are passed over");
    fx.dwRotationAngle = 9000;
    CHECK_HR(IDirectDrawSurface_Blt(back, &target, sprite, NULL, DDBLT_ROTATIONANGLE | DDBLT_WAIT, &fx), DDERR_NOROTATIONHW, "a rotated blit");
    CHECK_HR(IDirectDrawSurface_Blt(back, &target, sprite, NULL, DDBLT_ZBUFFER | DDBLT_WAIT, &fx), DD_OK, "the Z flags are passed over");
    fx.dwDDROP = 1;
    CHECK_HR(IDirectDrawSurface_Blt(back, &target, sprite, NULL, DDBLT_DDROPS | DDBLT_WAIT, &fx), DDERR_NODDROPSHW, "a DirectDraw raster operation");
    fx.dwROP = 0x00550009;   /* DSTINVERT */
    CHECK_HR(IDirectDrawSurface_Blt(back, &target, sprite, NULL, DDBLT_ROP | DDBLT_WAIT, &fx), DDERR_NORASTEROPHW, "a raster operation");
    memset(&d, 0, sizeof d);
    CHECK_HR(IDirectDrawSurface_Lock(back, NULL, &d, DDLOCK_WAIT, NULL), DDERR_INVALIDPARAMS, "Lock with a description without its size");
    {
        DDSURFACEDESC2 wide;
        memset(&wide, 0, sizeof wide);
        wide.dwSize = sizeof wide;
        CHECK_HR(IDirectDrawSurface_GetSurfaceDesc(back, (LPDDSURFACEDESC)&wide), DDERR_INVALIDPARAMS,
                 "the long description through the first interface");
    }

    /* GDI on a surface in system memory: the context draws on its pixels. */
    CHECK_HR(IDirectDrawSurface_GetDC(back, &dc), DD_OK, "GetDC of a surface in system memory");
    if (dc)
    {
        HBRUSH brush = CreateSolidBrush(RGB(10, 200, 30));
        RECT r = {2, 30, 30, 46};
        HDC other = NULL;
        FillRect(dc, &r, brush);
        DeleteObject(brush);
        SetPixel(dc, 60, 2, RGB(1, 2, 3));
        SetBkColor(dc, RGB(0, 0, 0));
        SetTextColor(dc, RGB(255, 255, 255));
        TextOutA(dc, 2, 30, "Dx", 2);
        CHECK_HR(IDirectDrawSurface_GetDC(back, &other), DDERR_DCALREADYCREATED, "a second GetDC");
        memset(&d, 0, sizeof d);
        d.dwSize = sizeof d;
        CHECK_HR(IDirectDrawSurface_Lock(back, NULL, &d, DDLOCK_WAIT, NULL), DDERR_SURFACEBUSY, "Lock while a context is out");
        CHECK_HR(IDirectDrawSurface_ReleaseDC(back, dc), DD_OK, "ReleaseDC");
        CHECK_HR(IDirectDrawSurface_ReleaseDC(back, dc), DDERR_NODC, "a second ReleaseDC");
        memset(&d, 0, sizeof d);
        d.dwSize = sizeof d;
        IDirectDrawSurface_Lock(back, NULL, &d, DDLOCK_WAIT, NULL);
        CHECK(Pixel32(&d, 28, 44) == 0x0ac81e && Pixel32(&d, 60, 2) == 0x010203, "what GDI drew is in the surface");
        wrong = 0;
        for (y = 30; y < 46; ++y)
            for (x = 2; x < 20; ++x)
                if (Pixel32(&d, x, y) == 0xffffff) ++wrong;
        CHECK(wrong > 8, "the text has white pixels");
        IDirectDrawSurface_Unlock(back, NULL);
    }
    /* GDI on the primary surface draws on the screen. */
    dc = NULL;
    CHECK_HR(IDirectDrawSurface_GetDC(primary, &dc), DD_OK, "GetDC of the primary surface");
    if (dc)
    {
        HBRUSH brush = CreateSolidBrush(RGB(250, 240, 20));
        RECT r = {WindowX + 10, WindowY + 10, WindowX + 60, WindowY + 40};
        FillRect(dc, &r, brush);
        DeleteObject(brush);
        CHECK_HR(IDirectDrawSurface_ReleaseDC(primary, dc), DD_OK, "ReleaseDC of the primary surface");
        memset(&d, 0, sizeof d);
        d.dwSize = sizeof d;
        IDirectDrawSurface_Lock(primary, NULL, &d, DDLOCK_WAIT | DDLOCK_READONLY, NULL);
        CHECK(Pixel32(&d, WindowX + 30, WindowY + 20) == 0xfaf014, "the rectangle of GDI is on the screen");
        IDirectDrawSurface_Unlock(primary, NULL);
    }
    Shot("gdi");

    /* A surface in video memory: a surface of the driver. */
    video = MakeSurface(dd, DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY, 64, 48);
    CHECK(video != NULL, "an off-screen surface in video memory");
    if (video)
    {
        DWORD total = 0, free_before = 0, free_after = 0;
        IDirectDraw2 *dd2 = NULL;
        DDSCAPS scaps = {DDSCAPS_OFFSCREENPLAIN};
        memset(&d, 0, sizeof d);
        d.dwSize = sizeof d;
        IDirectDrawSurface_GetSurfaceDesc(video, &d);
        CHECK((d.ddsCaps.dwCaps & DDSCAPS_VIDEOMEMORY) && !(d.ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY) && d.lPitch >= 256,
              "it says it lies in video memory");
        CHECK_HR(IDirectDrawSurface_Blt(video, NULL, back, NULL, DDBLT_WAIT, NULL), DD_OK, "system memory to video memory");
        memset(&fx, 0, sizeof fx);
        fx.dwSize = sizeof fx;
        fx.dwFillColor = 0x00334455;
        SetRect(&target, 0, 0, 8, 8);
        CHECK_HR(IDirectDrawSurface_Blt(video, &target, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &fx), DD_OK, "a fill in video memory");
        memset(&d, 0, sizeof d);
        d.dwSize = sizeof d;
        CHECK_HR(IDirectDrawSurface_Lock(video, NULL, &d, DDLOCK_WAIT, NULL), DD_OK, "Lock of video memory");
        CHECK(d.lpSurface != NULL && Pixel32(&d, 3, 3) == 0x334455 && Pixel32(&d, 50, 20) == Pattern(50, 20), "its pixels");
        Put32(&d, 63, 47, 0x00abcdef);
        IDirectDrawSurface_Unlock(video, NULL);
        CHECK_HR(IDirectDrawSurface_BltFast(sprite, 0, 0, video, NULL, DDBLTFAST_WAIT), DDERR_INVALIDRECT, "a source larger than the target");
        SetRect(&source, 48, 32, 64, 48);
        CHECK_HR(IDirectDrawSurface_BltFast(sprite, 0, 0, video, &source, DDBLTFAST_WAIT), DD_OK, "video memory to system memory");
        memset(&d, 0, sizeof d);
        d.dwSize = sizeof d;
        IDirectDrawSurface_Lock(sprite, NULL, &d, DDLOCK_WAIT, NULL);
        CHECK(Pixel32(&d, 15, 15) == 0xabcdef && Pixel32(&d, 2, 4) == Pattern(50, 36), "what came back");
        IDirectDrawSurface_Unlock(sprite, NULL);
        dc = NULL;
        CHECK_HR(IDirectDrawSurface_GetDC(video, &dc), DD_OK, "GetDC of video memory");
        if (dc)
        {
            SetPixel(dc, 20, 20, RGB(9, 8, 7));
            IDirectDrawSurface_ReleaseDC(video, dc);
            memset(&d, 0, sizeof d);
            d.dwSize = sizeof d;
            IDirectDrawSurface_Lock(video, NULL, &d, DDLOCK_WAIT, NULL);
            CHECK(Pixel32(&d, 20, 20) == 0x090807, "GDI drew into video memory");
            IDirectDrawSurface_Unlock(video, NULL);
        }
        CHECK_HR(IDirectDrawSurface_IsLost(video), DD_OK, "IsLost");
        CHECK_HR(IDirectDrawSurface_Restore(video), DD_OK, "Restore of a surface that is not lost");
        IDirectDraw_QueryInterface(dd, &IID_IDirectDraw2, (void **)&dd2);
        if (dd2)
        {
            IDirectDraw2_GetAvailableVidMem(dd2, &scaps, &total, &free_before);
            IDirectDrawSurface_Release(video);
            IDirectDraw2_GetAvailableVidMem(dd2, &scaps, &total, &free_after);
            CHECK(free_after > free_before, "a released surface gives its video memory back");
            IDirectDraw2_Release(dd2);
        }
    }
    CHECK_HR(IDirectDrawSurface_IsLost(primary), DD_OK, "the primary surface is not lost");
    CHECK_HR(IDirectDrawSurface_SetClipper(primary, NULL), DD_OK, "SetClipper(NULL)");
    CHECK(IDirectDrawClipper_Release(clipper) == 0, "the clipper goes with its last reference");
    CHECK(IDirectDrawSurface_Release(sprite) == 0 && IDirectDrawSurface_Release(back) == 0, "the surfaces go");
    CHECK(IDirectDrawSurface_Release(primary) == 0, "the primary surface goes");
}

/* What a blit costs: microseconds for one, over a hundred each, between two
   640 x 480 surfaces in system memory (the emulation layer), and for a Lock
   and Unlock in system memory and in video memory (a call into the driver). */
static void Timing(IDirectDraw *dd)
{
    IDirectDrawSurface *small = MakeSurface(dd, DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY, 320, 240);
    IDirectDrawSurface *big = MakeSurface(dd, DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY, 640, 480);
    IDirectDrawSurface *other = MakeSurface(dd, DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY, 640, 480);
    IDirectDrawSurface *video = MakeSurface(dd, DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY, 64, 64);
    LARGE_INTEGER frequency, begin, end;
    DDSURFACEDESC d;
    DDBLTFX fx;
    long stretch = 0, copy = 0, fill = 0, lock = 0, video_lock = 0;
    int i;
    if (small && big && other && QueryPerformanceFrequency(&frequency) && frequency.QuadPart)
    {
#define TIMED(result, statement) \
        QueryPerformanceCounter(&begin); \
        for (i = 0; i < 100; ++i) { statement; } \
        QueryPerformanceCounter(&end); \
        result = (long)((end.QuadPart - begin.QuadPart) * 10000 / frequency.QuadPart)   /* microseconds for one of the hundred */
        FillPattern(small, 320, 240);
        memset(&fx, 0, sizeof fx);
        fx.dwSize = sizeof fx;
        fx.dwFillColor = 0x123456;
        TIMED(stretch, IDirectDrawSurface_Blt(big, NULL, small, NULL, DDBLT_WAIT, NULL));
        TIMED(copy, IDirectDrawSurface_Blt(other, NULL, big, NULL, DDBLT_WAIT, NULL));
        TIMED(fill, IDirectDrawSurface_Blt(other, NULL, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &fx));
        memset(&d, 0, sizeof d);
        d.dwSize = sizeof d;
        TIMED(lock, IDirectDrawSurface_Lock(big, NULL, &d, DDLOCK_WAIT, NULL); IDirectDrawSurface_Unlock(big, NULL));
        if (video) { TIMED(video_lock, IDirectDrawSurface_Lock(video, NULL, &d, DDLOCK_WAIT, NULL); IDirectDrawSurface_Unlock(video, NULL)); }
#undef TIMED
        snprintf(timing, sizeof timing, "stretch_us=%ld copy_us=%ld fill_us=%ld lock_us=%ld video_lock_us=%ld", stretch, copy, fill, lock,
                 video_lock);
    }
    if (video) IDirectDrawSurface_Release(video);
    if (other) IDirectDrawSurface_Release(other);
    if (big) IDirectDrawSurface_Release(big);
    if (small) IDirectDrawSurface_Release(small);
}

/* ------------------------------------------------- formats, palettes, chains */
static void Formats(IDirectDraw *dd)
{
    IDirectDraw7 *dd7 = NULL;
    IDirectDrawSurface *high = NULL, *other = NULL, *indexed = NULL, *z = NULL, *got = NULL;
    IDirectDrawSurface7 *texture = NULL, *level = NULL, *next = NULL;
    IDirectDrawSurface3 *third = NULL;
    IDirectDrawPalette *palette = NULL, *held = NULL;
    PALETTEENTRY colors[256], read[4];
    DDSURFACEDESC d;
    DDSURFACEDESC2 d2;
    DDBLTFX fx;
    DDSCAPS caps;
    DDSCAPS2 caps2;
    HDC dc = NULL;
    static const GUID tag = {0x0badcafe, 1, 2, {1, 2, 3, 4, 5, 6, 7, 8}};
    DWORD value, size;
    int i, count = 0;

    /* A surface with a format of its own: 5-6-5. */
    memset(&d, 0, sizeof d);
    d.dwSize = sizeof d;
    d.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    d.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
    d.dwWidth = 20;
    d.dwHeight = 10;
    d.ddpfPixelFormat.dwSize = sizeof d.ddpfPixelFormat;
    d.ddpfPixelFormat.dwFlags = DDPF_RGB;
    d.ddpfPixelFormat.dwRGBBitCount = 16;
    d.ddpfPixelFormat.dwRBitMask = 0xf800;
    d.ddpfPixelFormat.dwGBitMask = 0x07e0;
    d.ddpfPixelFormat.dwBBitMask = 0x001f;
    CHECK_HR(IDirectDraw_CreateSurface(dd, &d, &high, NULL), DD_OK, "a 5-6-5 surface");
    CHECK_HR(IDirectDraw_CreateSurface(dd, &d, &other, NULL), DD_OK, "a second 5-6-5 surface");
    if (high && other)
    {
        WORD *row;
        memset(&d, 0, sizeof d);
        d.dwSize = sizeof d;
        CHECK_HR(IDirectDrawSurface_Lock(high, NULL, &d, DDLOCK_WAIT, NULL), DD_OK, "Lock of the 5-6-5 surface");
        CHECK(d.ddpfPixelFormat.dwRBitMask == 0xf800 && d.lPitch >= 40, "its description");
        for (i = 0; i < 20; ++i)
        {
            row = (WORD *)((BYTE *)d.lpSurface + 3 * d.lPitch);
            row[i] = (WORD)(0x1000 + i);
        }
        IDirectDrawSurface_Unlock(high, NULL);
        CHECK_HR(IDirectDrawSurface_Blt(other, NULL, high, NULL, DDBLT_WAIT, NULL), DD_OK, "Blt between two 5-6-5 surfaces");
        memset(&d, 0, sizeof d);
        d.dwSize = sizeof d;
        IDirectDrawSurface_Lock(other, NULL, &d, DDLOCK_WAIT, NULL);
        row = (WORD *)((BYTE *)d.lpSurface + 3 * d.lPitch);
        CHECK(row[0] == 0x1000 && row[19] == 0x1013, "sixteen bit pixels come over whole");
        IDirectDrawSurface_Unlock(other, NULL);
        CHECK_HR(IDirectDrawSurface_GetDC(high, &dc), DD_OK, "GetDC of a 5-6-5 surface");
        if (dc)
        {
            CHECK(SetPixel(dc, 0, 0, RGB(255, 0, 0)) == RGB(255, 0, 0), "5-6-5 red through GDI");
            CHECK(SetPixel(dc, 1, 0, RGB(0, 255, 0)) == RGB(0, 255, 0), "5-6-5 green through GDI");
            CHECK(GetPixel(dc, 1, 0) == RGB(0, 255, 0), "5-6-5 GetPixel decodes green");
            IDirectDrawSurface_ReleaseDC(high, dc);
            memset(&d, 0, sizeof d); d.dwSize = sizeof d;
            CHECK_HR(IDirectDrawSurface_Lock(high, NULL, &d, DDLOCK_WAIT, NULL), DD_OK, "Lock after 5-6-5 GDI drawing");
            row = (WORD *)d.lpSurface;
            CHECK(row[0] == 0xf800 && row[1] == 0x07e0, "GDI writes the surface's bitfields");
            row = (WORD *)((BYTE *)d.lpSurface + 3 * d.lPitch);
            CHECK(row[0] == 0x1000 && row[19] == 0x1013, "GetDC preserves untouched 5-6-5 pixels");
            IDirectDrawSurface_Unlock(high, NULL);
        }
        IDirectDrawSurface_Release(other);
        other = MakeSurface(dd, DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY, 20, 10);
        if (other)
        {
            CHECK_HR(IDirectDrawSurface_Blt(other, NULL, high, NULL, DDBLT_WAIT, NULL), DDERR_UNSUPPORTED,
                     "Blt between pixels of different width");
            IDirectDrawSurface_Release(other);
        }
    }
    if (high) IDirectDrawSurface_Release(high);
    d.ddpfPixelFormat.dwFlags = DDPF_FOURCC;
    d.ddpfPixelFormat.dwFourCC = MAKEFOURCC('D', 'X', 'T', '1');
    high = NULL;
    CHECK_HR(IDirectDraw_CreateSurface(dd, &d, &high, NULL), DDERR_INVALIDPARAMS, "a DXT1 surface in system memory");
    if (high) IDirectDrawSurface_Release(high);
    {
        /* The first IDirectDraw takes the short description only. */
        DDSURFACEDESC2 wide;
        memset(&wide, 0, sizeof wide);
        wide.dwSize = sizeof wide;
        wide.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
        wide.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
        wide.dwWidth = wide.dwHeight = 8;
        high = NULL;
        CHECK_HR(IDirectDraw_CreateSurface(dd, (LPDDSURFACEDESC)&wide, &high, NULL), DDERR_INVALIDPARAMS,
                 "the long description through the first IDirectDraw");
        if (high) IDirectDrawSurface_Release(high);
    }

    /* Eight bits with a palette. */
    memset(&d, 0, sizeof d);
    d.dwSize = sizeof d;
    d.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    d.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
    d.dwWidth = 32;
    d.dwHeight = 16;
    d.ddpfPixelFormat.dwSize = sizeof d.ddpfPixelFormat;
    d.ddpfPixelFormat.dwFlags = DDPF_RGB | DDPF_PALETTEINDEXED8;
    d.ddpfPixelFormat.dwRGBBitCount = 8;
    CHECK_HR(IDirectDraw_CreateSurface(dd, &d, &indexed, NULL), DD_OK, "an eight bit surface");
    for (i = 0; i < 256; ++i)
    {
        colors[i].peRed = (BYTE)i;
        colors[i].peGreen = (BYTE)(255 - i);
        colors[i].peBlue = (BYTE)(i / 2);
        colors[i].peFlags = 0;
    }
    CHECK_HR(IDirectDraw_CreatePalette(dd, DDPCAPS_4BIT, colors, &palette, NULL), DD_OK, "a sixteen colour palette");
    if (palette)
    {
        CHECK_HR(IDirectDrawPalette_GetEntries(palette, 0, 15, 2, read), DDERR_INVALIDPARAMS, "it has sixteen entries");
        CHECK(IDirectDrawPalette_Release(palette) == 0, "it goes");
        palette = NULL;
    }
    CHECK_HR(IDirectDraw_CreatePalette(dd, DDPCAPS_8BIT | DDPCAPS_ALLOW256, colors, &palette, NULL), DD_OK, "CreatePalette");
    if (indexed && palette)
    {
        CHECK_HR(IDirectDrawSurface_GetPalette(indexed, &held), DDERR_NOPALETTEATTACHED, "GetPalette without one");
        CHECK_HR(IDirectDrawSurface_SetPalette(indexed, palette), DD_OK, "SetPalette");
        CHECK_HR(IDirectDrawSurface_GetPalette(indexed, &held), DD_OK, "GetPalette");
        CHECK(held == palette, "the palette that was set");
        if (held) IDirectDrawPalette_Release(held);
        colors[7].peRed = 200; colors[7].peGreen = 10; colors[7].peBlue = 20;
        CHECK_HR(IDirectDrawPalette_SetEntries(palette, 0, 7, 1, &colors[7]), DD_OK, "SetEntries");
        CHECK_HR(IDirectDrawPalette_GetEntries(palette, 0, 6, 3, read), DD_OK, "GetEntries");
        CHECK(read[1].peRed == 200 && read[1].peGreen == 10 && read[0].peRed == 6 && read[2].peRed == 8, "the entries");
        CHECK_HR(IDirectDrawPalette_GetEntries(palette, 0, 250, 10, read), DDERR_INVALIDPARAMS, "entries beyond the palette");
        value = 0;
        CHECK_HR(IDirectDrawPalette_GetCaps(palette, &value), DD_OK, "caps of the palette");
        CHECK((value & DDPCAPS_8BIT) && (value & DDPCAPS_ALLOW256) && !(value & DDPCAPS_PRIMARYSURFACE), "eight bits, all 256 its own");
        /* GDI on it finds the colours of the palette. */
        dc = NULL;
        CHECK_HR(IDirectDrawSurface_GetDC(indexed, &dc), DD_OK, "GetDC of an eight bit surface");
        if (dc)
        {
            HBRUSH brush = CreateSolidBrush(RGB(200, 10, 20));
            RECT r = {0, 0, 32, 16};
            FillRect(dc, &r, brush);
            DeleteObject(brush);
            IDirectDrawSurface_ReleaseDC(indexed, dc);
            memset(&d, 0, sizeof d);
            d.dwSize = sizeof d;
            IDirectDrawSurface_Lock(indexed, NULL, &d, DDLOCK_WAIT, NULL);
            CHECK(((BYTE *)d.lpSurface)[5 * d.lPitch + 9] == 7, "the brush became the index of its colour");
            IDirectDrawSurface_Unlock(indexed, NULL);
        }
        CHECK_HR(IDirectDrawSurface_SetPalette(indexed, NULL), DD_OK, "SetPalette(NULL)");
    }
    {
        IDirectDrawSurface *plain = MakeSurface(dd, DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY, 8, 8);
        if (plain && palette)
            CHECK_HR(IDirectDrawSurface_SetPalette(plain, palette), DDERR_INVALIDPIXELFORMAT, "a palette on a true colour surface");
        if (plain) IDirectDrawSurface_Release(plain);
    }
    if (palette) CHECK(IDirectDrawPalette_Release(palette) == 0, "the palette goes");
    if (indexed) IDirectDrawSurface_Release(indexed);

    /* A Z buffer, attached to the surface it serves. */
    high = MakeSurface(dd, DDSCAPS_OFFSCREENPLAIN | DDSCAPS_3DDEVICE, 64, 64);
    memset(&d, 0, sizeof d);
    d.dwSize = sizeof d;
    d.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_ZBUFFERBITDEPTH;
    d.ddsCaps.dwCaps = DDSCAPS_ZBUFFER;
    d.dwWidth = d.dwHeight = 64;
    d.dwZBufferBitDepth = 16;
    CHECK_HR(IDirectDraw_CreateSurface(dd, &d, &z, NULL), DD_OK, "a Z buffer of sixteen bits");
    CHECK(high != NULL, "a 3D target surface");
    if (high && z)
    {
        memset(&d, 0, sizeof d);
        d.dwSize = sizeof d;
        IDirectDrawSurface_GetSurfaceDesc(z, &d);
        CHECK((d.ddpfPixelFormat.dwFlags & DDPF_ZBUFFER) && d.ddpfPixelFormat.dwZBufferBitDepth == 16 &&
              d.ddpfPixelFormat.dwZBitMask == 0xffff && (d.ddsCaps.dwCaps & DDSCAPS_ZBUFFER), "its description");
        caps.dwCaps = DDSCAPS_ZBUFFER;
        CHECK_HR(IDirectDrawSurface_GetAttachedSurface(high, &caps, &got), DDERR_NOTFOUND, "no Z buffer before it is attached");
        CHECK_HR(IDirectDrawSurface_AddAttachedSurface(high, z), DD_OK, "AddAttachedSurface");
        CHECK_HR(IDirectDrawSurface_AddAttachedSurface(high, z), DDERR_SURFACEALREADYATTACHED, "attached twice");
        CHECK_HR(IDirectDrawSurface_GetAttachedSurface(high, &caps, &got), DD_OK, "GetAttachedSurface");
        CHECK(got == z, "the Z buffer");
        if (got) IDirectDrawSurface_Release(got);
        CHECK_HR(IDirectDrawSurface_EnumAttachedSurfaces(high, &count, CountAttached), DD_OK, "EnumAttachedSurfaces");
        CHECK(count == 1, "one attached surface");
        memset(&fx, 0, sizeof fx);
        fx.dwSize = sizeof fx;
        fx.dwFillDepth = 0xffff;
        CHECK_HR(IDirectDrawSurface_Blt(z, NULL, NULL, NULL, DDBLT_DEPTHFILL | DDBLT_WAIT, &fx), DD_OK, "depth fill");
        CHECK_HR(IDirectDrawSurface_Blt(high, NULL, NULL, NULL, DDBLT_DEPTHFILL | DDBLT_WAIT, &fx), DDERR_INVALIDPARAMS,
                 "depth fill of a colour surface");
        memset(&d, 0, sizeof d);
        d.dwSize = sizeof d;
        IDirectDrawSurface_Lock(z, NULL, &d, DDLOCK_WAIT, NULL);
        CHECK(((WORD *)((BYTE *)d.lpSurface + 9 * d.lPitch))[9] == 0xffff, "the depth is in the buffer");
        IDirectDrawSurface_Unlock(z, NULL);
        CHECK_HR(IDirectDrawSurface_DeleteAttachedSurface(high, 0, z), DD_OK, "DeleteAttachedSurface");
        CHECK_HR(IDirectDrawSurface_DeleteAttachedSurface(high, 0, z), DDERR_SURFACENOTATTACHED, "detached twice");
        CHECK(IDirectDrawSurface_Release(z) == 0, "the Z buffer goes");
    }
    if (high) IDirectDrawSurface_Release(high);

    /* IDirectDraw7: a texture with its mip chain, private data. */
    IDirectDraw_QueryInterface(dd, &IID_IDirectDraw7, (void **)&dd7);
    if (!dd7) return;
    memset(&d2, 0, sizeof d2);
    d2.dwSize = sizeof d2;
    d2.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT | DDSD_MIPMAPCOUNT;
    d2.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_MIPMAP | DDSCAPS_COMPLEX;
    d2.dwWidth = 32;
    d2.dwHeight = 16;
    d2.dwMipMapCount = 4;
    d2.ddpfPixelFormat.dwSize = sizeof d2.ddpfPixelFormat;
    d2.ddpfPixelFormat.dwFlags = DDPF_RGB | DDPF_ALPHAPIXELS;
    d2.ddpfPixelFormat.dwRGBBitCount = 32;
    d2.ddpfPixelFormat.dwRBitMask = 0xff0000;
    d2.ddpfPixelFormat.dwGBitMask = 0xff00;
    d2.ddpfPixelFormat.dwBBitMask = 0xff;
    d2.ddpfPixelFormat.dwRGBAlphaBitMask = 0xff000000u;
    CHECK_HR(IDirectDraw7_CreateSurface(dd7, &d2, &texture, NULL), DD_OK, "a texture with four mip levels");
    if (texture)
    {
        int levels = 1, width = 32, height = 16;
        memset(&d2, 0, sizeof d2);
        d2.dwSize = sizeof d2;
        CHECK_HR(IDirectDrawSurface7_GetSurfaceDesc(texture, &d2), DD_OK, "GetSurfaceDesc with the long description");
        CHECK(d2.dwMipMapCount == 4 && (d2.dwFlags & DDSD_MIPMAPCOUNT) && (d2.ddpfPixelFormat.dwFlags & DDPF_ALPHAPIXELS) &&
              d2.ddpfPixelFormat.dwRGBAlphaBitMask == 0xff000000u, "it describes its levels and its alpha");
        memset(&d, 0, sizeof d);
        d.dwSize = sizeof d;
        CHECK_HR(IDirectDrawSurface7_GetSurfaceDesc(texture, (LPDDSURFACEDESC2)&d), DDERR_INVALIDPARAMS,
                 "the short description through the seventh interface");
        level = texture;
        IDirectDrawSurface7_AddRef(level);
        for (;;)
        {
            memset(&caps2, 0, sizeof caps2);
            caps2.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_MIPMAP;
            next = NULL;
            if (IDirectDrawSurface7_GetAttachedSurface(level, &caps2, &next) != DD_OK) break;
            IDirectDrawSurface7_Release(level);
            level = next;
            ++levels;
            width = width > 1 ? width / 2 : 1;
            height = height > 1 ? height / 2 : 1;
            memset(&d2, 0, sizeof d2);
            d2.dwSize = sizeof d2;
            IDirectDrawSurface7_GetSurfaceDesc(level, &d2);
            CHECK((int)d2.dwWidth == width && (int)d2.dwHeight == height && (d2.ddsCaps.dwCaps2 & DDSCAPS2_MIPMAPSUBLEVEL),
                  "a level half the one above");
        }
        IDirectDrawSurface7_Release(level);
        CHECK(levels == 4, "four levels in the chain");
        value = 0x11223344;
        CHECK_HR(IDirectDrawSurface7_SetPrivateData(texture, &tag, &value, sizeof value, 0), DD_OK, "SetPrivateData");
        value = 0;
        size = 2;
        CHECK_HR(IDirectDrawSurface7_GetPrivateData(texture, &tag, &value, &size), DDERR_MOREDATA, "GetPrivateData with too little room");
        CHECK(size == 4, "the size it needs");
        CHECK_HR(IDirectDrawSurface7_GetPrivateData(texture, &tag, &value, &size), DD_OK, "GetPrivateData");
        CHECK(value == 0x11223344, "the data");
        CHECK_HR(IDirectDrawSurface7_FreePrivateData(texture, &tag), DD_OK, "FreePrivateData");
        CHECK_HR(IDirectDrawSurface7_GetPrivateData(texture, &tag, &value, &size), DDERR_NOTFOUND, "data that is gone");
        value = 0;
        CHECK_HR(IDirectDrawSurface7_GetUniquenessValue(texture, &value), DD_OK, "GetUniquenessValue");
        size = value;
        memset(&d2, 0, sizeof d2);
        d2.dwSize = sizeof d2;
        IDirectDrawSurface7_Lock(texture, NULL, &d2, DDLOCK_WAIT, NULL);
        IDirectDrawSurface7_Unlock(texture, NULL);
        IDirectDrawSurface7_GetUniquenessValue(texture, &value);
        CHECK(value != size && value != 0, "a Lock changes the uniqueness value");
        CHECK_HR(IDirectDrawSurface7_QueryInterface(texture, &IID_IDirectDrawSurface3, (void **)&third), S_OK, "QueryInterface IDirectDrawSurface3");
        if (third)
        {
            IDirectDraw7 *owner = NULL;
            CHECK_HR(IDirectDrawSurface3_GetDDInterface(third, (LPVOID *)&owner), DD_OK, "GetDDInterface");
            CHECK(owner == dd7, "the interface of DirectDraw the surface was made through");
            if (owner) IDirectDraw7_Release(owner);
            IDirectDrawSurface3_Release(third);
        }
        {
            /* Every surface answers for the gamma control; the ramp is the display's. */
            IDirectDrawGammaControl *control = NULL;
            DDGAMMARAMP ramp;
            CHECK_HR(IDirectDrawSurface7_QueryInterface(texture, &IID_IDirectDrawGammaControl, (void **)&control), S_OK,
                     "the gamma control of a surface that is not the screen");
            if (control)
            {
                CHECK_HR(IDirectDrawGammaControl_GetGammaRamp(control, 0, &ramp), DDERR_INVALIDPARAMS, "its ramp");
                IDirectDrawGammaControl_Release(control);
            }
        }
        CHECK(IDirectDrawSurface7_Release(texture) == 0, "the texture goes with its levels");
    }
    IDirectDraw7_Release(dd7);
}

/* ------------------------------------------------------------ exclusive mode */
static void Exclusive(IDirectDraw *dd)
{
    IDirectDrawSurface *primary = NULL, *back = NULL, *lost = NULL, *fail = NULL;
    IDirectDrawGammaControl *gamma = NULL;
    IDirectDrawPalette *palette = NULL;
    IDirectDraw4 *dd4 = NULL;
    PALETTEENTRY colors[256];
    DDSURFACEDESC d;
    DDSCAPS caps;
    DDBLTFX fx;
    DDGAMMARAMP ramp, read;
    WORD *row;
    int x, y, i;

    memset(&d, 0, sizeof d);
    d.dwSize = sizeof d;
    d.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
    d.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP | DDSCAPS_COMPLEX;
    d.dwBackBufferCount = 1;
    CHECK_HR(IDirectDraw_CreateSurface(dd, &d, &fail, NULL), DDERR_NOEXCLUSIVEMODE, "a flipping primary surface in a window");
    CHECK_HR(IDirectDraw_SetDisplayMode(dd, 640, 480, 16), DDERR_NOEXCLUSIVEMODE, "SetDisplayMode in a window");
    CHECK_HR(IDirectDraw_SetCooperativeLevel(dd, window, DDSCL_EXCLUSIVE), DDERR_INVALIDPARAMS, "exclusive without full screen");
    CHECK_HR(IDirectDraw_SetCooperativeLevel(dd, NULL, DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN), DDERR_INVALIDPARAMS, "exclusive without a window");
    CHECK_HR(IDirectDraw_SetCooperativeLevel(dd, window, DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN), DD_OK, "exclusive mode");
    IDirectDraw_QueryInterface(dd, &IID_IDirectDraw4, (void **)&dd4);
    /* A surface of the driver from before the mode change. */
    lost = MakeSurface(dd, DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY, 16, 16);
    CHECK_HR(IDirectDraw_SetDisplayMode(dd, 640, 400, 16), DDERR_INVALIDMODE, "a mode the display does not have");
    CHECK_HR(IDirectDraw_SetDisplayMode(dd, 640, 480, 16), DD_OK, "SetDisplayMode 640x480x16");
    Pump();
    CHECK(GetSystemMetrics(SM_CXSCREEN) == 640 && GetSystemMetrics(SM_CYSCREEN) == 480, "the screen has the size of the mode");
    if (lost)
    {
        CHECK_HR(IDirectDrawSurface_IsLost(lost), DDERR_SURFACELOST, "a surface from before the mode change is lost");
        memset(&d, 0, sizeof d);
        d.dwSize = sizeof d;
        CHECK_HR(IDirectDrawSurface_Lock(lost, NULL, &d, DDLOCK_WAIT, NULL), DDERR_SURFACELOST, "Lock of a lost surface");
        CHECK_HR(IDirectDrawSurface_Restore(lost), DDERR_WRONGMODE, "it does not come back in another mode");
    }
    memset(&d, 0, sizeof d);
    d.dwSize = sizeof d;
    d.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT;
    d.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP | DDSCAPS_COMPLEX;
    d.dwBackBufferCount = 1;
    CHECK_HR(IDirectDraw_CreateSurface(dd, &d, &primary, NULL), DD_OK, "a primary surface with a back buffer");
    if (primary)
    {
        memset(&d, 0, sizeof d);
        d.dwSize = sizeof d;
        IDirectDrawSurface_GetSurfaceDesc(primary, &d);
        CHECK(d.dwWidth == 640 && d.dwHeight == 480 && d.ddpfPixelFormat.dwRGBBitCount == 16 && d.ddpfPixelFormat.dwRBitMask == 0x7c00 &&
              d.dwBackBufferCount == 1 && (d.ddsCaps.dwCaps & DDSCAPS_FRONTBUFFER), "the screen of the mode, 5-5-5");
        caps.dwCaps = DDSCAPS_BACKBUFFER;
        CHECK_HR(IDirectDrawSurface_GetAttachedSurface(primary, &caps, &back), DD_OK, "the back buffer");
    }
    if (primary && back)
    {
        /* Red above, green below into the back buffer; blue into the front. */
        memset(&d, 0, sizeof d);
        d.dwSize = sizeof d;
        CHECK_HR(IDirectDrawSurface_Lock(back, NULL, &d, DDLOCK_WAIT, NULL), DD_OK, "Lock of the back buffer");
        for (y = 0; y < 480; ++y)
        {
            row = (WORD *)((BYTE *)d.lpSurface + (size_t)y * d.lPitch);
            for (x = 0; x < 640; ++x) row[x] = y < 240 ? 0x7c00 : 0x03e0;
        }
        IDirectDrawSurface_Unlock(back, NULL);
        memset(&fx, 0, sizeof fx);
        fx.dwSize = sizeof fx;
        fx.dwFillColor = 0x001f;
        CHECK_HR(IDirectDrawSurface_Blt(primary, NULL, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &fx), DD_OK, "a fill of the screen");
        Shot("blue");
        CHECK_HR(IDirectDrawSurface_Flip(back, NULL, DDFLIP_WAIT), DDERR_NOTFLIPPABLE, "Flip of a back buffer");
        CHECK_HR(IDirectDrawSurface_Flip(primary, NULL, DDFLIP_WAIT), DD_OK, "Flip");
        Shot("flipped");
        /* The front now holds what was drawn into the back buffer. */
        memset(&d, 0, sizeof d);
        d.dwSize = sizeof d;
        CHECK_HR(IDirectDrawSurface_Lock(primary, NULL, &d, DDLOCK_WAIT | DDLOCK_READONLY, NULL), DD_OK, "Lock of the front after the flip");
        CHECK_HR((HRESULT)((WORD *)((BYTE *)d.lpSurface + 100 * d.lPitch))[320], 0x7c00, "the front shows the upper half of the back buffer");
        CHECK_HR((HRESULT)((WORD *)((BYTE *)d.lpSurface + 400 * d.lPitch))[320], 0x03e0, "the front shows the lower half of the back buffer");
        IDirectDrawSurface_Unlock(primary, NULL);
        memset(&d, 0, sizeof d);
        d.dwSize = sizeof d;
        IDirectDrawSurface_Lock(back, NULL, &d, DDLOCK_WAIT | DDLOCK_READONLY, NULL);
        CHECK_HR((HRESULT)((WORD *)((BYTE *)d.lpSurface + 100 * d.lPitch))[320], 0x001f, "the back buffer holds the old front");
        IDirectDrawSurface_Unlock(back, NULL);
        {
            /* Sixty flips a second: ten of them take about a sixth of a second. */
            const DWORD before = GetTickCount();
            DWORD took;
            for (i = 0; i < 10; ++i) IDirectDrawSurface_Flip(primary, NULL, DDFLIP_WAIT);
            took = GetTickCount() - before;
            CHECK(took >= 120 && took < 600, "a flip waits for the picture");
        }
        CHECK_HR(IDirectDraw_FlipToGDISurface(dd), DD_OK, "FlipToGDISurface");
        /* The gamma ramp of the display. */
        CHECK_HR(IDirectDrawSurface_QueryInterface(primary, &IID_IDirectDrawGammaControl, (void **)&gamma), S_OK, "the gamma control");
        if (gamma)
        {
            CHECK_HR(IDirectDrawGammaControl_GetGammaRamp(gamma, 0, &ramp), DD_OK, "GetGammaRamp");
            CHECK(ramp.red[255] == 0xffff && ramp.green[128] == 128 * 257 && ramp.blue[0] == 0, "the ramp is straight");
            /* The surface of GDI is in front again and holds the blue fill. */
            for (i = 0; i < 256; ++i) ramp.blue[i] = (WORD)(i * 128);
            CHECK_HR(IDirectDrawGammaControl_SetGammaRamp(gamma, 0, &ramp), DD_OK, "SetGammaRamp");
            Shot("gamma");
            memset(&read, 0, sizeof read);
            IDirectDrawGammaControl_GetGammaRamp(gamma, 0, &read);
            CHECK(!memcmp(&read, &ramp, sizeof ramp), "the ramp that was set");
            for (i = 0; i < 256; ++i) ramp.blue[i] = (WORD)(i * 257);
            IDirectDrawGammaControl_SetGammaRamp(gamma, 0, &ramp);
            IDirectDrawGammaControl_Release(gamma);
        }
        if (dd4) CHECK_HR(IDirectDraw4_TestCooperativeLevel(dd4), DD_OK, "TestCooperativeLevel in exclusive mode");
    }
    if (back) IDirectDrawSurface_Release(back);
    if (primary) CHECK(IDirectDrawSurface_Release(primary) == 0, "the chain goes");

    /* Eight bits: the palette of the primary surface is the colour table of the display. */
    CHECK_HR(IDirectDraw_SetDisplayMode(dd, 640, 480, 8), DD_OK, "SetDisplayMode 640x480x8");
    Pump();
    primary = NULL;
    memset(&d, 0, sizeof d);
    d.dwSize = sizeof d;
    d.dwFlags = DDSD_CAPS;
    d.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    CHECK_HR(IDirectDraw_CreateSurface(dd, &d, &primary, NULL), DD_OK, "the primary surface at eight bits");
    memset(colors, 0, sizeof colors);
    colors[1].peRed = 255;
    colors[2].peGreen = 255;
    colors[3].peBlue = 200; colors[3].peRed = 40;
    CHECK_HR(IDirectDraw_CreatePalette(dd, DDPCAPS_8BIT | DDPCAPS_ALLOW256, colors, &palette, NULL), DD_OK, "a palette for the screen");
    if (primary && palette)
    {
        DWORD value = 0;
        RECT half = {0, 0, 320, 480};
        memset(&d, 0, sizeof d);
        d.dwSize = sizeof d;
        IDirectDrawSurface_GetSurfaceDesc(primary, &d);
        CHECK((d.ddpfPixelFormat.dwFlags & DDPF_PALETTEINDEXED8) && d.ddpfPixelFormat.dwRGBBitCount == 8, "the screen has a palette");
        CHECK_HR(IDirectDrawSurface_SetPalette(primary, palette), DD_OK, "SetPalette on the primary surface");
        IDirectDrawPalette_GetCaps(palette, &value);
        CHECK(value & DDPCAPS_PRIMARYSURFACE, "the palette says it is the one of the screen");
        memset(&fx, 0, sizeof fx);
        fx.dwSize = sizeof fx;
        fx.dwFillColor = 1;
        CHECK_HR(IDirectDrawSurface_Blt(primary, &half, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &fx), DD_OK, "index 1 on the left");
        half.left = 320; half.right = 640;
        fx.dwFillColor = 2;
        CHECK_HR(IDirectDrawSurface_Blt(primary, &half, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &fx), DD_OK, "index 2 on the right");
        Shot("palette");
        /* The colours change, the indices stay. */
        colors[1] = colors[3];
        CHECK_HR(IDirectDrawPalette_SetEntries(palette, 0, 1, 1, &colors[1]), DD_OK, "SetEntries on the palette of the screen");
        Shot("palette2");
    }
    if (palette) IDirectDrawPalette_Release(palette);
    if (primary) IDirectDrawSurface_Release(primary);

    CHECK_HR(IDirectDraw_RestoreDisplayMode(dd), DD_OK, "RestoreDisplayMode");
    Pump();
    CHECK(GetSystemMetrics(SM_CXSCREEN) == 800 && GetSystemMetrics(SM_CYSCREEN) == 600, "the desktop is back");
    if (lost)
    {
        /* Back in the mode it was made in, the lost surface returns, empty. */
        CHECK_HR(IDirectDrawSurface_IsLost(lost), DDERR_SURFACELOST, "still lost until it is restored");
        CHECK_HR(IDirectDrawSurface_Restore(lost), DD_OK, "Restore in the mode of its creation");
        CHECK_HR(IDirectDrawSurface_IsLost(lost), DD_OK, "no longer lost");
        IDirectDrawSurface_Release(lost);
    }
    CHECK_HR(IDirectDraw_SetCooperativeLevel(dd, window, DDSCL_NORMAL), DD_OK, "back to a window");
    if (dd4) IDirectDraw4_Release(dd4);
    SetWindowPos(window, HWND_TOP, WindowX, WindowY, WindowWidth, WindowHeight, 0);
    Pump();
}

int main(int argc, char **argv)
{
    WNDCLASSA klass;
    IDirectDraw *dd = NULL;
    IDirectDraw7 *dd7 = NULL;
    IDirectDrawClipper *clipper = NULL;
    DDSURFACEDESC d;
    IDirectDrawSurface *surface = NULL;

    if (argc > 1) report_file = argv[1];
    memset(&klass, 0, sizeof klass);
    klass.lpfnWndProc = Procedure;
    klass.hInstance = GetModuleHandleA(NULL);
    klass.lpszClassName = "Arm64DirectDrawFixture";
    klass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassA(&klass);
    window = CreateWindowExA(0, klass.lpszClassName, "DirectDraw", WS_POPUP, WindowX, WindowY, WindowWidth, WindowHeight, NULL, NULL,
                             klass.hInstance, NULL);
    CHECK(window != NULL, "the window");
    ShowWindow(window, SW_SHOW);
    Pump();
    {
        /* Three calls the front ends of the games make besides DirectX. */
        RECT a = {0, 0, 10, 10}, b = {5, 5, 30, 20}, both = {0, 0, 0, 0};
        CHECK(UnionRect(&both, &a, &b) && both.left == 0 && both.top == 0 && both.right == 30 && both.bottom == 20, "UnionRect");
        CHECK(SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL), "SetThreadPriority");
        CHECK(GetCursor() == GetCursor(), "GetCursor");
    }

    CHECK_HR(DirectDrawCreate((GUID *)&IID_IDirectDraw, &dd, NULL), DDERR_INVALIDDIRECTDRAWGUID, "DirectDrawCreate with a strange GUID");
    CHECK_HR(DirectDrawCreateEx(NULL, (LPVOID *)&dd7, &IID_IDirectDraw4, NULL), DDERR_INVALIDPARAMS, "DirectDrawCreateEx for IDirectDraw4");
    CHECK_HR(DirectDrawCreateEx(NULL, (LPVOID *)&dd7, &IID_IDirectDraw7, NULL), DD_OK, "DirectDrawCreateEx");
    if (dd7) CHECK(IDirectDraw7_Release(dd7) == 0, "the object of DirectDrawCreateEx goes");
    CHECK_HR(DirectDrawCreateClipper(0, &clipper, NULL), DD_OK, "DirectDrawCreateClipper");
    if (clipper) IDirectDrawClipper_Release(clipper);
    CHECK_HR(DirectDrawCreate(NULL, &dd, NULL), DD_OK, "DirectDrawCreate");
    if (!dd)
    {
        Fail(__LINE__, 0);
        Report("DDRAW");
        return 1;
    }
    memset(&d, 0, sizeof d);
    d.dwSize = sizeof d;
    d.dwFlags = DDSD_CAPS;
    d.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    CHECK_HR(IDirectDraw_CreateSurface(dd, &d, &surface, NULL), DDERR_NOCOOPERATIVELEVELSET, "CreateSurface before SetCooperativeLevel");
    CHECK_HR(IDirectDraw_SetCooperativeLevel(dd, window, DDSCL_NORMAL | DDSCL_EXCLUSIVE), DDERR_INVALIDPARAMS, "both levels at once");
    CHECK_HR(IDirectDraw_SetCooperativeLevel(dd, window, DDSCL_NORMAL), DD_OK, "SetCooperativeLevel");

    Objects(dd);
    Windowed(dd);
    Formats(dd);
    Timing(dd);
    Exclusive(dd);

    CHECK(IDirectDraw_Release(dd) == 0, "the DirectDraw object goes");
    DestroyWindow(window);
    Report("DDRAW");
    return failures ? 1 : 0;
}
