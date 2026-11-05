/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Painting belongs to the window's owning thread. Thread A owns the window and owner-drawn button; thread B invalidates it and enters a kernel wait. Verify callback thread identity and that its exception registration resides on A's stack. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
typedef struct { U hwnd, message, wp, lp, time; int x, y; } Message;
typedef struct { int left, top, right, bottom; } Rect;
typedef struct {
    U style; U(__stdcall *proc)(U, U, U, U); int extra, window_extra;
    U instance, icon, cursor, background; const char *menu, *name;
} WindowClass;
API U ExitProcess(U);
API U GetCurrentThreadId(void);
API U CreateThread(U, U, U (__stdcall *)(void *), void *, U, U *);
API U WaitForSingleObject(U, U);
API U CloseHandle(U);
API void Sleep(U);
API U CreateEventA(U, U, U, const char *);
API U SetEvent(U);
API U GetModuleHandleA(const char *);
API U TlsAlloc(void);
API U TlsSetValue(U, void *);
API void *TlsGetValue(U);
API U RegisterClassA(const WindowClass *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U DestroyWindow(U);
API U ShowWindow(U, U);
API U InvalidateRect(U, const Rect *, U);
API U PeekMessageA(Message *, U, U, U, U);
API U GetMessageA(Message *, U, U, U);
API U TranslateMessage(const Message *);
API U DispatchMessageA(const Message *);
API void PostQuitMessage(U);
API U GetDlgItem(U, int);
API U SendMessageA(U, U, U, U);

/* An x86 stack exception registration: previous-record pointer followed by handler. */
typedef struct ExceptionRecord { struct ExceptionRecord *previous; U handler; } ExceptionRecord;
static U __stdcall never(U a, U b, U c, U d) { (void)a; (void)b; (void)c; (void)d; return 1; }
static U read_seh(void)
{
    U value;
    __asm__ __volatile__("movl %%fs:0, %0" : "=r"(value));
    return value;
}
static void write_seh(U value) { __asm__ __volatile__("movl %0, %%fs:0" : : "r"(value)); }

static U failures, checks;
static void check(int okay, U code)
{
    ++checks;
    if (!okay && !failures) failures = code;
}

static U owner_thread, worker_thread, draw_calls, colour_calls;
static U draw_thread, colour_thread, draw_frame, colour_frame, owner_frame, seh_frame;
static U ready, done;
static U cursor_tls, cursor_owner, cursor_calls;

static U __stdcall window_proc(U h, U m, U w, U l)
{
    if (m == 0x84 && cursor_owner) /* WM_NCHITTEST from host pointer motion */
    {
        ++cursor_calls;
        check(GetCurrentThreadId() == cursor_owner, 26021);
        check(TlsGetValue(cursor_tls) == (void *)0xc0ffee, 26022);
        return 1; /* HTCLIENT */
    }
    if (m == 0x20 && cursor_owner) /* WM_SETCURSOR */
    {
        ++cursor_calls;
        check(GetCurrentThreadId() == cursor_owner, 26023);
        check(TlsGetValue(cursor_tls) == (void *)0xc0ffee, 26024);
        check(SetEvent(done) != 0, 26025);
        PostQuitMessage(0);
        return 1;
    }
    if (m == 0x2b) /* WM_DRAWITEM */
    {
        ExceptionRecord record;
        record.previous = (ExceptionRecord *)read_seh();
        record.handler = (U)never;
        write_seh((U)&record);
        ++draw_calls;
        draw_thread = GetCurrentThreadId();
        draw_frame = (U)&record;
        seh_frame = read_seh();
        write_seh((U)record.previous);
        return 1;
    }
    if (m == 0x135) /* WM_CTLCOLORBTN */
    {
        U marker = 0;
        ++colour_calls;
        colour_thread = GetCurrentThreadId();
        colour_frame = (U)&marker;
        return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

static U __stdcall cursor_worker(void *parameter)
{
    U instance = GetModuleHandleA(0);
    cursor_owner = GetCurrentThreadId();
    check(TlsSetValue(cursor_tls, (void *)0xc0ffee) != 0, 26026);
    U window = CreateWindowExA(0, "NEO98CompositionThread", "Cursor thread binding",
                               0x90000000, 400, 40, 220, 160, 0, 0, instance, 0);
    check(window != 0, 26027);
    ShowWindow(window, 1);
    check(SetEvent(ready) != 0, 26028);
    Message message;
    while (GetMessageA(&message, 0, 0, 0)) { TranslateMessage(&message); DispatchMessageA(&message); }
    check(DestroyWindow(window) != 0, 26029);
    return 0;
}

static U __stdcall worker(void *parameter)
{
    U control = *(U *)parameter;
    /* A foreign thread requests repainting, then sleeps. */
    check(InvalidateRect(control, 0, 1) != 0, 26001);
    Sleep(40);
    check(SetEvent(done) != 0, 26002);
    return 0;
}

static int near_by(U a, U b) { return (a > b ? a - b : b - a) < 0x100000u; }

void run_composition_thread_test(void)
{
    U instance = GetModuleHandleA(0), marker = 0, parent, control, handle, id = 0;
    WindowClass klass;
    Message message;
    unsigned n;
    owner_thread = GetCurrentThreadId();
    owner_frame = (U)&marker;
    klass.style = 0; klass.proc = window_proc; klass.extra = 0; klass.window_extra = 0;
    klass.instance = instance; klass.icon = 0; klass.cursor = 0; klass.background = 0;
    klass.menu = 0; klass.name = "NEO98CompositionThread";
    check(RegisterClassA(&klass) != 0, 26003);
    parent = CreateWindowExA(0, "NEO98CompositionThread", "Composition thread binding",
                             0x90000000, 40, 40, 300, 200, 0, 0, instance, 0);
    check(parent != 0, 26004);
    control = CreateWindowExA(0, "BUTTON", "Owner drawn", 0x5000000b, 10, 10, 120, 40,
                              parent, 77, instance, 0);
    check(control != 0, 26005);
    check(GetDlgItem(parent, 77) == control, 26006);
    ShowWindow(parent, 1);
    done = CreateEventA(0, 1, 0, 0);
    ready = CreateEventA(0, 1, 0, 0);
    check(done != 0 && ready != 0, 26007);
    /* Paint first so counters measure only work triggered by the other thread. */
    for (n = 0; n < 8; ++n)
        while (PeekMessageA(&message, 0, 0, 0, 1)) { TranslateMessage(&message); DispatchMessageA(&message); }
    check(draw_calls != 0, 26008);
    check(draw_thread == owner_thread, 26009);
    draw_calls = 0; colour_calls = 0; draw_thread = 0; colour_thread = 0;
    handle = CreateThread(0, 0, worker, &control, 0, &id);
    check(handle != 0, 26010);
    worker_thread = id;
    check(worker_thread != owner_thread, 26011);
    /* Only the owning thread may paint while retrieving its messages. */
    while (WaitForSingleObject(done, 0) != 0)
        while (PeekMessageA(&message, 0, 0, 0, 1)) { TranslateMessage(&message); DispatchMessageA(&message); }
    check(WaitForSingleObject(handle, 5000) == 0, 26012);
    check(CloseHandle(handle) != 0, 26013);
    /* The callback must run on the window's owning thread. */
    check(draw_calls != 0, 26014);
    check(draw_thread == owner_thread, 26015);
    /* The handler's exception registration is on thread A's stack. */
    check(near_by(draw_frame, owner_frame), 26016);
    check(seh_frame == draw_frame, 26017);
    check(read_seh() != draw_frame, 26018);
    /* Repeat for a normal button's color message. */
    check(CloseHandle(done) != 0 && CloseHandle(ready) != 0, 26019);
    check(DestroyWindow(parent) != 0, 26020);

    /* PointerMoved originates on the host-facing main fiber. Its synchronous
       frame messages still belong to the worker that owns the target window. */
    done = CreateEventA(0, 1, 0, 0);
    ready = CreateEventA(0, 1, 0, 0);
    cursor_tls = TlsAlloc();
    check(cursor_tls != 0xffffffff && done && ready, 26030);
    handle = CreateThread(0, 0, cursor_worker, 0, 0, &id);
    check(handle != 0, 26031);
    check(WaitForSingleObject(ready, 5000) == 0, 26032);
    while (WaitForSingleObject(done, 0) != 0)
        while (PeekMessageA(&message, 0, 0, 0, 1)) { TranslateMessage(&message); DispatchMessageA(&message); }
    check(WaitForSingleObject(done, 0) == 0, 26033);
    check(WaitForSingleObject(handle, 5000) == 0 && CloseHandle(handle), 26034);
    check(cursor_calls >= 2, 26035);
    check(CloseHandle(done) != 0 && CloseHandle(ready) != 0, 26036);
    ExitProcess(failures);
}

#ifdef COMPOSITION_THREAD_STANDALONE
void mainCRTStartup(void) { run_composition_thread_test(); }
#endif
