/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 *
 * What ShellExecuteA, ShellExecuteExA, WinExec, CreateProcessA, LoadModule
 * and GetFileAttributesA answer for programs that do not start
 * (tools/test_start_errors_host.py). Each case writes one line into
 * C:\SE\RESULT.TXT:
 *
 *   label|GFA attributes/error|SE result/error|SEX ok,hInstApp/error|
 *   WE result/error|CP ok/error|CPA ok/error|LM result/error
 *
 * then the program opens the shell's Run dialog (SHELL32 ordinal 61) for a
 * few of the cases, types the command and presses OK, so the log of the run
 * holds the message Windows shows. A thread of its own closes every box that
 * comes up. The programs it starts are copies of itself: QUIET.EXE ends at
 * once, MISSEXP.EXE imports a name KERNEL32 does not export, MISSDLL.EXE a
 * DLL that does not exist.
 */
#include <windows.h>
#include <shellapi.h>

void *memset(void *p, int c, unsigned n)
{
    unsigned char *q = p;
    while (n--)
        *q++ = (unsigned char)c;
    return p;
}
void *memcpy(void *to, const void *from, unsigned n)
{
    unsigned char *q = to;
    const unsigned char *p = from;
    while (n--)
        *q++ = *p++;
    return to;
}
int memcmp(const void *a, const void *b, unsigned n)
{
    const unsigned char *p = a, *q = b;
    for (; n; n--, p++, q++)
        if (*p != *q)
            return *p - *q;
    return 0;
}
static HANDLE out = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION put_lock;
static void put(const char *s)
{
    DWORD n;
    EnterCriticalSection(&put_lock);
    WriteFile(out, s, lstrlenA(s), &n, 0);
    LeaveCriticalSection(&put_lock);
}
static void file(const char *name, const char *text, DWORD size)
{
    HANDLE h = CreateFileA(name, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    DWORD n;
    if (h == INVALID_HANDLE_VALUE)
        return;
    if (size)
        WriteFile(h, text, size, &n, 0);
    CloseHandle(h);
}

/* The boxes: closed with Cancel, or with OK the second time. The Run dialog
   ("StartRun") gets its command and OK; once nothing else is up for a
   while, Cancel. */
static volatile LONG run_pending;
static char run_text[300];
static volatile DWORD run_clicked;
static HWND run_dialog;
static HWND seen[64];
static int seen_count;
static BOOL CALLBACK edit_of(HWND child, LPARAM found)
{
    char klass[32];
    GetClassNameA(child, klass, sizeof klass);
    if (!lstrcmpiA(klass, "ComboBox") || !lstrcmpiA(klass, "Edit")) {
        *(HWND *)found = child;
        return FALSE;
    }
    return TRUE;
}
static BOOL CALLBACK boxes(HWND window, LPARAM unused)
{
    char klass[32], title[200];
    int i, known = 0;
    if (!IsWindowVisible(window))
        return TRUE;
    GetClassNameA(window, klass, sizeof klass);
    if (lstrcmpA(klass, "#32770"))
        return TRUE;
    GetWindowTextA(window, title, sizeof title);
    if (!lstrcmpA(title, "StartRun")) {
        run_dialog = window;
        if (run_pending) {
            HWND edit = 0;
            EnumChildWindows(window, edit_of, (LPARAM)&edit);
            if (edit)
                SetWindowTextA(edit, run_text);
            run_pending = 0;
            run_clicked = GetTickCount();
            PostMessageA(window, WM_COMMAND, IDOK, (LPARAM)GetDlgItem(window, IDOK));
        }
        return TRUE;
    }
    for (i = 0; i < seen_count; i++)
        known = known || seen[i] == window;
    if (!known) {
        if (seen_count < 64)
            seen[seen_count++] = window;
        PostMessageA(window, WM_COMMAND, IDCANCEL, 0);
    } else {
        PostMessageA(window, WM_COMMAND, IDOK, 0);
        PostMessageA(window, WM_CLOSE, 0, 0);
    }
    run_clicked = GetTickCount();
    return TRUE;
}
static DWORD WINAPI watcher(LPVOID unused)
{
    for (;;) {
        Sleep(150);
        run_dialog = 0;
        EnumWindows(boxes, 0);
        if (run_dialog && !run_pending && run_clicked && GetTickCount() - run_clicked > 1500) {
            PostMessageA(run_dialog, WM_COMMAND, IDCANCEL, 0);
            run_clicked = 0;
        }
    }
    return 0;
}

enum { SE = 1, SEX = 2, WE = 4, CP = 8, CPA = 16, LM = 32, GFA = 64, ALL = 127, NOSHELL = ALL & ~(SE | SEX) };
static void measure(const char *label, const char *target, int which)
{
    char line[600], part[160];
    DWORD r, e;
    wsprintfA(line, "%s", label);
    if (which & GFA) {
        SetLastError(0);
        r = GetFileAttributesA(target);
        e = GetLastError();
        wsprintfA(part, "|GFA %lx/%lu", r, e);
        lstrcatA(line, part);
    }
    if (which & SE) {
        SetLastError(0);
        r = (DWORD)ShellExecuteA(0, "open", target, 0, "C:\\SE", SW_SHOWMINNOACTIVE);
        e = GetLastError();
        wsprintfA(part, "|SE %lu/%lu", r > 32 ? 33 : r, e);
        lstrcatA(line, part);
    }
    if (which & SEX) {
        SHELLEXECUTEINFOA info;
        ZeroMemory(&info, sizeof info);
        info.cbSize = sizeof info;
        info.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOCLOSEPROCESS;
        info.lpVerb = "open";
        info.lpFile = target;
        info.lpDirectory = "C:\\SE";
        info.nShow = SW_SHOWMINNOACTIVE;
        info.hInstApp = (HINSTANCE)99;
        SetLastError(0);
        r = ShellExecuteExA(&info);
        e = GetLastError();
        wsprintfA(part, "|SEX %lu,%lu/%lu", r, (DWORD)info.hInstApp > 32 ? 33 : (DWORD)info.hInstApp, e);
        lstrcatA(line, part);
        if (info.hProcess)
            CloseHandle(info.hProcess);
    }
    if (which & WE) {
        SetLastError(0);
        r = WinExec(target, SW_SHOWMINNOACTIVE);
        e = GetLastError();
        wsprintfA(part, "|WE %lu/%lu", r > 32 ? 33 : r, e);
        lstrcatA(line, part);
    }
    if (which & (CP | CPA)) {
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        char command[300];
        int pass;
        for (pass = 0; pass < 2; pass++) {
            if (!(which & (pass ? CPA : CP)))
                continue;
            ZeroMemory(&si, sizeof si);
            si.cb = sizeof si;
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_SHOWMINNOACTIVE;
            ZeroMemory(&pi, sizeof pi);
            lstrcpyA(command, target);
            SetLastError(0);
            r = pass ? CreateProcessA(target, 0, 0, 0, FALSE, 0, 0, "C:\\SE", &si, &pi)
                     : CreateProcessA(0, command, 0, 0, FALSE, 0, 0, "C:\\SE", &si, &pi);
            e = GetLastError();
            wsprintfA(part, pass ? "|CPA %lu/%lu" : "|CP %lu/%lu", r, e);
            lstrcatA(line, part);
            if (r) {
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
            }
        }
    }
    if (which & LM) {
        WORD show[2] = { 2, SW_SHOWMINNOACTIVE };
        struct { LPVOID env; LPSTR cmd; LPVOID show; DWORD reserved; } params = { 0, "\0", show, 0 };
        SetLastError(0);
        r = LoadModule(target, &params);
        e = GetLastError();
        wsprintfA(part, "|LM %lu/%lu", r > 32 ? 33 : r, e);
        lstrcatA(line, part);
    }
    lstrcatA(line, "\r\n");
    put(line);
    Sleep(300);
}
typedef int(WINAPI *RunFileDlgProc)(HWND, HICON, LPCSTR, LPCSTR, LPCSTR, UINT);
static void run(const char *command)
{
    RunFileDlgProc dialog = (RunFileDlgProc)GetProcAddress(GetModuleHandleA("SHELL32.DLL"), (LPCSTR)61);
    char line[400];
    wsprintfA(line, "RUN %s\r\n", command);
    put(line);
    if (!dialog)
        return;
    lstrcpyA(run_text, command);
    run_clicked = 0;
    run_pending = 1;
    dialog(0, 0, "C:\\SE", "StartRun", "Run", 0);
    Sleep(500);
}
/* A copy of this program with one name of its import table changed. */
static void copy_changed(const char *target, const char *from, const char *to)
{
    char self[MAX_PATH];
    static char image[65536];
    DWORD size = 0, i, n = lstrlenA(from);
    HANDLE h;
    GetModuleFileNameA(0, self, sizeof self);
    h = CreateFileA(self, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    if (h == INVALID_HANDLE_VALUE)
        return;
    ReadFile(h, image, sizeof image, &size, 0);
    CloseHandle(h);
    for (i = 0; i + n <= size; i++)
        if (!memcmp(image + i, from, n))
            CopyMemory(image + i, to, n);
    file(target, image, size);
}
void mainCRTStartup(void)
{
    const char *command = GetCommandLineA();
    const char *end = command + lstrlenA(command);
    HANDLE locked;
    while (end > command && end[-1] == ' ')
        end--;
    char self[MAX_PATH];
    /* The copies end at once, also when they are started without "quiet". */
    GetModuleFileNameA(0, self, sizeof self);
    if ((end - command >= 5 && !lstrcmpiA(end - 5, "quiet")) || lstrcmpiA(self, "C:\\STARTERR.EXE"))
        ExitProcess(7);
    InitializeCriticalSection(&put_lock);
    CreateDirectoryA("C:\\SE", 0);
    out = CreateFileA("C:\\SE\\RESULT.TXT", GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
    CloseHandle(CreateThread(0, 0, watcher, 0, 0, 0));
    file("C:\\SE\\TEXT.EXE", "This is a text file, no program.\r\n", 34);
    file("C:\\SE\\EMPTY.EXE", "", 0);
    file("C:\\SE\\DOC.QQQ", "document\r\n", 10);
    {
        CopyFileA(self, "C:\\SE\\QUIET.EXE", FALSE);
        CopyFileA(self, "C:\\SE\\LOCKED.EXE", FALSE);
    }
    {
        /* The names are put together here, so that only the import table of
           the copy holds them. */
        char name[] = "GetModuleFileNameQ", library[] = "SHELL32.dlQ";
        name[17] = 'A';
        library[10] = 'l';
        copy_changed("C:\\SE\\MISSEXP.EXE", name, "GetModuleFileNameQ");
        copy_changed("C:\\SE\\MISSDLL.EXE", library, "NOSUCH3.DLL");
    }
    measure("missing-file", "C:\\SE\\NOFILE.EXE", ALL);
    measure("missing-folder", "C:\\SE\\NODIR\\NOFILE.EXE", ALL);
    measure("missing-name", "NOFILEXY", ALL);
    measure("text-exe", "C:\\SE\\TEXT.EXE", ALL);
    measure("empty-exe", "C:\\SE\\EMPTY.EXE", ALL);
    measure("no-association", "C:\\SE\\DOC.QQQ", ALL);
    measure("folder", "C:\\SE", NOSHELL);
    locked = CreateFileA("C:\\SE\\LOCKED.EXE", GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0);
    measure("locked-exe", "C:\\SE\\LOCKED.EXE", ALL);
    CloseHandle(locked);
    measure("missing-dll", "C:\\SE\\MISSDLL.EXE", ALL);
    measure("missing-export", "C:\\SE\\MISSEXP.EXE", ALL);
    measure("program", "C:\\SE\\QUIET.EXE quiet", WE | CP);
    run("C:\\SE\\NODIR\\NOFILE.EXE");
    run("C:\\SE\\TEXT.EXE");
    run("C:\\SE\\MISSEXP.EXE");
    put("done\r\n");
    CloseHandle(out);
    ExitProcess(0);
}
