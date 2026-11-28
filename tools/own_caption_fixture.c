/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Windows that paint their own frame, for tools/test_own_caption_host.py.

   "NoFrame" at 360,40 of 200 by 150 pixels, made first and hidden, answers
   WM_NCPAINT and WM_NCACTIVATE without DefWindowProc: on Windows it has no
   frame at all, only what was on the screen before. It is shown without the
   activation after 0.8 s, when the desktop has been painted under it.

   "OwnCaption" at 40,40 of 300 by 200 pixels hands WM_NCPAINT and
   WM_NCACTIVATE to DefWindowProc and then fills its caption with magenta
   through GetWindowDC, as a program that draws its own title bar does.
   After 0.8 s it invalidates its client area; after 1.5 s it opens a popup
   "Cover" at 100,30 of 150 by 60 pixels over part of the caption, which
   takes the activation; after 3 s it closes the popup again. After 3.3 s it
   shows the popup once more, this time without the activation, and closes
   it after 3.6 s: the part of the caption it uncovers is owed WM_NCPAINT
   alone, without a WM_NCACTIVATE before it.

   For each WM_NCPAINT it writes down the wParam (1, or the bounding box of
   the region in screen coordinates) and, before DefWindowProc, the colour
   of the screen at 175,56: a point of its caption under the client area of
   the popup, which nothing but DefWindowProc and the program itself may
   paint. After 4 s it writes the lines into C:\OWNCAP.TXT:

       ncpaint 1 <colour>
       ncpaint <left> <top> <right> <bottom> <colour>
       ... */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U GetModuleHandleA(const char *);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
API U DefWindowProcA(U, U, U, U);
API int GetMessageA(U *, U, U, U);
API U DispatchMessageA(const U *);
API void PostQuitMessage(int);
API int DestroyWindow(U);
API U GetWindowDC(U);
API int ReleaseDC(U, U);
API int GetWindowRect(U, int *);
API int GetSystemMetrics(int);
API U SetTimer(U, U, U, U);
API int KillTimer(U, U);
API int InvalidateRect(U, const int *, int);
API int ShowWindow(U, int);
API U CreateSolidBrush(U);
API int FillRect(U, const int *, U);
API int DeleteObject(U);
API U GetPixel(U, int, int);
API int GetRgnBox(U, int *);
API U CreateFileA(const char *, U, U, U, U, U, U);
API U WriteFile(U, const void *, U, U *, U);
API U CloseHandle(U);
API int lstrlenA(const char *);
__declspec(dllimport) int __cdecl wsprintfA(char *, const char *, ...);

struct Class { U style, proc; int class_extra, window_extra; U instance, icon, cursor, brush; const char *menu, *name; };
static U window, bare, cover;
static char text[3000];

static void FillCaption(U h)
{
    int r[4], fill[4];
    const int frame = GetSystemMetrics(33), caption = GetSystemMetrics(4); /* SM_CYFRAME, SM_CYCAPTION */
    const U dc = GetWindowDC(h), brush = CreateSolidBrush(0xff00ff);
    GetWindowRect(h, r);
    fill[0] = frame; fill[1] = frame; fill[2] = r[2] - r[0] - frame; fill[3] = frame + caption - 1;
    FillRect(dc, fill, brush);
    DeleteObject(brush);
    ReleaseDC(h, dc);
}
static void NotePaint(U h, U parameter)
{
    int r[4], box[4] = {0, 0, 0, 0};
    const U dc = GetWindowDC(h);
    U colour;
    GetWindowRect(h, r);
    colour = GetPixel(dc, 175 - r[0], 56 - r[1]);
    ReleaseDC(h, dc);
    if (lstrlenA(text) > 2900)
        return;
    if (parameter == 1)
        wsprintfA(text + lstrlenA(text), "ncpaint 1 %06x\r\n", colour);
    else
    {
        GetRgnBox(parameter, box);
        wsprintfA(text + lstrlenA(text), "ncpaint %d %d %d %d %06x\r\n", box[0], box[1], box[2], box[3], colour);
    }
}
static U __stdcall Procedure(U h, U message, U wparam, U lparam)
{
    if (!bare && message == 0x81) /* WM_NCCREATE of the first window */
        bare = h;
    else if (!window && h != bare && message == 0x81)
        window = h;
    if (h == bare && message == 0x85) /* WM_NCPAINT: nothing */
        return 0;
    if (h == bare && message == 0x86) /* WM_NCACTIVATE: nothing, and TRUE */
        return 1;
    if (h == window && (message == 0x85 || message == 0x86)) /* WM_NCPAINT, WM_NCACTIVATE */
    {
        U result;
        if (message == 0x85)
            NotePaint(h, wparam);
        result = DefWindowProcA(h, message, wparam, lparam);
        FillCaption(h);
        return result;
    }
    if (h == window && message == 0x113) /* WM_TIMER */
    {
        KillTimer(h, wparam);
        if (wparam == 1) { InvalidateRect(h, 0, 1); ShowWindow(bare, 4); } /* SW_SHOWNOACTIVATE */
        if (wparam == 2) cover = CreateWindowExA(0, "OwnCaption", "Cover", 0x90c00000u, 100, 30, 150, 60, 0, 0,
                                                 GetModuleHandleA(0), 0);
        if ((wparam == 3 || wparam == 6) && cover) { DestroyWindow(cover); cover = 0; }
        if (wparam == 5)
        {
            cover = CreateWindowExA(0, "OwnCaption", "Cover", 0x80c00000u, 100, 30, 150, 60, 0, 0,
                                    GetModuleHandleA(0), 0);
            ShowWindow(cover, 4); /* SW_SHOWNOACTIVATE */
        }
        if (wparam == 4)
        {
            U written = 0;
            const U file = CreateFileA("C:\\OWNCAP.TXT", 0x40000000u, 0, 0, 2, 0x80, 0);
            WriteFile(file, text, (U)lstrlenA(text), &written, 0);
            CloseHandle(file);
        }
        return 0;
    }
    if (h == window && message == 2)
        PostQuitMessage(0);
    return DefWindowProcA(h, message, wparam, lparam);
}
void __stdcall mainCRTStartup(void)
{
    struct Class c = {0, (U)Procedure, 0, 0, GetModuleHandleA(0), 0, 0, 6, 0, "OwnCaption"};
    U message[7];
    RegisterClassA(&c);
    CreateWindowExA(0, "OwnCaption", "NoFrame", 0x00cf0000u, 360, 40, 200, 150, 0, 0, c.instance, 0);
    CreateWindowExA(0, "OwnCaption", "OwnCaption", 0x10cf0000u, 40, 40, 300, 200, 0, 0, c.instance, 0);
    SetTimer(window, 1, 800, 0);
    SetTimer(window, 2, 1500, 0);
    SetTimer(window, 3, 3000, 0);
    SetTimer(window, 4, 4000, 0);
    SetTimer(window, 5, 3300, 0);
    SetTimer(window, 6, 3600, 0);
    while (GetMessageA(message, 0, 0, 0) > 0)
        DispatchMessageA(message);
    ExitProcess(0);
}
