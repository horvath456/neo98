/* SPDX-License-Identifier: GPL-2.0-or-later */
/* ConvertToGlobalHandle on a process handle and on events of two processes. SHELL32 puts ConvertToGlobalHandle(OpenProcess(
   SYNCHRONIZE, FALSE, own id)) into a list in shared memory, and every
   process that sends a change notification asks each of those handles with
   WaitForSingleObject(handle, 0) whether its process still lives. The
   program starts itself a second time; the exit code names the first step
   that failed, 0 is success. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U GetModuleFileNameA(U, char *, U);
API U CreateProcessA(const char *, char *, void *, void *, U, U, void *, const char *, void *, void *);
API U CreateFileMappingA(U, void *, U, U, U, const char *);
API U OpenFileMappingA(U, U, const char *);
API U *MapViewOfFile(U, U, U, U, U);
API U CreateEventA(void *, U, U, const char *);
API U SetEvent(U);
API U WaitForSingleObject(U, U);
API U GetExitCodeProcess(U, U *);
API U GetCurrentProcessId(void);
API U OpenProcess(U, U, U);
API U ConvertToGlobalHandle(U);
API U CloseHandle(U);
enum { ParentProcess, ParentEvent, ChildProcess, ChildEvent };
enum { Timeout = 0x102, Synchronize = 0x100000 };
/* The process's own handle as SHELL32 makes it, and an event. */
static void Own(U *process, U *event, U base)
{
    U local;
    *process = ConvertToGlobalHandle(OpenProcess(Synchronize, 0, GetCurrentProcessId()));
    if (!*process) ExitProcess(base);
    if (WaitForSingleObject(*process, 0) != Timeout) ExitProcess(base + 1);
    local = CreateEventA(0, 1, 0, 0);
    if (!local) ExitProcess(base + 2);
    *event = ConvertToGlobalHandle(local);
    if (!*event || *event == local) ExitProcess(base + 3);
    /* the source is closed. */
    if (CloseHandle(local)) ExitProcess(base + 4);
    if (WaitForSingleObject(*event, 0) != Timeout) ExitProcess(base + 5);
}
static void Child(U *shared)
{
    U ready, go;
    /* The parent's handles answer here as they do there. */
    if (WaitForSingleObject(shared[ParentProcess], 0) != Timeout) ExitProcess(40);
    if (WaitForSingleObject(shared[ParentEvent], 0) != Timeout) ExitProcess(41);
    Own(&shared[ChildProcess], &shared[ChildEvent], 50);
    /* Two processes, two objects, two values. */
    if (shared[ChildEvent] == shared[ParentEvent] || shared[ChildProcess] == shared[ParentProcess]) ExitProcess(42);
    if (!SetEvent(shared[ChildEvent])) ExitProcess(43);
    if (WaitForSingleObject(shared[ParentEvent], 0) != Timeout) ExitProcess(44);
    ready = CreateEventA(0, 1, 0, "NEO98GlobalHandleReady");
    go = CreateEventA(0, 1, 0, "NEO98GlobalHandleGo");
    if (!ready || !go || !SetEvent(ready)) ExitProcess(45);
    if (WaitForSingleObject(go, 20000)) ExitProcess(46);
    ExitProcess(0);
}
void mainCRTStartup(void)
{
    char path[260];
    U startup[17] = {68}, process[4] = {0}, code = 99;
    U mapping = OpenFileMappingA(0xf001f, 0, "NEO98GlobalHandle");
    if (mapping) {
        U *shared = MapViewOfFile(mapping, 0xf001f, 0, 0, 0);
        if (!shared) ExitProcess(39);
        Child(shared);
    }
    mapping = CreateFileMappingA(0xffffffff, 0, 4, 0, 4096, "NEO98GlobalHandle");
    U *shared = mapping ? MapViewOfFile(mapping, 0xf001f, 0, 0, 0) : 0;
    U ready = CreateEventA(0, 1, 0, "NEO98GlobalHandleReady"), go = CreateEventA(0, 1, 0, "NEO98GlobalHandleGo");
    if (!shared || !ready || !go) ExitProcess(1);
    Own(&shared[ParentProcess], &shared[ParentEvent], 10);
    if (!GetModuleFileNameA(0, path, sizeof path)) ExitProcess(2);
    if (!CreateProcessA(path, 0, 0, 0, 0, 0, 0, 0, startup, process)) ExitProcess(3);
    if (WaitForSingleObject(ready, 20000)) {
        if (!WaitForSingleObject(process[0], 0) && GetExitCodeProcess(process[0], &code) && code) ExitProcess(code);
        ExitProcess(4);
    }
    /* The child lives: its handle answers WAIT_TIMEOUT, its event is set,
       this process's own event is not. */
    if (WaitForSingleObject(shared[ChildProcess], 0) != Timeout) ExitProcess(5);
    if (WaitForSingleObject(shared[ChildEvent], 0) != 0) ExitProcess(6);
    if (WaitForSingleObject(shared[ParentEvent], 0) != Timeout) ExitProcess(7);
    if (!SetEvent(go)) ExitProcess(8);
    /* The child's end signals the handle the child made of itself. */
    if (WaitForSingleObject(shared[ChildProcess], 20000) != 0) ExitProcess(9);
    if (!GetExitCodeProcess(process[0], &code)) ExitProcess(20);
    if (code) ExitProcess(code);
    if (!CloseHandle(shared[ChildProcess]) || !CloseHandle(shared[ChildEvent])) ExitProcess(21);
    if (!CloseHandle(shared[ParentProcess]) || !CloseHandle(shared[ParentEvent])) ExitProcess(22);
    /* A closed global handle names nothing any more. */
    if (WaitForSingleObject(shared[ParentEvent], 0) != 0xffffffff) ExitProcess(23);
    ExitProcess(0);
}
