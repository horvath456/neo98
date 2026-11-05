/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Two processes and a hook DLL with a shared section. The first
   process starts a copy of itself with the argument "child" and waits until
   it is ready. The child starts a second thread that waits for the event
   "B5518Go" and then ends. The first process loads C:\SHRHOOK.DLL, sets its
   WH_GETMESSAGE procedure for the session, stores a mark in the shared
   section and sets the event. The first thing that walks the hook chains in
   the child is the end of that thread; only afterwards does the child's
   main thread pick a message out of its queue. Then the hook DLL has to be in
   the child at its preferred base 711D0000h, and both processes have to see
   the same.shrdata. Reports: C:\SHRHOOK.TXT and C:\SHRCHILD.TXT.
   tools/test_session_hook_shared_host.py. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API void Sleep(U);
API char *GetCommandLineA(void);
API U LoadLibraryA(const char *);
API U GetModuleHandleA(const char *);
API U WinExec(const char *, U);
API U GetProcAddress(U, const char *);
API U CreateFileA(const char *, U, U, U, U, U, U);
API U WriteFile(U, const void *, U, U *, U);
API U CloseHandle(U);
API U GetFileAttributesA(const char *);
API int lstrlenA(const char *);
API U CreateEventA(U, int, int, const char *);
API int SetEvent(U);
API U WaitForSingleObject(U, U);
API U CreateThread(U, U, U(__stdcall *)(U), U, U, U *);
API U GetCurrentThreadId(void);
API U SetWindowsHookExA(int, U, U, U);
API int UnhookWindowsHookEx(U);
API int PostThreadMessageA(U, U, U, U);
API int PeekMessageA(U *, U, U, U, U);
__declspec(dllimport) int __cdecl wsprintfA(char *, const char *, ...);

static void Report(const char *path, const char *text)
{
    U written = 0, file = CreateFileA(path, 0x40000000u, 0, 0, 2, 0x80, 0);
    WriteFile(file, text, (U)lstrlenA(text), &written, 0);
    CloseHandle(file);
}
static U __stdcall Waiter(U go)
{
    WaitForSingleObject(go, 30000);
    return 0;
}
static void Values(U module, U *out)
{
    void(__stdcall * values)(U *) = module ? (void(__stdcall *)(U *))GetProcAddress(module, "_SharedValues@4") : 0;
    for (int i = 0; i < 4; ++i) out[i] = 0;
    if (values) values(out);
}
static void Child(void)
{
    char text[200];
    U message[7], values[4], id = 0, picked = 0;
    U ready = CreateEventA(0, 1, 0, "B5518Ready"), go = CreateEventA(0, 1, 0, "B5518Go");
    U thread = CreateThread(0, 0, Waiter, go, 0, &id);
    SetEvent(ready);
    /* Nothing of this thread touches the hook chains until the other one has ended. */
    U ended = WaitForSingleObject(thread, 30000);
    Sleep(300);
    PostThreadMessageA(GetCurrentThreadId(), 0x8001, 0, 0);
    while (PeekMessageA(message, 0, 0, 0, 1)) ++picked;
    const U module = GetModuleHandleA("SHRHOOK.DLL");
    Values(module, values);
    wsprintfA(text, "ended=%u\r\npicked=%u\r\nmodule=%x\r\nattaches=%u\r\nmagic=%x\r\nmessages=%u\r\n", ended == 0, picked,
              module, values[0], values[1], values[2]);
    Report("C:\\SHRCHILD.TXT", text);
    ExitProcess(0);
}
void mainCRTStartup(void)
{
    char text[240];
    U values[4], hook = 0, child = 0;
    for (const char *p = GetCommandLineA(); *p; ++p)
        if (p[0] == 'c' && p[1] == 'h' && p[2] == 'i' && p[3] == 'l' && p[4] == 'd') Child();
    U ready = CreateEventA(0, 1, 0, "B5518Ready"), go = CreateEventA(0, 1, 0, "B5518Go");
    WinExec("C:\\SHRHOOK.EXE child", 1);
    const U started = WaitForSingleObject(ready, 30000) == 0;
    U dll = LoadLibraryA("C:\\SHRHOOK.DLL");
    U procedure = dll ? GetProcAddress(dll, "_SharedMessages@12") : 0;
    void(__stdcall * set)(U, U) = dll ? (void(__stdcall *)(U, U))GetProcAddress(dll, "_SharedSet@8") : 0;
    if (procedure && set)
    {
        hook = SetWindowsHookExA(3, procedure, dll, 0);
        set(0x5518, hook);
    }
    SetEvent(go);
    for (U i = 0; i < 300 && !child; ++i)
    {
        Sleep(100);
        child = GetFileAttributesA("C:\\SHRCHILD.TXT") != ~0u;
    }
    Sleep(300);
    Values(dll, values);
    const int removed = hook && UnhookWindowsHookEx(hook);
    wsprintfA(text, "started=%u\r\ndll=%x\r\nhook=%u\r\nchild=%u\r\nattaches=%u\r\nmagic=%x\r\nmessages=%u\r\nremoved=%d\r\n",
              started, dll, hook != 0, child, values[0], values[1], values[2], removed);
    Report("C:\\SHRHOOK.TXT", text);
    ExitProcess(0);
}
