/* SPDX-License-Identifier: GPL-2.0-or-later */
/* USER32 and GDI32 calls of other threads and of another process while the
 * DirectDraw primary surface is locked. The main thread locks the primary
 * surface of the original DDRAW.DLL (DDSCL_NORMAL) and holds it 300 ms. Under
 * Windows 98 DDRAW takes the Win16Mutex for that time and every other thread
 * stops in USER and GDI. The port locks only the pixels of the screen,
 * so a worker thread with its own window
 *
 * calls SetRect and GetSysColor, which never wait;
 * moves its window with SetWindowPos, which returns at once ("window");
 * draws with PatBlt on its window DC, which waits for the Unlock ("screen");
 * reads the screen with BitBlt into a memory DC, which waits ("read");
 * draws with PatBlt on a memory DC, which never waits ("memdc").
 *
 * The holder draws on the screen itself and does not wait. A round with a
 * system-memory surface locked with DDLOCK_NOSYSLOCK holds nothing back
 * ("plain"). When the holder sends a message to the worker waiting at its
 * PatBlt, the holder's send wait lets the worker draw and answer ("send"). A
 * holder that locks and unlocks frame after frame hands the screen to the
 * waiting worker at every Unlock ("handoff"). A second process that draws on
 * the screen waits as well, its other calls do not ("process").
 *
 * The fixture prints W16U lines; tools/test_ddraw_win16lock_user_guest.py
 * checks them. Started with "child" on its command line it is the second
 * process. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U LoadLibraryA(const char *);
API U GetProcAddress(U, const char *);
API void OutputDebugStringA(const char *);
API void ExitProcess(U);
API U GetTickCount(void);
API void Sleep(U);
API U CreateThread(U, U, U(__stdcall *)(U), U, U, U *);
API U CreateEventA(U, U, U, const char *);
API U SetEvent(U);
API U WaitForSingleObject(U, U);
API U GetModuleHandleA(const char *);
API const char *GetCommandLineA(void);
API U CreateProcessA(const char *, char *, U, U, U, U, U, U, void *, void *);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
API U DefWindowProcA(U, U, U, U);
API U SetWindowPos(U, U, int, int, int, int, U);
API U SetRect(void *, int, int, int, int);
API U GetSysColor(int);
API U SendMessageA(U, U, U, U);
API U MsgWaitForMultipleObjects(U, const U *, U, U, U);
API U PeekMessageA(void *, U, U, U, U);
API U DispatchMessageA(const void *);
API U GetDC(U);
API U ReleaseDC(U, U);
API U PatBlt(U, int, int, int, int, U);
API U BitBlt(U, int, int, int, int, U, int, int, U);
API U CreateCompatibleDC(U);
API U CreateCompatibleBitmap(U, int, int);
API U SelectObject(U, U);

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
    const char *prefix = "W16U ";
    while (prefix[i]) line[at++] = prefix[i++];
    i = 0;
    while (label[i]) line[at++] = label[i++];
    line[at++] = '=';
    for (i = 0; i < 8; ++i) line[at++] = digits[(value >> (28 - i * 4)) & 15];
    line[at] = 0;
    OutputDebugStringA(line);
}

enum { ActionWindow = 1, ActionScreen, ActionRead, ActionMemoryDc, ActionLoop, ActionQuit };
enum { LoopDraws = 20 };
static U go_event, done_event, ready_event, action, serve, worker_window, memory_dc;
static volatile U pure_tick, action_tick, answered, loop_draws;

static U __stdcall worker_procedure(U hwnd, U message, U wp, U lp)
{
    if (message == 0x0401) { answered = GetTickCount(); return 0x1234; }
    return DefWindowProcA(hwnd, message, wp, lp);
}

/* Serves sent messages until the holder's message has been answered. */
static void serve_until_answered(void)
{
    U msg[7];
    for (U n = 0; !answered && n < 100; ++n)
    {
        MsgWaitForMultipleObjects(0, 0, 0, 50, 0x4ff);
        while (PeekMessageA(msg, 0, 0, 0, 1)) DispatchMessageA(msg);
    }
}

