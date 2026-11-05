/* SPDX-License-Identifier: GPL-2.0-or-later */
/* a Direct3D game ends with its objects alive, as an
 * orphan whose launcher went first (Wonderland's loader, and DDHELP.EXE
 * after the game that started it).
 *
 * C:\DDLEAK.EXE the parent: keeps the brush of GetSysColorBrush
 * (COLOR_HIGHLIGHT), starts the launcher, waits for
 * the game's window to come and go, then checks.
 * C:\DDLEAK.EXE LAUNCH starts the game and ends at once.
 * C:\DDLEAK.EXE GAME 640x480x16 exclusive, a flipping primary surface
 * with a 3D back buffer, a Direct3D 7 HAL device,
 * and ExitProcess without releasing anything.
 *
 * Under Win98 the brushes of GetSysColorBrush belong to USER and outlive
 * every task; COMCTL32 keeps them for all processes. DDHELP releases the
 * game's DirectDraw objects after its end, with the driver's callback tables
 * in the shared arena, and the next DirectDraw program gets a fresh driver
 * object. The parent prints DDLEAK lines for tools/test_ddraw_leak_guest.py. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U LoadLibraryA(const char *);
API U GetProcAddress(U, const char *);
API void OutputDebugStringA(const char *);
API void ExitProcess(U);
API char *GetCommandLineA(void);
API U CreateProcessA(const char *, char *, U, U, U, U, const char *, const char *, U *, U *);
API U WaitForSingleObject(U, U);
API U GetTickCount(void);
API void Sleep(U);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
API U DefWindowProcA(U, U, U, U);
API U ShowWindow(U, int);
API U UpdateWindow(U);
API U SetForegroundWindow(U);
API U PeekMessageA(void *, U, U, U, U);
API U DispatchMessageA(const void *);
API U FindWindowA(const char *, const char *);
API U GetSysColorBrush(int);
API U GetObjectA(U, int, void *);

typedef U(__stdcall *CreateEx)(void *, void **, const void *, void *);
typedef U(__stdcall *M2)(void *, U);
typedef U(__stdcall *M3)(void *, U, U);
typedef U(__stdcall *M4)(void *, U, U, U);
typedef U(__stdcall *M5)(void *, U, U, U, U);
typedef U(__stdcall *M6)(void *, U, U, U, U, U);

static const U IID_IDirectDraw7[4] = {0x15e65ec0, 0x11d23b9c, 0x60002fb9, 0x5bea9797};
static const U IID_IDirect3D7[4] = {0xf5049e77, 0x11d24861, 0xa00007a4, 0xa82906c9};
static const U IID_IDirect3DHALDevice[4] = {0x84e63de0, 0x11cf46aa, 0x00006f81, 0x6e1520c0};

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

static U __stdcall procedure(U window, U message, U wp, U lp) { return DefWindowProcA(window, message, wp, lp); }

static void pump(U milliseconds)
{
    U message[8];
    const U start = GetTickCount();
    while (GetTickCount() - start < milliseconds)
    {
        while (PeekMessageA(message, 0, 0, 0, 1)) DispatchMessageA(message);
        Sleep(5);
    }
}

static U window(const char *name)
{
    U fields[10] = {0, (U)procedure, 0, 0, 0, 0, 0, 6, 0, (U)name};
    RegisterClassA(fields);
    const U hwnd = CreateWindowExA(0, name, name, 0x90000000u, 0, 0, 640, 480, 0, 0, 0, 0);
    if (hwnd) { ShowWindow(hwnd, 5); UpdateWindow(hwnd); SetForegroundWindow(hwnd); }
    return hwnd;
}

/* IDirectDraw7: 6 CreateSurface, 20 SetCooperativeLevel, 21 SetDisplayMode.
 * IDirectDrawSurface7: 12 GetAttachedSurface, 25 Lock, 32 Unlock.
 * IDirect3D7: 4 CreateDevice. Answers 0 or the first failing step. */
