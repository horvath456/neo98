/* SPDX-License-Identifier: GPL-2.0-or-later */
/* 256 colours as the stored display mode.

   STORE: the Settings page of DESK.CPL stores a new colour depth with
   ChangeDisplaySettingsEx(CDS_UPDATEREGISTRY | CDS_NORESET) and restarts.
   USER writes HKCC\Display\Settings and leaves the screen alone;
   CDS_TEST writes nothing; a null DEVMODE with CDS_UPDATEREGISTRY is
   DISP_CHANGE_BADFLAGS.

   CHECK (after the restart, same registry): the display came up with 8 bits.
   A logical palette realized in the window's DC takes free slots of the
   system palette; PALETTEINDEX, a 32-bit DIB section, SetDIBits
   into a device bitmap and AnimatePalette all show its colours.
   A window DC answers the device of the mode, a compatible bitmap of it has
   8 bits (what WLOADER.EXE measures) and blits onto the window. The window
   then paints a colour of the static palette and one that is not in it and
   waits; the test reads the screen. On WM_CLOSE it goes back to 32 bits with
   CDS_UPDATEREGISTRY, without a restart. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API char *GetCommandLineA(void);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U ShowWindow(U, U);
API U UpdateWindow(U);
API U GetMessageA(void *, U, U, U);
API U DispatchMessageA(const void *);
API void PostQuitMessage(int);
API U BeginPaint(U, void *);
API U EndPaint(U, const void *);
API U FillRect(U, const void *, U);
API U GetDC(U);
API U ReleaseDC(U, U);
API U EnumDisplaySettingsA(const char *, U, void *);
API U ChangeDisplaySettingsExA(const char *, void *, U, U, U);
API U ChangeDisplaySettingsA(void *, U);
API U GetDeviceCaps(U, int);
API U CreateCompatibleBitmap(U, int, int);
API U CreateCompatibleDC(U);
API U SelectObject(U, U);
API U DeleteObject(U);
API U DeleteDC(U);
API int GetObjectA(U, int, void *);
API U CreateSolidBrush(U);
API U BitBlt(U, int, int, int, int, U, int, int, U);
API U GetPixel(U, int, int);
API U CreatePalette(const void *);
API U SelectPalette(U, U, U);
API U RealizePalette(U);
API U GetSystemPaletteEntries(U, U, U, void *);
API U AnimatePalette(U, U, U, const void *);
API U CreateDIBSection(U, const void *, U, void **, U, U);
API U GetStockObject(int);
API int SetDIBits(U, U, U, U, const void *, const void *, U);
API U RegOpenKeyExA(U, const char *, U, U, U *);
API U RegQueryValueExA(U, const char *, U *, U *, void *, U *);
API U RegCloseKey(U);

static void check(int ok, U code) { if (!ok) ExitProcess(code); }
static int same(const char *a, const char *b)
{
    while (*a && *a == *b) ++a, ++b;
    return *a == *b;
}
static int has(const char *text, const char *word)
{
    for (; *text; ++text)
    {
        const char *a = text, *b = word;
        while (*b && *a == *b) ++a, ++b;
        if (!*b) return 1;
    }
    return 0;
}
/* DEVMODEA: dmSize at 36, dmFields at 40, dmBitsPerPel 104, width 108, height 112. */
static void mode_of(unsigned char *m, U depth)
{
    for (int i = 0; i < 156; ++i) m[i] = 0;
    m[36] = 156;
    *(U *)(m + 40) = 0x1c0000;
    *(U *)(m + 104) = depth;
    *(U *)(m + 108) = 800;
    *(U *)(m + 112) = 600;
}
static U depth_of(U which)
{
    unsigned char m[156] = {0};
    m[36] = 156;
    check(EnumDisplaySettingsA(0, which, m) != 0, 90);
    check(*(U *)(m + 108) == 800 && *(U *)(m + 112) == 600, 91);
    return *(U *)(m + 104);
}
static void registry(const char *depth)
{
    U key = 0, type = 0, size = 16;
    char text[16] = {0};
    check(RegOpenKeyExA(0x80000005u, "Display\\Settings", 0, 0x20019, &key) == 0, 80);
    check(RegQueryValueExA(key, "BitsPerPixel", 0, &type, text, &size) == 0 && type == 1, 81);
    check(same(text, depth), 82);
    size = 16;
    check(RegQueryValueExA(key, "Resolution", 0, &type, text, &size) == 0 && type == 1, 83);
    check(same(text, "800,600"), 84);
    RegCloseKey(key);
}

