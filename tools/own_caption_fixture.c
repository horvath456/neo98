/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A window that paints its own caption, for tools/test_own_caption_host.py.
   "OwnCaption" at 40,40 of 300 by 200 pixels hands WM_NCPAINT and
   WM_NCACTIVATE to DefWindowProc and then fills its caption with magenta
   through GetWindowDC, as a program that draws its own title bar does.
   After 0.8 s it invalidates its client area; after 1.5 s it opens a popup
   "Cover" at 100,30 of 150 by 60 pixels over part of the caption, which
   takes the activation; after 3 s it closes the popup again. It writes how
   many WM_NCPAINT and WM_NCACTIVATE it got into C:\OWNCAP.TXT when it
   closes. */
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
API U CreateSolidBrush(U);
API int FillRect(U, const int *, U);
API int DeleteObject(U);
API U CreateFileA(const char *, U, U, U, U, U, U);
API U WriteFile(U, const void *, U, U *, U);
API U CloseHandle(U);
API int lstrlenA(const char *);
__declspec(dllimport) int __cdecl wsprintfA(char *, const char *, ...);

struct Class { U style, proc; int class_extra, window_extra; U instance, icon, cursor, brush; const char *menu, *name; };
static U window, cover, ncpaints, ncactivates;

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
static U __stdcall Procedure(U h, U message, U wparam, U lparam)
{
    if (!window && message == 0x81) /* WM_NCCREATE of the first window */
        window = h;
    if (h == window && (message == 0x85 || message == 0x86)) /* WM_NCPAINT, WM_NCACTIVATE */
    {
        const U result = DefWindowProcA(h, message, wparam, lparam);
        if (message == 0x85) ++ncpaints; else ++ncactivates;
        FillCaption(h);
        return result;
    }
    if (h == window && message == 0x113) /* WM_TIMER */
    {
        KillTimer(h, wparam);
        if (wparam == 1) InvalidateRect(h, 0, 1);
        if (wparam == 2) cover = CreateWindowExA(0, "OwnCaption", "Cover", 0x90c00000u, 100, 30, 150, 60, 0, 0,
                                                 GetModuleHandleA(0), 0);
        if (wparam == 3 && cover) { DestroyWindow(cover); cover = 0; }
        return 0;
    }
    if (h == window && message == 0x10) /* WM_CLOSE */
    {
        char text[80];
        U written = 0, file;
        wsprintfA(text, "ncpaint %u ncactivate %u\r\n", ncpaints, ncactivates);
        file = CreateFileA("C:\\OWNCAP.TXT", 0x40000000u, 0, 0, 2, 0x80, 0);
        WriteFile(file, text, (U)lstrlenA(text), &written, 0);
        CloseHandle(file);
        DestroyWindow(h);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(h, message, wparam, lparam);
}
void __stdcall mainCRTStartup(void)
{
    struct Class c = {0, (U)Procedure, 0, 0, GetModuleHandleA(0), 0, 0, 6, 0, "OwnCaption"};
    U message[7];
    RegisterClassA(&c);
    window = CreateWindowExA(0, "OwnCaption", "OwnCaption", 0x10cf0000u, 40, 40, 300, 200, 0, 0, c.instance, 0);
    SetTimer(window, 1, 800, 0);
    SetTimer(window, 2, 1500, 0);
    SetTimer(window, 3, 3000, 0);
    while (GetMessageA(message, 0, 0, 0) > 0)
        DispatchMessageA(message);
    ExitProcess(0);
}