static U take_screen(U hwnd, void **back_out)
{
    static U desc[31], caps[4];
    void *ddraw = 0, *primary = 0, *back = 0;
    const U library = LoadLibraryA("DDRAW.DLL");
    CreateEx create = library ? (CreateEx)GetProcAddress(library, "DirectDrawCreateEx") : 0;
    if (!create || create(0, &ddraw, IID_IDirectDraw7, 0) || !ddraw) return 1;
    U **vt = *(U ***)ddraw;
    if (((M3)vt[20])(ddraw, hwnd, 0x13)) return 2;
    if (((M6)vt[21])(ddraw, 640, 480, 16, 0, 0)) return 3;
    for (U i = 0; i < 31; ++i) desc[i] = 0;
    desc[0] = 0x7c; desc[1] = 0x21; desc[5] = 1;
    desc[26] = 0x200 | 0x10 | 0x8 | 0x2000;   /* primary, flip, complex, 3D device */
    if (((M4)vt[6])(ddraw, (U)desc, (U)&primary, 0) || !primary) return 4;
    U **sv = *(U ***)primary;
    caps[0] = 4;
    if (((M3)sv[12])(primary, (U)caps, (U)&back) || !back) return 5;
    *back_out = back;
    static U lock[31];
    for (U i = 0; i < 31; ++i) lock[i] = 0;
    lock[0] = 0x7c;
    U **bv = *(U ***)back;
    if (((M5)bv[25])(back, 0, (U)lock, 1, 0) || !lock[9]) return 7;
    unsigned short *bits = (unsigned short *)lock[9];
    for (U y = 0; y < 480; ++y)
        for (U x = 0; x < 640; ++x) bits[y * (lock[4] / 2) + x] = 0x07e0;   /* green */
    ((M2)bv[32])(back, 0);
    /* The Direct3D 7 HAL device on the back buffer: the driver makes a context. */
    void *d3d = 0, *device = 0;
    if (((M3)vt[0])(ddraw, (U)IID_IDirect3D7, (U)&d3d) || !d3d) return 8;
    U **dv = *(U ***)d3d;
    if (((M4)dv[4])(d3d, (U)IID_IDirect3DHALDevice, (U)back, (U)&device) || !device) return 9;
    return 0;
}

static void game(void)
{
    const U hwnd = window("DDLEAKGAME");
    void *back = 0;
    const U failed = hwnd ? take_screen(hwnd, &back) : 100;
    number("DDLEAK game", failed);
    pump(500);
    ExitProcess(failed ? 7800 + failed : 0);   /* nothing released */
}

void mainCRTStartup(void)
{
    const char *command = GetCommandLineA();
    while (*command && *command != ' ') ++command;
    while (*command == ' ') ++command;
    if (command[0] == 'G') game();
    U si[17] = {68}, pi[4] = {0};
    if (command[0] == 'L')
    {
        char game_line[] = "C:\\DDLEAK.EXE GAME";
        ExitProcess(CreateProcessA(0, game_line, 0, 0, 0, 0, 0, 0, si, pi) ? 0 : 7702);
    }
    const U hwnd = window("DDLEAKPARENT");
    if (!hwnd) ExitProcess(7701);
    const U brush = GetSysColorBrush(13);   /* COLOR_HIGHLIGHT, the one COMCTL32 keeps */
    pump(200);
    char launch[] = "C:\\DDLEAK.EXE LAUNCH";
    if (!CreateProcessA(0, launch, 0, 0, 0, 0, 0, 0, si, pi)) ExitProcess(7703);
    WaitForSingleObject(pi[0], 30000);
    /* The game is an orphan now: its window comes, then goes with it. */
    U start = GetTickCount();
    while (!FindWindowA("DDLEAKGAME", 0) && GetTickCount() - start < 30000) pump(50);
    number("DDLEAK game_seen", FindWindowA("DDLEAKGAME", 0) ? 1 : 0);
    start = GetTickCount();
    while (FindWindowA("DDLEAKGAME", 0) && GetTickCount() - start < 60000) pump(50);
    number("DDLEAK game_gone", FindWindowA("DDLEAKGAME", 0) ? 0 : 1);
    pump(3000);   /* DDHELP cleans up after the game */
    U info[4] = {0, 0, 0, 0};
    number("DDLEAK brush_alive", GetObjectA(brush, 12, info) ? 1 : 0);
    number("DDLEAK brush_same", GetSysColorBrush(13) == brush ? 1 : 0);
    SetForegroundWindow(hwnd);
    void *back = 0;
    number("DDLEAK second", take_screen(hwnd, &back));
    OutputDebugStringA("DDLEAK done");
    for (;;) pump(1000);
}
