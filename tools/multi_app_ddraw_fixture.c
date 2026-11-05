/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
// A DirectDraw program beside other programs (tools/test_multi_app_host.py,

//
// One popup window of 320 by 240, as a game opens one before it takes the
// screen, black for A and C, white for B and D. NAME (a letter, -DNAME) starts every title, so two copies can run
// side by side. Unlike tools/host_ddraw_mode_fixture.c it never takes the
// foreground by itself. Keys:
// E DirectDrawCreate, SetCooperativeLevel(EXCLUSIVE | FULLSCREEN),
// SetDisplayMode(640, 480, 16), the primary surface -> "N exclusive"
// A refusal stays on the title: "N refused <hresult>"
// G the same as a game does it: a refusal ends the program without a word
// W SetCooperativeLevel(NORMAL) and the primary surface -> "N windowed"
// R RestoreDisplayMode, SetCooperativeLevel(NORMAL) -> "N normal"
// X ExitProcess(0), with whatever is still set
// K a read from address zero: the program crashes with whatever is set
// With a primary surface a timer of 100 ms locks it, writes its colour into
// the first rows through the pointer of the lock and unlocks. The title then
// ends in " drawing"; a lost surface (DDERR_SURFACELOST) shows " lost" until
// Restore succeeds, with the answer of Restore before it when that is a
// failure: "N windowed 887600d1 lost" (DDERR_NOEXCLUSIVEMODE and the like).
// C the title says whether a WM_DISPLAYCHANGE came: "N changes 0|1"
typedef unsigned U;
typedef struct { U hwnd, message, wp, lp, time; int x, y; } MSG;
typedef struct { U style, proc, extra, classExtra, instance, icon, cursor, brush;
                 const char *menu, *name; } CLASS;
