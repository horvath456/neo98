/* SPDX-License-Identifier: GPL-2.0-or-later */
/* WM_MOUSEACTIVATE at a 16-bit window: the 32-bit side. Sets the
   WH_CBT hook of C:\MA16HOOK.DLL (tools/mouse_activate_win16_dll_fixture.c)
   for the session, shows a window G at 600,400 and starts the 16-bit File
   Manager. The test clicks G, the client area of File Manager, G, the caption
   of File Manager and G. Each time G is asked with WM_MOUSEACTIVATE it writes
   down whether File Manager was the foreground window; the third press on G
   closes File Manager and ends the program. C:\MA16G.TXT.
   tools/test_mouse_activate_cases_native.py. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API void Sleep(U);
API U LoadLibraryA(const char *);
API U WinExec(const char *, U);
API U GetProcAddress(U, const char *);
API U CreateFileA(const char *, U, U, U, U, U, U);
API U WriteFile(U, const void *, U, U *, U);
API U CloseHandle(U);
API int lstrlenA(const char *);
API U SetWindowsHookExA(int, U, U, U);
API int UnhookWindowsHookEx(U);
API U FindWindowA(const char *, const char *);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
API U DefWindowProcA(U, U, U, U);
API int GetMessageA(U *, U, U, U);
API int PeekMessageA(U *, U, U, U, U);
API int DispatchMessageA(const U *);
API int PostMessageA(U, U, U, U);
API void PostQuitMessage(int);
API int IsWindow(U);
API U GetForegroundWindow(void);
__declspec(dllimport) int __cdecl wsprintfA(char *, const char *, ...);

struct Class { U style, proc; int class_extra, window_extra; U instance, icon, cursor, brush; const char *menu, *name; };
static char text[600];
static int presses;

static U __stdcall Procedure(U window, U message, U wparam, U lparam)
{
    if (message == 0x21) /* WM_MOUSEACTIVATE */
    {
        const U frame = FindWindowA("WFS_Frame", 0);
        wsprintfA(text + lstrlenA(text), "asked=%d frame=%d foreground_is_frame=%d\r\n", presses + 1, frame != 0,
                  frame && GetForegroundWindow() == frame);
    }
    if (message == 0x201 && ++presses == 3) PostQuitMessage(0);
    return DefWindowProcA(window, message, wparam, lparam);
}
void mainCRTStartup(void)
{
    struct Class klass = {0};
    U message[7], dll, cbt, frame = 0, written = 0, file;
    dll = LoadLibraryA("C:\\MA16HOOK.DLL");
    cbt = dll ? SetWindowsHookExA(5, GetProcAddress(dll, "_MouseActivateCbt@12"), dll, 0) : 0;
    klass.proc = (U)Procedure;
    klass.instance = 0x400000;
    klass.brush = 6;
    klass.name = "Neo98MouseActivate16";
    RegisterClassA(&klass);
    CreateWindowExA(0, klass.name, "G", 0x10c80000u, 600, 400, 150, 100, 0, 0, 0x400000, 0);
    WinExec("C:\\WINDOWS\\WINFILE.EXE", 1);
    while (GetMessageA(message, 0, 0, 0) > 0) DispatchMessageA(message);
    frame = FindWindowA("WFS_Frame", 0);
    wsprintfA(text + lstrlenA(text), "hook=%d frame_at_end=%d\r\n", cbt != 0, frame != 0);
    if (frame) PostMessageA(frame, 0x10, 0, 0);
    for (U i = 0; i < 100 && frame && IsWindow(frame); ++i)
    {
        while (PeekMessageA(message, 0, 0, 0, 1)) DispatchMessageA(message);
        Sleep(100);
    }
    wsprintfA(text + lstrlenA(text), "closed=%d\r\n", !frame || !IsWindow(frame));
    if (cbt) UnhookWindowsHookEx(cbt);
    file = CreateFileA("C:\\MA16G.TXT", 0x40000000u, 0, 0, 2, 0x80, 0);
    WriteFile(file, text, (U)lstrlenA(text), &written, 0);
    CloseHandle(file);
    ExitProcess(0);
}
