/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 *
 * Hooks of the session that write where they are called, into
 * C:\MEAS\HOOK.TXT: the process and its module, the event, the class and the
 * title of the window (tools/test_session_hooks_dos_host.py). WH_CBT writes
 * creation, activation and focus, WH_CALLWNDPROC WM_SETFOCUS and
 * WM_INITDIALOG, WH_GETMESSAGE clicks and keys, each for the windows of the
 * MS-DOS window and its property sheet only. When the MS-DOS window is
 * activated, WH_CBT puts a procedure of the DLL before the window's own;
 * it writes WM_SETFOCUS and the keys that pass through it.
 */
#include <windows.h>
void *memset(void *p,int c,unsigned n){unsigned char*q=p;while(n--)*q++=(unsigned char)c;return p;}
static HINSTANCE self;
static HHOOK cbt, callwnd, getmsg;
static void log_line(const char *kind, HWND w, UINT message)
{
    char line[400], klass[64] = "", title[64] = "", module[MAX_PATH] = "";
    DWORD n;
    HANDLE f;
    if (w) { GetClassNameA(w, klass, sizeof klass); GetWindowTextA(w, title, sizeof title); }
    GetModuleFileNameA(0, module, sizeof module);
    wsprintfA(line, "%s pid=%lx tid=%lx module=%s msg=%x class=%s title=%s\r\n", kind, GetCurrentProcessId(),
              GetCurrentThreadId(), module, message, klass, title);
    f = CreateFileA("C:\\MEAS\\HOOK.TXT", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_ALWAYS, 0, 0);
    if (f == INVALID_HANDLE_VALUE) return;
    SetFilePointer(f, 0, 0, FILE_END);
    WriteFile(f, line, lstrlenA(line), &n, 0);
    CloseHandle(f);
}
static int interesting(HWND w)
{
    char klass[64] = "";
    if (!w) return 0;
    GetClassNameA(w, klass, sizeof klass);
    return !lstrcmpiA(klass, "tty") || !lstrcmpA(klass, "#32770") || !lstrcmpiA(klass, "Edit") ||
           !lstrcmpiA(klass, "ComboBox") || !lstrcmpiA(klass, "SysTabControl32");
}
/* A procedure put before that of the MS-DOS window when it is activated,
   as a shell's hook does it to dress the window: it notes WM_SETFOCUS and
   the keys and hands everything to the window's own procedure. */
static WNDPROC tty_procedure;
static HWND subclassed;
static LRESULT CALLBACK Subclass(HWND w, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == WM_SETFOCUS || message == WM_CHAR)
        log_line("SUBCLASS", w, message);
    return CallWindowProcA(tty_procedure, w, message, wp, lp);
}
__declspec(dllexport) LRESULT CALLBACK CbtProc(int code, WPARAM w, LPARAM l)
{
    if (code == HCBT_ACTIVATE && !subclassed) {
        char klass[16] = "";
        GetClassNameA((HWND)w, klass, sizeof klass);
        if (!lstrcmpiA(klass, "tty")) {
            subclassed = (HWND)w;
            tty_procedure = (WNDPROC)SetWindowLongA((HWND)w, GWL_WNDPROC, (LONG)Subclass);
        }
    }
    if (code == HCBT_CREATEWND || code == HCBT_ACTIVATE || code == HCBT_SETFOCUS)
        if (interesting((HWND)w))
            log_line(code == HCBT_CREATEWND ? "CBT-CREATE" : code == HCBT_ACTIVATE ? "CBT-ACTIVATE" : "CBT-FOCUS",
                     (HWND)w, 0);
    return CallNextHookEx(cbt, code, w, l);
}
__declspec(dllexport) LRESULT CALLBACK CallWndProc(int code, WPARAM w, LPARAM l)
{
    CWPSTRUCT *c = (CWPSTRUCT *)l;
    if (code >= 0 && c && (c->message == WM_SETFOCUS || c->message == WM_INITDIALOG) && interesting(c->hwnd))
        log_line("CALLWND", c->hwnd, c->message);
    return CallNextHookEx(callwnd, code, w, l);
}
__declspec(dllexport) LRESULT CALLBACK GetMsgProc(int code, WPARAM w, LPARAM l)
{
    MSG *m = (MSG *)l;
    if (code >= 0 && m && (m->message == WM_LBUTTONDOWN || m->message == WM_KEYDOWN || m->message == WM_CHAR) &&
        interesting(m->hwnd))
        log_line("GETMSG", m->hwnd, m->message);
    return CallNextHookEx(getmsg, code, w, l);
}
__declspec(dllexport) void Install(void)
{
    cbt = SetWindowsHookExA(WH_CBT, CbtProc, self, 0);
    callwnd = SetWindowsHookExA(WH_CALLWNDPROC, CallWndProc, self, 0);
    getmsg = SetWindowsHookExA(WH_GETMESSAGE, GetMsgProc, self, 0);
}
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID unused)
{
    if (reason == DLL_PROCESS_ATTACH) { self = h; log_line("ATTACH", 0, 0); }
    return TRUE;
}