static U __stdcall worker(U unused)
{
    (void)unused;
    struct { U style, proc; int extra, window; U instance, icon, cursor, brush; const char *menu, *name; } wc = {0};
    wc.proc = (U)worker_procedure;
    wc.instance = GetModuleHandleA(0);
    wc.name = "W16UWorker";
    RegisterClassA(&wc);
    worker_window = CreateWindowExA(0, "W16UWorker", "W16U worker", 0x10000000u, 300, 10, 120, 80, 0, 0, 0, 0);
    const U screen = GetDC(0);
    memory_dc = CreateCompatibleDC(screen);
    SelectObject(memory_dc, CreateCompatibleBitmap(screen, 32, 32));
    ReleaseDC(0, screen);
    SetEvent(ready_event);
    for (;;)
    {
        WaitForSingleObject(go_event, 10000);
        if (action == ActionQuit) break;
        int rect[4];
        SetRect(rect, 1, 2, 3, 4);
        GetSysColor(5);
        pure_tick = GetTickCount();
        if (action == ActionWindow)
            SetWindowPos(worker_window, 0, 300 + (int)(pure_tick & 7), 10, 120, 80, 0x14); /* NOZORDER|NOACTIVATE */
        else if (action == ActionScreen)
        {
            const U dc = GetDC(worker_window);
            PatBlt(dc, 0, 0, 16, 16, 0x00000042); /* BLACKNESS */
            ReleaseDC(worker_window, dc);
        }
        else if (action == ActionRead)
        {
            const U dc = GetDC(0);
            BitBlt(memory_dc, 0, 0, 16, 16, dc, 10, 10, 0x00cc0020); /* SRCCOPY */
            ReleaseDC(0, dc);
        }
        else if (action == ActionLoop)
        {
            const U dc = GetDC(worker_window);
            for (U k = 0; k < LoopDraws; ++k)
            {
                PatBlt(dc, (int)k, 0, 1, 16, 0x00ff0062); /* WHITENESS */
                loop_draws = k + 1;
            }
            ReleaseDC(worker_window, dc);
        }
        else
            PatBlt(memory_dc, 0, 0, 16, 16, 0x00000042);
        action_tick = GetTickCount();
        if (serve) serve_until_answered();
        SetEvent(done_event);
    }
    return 0;
}

static void **surface_primary, **surface_plain;
static U main_window;

static void label_number(const char *name, const char *suffix, U value)
{
    char label[40];
    U at = 0, k = 0;
    while (name[at]) { label[at] = name[at]; ++at; }
    while (suffix[k]) { label[at + k] = suffix[k]; ++k; }
    label[at + k] = 0;
    number(label, value);
}

/* One round: lock, let the worker act, hold, unlock. */
static void round_trip(const char *name, void **surface, U flags, U act, int send)
{
    U desc[27] = {0};
    desc[0] = 108;
    pure_tick = action_tick = answered = 0;
    action = act;
    serve = send;
    U result = ((M5)((void **)*surface)[25])(surface, 0, (U)desc, flags, 0);
    const U locked = GetTickCount();
    SetEvent(go_event);
    /* The holder calls on and draws on the screen itself: it never waits. */
    SetWindowPos(main_window, 0, 10, 10, 200 + (int)(locked & 3), 100, 0x14);
    const U own = GetDC(0);
    PatBlt(own, 12, 12, 4, 4, 0x00ff0062);
    ReleaseDC(0, own);
    const U holder_tick = GetTickCount();
    Sleep(150);
    U reply = 0;
    if (send) reply = SendMessageA(worker_window, 0x0401, 0, 0);
    Sleep(150);
    const U before = GetTickCount();
    ((M2)((void **)*surface)[32])(surface, 0);
    WaitForSingleObject(done_event, 5000);
    label_number(name, "_lock", result);
    label_number(name, "_locked", locked);
    label_number(name, "_holder", holder_tick);
    label_number(name, "_pure", pure_tick);
    label_number(name, "_action", action_tick);
    label_number(name, "_unlock", before);
    if (send) { label_number(name, "_reply", reply); label_number(name, "_answered", answered); }
}

