/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Guest side of the surface half of tools/test_directx_guest.py: the whole
   way a DirectDraw program takes over the screen, through the original
   DDRAW.DLL and its HEL.

   SetCooperativeLevel, SetDisplayMode 640x480x8, a primary surface with one
   back buffer, a palette, Lock/Unlock on the back buffer, Flip, an offscreen
   surface blitted onto the primary, and GetDC/TextOut on the primary. Every
   answer goes out through OutputDebugStringA, and the pattern the program
   draws is checked afterwards in the host image, so the test proves that the
   pixels arrived and not only that the calls answered S_OK.

   Built twice. Without RESTORE the program leaves the display in its mode and
   exits, so the screenshot shows the DirectDraw picture. With RESTORE it calls
   RestoreDisplayMode first, so the screenshot shows the shell again. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U LoadLibraryA(const char *);
API U GetProcAddress(U, const char *);
API void OutputDebugStringA(const char *);
API void ExitProcess(U);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
API U DefWindowProcA(U, U, U, U);
API U ShowWindow(U, int);
API U SetForegroundWindow(U);
API U GetSystemMetrics(int);
API U TextOutA(U, int, int, const char *, int);
API U SetTextColor(U, U);
API U SetBkMode(U, int);

typedef U(__stdcall *Create)(void *, void **, void *);

static char line[256];
static void number(const char *label, U value)
{
    static const char digits[] = "0123456789abcdef";
    U at = 0, i = 0;
    while (label[i]) line[at++] = label[i++];
    line[at++] = '=';
    for (i = 0; i < 8; ++i) line[at++] = digits[(value >> (28 - i * 4)) & 15];
    line[at] = 0;
    OutputDebugStringA(line);
}

/* The two interfaces, by vtable slot. IDirectDraw: 6 CreateSurface,
   19 RestoreDisplayMode, 20 SetCooperativeLevel, 21 SetDisplayMode,
   5 CreatePalette. IDirectDrawSurface: 5 Blt, 11 Flip, 12 GetAttachedSurface,
   17 GetDC, 22 GetSurfaceDesc, 25 Lock, 26 ReleaseDC, 31 SetPalette,
   32 Unlock. */
typedef U(__stdcall * M1)(void *);
typedef U(__stdcall * M2)(void *, U);
typedef U(__stdcall * M3)(void *, U, U);
typedef U(__stdcall * M4)(void *, U, U, U);
typedef U(__stdcall * M5)(void *, U, U, U, U);
typedef U(__stdcall * M6)(void *, U, U, U, U, U);

/* DDSURFACEDESC, 0x6c bytes, as IDirectDraw of the first interface version
   wants it: dwSize, dwFlags, dwHeight, dwWidth, lPitch, dwBackBufferCount at
   the head, lpSurface at 0x24, the pixel format at 0x48 and ddsCaps at 0x68. */
static U desc[27], back_desc[27], off_desc[27];
static U caps_query[1];
static int blt_rect[4];
static U palette_entries[256];

static U __stdcall window_procedure(U window, U message, U wp, U lp)
{
    return DefWindowProcA(window, message, wp, lp);
}

