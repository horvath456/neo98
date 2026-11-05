/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The Win16Lock while the DirectDraw primary surface is locked. One thread
 * locks the primary surface of the original DDRAW.DLL (DDSCL_NORMAL, no
 * DDLOCK_NOSYSLOCK), which takes the Win16Lock (KERNEL32 ordinal 97 on the
 * record of ordinal 93) until Unlock, and holds it 400 ms. A second thread
 * draws on the screen with GetDC(NULL) and PatBlt meanwhile. Under Win98 that
 * draw waits for the Unlock; with DDLOCK_NOSYSLOCK it does not. The fixture
 * prints W16L lines; tools/test_ddraw_win16lock_guest.py checks them. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U LoadLibraryA(const char *);
API U GetProcAddress(U, const char *);
API void OutputDebugStringA(const char *);
API void ExitProcess(U);
API U GetTickCount(void);
API void Sleep(U);
API U CreateThread(U, U, U(__stdcall *)(U), U, U, U *);
API U CreateEventA(U, U, U, U);
API U SetEvent(U);
API U WaitForSingleObject(U, U);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
API U GetDC(U);
API U ReleaseDC(U, U);
API U PatBlt(U, int, int, int, int, U);

typedef U(__stdcall *Create)(void *, void **, void *);
typedef U(__stdcall *M1)(void *);
typedef U(__stdcall *M2)(void *, U);
typedef U(__stdcall *M3)(void *, U, U);
typedef U(__stdcall *M4)(void *, U, U, U);
typedef U(__stdcall *M5)(void *, U, U, U, U);

static char line[256];
static void number(const char *label, U value)
{
    static const char digits[] = "0123456789abcdef";
    U at = 0, i = 0;
    const char *prefix = "W16L ";
    while (prefix[i]) line[at++] = prefix[i++];
    i = 0;
    while (label[i]) line[at++] = label[i++];
    line[at++] = '=';
    for (i = 0; i < 8; ++i) line[at++] = digits[(value >> (28 - i * 4)) & 15];
    line[at] = 0;
    OutputDebugStringA(line);
}

static U locked_event, drawn_tick, drawer_done;
static U __stdcall drawer(U unused)
{
    (void)unused;
    WaitForSingleObject(locked_event, 5000);
    const U dc = GetDC(0);
    PatBlt(dc, 10, 10, 20, 20, 0x00000042); /* BLACKNESS */
    drawn_tick = GetTickCount();
    ReleaseDC(0, dc);
    SetEvent(drawer_done);
    return 0;
}

static U round_trip(void **surface, U flags, const char *label_before, const char *label_after)
{
    U id;
    U desc[27] = {0};
    locked_event = CreateEventA(0, 0, 0, 0);
    drawer_done = CreateEventA(0, 0, 0, 0);
    drawn_tick = 0;
    CreateThread(0, 0, drawer, 0, 0, &id);
    desc[0] = 108;
    U result = ((M5)((void **)*surface)[25])(surface, 0, (U)desc, flags, 0);
    number("lock", result);
    SetEvent(locked_event);
    Sleep(400);
    const U before = GetTickCount();
    ((M2)((void **)*surface)[32])(surface, 0);
    WaitForSingleObject(drawer_done, 5000);
    number(label_before, before);
    number(label_after, drawn_tick);
    return drawn_tick >= before;
}

void mainCRTStartup(void)
{
    const U library = LoadLibraryA("DDRAW.DLL");
    const Create create = (Create)GetProcAddress(library, "DirectDrawCreate");
    void **ddraw = 0;
    number("create", create ? create(0, (void **)&ddraw, 0) : 0xdeadu);
    if (!ddraw) ExitProcess(1);
    const U hwnd = CreateWindowExA(0, "STATIC", "W16L", 0x90000000u, 0, 0, 200, 100, 0, 0, 0, 0);
    number("cooperative", ((M3)((void **)*ddraw)[20])(ddraw, hwnd, 8)); /* DDSCL_NORMAL */
    U desc[27] = {0};
    desc[0] = 108;
    desc[1] = 1;              /* DDSD_CAPS */
    desc[26] = 0x200;         /* DDSCAPS_PRIMARYSURFACE */
    void **surface = 0, **plain = 0;
    number("surface", ((M4)((void **)*ddraw)[6])(ddraw, (U)desc, (U)&surface, 0));
    if (!surface) ExitProcess(2);
    /* DDLOCK_WAIT on the primary: the Win16Lock is held (DDRAW ignores
     * DDLOCK_NOSYSLOCK for the primary), the drawer waits for the Unlock. */
    number("waited", round_trip(surface, 1, "unlock_tick", "drawn_tick"));
    /* A surface in system memory, locked with DDLOCK_NOSYSLOCK: nothing
     * holds the drawer back. */
    U plain_desc[27] = {0};
    plain_desc[0] = 108;
    plain_desc[1] = 1 | 2 | 4;       /* DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH */
    plain_desc[2] = 32; plain_desc[3] = 32;
    plain_desc[26] = 0x40 | 0x800;   /* DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY */
    number("plain", ((M4)((void **)*ddraw)[6])(ddraw, (U)plain_desc, (U)&plain, 0));
    if (!plain) ExitProcess(3);
    number("plain_waited", round_trip(plain, 1 | 0x800, "plain_unlock_tick", "plain_drawn_tick"));
    ((M1)((void **)*plain)[2])(plain);
    ((M1)((void **)*surface)[2])(surface);
    ((M1)((void **)*ddraw)[2])(ddraw);
    OutputDebugStringA("W16L done");
    ExitProcess(0);
}
