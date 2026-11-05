// SPDX-License-Identifier: GPL-2.0-or-later
// The desktop in 1024 by 768.
//
// The fixture stores 1024x768x32 as the display mode with CDS_UPDATEREGISTRY,
// as DESK.CPL does for "Screen area", and shows a red window beyond the old
// 800 by 600 edge. A press on it retitles the window "clicked".
typedef unsigned U;
typedef struct { U hwnd, message, wp, lp, time; int x, y; } MSG;
typedef struct { U style, proc, extra, classExtra, instance, icon, cursor, brush;
                 const char *menu, *name; } CLASS;
typedef struct { U dc, erase; int left, top, right, bottom; U restore, incremental;
                 unsigned char reserved[32]; } PAINT;
#define API __declspec(dllimport) __stdcall
API U RegisterClassA(CLASS *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U GetMessageA(MSG *, U, U, U);
API U TranslateMessage(MSG *);
API U DispatchMessageA(MSG *);
API void ExitProcess(U);
API U GetModuleHandleA(const char *);
API U BeginPaint(U, PAINT *);
API U EndPaint(U, const PAINT *);
API U FillRect(U, const int *, U);
API U CreateSolidBrush(U);
API U SetWindowTextA(U, const char *);
API int ChangeDisplaySettingsA(void *, U);
API int GetSystemMetrics(int);

static U red;
static unsigned char mode[0x94];
static void check(U value, U code) { if (!value) ExitProcess(code); }

static U __stdcall proc(U h, U m, U wp, U lp)
{
    if (m == 0x0f)
    {
        PAINT paint;
        const U dc = BeginPaint(h, &paint);
        FillRect(dc, &paint.left, red);
        EndPaint(h, &paint);
        return 0;
    }
    if (m == 0x201) { SetWindowTextA(h, "clicked"); return 0; }
    if (m == 0x10) { ExitProcess(0); return 0; }
    return DefWindowProcA(h, m, wp, lp);
}

void mainCRTStartup(void)
{
    mode[0x24] = 0x94;
    *(U *)(mode + 0x28) = 0x1c0000;
    *(U *)(mode + 0x68) = 32;
    *(U *)(mode + 0x6c) = 1024;
    *(U *)(mode + 0x70) = 768;
    check(ChangeDisplaySettingsA(mode, 1) == 0, 7841);
    check(GetSystemMetrics(0) == 1024 && GetSystemMetrics(1) == 768, 7842);
    red = CreateSolidBrush(0x000000ff);
    const U instance = GetModuleHandleA(0);
    CLASS c = {0, (U)proc, 0, 0, instance, 0, 0, 0, 0, "DESKTOP_SIZE"};
    check(RegisterClassA(&c), 7843);
    // WS_POPUP | WS_VISIBLE, beyond 800 by 600.
    check(CreateWindowExA(0, "DESKTOP_SIZE", "size", 0x90000000, 850, 650, 150, 100, 0, 0, instance, 0), 7844);
    MSG m;
    while (GetMessageA(&m, 0, 0, 0)) { TranslateMessage(&m); DispatchMessageA(&m); }
    ExitProcess(0);
}