#define API __declspec(dllimport) __stdcall
API U RegisterClassA(CLASS *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U GetMessageA(MSG *, U, U, U);
API U DispatchMessageA(MSG *);
API void ExitProcess(U);
API U GetModuleHandleA(const char *);
API U SetWindowTextA(U, const char *);
API U GetStockObject(int);
API U SetTimer(U, U, U, U);
API U DirectDrawCreate(void *, void **, void *);

#ifndef NAME
#define NAME 'A'
#endif
#ifndef COLOUR
#define COLOUR 0x55
#endif

typedef U (__stdcall *Method1)(void *);
typedef U (__stdcall *Method2)(void *, void *);
typedef U (__stdcall *Method3)(void *, U, U);
typedef U (__stdcall *Method4)(void *, U, U, U);
typedef U (__stdcall *Method5)(void *, void *, void *, U, U);
static void **draw, **primary;
#define CALL(object, kind, index) ((kind)(*(void ***)(object))[index])

static const char *state = "ready";
static const char *paint = "";
static U changes, window, refused;

static void title(void)
{
    static char text[64];
    static const char digits[] = "0123456789abcdef";
    char *p = text;
    const char *s;
    *p++ = NAME; *p++ = ' ';
    for (s = state; *s; ++s) *p++ = *s;
    if (refused)
    {
        int i;
        *p++ = ' ';
        for (i = 28; i >= 0; i -= 4) *p++ = digits[(refused >> i) & 15];
    }
    for (s = paint; *s; ++s) *p++ = *s;
    *p = 0;
    SetWindowTextA(window, text);
}

static U surface(void)
{
    U desc[27] = {0};
    void *object = 0;
    U result;
    desc[0] = 108; desc[1] = 1; desc[26] = 0x200;   // DDSD_CAPS, DDSCAPS_PRIMARYSURFACE
    result = CALL(draw, Method4, 6)(draw, (U)desc, (U)&object, 0);
    if (!result) primary = (void **)object;
    return result;
}

static void exclusive(int quiet)
{
    void *object = 0;
    if (!draw)
    {
        if (DirectDrawCreate(0, &object, 0) || !object) ExitProcess(4);
        draw = (void **)object;
    }
    if (primary) { CALL(primary, Method1, 2)(primary); primary = 0; }      // one primary surface at a time
    refused = CALL(draw, Method3, 20)(draw, window, 0x11);                  // SetCooperativeLevel
    if (!refused) refused = CALL(draw, Method4, 21)(draw, 640, 480, 16);    // SetDisplayMode
    if (!refused) refused = surface();
    if (refused && quiet) ExitProcess(7);
    state = refused ? "refused" : "exclusive";
    paint = "";
    title();
}

static void tick(void)
{
    U desc[27] = {0};
    U result;
    const char *before = paint;
    if (!primary) return;
    desc[0] = 108;
    result = CALL(primary, Method5, 25)(primary, 0, desc, 1, 0);            // Lock, DDLOCK_WAIT
    if (result == 0x887601c2u)                                              // DDERR_SURFACELOST
    {
        const U restored = CALL(primary, Method1, 27)(primary);              // Restore
        paint = " lost";
        if (restored != refused) { refused = restored; before = 0; }
    }
    else if (!result)
    {
        unsigned char *bits = (unsigned char *)desc[9];
        const U pitch = desc[4], rows = desc[2] < 32 ? desc[2] : 32;
        U y, x;
        if (bits)
            for (y = 0; y < rows; ++y)
                for (x = 0; x < pitch && x < 256; ++x) bits[y * pitch + x] = COLOUR;
        CALL(primary, Method2, 32)(primary, 0);                             // Unlock
        paint = " drawing";
        refused = 0;
    }
    else
        paint = " failing";
    if (paint != before) title();
}

static U __stdcall proc(U h, U m, U wp, U lp)
{
    if (m == 0x113) tick();                     // WM_TIMER
    if (m == 0x7e) ++changes;                   // WM_DISPLAYCHANGE
    if (m == 0x100)                             // WM_KEYDOWN
    {
        if (wp == 'E') exclusive(0);
        if (wp == 'G') exclusive(1);
        if (wp == 'W' && !draw)
        {
            void *object = 0;
            if (DirectDrawCreate(0, &object, 0) || !object) ExitProcess(4);
            draw = (void **)object;
            refused = CALL(draw, Method3, 20)(draw, h, 8);
            if (!refused) refused = surface();
            state = refused ? "refused" : "windowed";
            title();
        }
        if (wp == 'R' && draw)
        {
            if (primary) { CALL(primary, Method1, 2)(primary); primary = 0; }
            CALL(draw, Method1, 19)(draw);
            CALL(draw, Method3, 20)(draw, h, 8);
            state = "normal"; paint = ""; refused = 0;
            title();
        }
        if (wp == 'C')
        {
            static char text[] = "N changes 0";
            text[0] = NAME; text[10] = (char)(changes ? '1' : '0');
            SetWindowTextA(h, text);
        }
        if (wp == 'X') ExitProcess(0);
        if (wp == 'K') return *(volatile U *)0;
    }
    if (m == 2) ExitProcess(0);
    return DefWindowProcA(h, m, wp, lp);
}

void mainCRTStartup(void)
{
    CLASS c = {0};
    MSG msg;
    c.proc = (U)proc;
    c.instance = GetModuleHandleA(0);
    c.brush = GetStockObject(NAME & 1 ? 4 : 0); // BLACK_BRUSH for A and C, WHITE_BRUSH for B and D
    c.name = "NEO98MultiAppDirectDraw";
    if (!RegisterClassA(&c)) ExitProcess(2);
    window = CreateWindowExA(0, c.name, "", 0x90000000u, 40 + (NAME - 'A') * 60, 40 + (NAME - 'A') * 40, 320, 240,
                             0, 0, c.instance, 0);
    if (!window) ExitProcess(3);
    title();
    SetTimer(window, 1, 100, 0);
    while (GetMessageA(&msg, 0, 0, 0)) DispatchMessageA(&msg);
    ExitProcess(0);
}