void __stdcall mainCRTStartup(void)
{
    U library, window = 0, klass;
    U class_fields[10];
    void *ddraw = 0, *primary = 0, *back = 0, *offscreen = 0, *palette = 0;
    U **vtable, **surface_vtable;
    U value, i, x, y;

    library = LoadLibraryA("DDRAW.DLL");
    number("DDSURF ddraw_loaded", library ? 1 : 0);
    if (!library) ExitProcess(1);
    {
        Create create = (Create)GetProcAddress(library, "DirectDrawCreate");
        if (!create) ExitProcess(2);
        value = create(0, &ddraw, 0);
        number("DDSURF create", value);
        if (value || !ddraw) ExitProcess(3);
    }
    vtable = *(U ***)ddraw;

    /* An exclusive cooperative level needs a window of this process. */
    for (i = 0; i < 10; ++i) class_fields[i] = 0;
    class_fields[1] = (U)window_procedure;      /* lpfnWndProc */
    class_fields[7] = 6;                        /* hbrBackground: COLOR_WINDOW+1 */
    class_fields[9] = (U) "DDSurfaceFixture";   /* lpszClassName */
    klass = RegisterClassA(class_fields);
    number("DDSURF class", klass);
    window = CreateWindowExA(0, "DDSurfaceFixture", "DDSurface", 0x90000000u, 0, 0,
                             GetSystemMetrics(0), GetSystemMetrics(1), 0, 0, 0, 0);
    number("DDSURF window", window ? 1 : 0);
    if (!window) ExitProcess(4);
    ShowWindow(window, 5);
    SetForegroundWindow(window);

    /* DDSCL_EXCLUSIVE 0x10 | DDSCL_FULLSCREEN 0x01 | DDSCL_ALLOWREBOOT 0x02 */
    value = ((M3)vtable[20])(ddraw, window, 0x13);
    number("DDSURF cooperative", value);
    if (value) ExitProcess(5);

    value = ((M4)vtable[21])(ddraw, 640, 480, 8);
    number("DDSURF setmode", value);
    number("DDSURF metric_width", GetSystemMetrics(0));
    number("DDSURF metric_height", GetSystemMetrics(1));
    if (value) ExitProcess(6);

    /* A primary surface with one back buffer: DDSD_CAPS 1 |
       DDSD_BACKBUFFERCOUNT 0x20, DDSCAPS_PRIMARYSURFACE 0x200 |
       DDSCAPS_FLIP 0x10 | DDSCAPS_COMPLEX 8. */
    for (i = 0; i < 27; ++i) desc[i] = 0;
    desc[0] = 0x6c;
    desc[1] = 0x21;
    desc[5] = 1;
    desc[26] = 0x218;
    value = ((M4)vtable[6])(ddraw, (U)desc, (U)&primary, 0);
    number("DDSURF primary", value);
    if (value || !primary) ExitProcess(7);
    surface_vtable = *(U ***)primary;

    /* The back buffer of the flipping chain: DDSCAPS_BACKBUFFER 4. */
    caps_query[0] = 4;
    value = ((M3)surface_vtable[12])(primary, (U)caps_query, (U)&back);
    number("DDSURF backbuffer", value);
    if (value || !back) ExitProcess(8);

    /* A palette of 256 entries: index 1 red, 2 green, 3 blue, 4 white.
       DDPCAPS_8BIT 4 | DDPCAPS_INITIALIZE 8 | DDPCAPS_ALLOW256 0x40. */
    for (i = 0; i < 256; ++i) palette_entries[i] = 0;
    palette_entries[1] = 0x0000ff;   /* PALETTEENTRY is r, g, b, flags */
    palette_entries[2] = 0x00ff00;
    palette_entries[3] = 0xff0000;
    palette_entries[4] = 0xffffff;
    value = ((M5)vtable[5])(ddraw, 0x4c, (U)palette_entries, (U)&palette, 0);
    number("DDSURF palette", value);
    if (!value && palette)
    {
        value = ((M2)surface_vtable[31])(primary, (U)palette);
        number("DDSURF setpalette", value);
    }

    /* Lock the back buffer and paint it: left half index 1, right half 2. */
    for (i = 0; i < 27; ++i) back_desc[i] = 0;
    back_desc[0] = 0x6c;
    value = ((M5)surface_vtable[25])(back, 0, (U)back_desc, 1, 0);   /* DDLOCK_WAIT */
    number("DDSURF lock", value);
    number("DDSURF lock_pitch", back_desc[4]);
    number("DDSURF lock_bits", back_desc[9] ? 1 : 0);
    if (!value && back_desc[9])
    {
        unsigned char *bits = (unsigned char *)back_desc[9];
        const U pitch = back_desc[4];
        for (y = 0; y < 480; ++y)
            for (x = 0; x < 640; ++x)
                bits[y * pitch + x] = x < 320 ? 1 : 2;
        value = ((M2)surface_vtable[32])(back, (U)bits);
        number("DDSURF unlock", value);
    }

    value = ((M3)surface_vtable[11])(primary, 0, 1);   /* Flip, DDFLIP_WAIT */
    number("DDSURF flip", value);

    /* Lock the primary surface itself and paint a block into it, the way an
       Allegro program in a full screen mode does: it never asks for a device
       context and never blits, it writes its pixels through Lock and expects
       them on the screen. The block is 32 by 32 at x 64, in palette slot 3.
       Without the DCI primary surface of the display driver the HEL has no
       screen to write to and the block never appears. */
    for (i = 0; i < 27; ++i) back_desc[i] = 0;
    back_desc[0] = 0x6c;
    value = ((M5)surface_vtable[25])(primary, 0, (U)back_desc, 1, 0);   /* DDLOCK_WAIT */
    number("DDSURF primary_lock", value);
    number("DDSURF primary_lock_pitch", back_desc[4]);
    number("DDSURF primary_lock_bits", back_desc[9] ? 1 : 0);
    if (!value && back_desc[9])
    {
        unsigned char *bits = (unsigned char *)back_desc[9];
        const U pitch = back_desc[4];
        for (y = 0; y < 32; ++y)
            for (x = 64; x < 96; ++x) bits[y * pitch + x] = 3;
        value = ((M2)surface_vtable[32])(primary, (U)bits);
        number("DDSURF primary_unlock", value);
    }

    /* An offscreen surface, filled with index 4, blitted into the corner.
       DDSD_CAPS 1 | DDSD_HEIGHT 2 | DDSD_WIDTH 4, DDSCAPS_OFFSCREENPLAIN
       0x40 | DDSCAPS_SYSTEMMEMORY 0x800. */
    for (i = 0; i < 27; ++i) off_desc[i] = 0;
    off_desc[0] = 0x6c;
    off_desc[1] = 7;
    off_desc[2] = 32;
    off_desc[3] = 32;
    off_desc[26] = 0x840;
    value = ((M4)vtable[6])(ddraw, (U)off_desc, (U)&offscreen, 0);
    number("DDSURF offscreen", value);
    if (!value && offscreen)
    {
        U **off_vtable = *(U ***)offscreen;
        /* An offscreen surface has its own palette. Assign it explicitly so blitting to the primary surface preserves the intended colors. */
        if (palette)
        {
            value = ((M2)off_vtable[31])(offscreen, (U)palette);
            number("DDSURF offscreen_palette", value);
        }
        for (i = 0; i < 27; ++i) off_desc[i] = 0;
        off_desc[0] = 0x6c;
        value = ((M5)off_vtable[25])(offscreen, 0, (U)off_desc, 1, 0);
        number("DDSURF offscreen_lock", value);
        if (!value && off_desc[9])
        {
            unsigned char *bits = (unsigned char *)off_desc[9];
            for (y = 0; y < 32; ++y)
                for (x = 0; x < 32; ++x) bits[y * off_desc[4] + x] = 4;
            ((M2)off_vtable[32])(offscreen, (U)bits);
        }
        /* Blt(destRect, source, srcRect, DDBLT_WAIT, fx). Without a
           destination rectangle DirectDraw stretches the source over the whole
           primary surface, so the corner is named. */
        blt_rect[0] = 0; blt_rect[1] = 0; blt_rect[2] = 32; blt_rect[3] = 32;
        value = ((M6)surface_vtable[5])(primary, (U)blt_rect, (U)offscreen, 0, 0x01000000, 0);
        number("DDSURF blt", value);
    }

    /* GDI on the primary surface. */
    {
        U hdc = 0;
        value = ((M2)surface_vtable[17])(primary, (U)&hdc);
        number("DDSURF getdc", value);
        number("DDSURF dc", hdc ? 1 : 0);
        if (!value && hdc)
        {
            SetBkMode(hdc, 1);
            SetTextColor(hdc, 0x0000ff);
            TextOutA(hdc, 360, 8, "NEO98", 5);
            value = ((M2)surface_vtable[26])(primary, hdc);
            number("DDSURF releasedc", value);
        }
    }

#ifdef RESTORE
    value = ((M1)vtable[19])(ddraw);
    number("DDSURF restore", value);
    number("DDSURF restored_width", GetSystemMetrics(0));
    number("DDSURF restored_height", GetSystemMetrics(1));
#endif
    OutputDebugStringA("DDSURF done");
    ExitProcess(0);
}
