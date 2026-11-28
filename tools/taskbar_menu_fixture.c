/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The system menu of 0313h from the keyboard, for
   tools/test_taskbar_system_menu_host.py. A window "TaskMenu" at 100,100 of
   300 by 200 pixels sends itself 0313h with lParam -1 two seconds after it
   was made, as the taskbar does when the menu of a button is asked for from
   the keyboard. When the menu loop is first idle (WM_ENTERIDLE, the menu's
   window in lParam) it writes down the menu's rectangle on the screen and
   those of the taskbar's window and of its band of buttons; it ends the
   menu three seconds after it sent the message, and half a second after that
   it writes C:\TASKMENU.TXT and closes:

       sent
       enter <WM_ENTERMENULOOP wParam>
       popup default=<default item>
       menu <left> <top> <right> <bottom>
       band <left> <top> <right> <bottom>
       tray <left> <top> <right> <bottom>
       done */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U GetModuleHandleA(const char *);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
API U DefWindowProcA(U, U, U, U);
API int GetMessageA(U *, U, U, U);
API U DispatchMessageA(const U *);
API int DestroyWindow(U);
API void PostQuitMessage(int);
API U SendMessageA(U, U, U, U);
API U SetTimer(U, U, U, U);
API int KillTimer(U, U);
API int EndMenu(void);
API U FindWindowA(const char *, const char *);
API U FindWindowExA(U, U, const char *, const char *);
API int GetWindowRect(U, int *);
API U GetMenuDefaultItem(U, U, U);
API U CreateFileA(const char *, U, U, U, U, U, U);
API U WriteFile(U, const void *, U, U *, U);
API U CloseHandle(U);
API int lstrlenA(const char *);
API U LoadCursorA(U, U);
__declspec(dllimport) int __cdecl wsprintfA(char *, const char *, ...);

struct Class { U style, proc; int class_extra, window_extra; U instance, icon, cursor, brush; const char *menu, *name; };
static char text[2000];
static int idle_seen;

static void Rect(const char *name, U window)
{
    int r[4] = {0, 0, 0, 0};
    if (window)
        GetWindowRect(window, r);
    if (lstrlenA(text) < 1900)
        wsprintfA(text + lstrlenA(text), "%s %d %d %d %d\r\n", name, r[0], r[1], r[2], r[3]);
}
static void Note(const char *line, U value)
{
    if (lstrlenA(text) < 1900)
        wsprintfA(text + lstrlenA(text), line, value);
}
static U __stdcall Procedure(U window, U message, U wparam, U lparam)
{
    if (message == 0x211) /* WM_ENTERMENULOOP */
        Note("enter %u\r\n", wparam);
    if (message == 0x117 && (lparam >> 16)) /* WM_INITMENUPOPUP of the system menu */
        Note("popup default=%04x\r\n", GetMenuDefaultItem(wparam, 0, 0));
    if (message == 0x121 && wparam == 2 && !idle_seen) /* WM_ENTERIDLE, MSGF_MENU */
    {
        const U tray = FindWindowA("Shell_TrayWnd", 0);
        const U bar = tray ? FindWindowExA(tray, 0, "ReBarWindow32", 0) : 0;
        const U tasks = bar ? FindWindowExA(bar, 0, "MSTaskSwWClass", 0) : 0;
        idle_seen = 1;
        Rect("menu", lparam);
        Rect("band", tasks ? FindWindowExA(tasks, 0, "SysTabControl32", 0) : 0);
        Rect("tray", tray);
        return 0;
    }
    if (message == 0x113) /* WM_TIMER */
    {
        KillTimer(window, wparam);
        if (wparam == 1)
        {
            Note("sent%s\r\n", (U)"");
            SetTimer(window, 2, 3000, 0);
            SendMessageA(window, 0x313, 0, 0xffffffffu);
            SetTimer(window, 3, 500, 0);
        }
        else if (wparam == 2)
            EndMenu();
        else
        {
            U written = 0, file;
            Note("done%s\r\n", (U)"");
            file = CreateFileA("C:\\TASKMENU.TXT", 0x40000000u, 0, 0, 2, 0x80, 0);
            WriteFile(file, text, (U)lstrlenA(text), &written, 0);
            CloseHandle(file);
            DestroyWindow(window);
        }
        return 0;
    }
    if (message == 2)
        PostQuitMessage(0);
    return DefWindowProcA(window, message, wparam, lparam);
}
void __stdcall mainCRTStartup(void)
{
    struct Class c = {0, (U)Procedure, 0, 0, GetModuleHandleA(0), 0, LoadCursorA(0, 0x7f00), 6, 0, "TaskMenu"};
    U message[7], window;
    RegisterClassA(&c);
    window = CreateWindowExA(0, "TaskMenu", "TaskMenu", 0x10cf0000, 100, 100, 300, 200, 0, 0, c.instance, 0);
    SetTimer(window, 1, 2000, 0);
    while (GetMessageA(message, 0, 0, 0) > 0)
        DispatchMessageA(message);
    ExitProcess(0);
}
