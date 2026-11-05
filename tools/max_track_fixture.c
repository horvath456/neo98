/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The largest tracking size follows the screen, and DefWindowProc holds the
   size of a window to it. USER recomputes
   SM_CXMAXTRACK and SM_CYMAXTRACK at a change of the display mode: twice the
   sizing frame plus the screen plus four; with clamps the size
   in WM_WINDOWPOSCHANGING for a window of the top level or one with
   WS_THICKFRAME, after asking it with WM_GETMINMAXINFO. The program writes
   the metrics at 800 by 600, changes to 640 by 480 and writes them again;
   in each mode it asks for a window of 3000 by 3000 and one of 1 by 1, a
   second time with a procedure that answers WM_GETMINMAXINFO with tracking
   sizes of its own, and once for a pop-up without a sizing frame, which is
   not held. D:\MAXTRACK.TXT. tools/test_max_track_native.py. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U CreateFileA(const char *, U, U, U, U, U, U);
API U WriteFile(U, const void *, U, U *, U);
API U CloseHandle(U);
API int lstrlenA(const char *);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
API U DefWindowProcA(U, U, U, U);
API int MoveWindow(U, int, int, int, int, int);
API int GetWindowRect(U, int *);
API int GetSystemMetrics(int);
API int ChangeDisplaySettingsA(void *, U);
__declspec(dllimport) int __cdecl wsprintfA(char *, const char *, ...);

struct Class { U style, proc; int class_extra, window_extra; U instance, icon, cursor, brush; const char *menu, *name; };
static char text[2000];
static int own, asked;

static U __stdcall Procedure(U window, U message, U wparam, U lparam)
{
    if (message == 0x24) /* WM_GETMINMAXINFO: reserved, max size, max position, min track, max track */
    {
        ++asked;
        if (own)
        {
            int *info = (int *)lparam;
            DefWindowProcA(window, message, wparam, lparam);
            info[6] = 200; info[7] = 150; info[8] = 400; info[9] = 300;
            return 0;
        }
    }
    return DefWindowProcA(window, message, wparam, lparam);
}
static void Size(const char *name, U window, int width, int height)
{
    int r[4];
    asked = 0;
    MoveWindow(window, 10, 10, width, height, 0);
    GetWindowRect(window, r);
    wsprintfA(text + lstrlenA(text), "%s %d,%d asked=%d\r\n", name, r[2] - r[0], r[3] - r[1], asked != 0);
}
static void Mode(const char *name, U overlapped, U popup)
{
    wsprintfA(text + lstrlenA(text), "%s screen=%d,%d frame=%d,%d maxtrack=%d,%d mintrack=%d,%d\r\n", name, GetSystemMetrics(0),
              GetSystemMetrics(1), GetSystemMetrics(32), GetSystemMetrics(33), GetSystemMetrics(59), GetSystemMetrics(60),
              GetSystemMetrics(34), GetSystemMetrics(35));
    own = 0;
    Size("large", overlapped, 3000, 3000);
    Size("small", overlapped, 1, 1);
    own = 1;
    Size("own-large", overlapped, 3000, 3000);
    Size("own-small", overlapped, 1, 1);
    own = 0;
    Size("popup", popup, 3000, 3000);
}
void mainCRTStartup(void)
{
    struct Class klass = {0};
    unsigned char mode[156] = {0};
    U overlapped, popup, written = 0, file;
    klass.proc = (U)Procedure;
    klass.instance = 0x400000;
    klass.name = "Neo98MaxTrack";
    RegisterClassA(&klass);
    overlapped = CreateWindowExA(0, klass.name, "W", 0x00cf0000u, 10, 10, 300, 200, 0, 0, 0x400000, 0);
    popup = CreateWindowExA(0, klass.name, "P", 0x80000000u, 10, 10, 300, 200, 0, 0, 0x400000, 0);
    Mode("first", overlapped, popup);
    *(unsigned short *)(mode + 36) = 156;    /* dmSize */
    *(U *)(mode + 40) = 0x00180000;          /* DM_PELSWIDTH | DM_PELSHEIGHT */
    *(U *)(mode + 108) = 640;
    *(U *)(mode + 112) = 480;
    wsprintfA(text + lstrlenA(text), "change=%d\r\n", ChangeDisplaySettingsA(mode, 0));
    Mode("second", overlapped, popup);
    file = CreateFileA("D:\\MAXTRACK.TXT", 0x40000000u, 0, 0, 2, 0x80, 0);
    WriteFile(file, text, (U)lstrlenA(text), &written, 0);
    CloseHandle(file);
    ExitProcess(0);
}
