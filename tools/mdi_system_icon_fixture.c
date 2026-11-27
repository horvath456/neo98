/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The system menu of a maximized MDI child from its icon in the menu bar of
   the frame. The frame "MdiIcon" at 40,40 of 400 by 300 pixels has a menu
   bar with File and an MDI client with two children, maximized. The frame
   writes down WM_INITMENUPOPUP (the menu: "child" when it is the system menu
   of the maximized child, and the system flag) and WM_COMMAND; a child writes
   down WM_SYSCOMMAND and WM_CLOSE. When the last child is gone the log goes
   to C:\MDIICON.TXT and the program ends. tools/test_system_menu_icon_host.py. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U GetModuleHandleA(const char *);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
API U DefFrameProcA(U, U, U, U, U);
API U DefMDIChildProcA(U, U, U, U);
API int GetMessageA(U *, U, U, U);
API int TranslateMessage(const U *);
API int TranslateMDISysAccel(U, U *);
API U DispatchMessageA(const U *);
API int DestroyWindow(U);
API void PostQuitMessage(int);
API U SendMessageA(U, U, U, U);
API U GetMenu(U);
API U GetSubMenu(U, int);
API U CreateMenu(void);
API U CreatePopupMenu(void);
API int AppendMenuA(U, U, U, const char *);
API U CreateFileA(const char *, U, U, U, U, U, U);
API U WriteFile(U, const void *, U, U *, U);
API U CloseHandle(U);
API int lstrlenA(const char *);
API U LoadCursorA(U, U);
__declspec(dllimport) int __cdecl wsprintfA(char *, const char *, ...);

struct Class { U style, proc; int class_extra, window_extra; U instance, icon, cursor, brush; const char *menu, *name; };
struct ClientCreate { U window_menu, first_child; };
struct MdiCreate { const char *klass, *title; U owner; int x, y, cx, cy; U style, parameter; };
static char text[2000];
static U frame, client;
static int children;

static void Note(const char *format, U a)
{
    if (lstrlenA(text) < 1900)
        wsprintfA(text + lstrlenA(text), format, a);
}
static U __stdcall Child(U window, U message, U wparam, U lparam)
{
    if (message == 0x112) /* WM_SYSCOMMAND */
        Note("child syscommand %04x\r\n", wparam & 0xfff0);
    if (message == 0x10) /* WM_CLOSE */
        Note("child close%s\r\n", (U)"");
    if (message == 2 && --children == 0)
    {
        U written = 0, file = CreateFileA("C:\\MDIICON.TXT", 0x40000000u, 0, 0, 2, 0x80, 0);
        WriteFile(file, text, (U)lstrlenA(text), &written, 0);
        CloseHandle(file);
        PostQuitMessage(0);
    }
    return DefMDIChildProcA(window, message, wparam, lparam);
}
static U __stdcall Frame(U window, U message, U wparam, U lparam)
{
    if (message == 0x117) /* WM_INITMENUPOPUP */
    {
        /* The system menu of the maximized child is the popup of the first
           item of the frame's bar; USER takes WS_SYSMENU off the child
           meanwhile, so GetSystemMenu of the child answers none. */
        Note(wparam == GetSubMenu(GetMenu(window), 0) ? "popup child" : "popup other", 0);
        Note(" system=%u\r\n", lparam >> 16);
    }
    if (message == 0x111) /* WM_COMMAND */
        Note("frame command %04x\r\n", wparam & 0xffff);
    return DefFrameProcA(window, client, message, wparam, lparam);
}
void __stdcall mainCRTStartup(void)
{
    const U instance = GetModuleHandleA(0);
    struct Class f = {0, (U)Frame, 0, 0, instance, 0, LoadCursorA(0, 0x7f00), 12, 0, "MdiIcon"};
    struct Class c = {0, (U)Child, 0, 0, instance, 0, LoadCursorA(0, 0x7f00), 6, 0, "MdiIconChild"};
    U bar = CreateMenu(), file = CreatePopupMenu(), message[7];
    struct ClientCreate cc = {file, 100};
    RegisterClassA(&f);
    RegisterClassA(&c);
    AppendMenuA(file, 0, 1, "E&xit");
    AppendMenuA(bar, 0x10, file, "&File");
    frame = CreateWindowExA(0, "MdiIcon", "MdiIcon", 0x12cf0000, 40, 40, 400, 300, 0, bar, instance, 0);
    client = CreateWindowExA(0, "MDICLIENT", 0, 0x52000000, 0, 0, 0, 0, frame, 0, instance, &cc);
    for (int i = 0; i < 2; ++i)
    {
        struct MdiCreate mc = {"MdiIconChild", i ? "Second" : "First", instance, 0, 0, 200, 150, 0x01000000, 0};
        ++children;
        SendMessageA(client, 0x220, 0, (U)&mc); /* WM_MDICREATE, maximized */
    }
    while (GetMessageA(message, 0, 0, 0) > 0)
    {
        if (TranslateMDISysAccel(client, message))
            continue;
        TranslateMessage(message);
        DispatchMessageA(message);
    }
    DestroyWindow(frame);
    ExitProcess(0);
}
