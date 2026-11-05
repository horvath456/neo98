/* SPDX-License-Identifier: GPL-2.0-or-later */
/* DEVCHG.EXE of tools/test_media_change_guest.py.

   A top-level window that writes every WM_DEVICECHANGE it gets into
   C:\DEVCHG.LOG, one line each: the event, the device type, the unit mask and
   the flags of the DEV_BROADCAST_VOLUME, then what the drive shows at that
   moment: the volume label and whether the file the test put on the medium is
   there. Before the first change it opens that file and keeps the handle; a
   ReadFile on it after the medium left must fail with ERROR_NOT_READY.

   Started as...\SETUP.EXE it only writes C:\AUTORUN.OK and ends: that is the
   program the AUTORUN.INF of the test disc names, so the file shows that
   Explorer ran it on the arrival of the disc. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U GetLastError(void);
API void SetLastError(U);
API char *GetCommandLineA(void);
API U CreateFileA(const char *, U, U, U, U, U, U);
API U WriteFile(U, const void *, U, U *, U);
API U ReadFile(U, void *, U, U *, U);
API U SetFilePointer(U, U, U, U);
API U CloseHandle(U);
API U GetVolumeInformationA(const char *, char *, U, U *, U *, U *, char *, U);
API U GetFileAttributesA(const char *);
API U GetModuleHandleA(const char *);
typedef struct { U style; void *proc; int extra, window_extra; U instance, icon, cursor, brush; const char *menu, *name; } Class;
typedef struct { U hwnd, message, wparam, lparam, time, x, y; } Msg;
API unsigned short RegisterClassA(const Class *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
API U DefWindowProcA(U, U, U, U);
API int GetMessageA(Msg *, U, U, U);
API U DispatchMessageA(const Msg *);
API U SetTimer(U, U, U, void *);
API void PostQuitMessage(int);

static char drive_root[4] = "D:\\";
static char probe[64] = "D:\\README.TXT";
static U held = 0xffffffffu;
static U events = 0;

static U length(const char *s) { U n = 0; while (s[n]) ++n; return n; }
static char *put(char *p, const char *s) { while (*s) *p++ = *s++; return p; }
static char *hex(char *p, U v)
{
    for (int shift = 28; shift >= 0; shift -= 4) *p++ = "0123456789abcdef"[(v >> shift) & 15];
    return p;
}
static void line(const char *text)
{
    U file = CreateFileA("C:\\DEVCHG.LOG", 0x40000000u, 1, 0, 4 /* OPEN_ALWAYS */, 0x80, 0);
    if (file == 0xffffffffu) return;
    SetFilePointer(file, 0, 0, 2);
    U done = 0;
    WriteFile(file, text, length(text), &done, 0);
    CloseHandle(file);
}

static U __stdcall procedure(U hwnd, U message, U wparam, U lparam)
{
    if (message == 0x0219) /* WM_DEVICECHANGE */
    {
        char text[256], *p = text;
        const U *record = (const U *)lparam;
        p = put(p, "event="); p = hex(p, wparam);
        if (record && (wparam == 0x8000 || wparam == 0x8004))
        {
            p = put(p, " size="); p = hex(p, record[0]);
            p = put(p, " type="); p = hex(p, record[1]);
            p = put(p, " mask="); p = hex(p, record[3]);
            p = put(p, " flags="); p = hex(p, *(const unsigned short *)((const char *)record + 16));
        }
        char label[32] = {0};
        U serial = 0, maximum = 0, flags = 0;
        SetLastError(0);
        const U volume = GetVolumeInformationA(drive_root, label, sizeof label, &serial, &maximum, &flags, 0, 0);
        p = put(p, volume ? " label=" : " volume-error=");
        if (volume) p = put(p, label[0] ? label : "-");
        else p = hex(p, GetLastError());
        p = put(p, GetFileAttributesA(probe) != 0xffffffffu ? " probe=present" : " probe=absent");
        U current = CreateFileA(probe, 0x80000000u, 1, 0, 3, 0x80, 0);
        char first = 0;
        U current_done = 0;
        const U current_read = current != 0xffffffffu && ReadFile(current, &first, 1, &current_done, 0);
        p = put(p, " current-read="); p = hex(p, current_read ? 0 : GetLastError());
        if (current_read && current_done) { p = put(p, " current-byte="); p = hex(p, (unsigned char)first); }
        if (current != 0xffffffffu) CloseHandle(current);
        if (held != 0xffffffffu)
        {
            char byte;
            U done = 0;
            SetLastError(0);
            const U read = ReadFile(held, &byte, 1, &done, 0);
            p = put(p, " held-read=");
            p = hex(p, read ? 0 : GetLastError());
        }
        p = put(p, "\r\n");
        *p = 0;
        line(text);
        if (++events >= 8) PostQuitMessage(0);
        return 1;
    }
    if (message == 0x0113) /* WM_TIMER: the test is over */
    {
        line("timeout\r\n");
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, message, wparam, lparam);
}

void mainCRTStartup(void)
{
    const char *command = GetCommandLineA();
    for (const char *p = command; *p; ++p)
        if ((p[0] | 32) == 's' && (p[1] | 32) == 'e' && (p[2] | 32) == 't' && (p[3] | 32) == 'u' &&
            (p[4] | 32) == 'p' && p[5] == '.')
        {
            U file = CreateFileA("C:\\AUTORUN.OK", 0x40000000u, 0, 0, 2, 0x80, 0);
            U done = 0;
            WriteFile(file, command, length(command), &done, 0);
            CloseHandle(file);
            ExitProcess(0);
        }
    held = CreateFileA(probe, 0x80000000u, 1, 0, 3 /* OPEN_EXISTING */, 0x80, 0);
    line(held != 0xffffffffu ? "start held=1\r\n" : "start held=0\r\n");
    Class c = {0};
    c.proc = (void *)procedure;
    c.instance = GetModuleHandleA(0);
    c.name = "NEO98DeviceChange";
    RegisterClassA(&c);
    U hwnd = CreateWindowExA(0, c.name, "NEO98 device change", 0x00cf0000u, 0, 0, 200, 100, 0, 0, c.instance, 0);
    SetTimer(hwnd, 1, 120000, 0);
    Msg m;
    while (GetMessageA(&m, 0, 0, 0) > 0) DispatchMessageA(&m);
    ExitProcess(0);
}
