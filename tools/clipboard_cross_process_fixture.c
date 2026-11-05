/* SPDX-License-Identifier: GPL-2.0-or-later */
#define fixture_entry unused_fixture_entry
#include "host_clipboard_fixture.c"
#undef fixture_entry
API int CreateProcessA(const char *, char *, void *, void *, int, U, void *, const char *, U *, U *);
API H SetClipboardViewer(H);
API int ChangeClipboardChain(H, H);
API H GetClipboardOwner(void);
API int PostMessageA(H, U, U, U);
static U format, rendered, destroyed, drawn;
static H next_viewer;
static const char payload[] = "Delayed across processes: 123";
static void require(int okay, U code) { if (!okay) ExitProcess(code); }
static int __stdcall owner_window(H window, U message, U wp, U lp)
{
    if (message == 0x305) {
        require(wp == format, 51);
        ++rendered;
        require(SetClipboardData(format, block(payload, sizeof payload, 0)) != 0, 52);
        put("render\r\n"); write_output();
        return 0;
    }
    if (message == 0x307) { ++destroyed; put("destroy\r\n"); write_output(); return 0; }
    if (message == 0x308) { ++drawn; put("draw\r\n"); write_output(); return 0; }
    if (message == 0x8001) {
        require(rendered == 1 && destroyed == 1 && drawn >= 3, 53);
        ChangeClipboardChain(window, next_viewer);
        put("PASS owner\r\n"); write_output();
        PostQuitMessage(0);
        return 0;
    }
    if (message == 0x113) ExitProcess(54);
    return DefWindowProcA(window, message, wp, lp);
}
void fixture_entry(void)
{
    GetModuleFileNameA(0, path, sizeof path);
    format = RegisterClipboardFormatA("NEO98CrossProcessDelayed");
    require(format != 0, 55);
    if (named("READ")) {
        require(OpenClipboard(0), 56);
        H owner = GetClipboardOwner();
        H data = GetClipboardData(format);
        const char *p = data ? GlobalLock(data) : 0;
        require(p != 0, 57);
        for (U i = 0; i < sizeof payload; ++i) require(p[i] == payload[i], 58);
        GlobalUnlock(data);
        require(CloseClipboard(), 59);
        require(OpenClipboard(0), 60);
        require(EmptyClipboard(), 61);
        require(CloseClipboard(), 62);
        put("PASS reader\r\n"); write_output();
        require(PostMessageA(owner, 0x8001, 0, 0), 63);
        ExitProcess(0);
    }
    struct { U style; void *procedure; int a, b; H instance, icon, cursor, brush; const char *menu, *name; } cls = {0};
    cls.procedure = (void *)owner_window;
    cls.instance = GetModuleHandleA(0);
    cls.name = "NEO98CrossClipboard";
    require(RegisterClassA(&cls) != 0, 64);
    H window = CreateWindowExA(0, cls.name, "owner", 0, 0, 0, 10, 10, 0, 0, cls.instance, 0);
    require(window != 0, 65);
    next_viewer = SetClipboardViewer(window);
    require(OpenClipboard(window), 66);
    require(EmptyClipboard(), 67);
    destroyed = 0;
    SetClipboardData(format, 0);
    require(CloseClipboard(), 68);
    U startup[17] = {68}, process[4] = {0};
    require(CreateProcessA("E:\\READ.EXE", 0, 0, 0, 0, 0, 0, 0, startup, process), 69);
    CloseHandle((H)process[0]); CloseHandle((H)process[1]);
    SetTimer(window, 1, 7000, 0);
    U message[8];
    while (GetMessageA(message, 0, 0, 0) > 0) DispatchMessageA(message);
    ExitProcess(0);
}