static void store(void)
{
    unsigned char m[156];
    check(depth_of(0xfffffffeu) == 32 && depth_of(0xffffffffu) == 32, 10);
    registry("32");
    mode_of(m, 8);
    check(ChangeDisplaySettingsExA(0, m, 0, 2, 0) == 0, 11);                  /* CDS_TEST */
    check(depth_of(0xfffffffeu) == 32, 12);
    check(ChangeDisplaySettingsExA(0, 0, 0, 1, 0) == (U)-4, 13);              /* null + UPDATEREGISTRY */
    check(ChangeDisplaySettingsExA(0, m, 0, 0x10000011u, 0) == 0, 14);        /* DESK.CPL's flags */
    check(depth_of(0xffffffffu) == 32, 15);
    check(depth_of(0xfffffffeu) == 8, 16);
    {
        U dc = GetDC(0);
        check(GetDeviceCaps(dc, 12) == 32, 17);
        ReleaseDC(0, dc);
    }
    registry("8");
}

static U window;
static U __stdcall proc(U h, U m, U w, U l)
{
    if (m == 15)
    {
        U paint[16];
        int left[4] = {0, 0, 100, 100}, right[4] = {100, 0, 200, 100};
        U dc = BeginPaint(h, paint), olive = CreateSolidBrush(0x00008080u), odd = CreateSolidBrush(0x003264c8u);
        /* RGB(128,128,0) is the static palette's olive; RGB(200,100,50) is no entry of it. */
        FillRect(dc, left, olive);
        FillRect(dc, right, odd);
        DeleteObject(olive);
        DeleteObject(odd);
        EndPaint(h, paint);
        return 0;
    }
    if (m == 2) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(h, m, w, l);
}

