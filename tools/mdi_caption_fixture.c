/* SPDX-License-Identifier: GPL-2.0-or-later */
/* An MDI frame with two children for tools/test_mdi_caption_host.py. The
   frame stands at 0,0 with 640 by 480 pixels, the children side by side in
   its MDI client; the second one is created last and so is the active child.
   C:\MDICAP.TXT gets the screen rectangles of both children. A key typed
   into the active child opens a message box over the frame, which takes the
   activation from it until the box is closed. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U GetModuleHandleA(const char *);
API U CreateFileA(const char *, U, U, U, U, U, U);
API U WriteFile(U, const void *, U, U *, U);
API U CloseHandle(U);
API int lstrlenA(const char *);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
API U DefFrameProcA(U, U, U, U, U);
API U DefMDIChildProcA(U, U, U, U);
API U SendMessageA(U, U, U, U);
API int ShowWindow(U, int);
API int UpdateWindow(U);
API int GetWindowRect(U, int *);
API int GetMessageA(void *, U, U, U);
API int TranslateMessage(const void *);
API U DispatchMessageA(const void *);
API int TranslateMDISysAccel(U, void *);
API int MessageBoxA(U, const char *, const char *, U);
API void PostQuitMessage(int);
API U LoadCursorA(U, U);
API U GetSysColorBrush(int);
__declspec(dllimport) int __cdecl wsprintfA(char *, const char *, ...);

struct Class { U style, proc; int class_extra, window_extra; U instance, icon, cursor, brush; const char *menu, *name; };
static U frame, client;

static U __stdcall Frame(U window, U message, U wparam, U lparam)
{
    if (message == 0x0001) /* WM_CREATE: the MDI client fills the frame */
    {
        U create[2] = {0, 0x100};
        client = CreateWindowExA(0, "MDICLIENT", 0, 0x52000000, 0, 0, 0, 0, window, 1, GetModuleHandleA(0), create);
        return 0;
    }
    if (message == 0x0002) /* WM_DESTROY */
        PostQuitMessage(0);
    return DefFrameProcA(window, client, message, wparam, lparam);
}
static U __stdcall Child(U window, U message, U wparam, U lparam)
{
    if (message == 0x0102) /* WM_CHAR */
    {
        MessageBoxA(frame, "The frame is inactive now.", "MDI caption", 0);
        return 0;
    }
    return DefMDIChildProcA(window, message, wparam, lparam);
}
static U Create(const char *title, int x)
{
    U create[9] = {(U) "Neo98MdiCaptionChild", (U)title, GetModuleHandleA(0), x, 10, 290, 200, 0x00cf0000, 0};
    return SendMessageA(client, 0x220, 0, (U)create); /* WM_MDICREATE */
}
void mainCRTStartup(void)
{
    struct Class frame_class = {0, (U)Frame, 0, 0, GetModuleHandleA(0), 0, LoadCursorA(0, 32512), GetSysColorBrush(12), 0,
                                "Neo98MdiCaptionFrame"};
    struct Class child_class = {0, (U)Child, 0, 0, GetModuleHandleA(0), 0, LoadCursorA(0, 32512), GetSysColorBrush(5), 0,
                                "Neo98MdiCaptionChild"};
    U first, second, written, file, message[7];
    int a[4], b[4];
    char text[200];
    RegisterClassA(&frame_class);
    RegisterClassA(&child_class);
    frame = CreateWindowExA(0, "Neo98MdiCaptionFrame", "MDI caption", 0x02cf0000, 0, 0, 640, 480, 0, 0,
                            GetModuleHandleA(0), 0);
    first = Create("First", 10);
    second = Create("Second", 320);
    ShowWindow(frame, 1);
    UpdateWindow(frame);
    GetWindowRect(first, a);
    GetWindowRect(second, b);
    wsprintfA(text, "%d %d %d %d\r\n%d %d %d %d\r\n", a[0], a[1], a[2], a[3], b[0], b[1], b[2], b[3]);
    file = CreateFileA("C:\\MDICAP.TXT", 0x40000000, 0, 0, 2, 0x80, 0);
    WriteFile(file, text, lstrlenA(text), &written, 0);
    CloseHandle(file);
    while (GetMessageA(message, 0, 0, 0))
        if (!TranslateMDISysAccel(client, message))
        {
            TranslateMessage(message);
            DispatchMessageA(message);
        }
    ExitProcess(0);
}