/* A game's frames: lock, write, unlock, as fast as it can, while the worker
 * draws LoopDraws times on the screen. Every Unlock hands the screen to the
 * worker, and the next Lock waits until it has drawn. */
static void handoff_round(void)
{
    U desc[27] = {0};
    U frames = 0;
    loop_draws = 0;
    action = ActionLoop;
    serve = 0;
    const U start = GetTickCount();
    SetEvent(go_event);
    while (loop_draws < LoopDraws && frames < 100000 && GetTickCount() - start < 10000)
    {
        desc[0] = 108;
        if (((M5)((void **)*surface_primary)[25])(surface_primary, 0, (U)desc, 1, 0)) break;
        volatile U *bits = (volatile U *)desc[9]; /* lpSurface */
        for (U k = 0; bits && k < 2000; ++k) bits[k & 63] = k;
        ((M2)((void **)*surface_primary)[32])(surface_primary, 0);
        ++frames;
    }
    const U end = GetTickCount();
    WaitForSingleObject(done_event, 5000);
    number("handoff_frames", frames);
    number("handoff_draws", loop_draws);
    number("handoff_ms", end - start);
}

/* The second process: waits for the go, then a call that does not draw and
 * one that draws on the screen. A second thread of it locks its own primary
 * surface meanwhile. */
static void **child_primary;
static volatile U child_lock_result = 0xdeadu, child_lock_tick;
static U __stdcall child_locker(U unused)
{
    (void)unused;
    /* The parent holds its Lock of the primary surface: under Win98 this
     * Lock waits for the Win16Mutex, in the port for DDRAW's global critical
     * section, which DDRAW keeps from the Lock of the primary surface to its
     * Unlock. */
    U desc[27] = {0};
    desc[0] = 108;
    const U result = ((M5)((void **)*child_primary)[25])(child_primary, 0, (U)desc, 1, 0);
    child_lock_tick = GetTickCount();
    if (!result) ((M2)((void **)*child_primary)[32])(child_primary, 0);
    child_lock_result = result;
    return 0;
}

static void child(void)
{
    const U go = CreateEventA(0, 1, 0, "W16UGo");
    const U ready = CreateEventA(0, 1, 0, "W16UReady");
    /* Its own DirectDraw object and primary surface, made before the parent
     * locks: DDRAW's shared data and its global critical section belong to
     * both processes. No window: a visible
     * one would send the parent, which waits in WaitForSingleObject, its
     * activation and wait for the answer. */
    const U library = LoadLibraryA("DDRAW.DLL");
    const Create create = (Create)GetProcAddress(library, "DirectDrawCreate");
    void **ddraw = 0;
    number("child_create", create ? create(0, (void **)&ddraw, 0) : 0xdeadu);
    if (ddraw)
    {
        number("child_cooperative", ((M3)((void **)*ddraw)[20])(ddraw, 0, 8)); /* DDSCL_NORMAL */
        U desc[27] = {0};
        desc[0] = 108;
        desc[1] = 1;              /* DDSD_CAPS */
        desc[26] = 0x200;         /* DDSCAPS_PRIMARYSURFACE */
        number("child_surface", ((M4)((void **)*ddraw)[6])(ddraw, (U)desc, (U)&child_primary, 0));
    }
    SetEvent(ready);
    WaitForSingleObject(go, 10000);
    U id = 0, locker = 0;
    if (child_primary) locker = CreateThread(0, 0, child_locker, 0, 0, &id);
    GetSysColor(5);
    const U pure = GetTickCount();
    const U dc = GetDC(0);
    PatBlt(dc, 40, 40, 8, 8, 0x00000042);
    const U drawn = GetTickCount();
    ReleaseDC(0, dc);
    if (locker) WaitForSingleObject(locker, 10000);
    number("child_pure", pure);
    number("child_action", drawn);
    number("child_ddlock", child_lock_result);
    number("child_ddlock_tick", child_lock_tick);
    if (child_primary) ((M1)((void **)*child_primary)[2])(child_primary);
    if (ddraw) ((M1)((void **)*ddraw)[2])(ddraw);
    ExitProcess(0);
}

