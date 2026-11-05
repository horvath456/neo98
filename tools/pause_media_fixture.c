/* SPDX-License-Identifier: GPL-2.0-or-later
   A CD put into the drive while the machine is paused, seen by the guest
   (tools/test_pause_host.py). The window counts the
   WM_DEVICECHANGE messages with DBT_DEVICEARRIVAL it gets. On the key W it
   appends a line to D:\PMEDIA.TXT: the arrivals so far, the first CD-ROM
   drive above D: (0 when there is none), whether its label is NEO98CD and
   whether \README.TXT on it reads "HELLO CD". The title counts the lines. */
typedef unsigned U;
typedef struct { U style; void *proc; int a, b; U instance, icon, cursor, brush; const char *menu, *name; } WNDCLASSA;
typedef struct { U hwnd, message, wparam, lparam, time; int x, y; } MSG;
#define I(r, n, ...) __declspec(dllimport) r __stdcall n(__VA_ARGS__)
I(U, RegisterClassA, const WNDCLASSA *);
I(U, CreateWindowExA, U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
I(U, DefWindowProcA, U, U, U, U);
I(int, GetMessageA, MSG *, U, U, U);
I(U, DispatchMessageA, const MSG *);
I(int, ShowWindow, U, int);
I(U, SetFocus, U);
I(int, SetWindowTextA, U, const char *);
__declspec(dllimport) int __cdecl wsprintfA(char *, const char *, ...);
I(U, GetStockObject, int);
I(U, GetModuleHandleA, const char *);
I(U, GetDriveTypeA, const char *);
I(int, GetVolumeInformationA, const char *, char *, U, U *, U *, U *, char *, U);
I(U, CreateFileA, const char *, U, U, void *, U, U, U);
I(int, ReadFile, U, void *, U, U *, void *);
I(int, WriteFile, U, const void *, U, U *, void *);
I(U, SetFilePointer, U, int, int *, U);
I(int, CloseHandle, U);
I(void, ExitProcess, U);

static U arrivals, lines;

static int Same(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static void Line(U window)
{
    char root[4] = "E:\\", path[16] = "E:\\README.TXT", label[32] = {0}, read[16] = {0}, text[120];
    U serial = 0, length = 0, flags = 0, got = 0, written, file, n, drive = 0, labelled = 0, readable = 0;
    for (; root[0] <= 'Z'; ++root[0])
        if (GetDriveTypeA(root) == 5) { drive = (U)root[0]; break; }
    if (drive)
    {
        path[0] = (char)drive;
        if (GetVolumeInformationA(root, label, sizeof label, &serial, &length, &flags, 0, 0)) labelled = Same(label, "NEO98CD");
        file = CreateFileA(path, 0x80000000, 1, 0, 3, 0, 0);
        if (file != 0xffffffff)
        {
            if (ReadFile(file, read, sizeof read - 1, &got, 0) && got == 8) readable = Same(read, "HELLO CD");
            CloseHandle(file);
        }
    }
    n = wsprintfA(text, "arrivals=%u drive=%u label=%u file=%u\r\n", arrivals, drive, labelled, readable);
    file = CreateFileA("D:\\PMEDIA.TXT", 0x40000000, 0, 0, 4, 0x80, 0);   /* OPEN_ALWAYS */
    SetFilePointer(file, 0, 0, 2);
    WriteFile(file, text, n, &written, 0);
    CloseHandle(file);
    wsprintfA(text, "Media %u", ++lines);
    SetWindowTextA(window, text);
}
static U __stdcall Procedure(U hwnd, U message, U wparam, U lparam)
{
    if (message == 0x219 && wparam == 0x8000) ++arrivals;   /* WM_DEVICECHANGE, DBT_DEVICEARRIVAL */
    if (message == 0x100 && wparam == 'W') Line(hwnd);
    if (message == 2) ExitProcess(0);
    return DefWindowProcA(hwnd, message, wparam, lparam);
}
void __stdcall mainCRTStartup(void)
{
    WNDCLASSA c = {0};
    MSG message;
    U window;
    c.proc = Procedure;
    c.instance = GetModuleHandleA(0);
    c.brush = GetStockObject(0);
    c.name = "PauseMediaFixture";
    RegisterClassA(&c);
    window = CreateWindowExA(0, "PauseMediaFixture", "Media ready", 0x00cf0000, 40, 40, 400, 300, 0, 0, c.instance, 0);
    ShowWindow(window, 1);
    SetFocus(window);
    while (GetMessageA(&message, 0, 0, 0) > 0) DispatchMessageA(&message);
    ExitProcess(0);
}
