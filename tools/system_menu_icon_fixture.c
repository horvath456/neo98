/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The system menu from the icon of the caption, and from the taskbar's
   message 0313h. A window "SysIcon" at 40,40 of 300 by 200 pixels writes
   down WM_ENTERMENULOOP, WM_INITMENUPOPUP of its system menu with the
   menu's default item, WM_SYSCOMMAND and WM_CLOSE. The character p sends it
   0313h with the screen point 150,150, as the taskbar does for a right click
   on the window's button. The log goes to C:\SYSICON.TXT when the window
   closes; the program ends then. tools/test_system_menu_icon_host.py. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U GetModuleHandleA(const char *);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
API U DefWindowProcA(U, U, U, U);
API int GetMessageA(U *, U, U, U);
API int TranslateMessage(const U *);
API U DispatchMessageA(const U *);
API int DestroyWindow(U);
API void PostQuitMessage(int);
API U SendMessageA(U, U, U, U);
API U GetMenuDefaultItem(U, U, U);
API U CreateFileA(const char *, U, U, U, U, U, U);
API U WriteFile(U, const void *, U, U *, U);
API U CloseHandle(U);
API int lstrlenA(const char *);
API U LoadCursorA(U, U);
__declspec(dllimport) int __cdecl wsprintfA(char *, const char *, ...);

struct Class { U style, proc; int class_extra, window_extra; U instance, icon, cursor, brush; const char *menu, *name; };
static char text[2000];

static void Note(const char *format, U a, U b)
{
    if (lstrlenA(text) < 1900)
        wsprintfA(text + lstrlenA(text), format, a, b);
}
static U __stdcall Procedure(U window, U message, U wparam, U lparam)
{
    if (message == 0x211) /* WM_ENTERMENULOOP */
        Note("enter %u%s\r\n", wparam, (U)"");
    if (message == 0x117 && (lparam >> 16)) /* WM_INITMENUPOPUP of the system menu */
        Note("popup system default=%04x%s\r\n", GetMenuDefaultItem(wparam, 0, 0), (U)"");
    if (message == 0x112) /* WM_SYSCOMMAND */
        Note("syscommand %04x%s\r\n", wparam & 0xfff0, (U)"");
    if (message == 0x102 && wparam == 'p')
    {
        Note("taskbar 0313h%s%s\r\n", (U)"", (U)"");
        SendMessageA(window, 0x313, 0, 150 | (150 << 16));
        return 0;
    }
    if (message == 0x10) /* WM_CLOSE */
    {
        U written = 0, file;
        Note("close%s%s\r\n", (U)"", (U)"");
        file = CreateFileA("C:\\SYSICON.TXT", 0x40000000u, 0, 0, 2, 0x80, 0);
        WriteFile(file, text, (U)lstrlenA(text), &written, 0);
        CloseHandle(file);
        DestroyWindow(window);
        return 0;
    }
    if (message == 2)
        PostQuitMessage(0);
    return DefWindowProcA(window, message, wparam, lparam);
}
void __stdcall mainCRTStartup(void)
{
    struct Class c = {0, (U)Procedure, 0, 0, GetModuleHandleA(0), 0, LoadCursorA(0, 0x7f00), 6, 0, "SysIcon"};
    U message[7];
    RegisterClassA(&c);
    CreateWindowExA(0, "SysIcon", "SysIcon", 0x10cf0000, 40, 40, 300, 200, 0, 0, c.instance, 0);
    while (GetMessageA(message, 0, 0, 0) > 0)
    {
        TranslateMessage(message);
        DispatchMessageA(message);
    }
    ExitProcess(0);
}