static void verify(void)
{
    check(depth_of(0xffffffffu) == 8, 20);
    check(depth_of(0xfffffffeu) == 8, 21);
    registry("8");
    U cls[10] = {0, (U)proc, 0, 0, 0, 0, 0, 0, 0, (U)"DesktopDepth98"};
    check(RegisterClassA(cls) != 0, 22);
    window = CreateWindowExA(0, "DesktopDepth98", "depth", 0x80000000u, 100, 100, 200, 100, 0, 0, 0, 0);
    check(window != 0, 23);
    ShowWindow(window, 1);
    UpdateWindow(window);
    U dc = GetDC(window);
    check(GetDeviceCaps(dc, 12) == 8, 24);                        /* BITSPIXEL */
    check((GetDeviceCaps(dc, 38) & 0x100) != 0, 25);             /* RASTERCAPS, RC_PALETTE */
    check(GetDeviceCaps(dc, 104) == 256 && GetDeviceCaps(dc, 24) == 20, 26);
    /* WLOADER.EXE's measurement: a compatible bitmap of a display DC. */
    U bitmap = CreateCompatibleBitmap(dc, 16, 16);
    int info[6] = {0};
    check(bitmap != 0, 27);
    check(GetObjectA(bitmap, 24, info) == 24, 28);
    check(((info[4] >> 16) & 0xffff) == 8, 29);                   /* bmBitsPixel */
    U memory = CreateCompatibleDC(dc), red = CreateSolidBrush(0x000000ffu);
    int all[4] = {0, 0, 16, 16};
    U old = SelectObject(memory, bitmap);
    check(FillRect(memory, all, red) != 0, 30);
    check(BitBlt(dc, 160, 60, 16, 16, memory, 0, 0, 0x00cc0020u) != 0, 31);
    check(GetPixel(dc, 165, 65) == 0x000000ffu, 32);
    SelectObject(memory, old);
    DeleteObject(red);
    {
        /* LOGPALETTE: version 300h, four entries; entry 2 is PC_RESERVED. */
        U logical[5] = {0x00040300u, 0x001ec80au, 0x006405fau, 0x01030201u, 0x00008080u};
        U palette = CreatePalette(logical);
        check(palette != 0, 50);
        U previous = SelectPalette(dc, palette, 0);
        check(previous != 0, 51);
        check(RealizePalette(dc) != 0, 52);
        U system[256];
        check(GetSystemPaletteEntries(dc, 0, 256, system) == 256, 53);
        U found = 0;
        for (U i = 10; i < 246; ++i)
        {
            if ((system[i] & 0xffffff) == 0x001ec80au) found |= 1;
            if ((system[i] & 0xffffff) == 0x006405fau) found |= 2;
            if ((system[i] & 0xffffff) == 0x00030201u) found |= 4;
        }
        check(found == 7, 54);
        /* PALETTEINDEX(0) names the slot of entry 0. */
        U index0 = CreateSolidBrush(0x01000000u), index2 = CreateSolidBrush(0x01000002u);
        int a[4] = {10, 10, 20, 20}, b[4] = {20, 10, 30, 20};
        check(FillRect(dc, a, index0) && GetPixel(dc, 12, 12) == 0x001ec80au, 55);
        check(FillRect(dc, b, index2) && GetPixel(dc, 22, 12) == 0x00030201u, 56);
        /* A 32-bit DIB section goes through the DC's palette onto the screen. */
        int header[10] = {40, 2, -1, 0x00200001, 0, 0, 0, 0, 0, 0};
        U *pixels = 0;
        U section = CreateDIBSection(dc, header, 0, (void **)&pixels, 0, 0);
        check(section && pixels, 57);
        pixels[0] = 0x00fa0564u;   /* DIB order: RGB(250,5,100) */
        pixels[1] = 0x000ac81eu;   /* RGB(10,200,30) */
        U held = SelectObject(memory, section);
        check(BitBlt(dc, 40, 10, 2, 1, memory, 0, 0, 0x00cc0020u) != 0, 58);
        check(GetPixel(dc, 40, 10) == 0x006405fau && GetPixel(dc, 41, 10) == 0x001ec80au, 59);
        SelectObject(memory, held);
        /* SetDIBits into a bitmap of the display, then onto the screen. */
        U device = CreateCompatibleBitmap(dc, 2, 1);
        check(device && SetDIBits(dc, device, 0, 1, pixels, header, 0) == 1, 60);
        held = SelectObject(memory, device);
        check(BitBlt(dc, 50, 10, 2, 1, memory, 0, 0, 0x00cc0020u) != 0, 61);
        check(GetPixel(dc, 50, 10) == 0x006405fau && GetPixel(dc, 51, 10) == 0x001ec80au, 62);
        SelectObject(memory, held);
        /* AnimatePalette recolours the reserved slot, and with it the screen. */
        U animated = 0x01090909u;
        check(AnimatePalette(palette, 2, 1, &animated) == 1, 63);
        check(GetPixel(dc, 22, 12) == 0x00090909u, 64);
        SelectPalette(dc, GetStockObject(15), 0);
        RealizePalette(dc);
        DeleteObject(index0);
        DeleteObject(index2);
        DeleteObject(device);
        DeleteObject(section);
        DeleteObject(palette);
    }
    DeleteDC(memory);
    DeleteObject(bitmap);
    ReleaseDC(window, dc);
    /* A null DEVMODE goes to the registry mode, which is this one. */
    check(ChangeDisplaySettingsA(0, 0) == 0 && depth_of(0xffffffffu) == 8, 33);
    U message[7];
    while (GetMessageA(message, 0, 0, 0)) DispatchMessageA(message);
    /* Back to 32 bits the way the Settings page applies without a restart. */
    unsigned char m[156];
    mode_of(m, 32);
    check(ChangeDisplaySettingsExA(0, m, 0, 1, 0) == 0, 40);
    check(depth_of(0xffffffffu) == 32 && depth_of(0xfffffffeu) == 32, 41);
    registry("32");
}

void mainCRTStartup(void)
{
    const char *command = GetCommandLineA();
    if (has(command, "STORE")) store();
    else if (has(command, "CHECK")) verify();
    else ExitProcess(99);
    ExitProcess(0);
}
