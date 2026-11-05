/* SPDX-License-Identifier: GPL-2.0-or-later
   The guest of tools/test_state_host.py : a
   window, a counter in memory that a timer of 50 ms raises, a picture drawn
   with GDI from the counter, a file that stays open and gets four bytes at
   every tick, a block of the heap, a thread that sleeps in a loop and a
   thread that waits for an event. A click or a key signals the event and
   writes everything the program knows to CLICK.TXT or KEY.TXT beside the
   program; the key Q writes QUIT.TXT there and ends the program. */
typedef unsigned U;
typedef struct { U style; void *proc; int a, b; U instance, icon, cursor, brush; const char *menu, *name; } WNDCLASSA;
typedef struct { U hwnd, message, wparam, lparam, time; int x, y; } MSG;
typedef struct { int left, top, right, bottom; } RECT;
typedef struct { U hdc; int erase; RECT paint; int restore, update; unsigned char reserved[32]; } PAINTSTRUCT;
#define I(r, n, ...) __declspec(dllimport) r __stdcall n(__VA_ARGS__)
I(U, RegisterClassA, const WNDCLASSA *);
I(U, CreateWindowExA, U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
I(U, DefWindowProcA, U, U, U, U);
I(int, GetMessageA, MSG *, U, U, U);
I(U, DispatchMessageA, const MSG *);
I(int, TranslateMessage, const MSG *);
I(int, ShowWindow, U, int);
I(int, SetWindowTextA, U, const char *);
I(U, SetTimer, U, U, U, void *);
I(U, SetFocus, U);
I(int, InvalidateRect, U, const RECT *, int);
I(U, BeginPaint, U, PAINTSTRUCT *);
I(int, EndPaint, U, const PAINTSTRUCT *);
I(int, FillRect, U, const RECT *, U);
I(void, PostQuitMessage, int);
__declspec(dllimport) int __cdecl wsprintfA(char *, const char *, ...);
I(U, GetStockObject, int);
I(U, CreateSolidBrush, U);
I(U, CreatePen, int, int, U);
I(U, SelectObject, U, U);
I(int, MoveToEx, U, int, int, void *);
I(int, LineTo, U, int, int);
I(int, Ellipse, U, int, int, int, int);
I(int, TextOutA, U, int, int, const char *, int);
I(U, SetBkColor, U, U);
I(U, SetTextColor, U, U);
I(U, GetModuleHandleA, const char *);
I(U, GetTickCount, void);
I(U, GetLastError, void);
I(void, SetLastError, U);
I(U, TlsGetValue, U);
I(int, TlsSetValue, U, U);
I(U, CreateThread, void *, U, void *, void *, U, U *);
I(void, Sleep, U);
I(U, CreateEventA, void *, int, int, const char *);
I(int, SetEvent, U);
I(U, WaitForSingleObject, U, U);
I(U, CreateFileA, const char *, U, U, void *, U, U, U);
I(int, WriteFile, U, const void *, U, U *, void *);
I(int, ReadFile, U, void *, U, U *, void *);
I(U, SetFilePointer, U, int, int *, U);
I(int, CloseHandle, U);
I(U, GetProcessHeap, void);
I(void *, HeapAlloc, U, U, U);
I(void, ExitProcess, U);
I(U, GetModuleFileNameA, U, char *, U);
I(int, MessageBoxA, U, const char *, const char *, U);
I(const char *, GetCommandLineA, void);
I(int, DialogBoxIndirectParamA, U, const void *, U, void *, U);
I(int, EndDialog, U, int);
I(int, PeekMessageA, void *, U, U, U, U);
I(int, EnumWindows, void *, U);
I(U, CreatePopupMenu, void);
#ifdef STATE_BAR_MENU
I(U, CreateMenu, void);
#endif
I(int, AppendMenuA, U, U, U, const char *);
I(U, TrackPopupMenu, U, U, int, int, int, U, const RECT *);
I(int, DestroyMenu, U);
#ifdef STATE_CRT_THREADS
__declspec(dllimport) U __cdecl _beginthreadex(void *, U, void *, void *, U, U *);
__declspec(dllimport) U __cdecl _beginthread(void *, U, void *);
#ifdef STATE_CRT_EXPLICIT_END
__declspec(dllimport) void __cdecl _endthreadex(U);
__declspec(dllimport) void __cdecl _endthread(void);
#endif
I(int, GetExitCodeThread, U, U *);
I(U, GetLastError, void);
static volatile U crt_finish;
static U crt_ex, crt_legacy, crt_legacy_returned;
#endif
#ifdef STATE_FILE_DIALOG
typedef struct {
    U size, owner, instance; const char *filter; char *custom_filter;
    U max_custom, filter_index; char *file; U max_file; char *file_title;
    U max_title; const char *initial, *title; U flags;
    unsigned short file_offset, file_extension;
    const char *default_extension; U custom; void *hook; const char *template_name;
} OPENFILENAME;
I(int, GetOpenFileNameA, OPENFILENAME *);
#endif

static U counter, clicks, keys, sleeps, wakes, data_file, wake_event, window, started, paint_box;
static U creating;
/* The files lie beside the program: D:\ in the tests of the machine state,
   the folder of the game in a game machine. */
static char folder[260], path_buffer[300];
static const char *Beside(const char *name)
{
    U i = 0, j = 0;
    for (; folder[i]; ++i) path_buffer[i] = folder[i];
    for (; name[j]; ++j) path_buffer[i + j] = name[j];
    path_buffer[i + j] = 0;
    return path_buffer;
}
static U brushes[4], pen;
static unsigned char *block;
enum { BlockBytes = 300000 };

static int Length(const char *s) { int n = 0; while (s[n]) ++n; return n; }
static U Checksum(void)
{
    U sum = 0, i;
    for (i = 0; i < BlockBytes; ++i) sum = sum * 31 + block[i];
    return sum;
}
static void Report(const char *why, const char *path)
{
    char text[512];
    U position = SetFilePointer(data_file, 0, 0, 1), first = 0, last = 0, done = 0, file;
    /* The file works through the handle that was open all along: the first
       record and the last one, then back to where the next one goes. */
    SetFilePointer(data_file, 0, 0, 0);
    ReadFile(data_file, &first, 4, &done, 0);
    if (position >= 4) {
        SetFilePointer(data_file, (int)position - 4, 0, 0);
        ReadFile(data_file, &last, 4, &done, 0);
    }
    SetFilePointer(data_file, (int)position, 0, 0);
    wsprintfA(text, "why=%s\r\ncounter=%u\r\nclicks=%u\r\nkeys=%u\r\nsleeps=%u\r\nwakes=%u\r\nposition=%u\r\nfirst=%u\r\n"
                    "last=%u\r\nchecksum=%u\r\ntick=%u\r\n",
              why, counter, clicks, keys, sleeps, wakes, position, first, last, Checksum(), GetTickCount() - started);
    file = CreateFileA(path, 0x40000000, 0, 0, 2, 0x80, 0);
    if (file != 0xffffffff) {
        WriteFile(file, text, (U)Length(text), &done, 0);
        CloseHandle(file);
    }
}
static U __stdcall Sleeper(void *unused)
{
    (void)unused;
    TlsSetValue(79, 0x1234abcd);
    for (;;) {
        Sleep(100); ++sleeps;
        if (TlsGetValue(79) != 0x1234abcd) ExitProcess(71);
#ifdef STATE_CRT_THREADS
        if (crt_finish) {
#ifdef STATE_CRT_EXPLICIT_END
            _endthreadex(73);
#endif
            return 73;
        }
#endif
    }
}
#ifdef STATE_CRT_THREADS
static void __cdecl LegacyWaiter(void *unused)
{
    (void)unused;
    for (;;) {
        WaitForSingleObject(wake_event, 0xffffffff); ++wakes;
        if (crt_finish) {
            ++crt_legacy_returned;
#ifdef STATE_CRT_EXPLICIT_END
            _endthread();
#endif
            return;
        }
    }
}
#endif
static U __stdcall Waiter(void *unused)
{
    (void)unused;
    for (;;) { WaitForSingleObject(wake_event, 0xffffffff); ++wakes; }
}
static void Paint(U hwnd)
{
    PAINTSTRUCT ps;
    char text[96];
    U dc = BeginPaint(hwnd, &ps), old, i;
    RECT all = {0, 0, 360, 220}, bar = {10, 60, 10 + (int)(counter % 300), 90};
    FillRect(dc, &all, brushes[0]);
    FillRect(dc, &bar, brushes[1 + counter % 3]);
    old = SelectObject(dc, pen);
    for (i = 0; i < 8; ++i) {
        MoveToEx(dc, 10 + (int)((counter + i * 37) % 320), 100, 0);
        LineTo(dc, 10 + (int)((counter * 3 + i * 53) % 320), 190);
    }
    SelectObject(dc, brushes[1 + (counter / 7) % 3]);
    Ellipse(dc, 300, 10, 340, 50);
    SelectObject(dc, old);
    SetBkColor(dc, 0x00ffffff);
    SetTextColor(dc, 0x00000000);
    wsprintfA(text, "counter=%u clicks=%u keys=%u", counter, clicks, keys);
    TextOutA(dc, 10, 10, text, Length(text));
    wsprintfA(text, "position=%u wakes=%u", SetFilePointer(data_file, 0, 0, 1), wakes);
    TextOutA(dc, 10, 30, text, Length(text));
    EndPaint(hwnd, &ps);
}
static void Note(const char *file, U value);
/* The enumeration has work left after its callback. A save requested
   during it must wait for the call to finish rather than discard its tail. */
static int __stdcall BusyCallback(U hwnd, U unused)
{
    (void)hwnd; (void)unused;
    Sleep(700);
    return 0;
}
static int __stdcall DialogProc(U hwnd, U message, U wparam, U lparam);
static const unsigned short dialog_template[];
/* A loop of its own inside the window procedure, the way a game runs that
   never returns to its first message loop: E ends it. */
static void Loop(void)
{
    struct { U hwnd, message, wparam, lparam, time; int x, y; } m;
    for (;;) {
        if (PeekMessageA(&m, 0, 0, 0, 1)) {
            if (m.message == 0x100 && m.wparam == 'E') break;
            DispatchMessageA((void *)&m);
        } else Sleep(1);
    }
    Note("LOOP.TXT", 1);
}
static U __stdcall Proc(U hwnd, U message, U wparam, U lparam)
{
    if (message == 1 && creating) {
        creating = 0;
        Note("BOX.TXT", (U)MessageBoxA(hwnd, "A box during WM_CREATE.", "StateFixture box", 1));
        return 0;
    }
    U done;
    switch (message) {
    case 0x113: /* WM_TIMER */
        /* The TLS value survives a saved state and remains private to this
           thread. Error access and invalid-index fallback still work. */
        if (TlsGetValue(79) != 0x5678abcd) ExitProcess(72);
        SetLastError(123);
        if (GetLastError() != 123 || TlsSetValue(80, 1) || GetLastError() != 87) ExitProcess(73);
        SetLastError(0);
        ++counter;
        WriteFile(data_file, &counter, 4, &done, 0);
        InvalidateRect(hwnd, 0, 0);
        return 0;
    case 0xf:
        if (paint_box) {
            paint_box = 0;
            Note("BOX.TXT", (U)MessageBoxA(hwnd, "A box inside WM_PAINT.", "StateFixture paint", 1));
        }
        Paint(hwnd); return 0;
    case 0x201: ++clicks; SetEvent(wake_event); Sleep(30); Report("click", Beside("CLICK.TXT")); InvalidateRect(hwnd, 0, 0); return 0;
#ifdef STATE_BAR_MENU
    case 0x111: Note("BAR.TXT", wparam & 65535); return 0;
    case 0x212: Note("MENUEXIT.TXT", 1); return 0;
#endif
    case 0x100: /* WM_KEYDOWN: Q ends the program */
        ++keys;
        if (wparam == 'V') { paint_box = 1; InvalidateRect(hwnd, 0, 0); return 0; }
        if (wparam == 'T') { EnumWindows(BusyCallback, 0); Note("BUSY.TXT", 1); return 0; }
        if (wparam == 'P') {
            U menu = CreatePopupMenu();
            AppendMenuA(menu, 0, 7, "Resume");
            Note("POPUP.TXT", TrackPopupMenu(menu, 0x100, 120, 160, 0, hwnd, 0));
            DestroyMenu(menu);
            return 0;
        }
        if (wparam == 'R') { Note("MOVE.TXT", DefWindowProcA(hwnd, 0x112, 0xf010, 0)); return 0; }
#ifdef STATE_FILE_DIALOG
        if (wparam == 'F') {
            char file[260];
            OPENFILENAME ofn;
            volatile unsigned char *bytes = (volatile unsigned char *)&ofn;
            for (U i = 0; i < sizeof ofn; ++i) bytes[i] = 0;
            file[0] = 0;
            ofn.size = sizeof ofn; ofn.owner = hwnd; ofn.file = file; ofn.max_file = sizeof file;
            ofn.filter = "All files\0*.*\0\0"; ofn.initial = "D:\\"; ofn.flags = 0x8000c;
            Note("FILE.TXT", (U)GetOpenFileNameA(&ofn));
            return 0;
        }
#endif
        /* M opens a message box inside the window procedure. */
        if (wparam == 'M') { Note("BOX.TXT", (U)MessageBoxA(hwnd, "A modal dialog of the fixture.", "StateFixture box", 0)); return 0; }
        /* D opens a dialog with two buttons; its second button opens a
           message box inside the dialog procedure. */
        if (wparam == 'D') { Note("DIALOG.TXT", (U)DialogBoxIndirectParamA(GetModuleHandleA(0), dialog_template, hwnd, DialogProc, 0)); return 0; }
        if (wparam == 'I') { Note("DIALOG.TXT", (U)DialogBoxIndirectParamA(GetModuleHandleA(0), dialog_template, hwnd, DialogProc, 1)); return 0; }
        if (wparam == 'H') {
            creating = 1;
            U child = CreateWindowExA(0, "StateFixture", "Created fixture", 0x80c80000, 450, 40, 200, 180, hwnd, 0, GetModuleHandleA(0), 0);
            Note("CREATE.TXT", child != 0);
            SetFocus(hwnd);
            return 0;
        }
        if (wparam == 'L') { Loop(); return 0; }
#ifdef STATE_CRT_THREADS
        /* V opens the paint dialog; C completes the CRT thread callbacks. */
        if (wparam == 'C') {
            U code = 0, ignored = 0;
            crt_finish = 1; SetEvent(wake_event);
            U waited = WaitForSingleObject(crt_ex, 3000);
            int got = GetExitCodeThread(crt_ex, &code);
            Sleep(100);
            int legacy_got = GetExitCodeThread(crt_legacy, &ignored);
            U legacy_error = GetLastError();
            int closed = CloseHandle(crt_ex);
            Note("CRT.TXT", waited == 0 && got && code == 73 && closed &&
                crt_legacy_returned == 1 && !legacy_got && legacy_error == 6);
            return 0;
        }
#endif
        if (wparam == 'Q') { Report("quit", Beside("QUIT.TXT")); PostQuitMessage(0); return 0; }
        SetEvent(wake_event);
        Sleep(30);
        Report("key", Beside("KEY.TXT"));
        InvalidateRect(hwnd, 0, 0);
        return 0;
    case 2: PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(hwnd, message, wparam, lparam);
}
/* What a modal call answered, into a file beside the program: the program
   started with "box" or "dialog" opens one before its message loop, so a
   state can be taken while the thread stands in that call itself. */
static void Note(const char *file, U value)
{
    char text[64];
    U done, handle = CreateFileA(Beside(file), 0x40000000, 0, 0, 2, 0x80, 0);
    wsprintfA(text, "result=%u\r\ncounter=%u\r\n", value, counter);
    if (handle != 0xffffffff) { WriteFile(handle, text, (U)Length(text), &done, 0); CloseHandle(handle); }
}
static int __stdcall DialogProc(U hwnd, U message, U wparam, U lparam)
{
    (void)lparam;
    if (message == 0x110) {
        if (lparam == 1) Note("INIT.TXT", (U)MessageBoxA(hwnd, "A box during WM_INITDIALOG.", "StateFixture box", 1));
        return 1;
    }
    if (message == 0x111) {                                       /* WM_COMMAND */
        int id = (int)(wparam & 0xffff);
        if (id == 3) id += MessageBoxA(hwnd, "A box of the dialog.", "StateFixture box", 1);
        EndDialog(hwnd, 40 + id);
        return 1;
    }
    return 0;
}
static int Has(const char *text, const char *word)
{
    U i, j;
    for (i = 0; text[i]; ++i) {
        for (j = 0; word[j] && text[i + j] == word[j]; ++j) {}
        if (!word[j]) return 1;
    }
    return 0;
}
/* A dialog with two buttons, as DialogBoxIndirectParam reads it. */
static const unsigned short dialog_template[] = {
    0x0080, 0x90c8, 0, 0, 2, 20, 20, 120, 50,          /* DS_MODALFRAME | WS_POPUP | WS_VISIBLE | WS_CAPTION | WS_SYSMENU, two items */
    0, 0, 'S', 't', 'a', 't', 'e', 'F', 'i', 'x', 't', 'u', 'r', 'e', ' ', 'd', 'i', 'a', 'l', 'o', 'g', 0, 0,
    0x0001, 0x5001, 0, 0, 10, 20, 45, 14, 1,          /* BS_DEFPUSHBUTTON | WS_CHILD | WS_VISIBLE | WS_TABSTOP, id 1 */
    0xffff, 0x0080, 'O', 'K', 0, 0,
    0,                                                /* (an item begins on a multiple of four) */
    0x0000, 0x5001, 0, 0, 65, 20, 45, 14, 3,          /* BS_PUSHBUTTON, id 3 */
    0xffff, 0x0080, 'B', 'o', 'x', 0, 0,
};
void mainCRTStartup(void)
{
    WNDCLASSA c = {0};
    MSG m;
    U i;
    started = GetTickCount();
    TlsSetValue(79, 0x5678abcd);
    block = HeapAlloc(GetProcessHeap(), 0, BlockBytes);
    for (i = 0; i < BlockBytes; ++i) block[i] = (unsigned char)(i * 7 + (i >> 8));
    brushes[0] = CreateSolidBrush(0x00ffffff);
    brushes[1] = CreateSolidBrush(0x000000c0);
    brushes[2] = CreateSolidBrush(0x0000a000);
    brushes[3] = CreateSolidBrush(0x00c06000);
    pen = CreatePen(0, 2, 0x00800080);
    {
        U length = GetModuleFileNameA(0, folder, sizeof folder - 1), cut = 0, k;
        for (k = 0; k < length; ++k) if (folder[k] == '\\') cut = k + 1;
        folder[cut] = 0;
    }
    data_file = CreateFileA(Beside("STATE.DAT"), 0xc0000000, 1, 0, 2, 0x80, 0);
    wake_event = CreateEventA(0, 0, 0, 0);
#ifdef STATE_CRT_THREADS
    crt_ex = _beginthreadex(0, 0, Sleeper, 0, 0, &i);
    crt_legacy = _beginthread(LegacyWaiter, 0, 0);
#else
    CreateThread(0, 0, Sleeper, 0, 0, &i);
    CreateThread(0, 0, Waiter, 0, 0, &i);
#endif
    c.proc = Proc;
    c.instance = GetModuleHandleA(0);
    c.brush = GetStockObject(0);
    c.name = "StateFixture";
    RegisterClassA(&c);
    U bar = 0;
#ifdef STATE_BAR_MENU
    bar = CreateMenu();
    U first = CreatePopupMenu(), second = CreatePopupMenu();
    AppendMenuA(first, 0, 7, "Resume first"); AppendMenuA(second, 0, 8, "Resume second");
    AppendMenuA(bar, 0x10, first, "&First"); AppendMenuA(bar, 0x10, second, "&Second");
#endif
    window = CreateWindowExA(0, "StateFixture", "State fixture", 0x10cf0000, 40, 40, 380, 260, 0, bar, c.instance, 0);
    ShowWindow(window, 5);
    SetFocus(window);
    SetTimer(window, 1, 50, 0);
    if (Has(GetCommandLineA(), " box")) Note("BOX.TXT", (U)MessageBoxA(window, "A modal box before the loop.", "StateFixture box", 1));
    if (Has(GetCommandLineA(), " dialog"))
        Note("DIALOG.TXT", (U)DialogBoxIndirectParamA(c.instance, dialog_template, window, DialogProc, 0));
    while (GetMessageA(&m, 0, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
    ExitProcess(0);
}
