/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Guest for every Win32 thread has its own
   activation queue, and only one queue is the foreground (USER ds:0AA6).
   1. The main thread shows W1; the process still holds the initial activation permission, so W1 becomes the foreground window.
   2. A second thread creates the hidden window W2 and sets the focus to it,
      as the dialog manager does for scanregw's hidden dialog. The activation
      stays in that thread's queue (no permission left), so W2 is the
      thread's active window and focus, while the foreground stays W1.
   3. A child process shows its first window; a new process may take the
      foreground once, so its window becomes the foreground.
   Exit code zero means every check held; codes 4760..4779. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U GetModuleHandleA(const char *);
API U CreateThread(U, U, U (__stdcall *)(U), U, U, U *);
API U WaitForSingleObject(U, U);
API U CreateProcessA(const char *, char *, U, U, U, U, U, const char *, U *, U *);
API void Sleep(U);
API U TerminateProcess(U, U);
API U RegisterClassA(const U *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U ShowWindow(U, int);
API U SetFocus(U);
API U GetFocus(void);
API U GetActiveWindow(void);
API U GetForegroundWindow(void);
API U DefWindowProcA(U, U, U, U);
API U PeekMessageA(U *, U, U, U, U);
API U DispatchMessageA(const U *);
API U FindWindowA(const char *, const char *);

static U failure;
static void check(int ok, U code)
{
    if (!ok && !failure)
        failure = code;
}
static U __stdcall procedure(U h, U m, U w, U l) { return DefWindowProcA(h, m, w, l); }
static void pump(U rounds)
{
    U msg[7];
    for (U i = 0; i < rounds; ++i)
    {
        while (PeekMessageA(msg, 0, 0, 0, 1))
            DispatchMessageA(msg);
        Sleep(10);
    }
}
static U w1, w2, thread_active, thread_focus, foreground_seen;
static U __stdcall worker(U unused)
{
    (void)unused;
    w2 = CreateWindowExA(0, "QueueTest", "hidden", 0x00c00000, 0, 0, 100, 100, 0, 0, GetModuleHandleA(0), 0);
    SetFocus(w2);
    thread_active = GetActiveWindow();
    thread_focus = GetFocus();
    foreground_seen = GetForegroundWindow();
    pump(5);
    return 0;
}
void mainCRTStartup(void)
{
    U wc[10] = {0};
    wc[1] = (U) procedure;
    wc[4] = GetModuleHandleA(0);
    wc[9] = (U) "QueueTest";
    check(RegisterClassA(wc) != 0, 4760);
    w1 = CreateWindowExA(0, "QueueTest", "shown", 0x00cf0000, 10, 10, 200, 150, 0, 0, GetModuleHandleA(0), 0);
    check(w1 != 0, 4761);
    ShowWindow(w1, 1);
    pump(3);
    check(GetForegroundWindow() == w1, 4762);
    check(GetActiveWindow() == w1, 4763);

    U id = 0;
    const U thread = CreateThread(0, 0, worker, 0, 0, &id);
    check(thread != 0, 4764);
    while (WaitForSingleObject(thread, 0))
        pump(1);
    check(w2 != 0, 4765);
    check(thread_active == w2, 4766);
    check(thread_focus == w2, 4767);
    check(foreground_seen == w1, 4768);
    check(GetForegroundWindow() == w1, 4769);
    check(GetActiveWindow() == w1, 4770);

    U startup[17] = {68};
    U information[4] = {0};
    char command[] = "D:\\QCHILD.EXE";
    check(CreateProcessA(0, command, 0, 0, 0, 0, 0, 0, startup, information) != 0, 4771);
    U child = 0;
    for (U i = 0; i < 300 && !child; ++i)
    {
        pump(1);
        child = FindWindowA("QueueChild", 0);
    }
    check(child != 0, 4772);
    pump(10);
    check(GetForegroundWindow() == child, 4773);
    check(GetActiveWindow() != child, 4774);
    /* 4. The child ends while it owns the foreground: the foreground goes
          back to a window of another process, W1. */
    TerminateProcess(information[0], 0);
    pump(10);
    check(GetForegroundWindow() == w1, 4775);
    ExitProcess(failure);
}
