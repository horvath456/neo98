/* SPDX-License-Identifier: GPL-2.0-or-later
 * Own x86 test program for a thread routine that returns while the
 * host still runs it inside a nested loop.
 *
 * An exception handler that jumps out of RaiseException never comes back to
 * the dispatcher; Win98 simply leaves the kernel frame on the stack. The port
 * runs the handler in a loop of its own (Application::Call), and that loop
 * keeps running the guest after the jump. When the thread routine returns
 * afterwards, it reaches its exit address from inside that loop. The same
 * holds for a window procedure called by a native CallWindowProcA that the
 * handler jumps out of. Win98 ends the thread in every case: the exit code is
 * the routine's EAX, DLL_THREAD_DETACH reaches every DLL, and then the thread
 * handle is signaled. The initial thread ends the process with its EAX. A
 * window procedure that catches its own exception returns through the mark
 * of the outer call while the handler's loop is still open; that loop has to
 * hand the return on.
 *
 * Exit codes below 100 of the process name the failed check; 0x2259 is the
 * pass, returned by the initial thread from inside a caught exception. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U CreateThread(void *, U, U(__stdcall *)(void *), void *, U, U *);
API U WaitForSingleObject(U, U);
API U GetExitCodeThread(U, U *);
API U CloseHandle(U);
API void RaiseException(U, U, U, const U *);
API void RtlUnwind(void *, void *, void *, U);
API void Sleep(U);
API U LoadLibraryA(const char *);
API void *GetProcAddress(U, const char *);
API U CallWindowProcA(void *, U, U, U, U);

struct Frame
{
    struct Frame *next;
    void *handler;
    U resume, esp;
};
/* tools/thread_exit_probe.S: guarded links the frame, runs body(argument)
 * and answers 0 when the body returned, 1 when the handler leapt back. */
extern U guarded(struct Frame *frame, void (*body)(U), U argument);
extern void leap(struct Frame *frame);

static void check(int value, U code)
{
    if (!value)
        ExitProcess(code);
}
/* The shape of MSVC's _except_handler3 when a filter accepts: unwind the
 * frames above, leave the guarded block and continue behind it. The handler
 * never returns to the dispatcher. */
static int __cdecl handler(U *record, struct Frame *frame, void *context, void *dispatcher)
{
    (void)context;
    (void)dispatcher;
    if (record[1] & 6)
        return 1;
    RtlUnwind(frame, 0, record, 0);
    __asm__ volatile("movl %0,%%fs:0" ::"r"(frame->next) : "memory");
    leap(frame);
    return 1;
}
static void raise_body(U code) { RaiseException(code, 0, 0, 0); }
static U __stdcall procedure(U window, U message, U wp, U lp)
{
    (void)window;
    (void)lp;
    if (message == 0x400)
        RaiseException(wp, 0, 0, 0);
    return 0;
}
static void window_body(U code) { CallWindowProcA((void *)procedure, 0, 0x400, code, 0); }
static U caught(void (*body)(U), U code)
{
    struct Frame frame = {0, (void *)handler, 0, 0};
    return guarded(&frame, body, code);
}
/* Three caught exceptions leave three handler loops behind, the depth of the
 * Explorer thread during queue shutdown. */
static U __stdcall raising_thread(void *parameter)
{
    (void)parameter;
    for (U round = 0; round < 3; ++round)
        if (caught(raise_body, 0xe0002259) != 1)
            return 0xbad1;
    Sleep(0);
    return 0x2259a;
}
/* The handler jumps out of the window procedure and out of the native
 * CallWindowProcA that called it. */
static U __stdcall window_thread(void *parameter)
{
    (void)parameter;
    if (caught(window_body, 0xe000225b) != 1)
        return 0xbad2;
    Sleep(0);
    return 0x2259b;
}
/* The window procedure catches its own exception and returns normally. The
 * return goes through the mark of the call CallWindowProcA made, while the
 * loop of the handler is still open above it; that loop must hand the return
 * on instead of taking it as the handler's answer. */
static U __stdcall catching_procedure(U window, U message, U wp, U lp)
{
    (void)window;
    (void)lp;
    if (message != 0x401)
        return 0;
    if (caught(raise_body, wp) != 1)
        return 0xbad3;
    return wp ^ 0x5a5a;
}
static U __stdcall catching_thread(void *parameter)
{
    (void)parameter;
    for (U round = 0; round < 3; ++round)
    {
        const U code = 0xe000225d + round;
        if (CallWindowProcA((void *)catching_procedure, 0, 0x401, code, 0) != (code ^ 0x5a5a))
            return 0xbad4;
    }
    return 0x2259d;
}
static U __stdcall plain_thread(void *parameter)
{
    (void)parameter;
    Sleep(0);
    return 0x2259c;
}
U mainCRTStartup(void)
{
    /* A DLL of its own on drive D: counts its notifications. */
    U library = LoadLibraryA("D:\\TXEXIT.DLL");
    check(library != 0, 1);
    U(__cdecl * notices)(U) = (U(__cdecl *)(U))GetProcAddress(library, "ThreadNotices");
    check(notices != 0, 2);
    check(notices(1) == 1, 3);
    U(__stdcall * routines[4])(void *) = {raising_thread, window_thread, plain_thread, catching_thread};
    const U expected[4] = {0x2259a, 0x2259b, 0x2259c, 0x2259d};
    U handles[4], id;
    for (U i = 0; i < 4; ++i)
    {
        handles[i] = CreateThread(0, 0, routines[i], 0, 0, &id);
        check(handles[i] != 0, 4 + i);
    }
    for (U i = 0; i < 4; ++i)
    {
        U code = 0;
        check(WaitForSingleObject(handles[i], 10000) == 0, 10 + i);
        check(GetExitCodeThread(handles[i], &code), 20 + i);
        check(code == expected[i], 30 + i);
        check(CloseHandle(handles[i]), 40 + i);
    }
    check(notices(2) == 4, 50);
    check(notices(3) == 4, 51);
    /* The initial thread ends the process from inside a handler loop too. */
    check(caught(raise_body, 0xe000225c) == 1, 60);
    return 0x2259;
}
