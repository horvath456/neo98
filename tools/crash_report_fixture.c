/* SPDX-License-Identifier: GPL-2.0-or-later
 * Real x86 PE test program for the crash report. It makes a short, known run of
 * API calls and then stops in one of the three ways the report has to describe:
 * a loop without any API call, a wait that is never signalled, and a call into
 * an import the native lower edge does not have. A fourth mode builds a known
 * call chain with function pointers next to it on the stack. The
 * command line picks one.
 * */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API const char *GetCommandLineA(void);
API U GetTickCount(void);
API U GetCurrentThreadId(void);
API void SetLastError(U);
API U IsBadCodePtr(const void *);
API U GetLastError(void);
API U CreateEventA(void *, U, U, const char *);
API U WaitForSingleObject(U, U);
API void Sleep(U);
/* Deliberately not an export of the native KERNEL32: the loader binds it to a
 * gate with an unknown argument count, and the first call stops the process. */
API U Neo98NotImplemented(void);

static volatile U spin;

/* Five calls in a fixed order. The report must show exactly these, oldest
 * first, before whatever the chosen stop adds. */
static U preamble(void)
{
    U ticks = GetTickCount();
    U id = GetCurrentThreadId();
    SetLastError(0x1234);
    U error = GetLastError();
    Sleep(0);
    return ticks + id + error;
}

/* Mode "frames": three real frames under the endless wait, and three code
 * addresses on the stack next to them that no call put there. The report must
 * keep the frames and drop the three, so the fixture tells the host which
 * values they are through IsBadCodePtr. Not SetLastError: since 85388ad that
 * one runs as guest code in the thread block, as in Win98, and leaves no entry
 * in the call ring of the report. */
__declspec(noinline) static void level_one(U event);

__declspec(noinline) static void level_two(U event)
{
    void *volatile bait[3];
    bait[0] = (void *)&level_one;
    bait[1] = (void *)&preamble;
    bait[2] = (void *)((const char *)&preamble + 3);
    IsBadCodePtr(bait[0]);
    IsBadCodePtr(bait[1]);
    IsBadCodePtr(bait[2]);
    WaitForSingleObject(event, 0xffffffffu);
    spin = 1;
}
__declspec(noinline) static void level_one(U event)
{
    level_two(event);
    spin = 2;
}

int mainCRTStartup(void)
{
    const char *line = GetCommandLineA();
    U mode = 0;
    for (const char *p = line; p && *p; ++p)
        if (p[0] == 's' && p[1] == 'p') { mode = 1; break; }
        else if (p[0] == 'w' && p[1] == 'a') { mode = 2; break; }
        else if (p[0] == 'm' && p[1] == 'i') { mode = 3; break; }
        else if (p[0] == 'f' && p[1] == 'r') { mode = 4; break; }
    spin = preamble();
    if (mode == 1)
    {
        /* No API call ever again. The scheduler keeps handing out slices. */
        for (;;) spin = spin * 1664525u + 1013904223u;
    }
    if (mode == 2)
    {
        /* Manual-reset event that nothing signals: a wait without an end. */
        U event = CreateEventA(0, 1, 0, 0);
        WaitForSingleObject(event, 0xffffffffu);
        ExitProcess(2);
    }
    if (mode == 3)
    {
        Neo98NotImplemented();
        ExitProcess(3);
    }
    if (mode == 4)
    {
        level_one(CreateEventA(0, 1, 0, 0));
        ExitProcess(4);
    }
    ExitProcess(0);
    return 0;
}