static void process_round(void)
{
    const U go = CreateEventA(0, 1, 0, "W16UGo");
    const U ready = CreateEventA(0, 1, 0, "W16UReady");
    U startup[17] = {0}, information[4] = {0};
    char command[] = "W16U.EXE child";
    startup[0] = sizeof startup;
    number("process_create", CreateProcessA("C:\\W16U.EXE", command, 0, 0, 0, 0, 0, 0, startup, information));
    number("process_ready", WaitForSingleObject(ready, 10000));
    U desc[27] = {0};
    desc[0] = 108;
    number("process_lock", ((M5)((void **)*surface_primary)[25])(surface_primary, 0, (U)desc, 1, 0));
    SetEvent(go);
    Sleep(300);
    const U before = GetTickCount();
    ((M2)((void **)*surface_primary)[32])(surface_primary, 0);
    number("process_unlock", before);
    number("process_exit", WaitForSingleObject(information[0], 10000));
}

void mainCRTStartup(void)
{
    const char *command = GetCommandLineA();
    for (U i = 0; command[i]; ++i)
        if (command[i] == 'c' && command[i + 1] == 'h' && command[i + 2] == 'i' && command[i + 3] == 'l' &&
            command[i + 4] == 'd')
            child();
    const U library = LoadLibraryA("DDRAW.DLL");
    const Create create = (Create)GetProcAddress(library, "DirectDrawCreate");
    void **ddraw = 0;
    number("create", create ? create(0, (void **)&ddraw, 0) : 0xdeadu);
    if (!ddraw) ExitProcess(1);
    main_window = CreateWindowExA(0, "STATIC", "W16U", 0x90000000u, 10, 10, 200, 100, 0, 0, 0, 0);
    number("cooperative", ((M3)((void **)*ddraw)[20])(ddraw, main_window, 8)); /* DDSCL_NORMAL */
    U desc[27] = {0};
    desc[0] = 108;
    desc[1] = 1;              /* DDSD_CAPS */
    desc[26] = 0x200;         /* DDSCAPS_PRIMARYSURFACE */
    number("surface", ((M4)((void **)*ddraw)[6])(ddraw, (U)desc, (U)&surface_primary, 0));
    U plain_desc[27] = {0};
    plain_desc[0] = 108;
    plain_desc[1] = 1 | 2 | 4;       /* DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH */
    plain_desc[2] = 32; plain_desc[3] = 32;
    plain_desc[26] = 0x40 | 0x800;   /* DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY */
    number("plain", ((M4)((void **)*ddraw)[6])(ddraw, (U)plain_desc, (U)&surface_plain, 0));
    if (!surface_primary || !surface_plain) ExitProcess(2);
    go_event = CreateEventA(0, 0, 0, 0);
    done_event = CreateEventA(0, 0, 0, 0);
    ready_event = CreateEventA(0, 1, 0, 0);
    U id;
    CreateThread(0, 0, worker, 0, 0, &id);
    WaitForSingleObject(ready_event, 5000);
    round_trip("window", surface_primary, 1, ActionWindow, 0);
    round_trip("screen", surface_primary, 1, ActionScreen, 0);
    round_trip("read", surface_primary, 1, ActionRead, 0);
    round_trip("memdc", surface_primary, 1, ActionMemoryDc, 0);
    round_trip("plain", surface_plain, 1 | 0x800, ActionScreen, 0);
    round_trip("send", surface_primary, 1, ActionScreen, 1);
    handoff_round();
    process_round();
    action = ActionQuit;
    SetEvent(go_event);
    ((M1)((void **)*surface_plain)[2])(surface_plain);
    ((M1)((void **)*surface_primary)[2])(surface_primary);
    ((M1)((void **)*ddraw)[2])(ddraw);
    OutputDebugStringA("W16U done");
    ExitProcess(0);
}
