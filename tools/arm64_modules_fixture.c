/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* DLLs of ARM64 programs and hooks of ARM64 threads. The fixture carries a
   DLL and a second program as resources, writes them into the temporary
   folder and then loads the DLL (LoadLibrary, GetProcAddress, FreeLibrary)
   and runs the program that imports from it at load time. Then it sets hooks
   on its own thread. Prints one line per failed check and a summary.
   tools/test_arm64_stage2_native.py, docs/ARM64-APPS.md. */
#include <windows.h>
#include <stdio.h>
#include <shellhelp.h>
#include <shellapi.h>
#include <commdlg.h>

static int checks, failures;
static void Check(int id, int condition)
{
    ++checks;
    if (condition) return;
    ++failures;
    printf("FAIL %d\n", id);
}
static int Unpack(int id, const char *path)
{
    const HRSRC found = FindResourceA(NULL, MAKEINTRESOURCE(id), RT_RCDATA);
    const HGLOBAL loaded = found ? LoadResource(NULL, found) : NULL;
    const void *data = loaded ? LockResource(loaded) : NULL;
    const DWORD size = found ? SizeofResource(NULL, found) : 0;
    DWORD written = 0;
    HANDLE file;
    if (!data || !size) return 0;
    file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    WriteFile(file, data, size, &written, NULL);
    CloseHandle(file);
    return written == size;
}

static void Modules(void)
{
    char folder[MAX_PATH], dll[MAX_PATH], exe[MAX_PATH], marker[MAX_PATH], command[MAX_PATH + 8];
    HMODULE module;
    STARTUPINFOA startup;
    PROCESS_INFORMATION process;
    DWORD code = 0;
    GetTempPathA(MAX_PATH - 32, folder);
    snprintf(dll, sizeof dll, "%sMODDLL.DLL", folder);
    snprintf(exe, sizeof exe, "%sIMPORTER.EXE", folder);
    snprintf(marker, sizeof marker, "%s.detached", dll);
    DeleteFileA(marker);
    Check(100, Unpack(1, dll) && Unpack(2, exe));

    module = LoadLibraryA(dll);
    Check(101, module != NULL);
    if (module)
    {
        int (*add)(int, int) = (int (*)(int, int))GetProcAddress(module, "ModuleAdd");
        int (*attached)(void) = (int (*)(void))GetProcAddress(module, "ModuleAttached");
        HINSTANCE (*instance)(void) = (HINSTANCE (*)(void))GetProcAddress(module, "ModuleInstance");
        int *counter = (int *)GetProcAddress(module, "module_counter");
        char path[MAX_PATH];
        Check(102, add && attached && instance && counter);
        Check(103, add && add(40, 2) == 42);
        Check(104, attached && attached() == 1 && instance && instance() == module);
        Check(105, counter && *counter == 7);
        Check(106, GetProcAddress(module, "NoSuchExport") == NULL);
        Check(107, GetModuleHandleA("MODDLL.DLL") == module && GetModuleFileNameA(module, path, MAX_PATH) && !lstrcmpiA(path, dll));
        Check(108, LoadLibraryA(dll) == module && FreeLibrary(module));
        Check(109, GetFileAttributesA(marker) == 0xffffffffu);
        Check(110, FreeLibrary(module) && GetModuleHandleA("MODDLL.DLL") == NULL);
        /* DllMain saw DLL_PROCESS_DETACH. */
        Check(111, GetFileAttributesA(marker) != 0xffffffffu);
    }
    /* A function of a system DLL by its address. */
    {
        DWORD (WINAPI *ticks)(void) = (DWORD (WINAPI *)(void))GetProcAddress(GetModuleHandleA("KERNEL32.DLL"), "GetTickCount");
        Check(112, ticks && ticks() != 0);
    }
    /* An x86 DLL is no module of an ARM64 process. */
    SetLastError(0);
    Check(113, LoadLibraryA("COMCTL32.DLL") == NULL && GetLastError() == 193);

    /* The program that imports from the DLL when it is loaded. */
    memset(&startup, 0, sizeof startup);
    startup.cb = sizeof startup;
    snprintf(command, sizeof command, "\"%s\"", exe);
    Check(120, CreateProcessA(NULL, command, NULL, NULL, FALSE, 0, NULL, folder, &startup, &process));
    Check(121, WaitForSingleObject(process.hProcess, 30000) == WAIT_OBJECT_0);
    Check(122, GetExitCodeProcess(process.hProcess, &code) && code == 100);
    if (code != 100) printf("importer exit code %lu\n", (unsigned long)code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
}

/* --- hooks ------------------------------------------------------------------ */
static HHOOK message_hook, send_hook, create_hook;
static int message_seen, send_seen, create_seen, activate_seen, next_result_ok = 1;
static HWND hooked_window;
static char created_class[32];
static LRESULT CALLBACK MessageHook(int code, WPARAM wParam, LPARAM lParam)
{
    MSG *message = (MSG *)lParam;
    if (code == HC_ACTION && message->message == WM_APP + 5 && message->hwnd == hooked_window && wParam == PM_REMOVE)
    {
        ++message_seen;
        message->wParam += 1000; /* the program receives what the hook leaves */
    }
    return CallNextHookEx(message_hook, code, wParam, lParam);
}
static LRESULT CALLBACK SendHook(int code, WPARAM wParam, LPARAM lParam)
{
    const CWPSTRUCT *sent = (const CWPSTRUCT *)lParam;
    if (code == HC_ACTION && sent->message == WM_APP + 6 && sent->hwnd == hooked_window && sent->wParam == 11 &&
        sent->lParam == -22)
        ++send_seen;
    return CallNextHookEx(send_hook, code, wParam, lParam);
}
static LRESULT CALLBACK CreateHook(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HCBT_CREATEWND)
    {
        CBT_CREATEWNDA *create = (CBT_CREATEWNDA *)lParam;
        if (create->lpcs && create->lpcs->lpszName && !lstrcmpA(create->lpcs->lpszName, "hooked child"))
        {
            ++create_seen;
            lstrcpynA(created_class, create->lpcs->lpszClass, sizeof created_class);
            create->lpcs->cx = 123; /* the window is created with what the hook leaves */
        }
    }
    if (code == HCBT_ACTIVATE)
    {
        const CBTACTIVATESTRUCT *activate = (const CBTACTIVATESTRUCT *)lParam;
        if ((HWND)wParam == hooked_window && !activate->fMouse) ++activate_seen;
    }
    if (CallNextHookEx(create_hook, code, wParam, lParam) != 0) next_result_ok = 0;
    return 0;
}
static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProcA(hwnd, message, wParam, lParam);
}
static void Hooks(void)
{
    WNDCLASSA wc;
    MSG message;
    HWND other, child;
    RECT rect;
    const DWORD thread = GetCurrentThreadId();
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "ModulesFixture";
    RegisterClassA(&wc);
    hooked_window = CreateWindowExA(0, "ModulesFixture", "hooks", WS_OVERLAPPEDWINDOW, 10, 10, 200, 100, NULL, NULL,
                                    wc.hInstance, NULL);
    other = CreateWindowExA(0, "ModulesFixture", "other", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 300, 10, 200, 100, NULL, NULL,
                            wc.hInstance, NULL);
    Check(200, hooked_window && other);

    message_hook = SetWindowsHookExA(WH_GETMESSAGE, MessageHook, NULL, thread);
    Check(201, message_hook != NULL);
    PostMessageA(hooked_window, WM_APP + 5, 7, 0);
    Check(202, GetMessageA(&message, hooked_window, WM_APP + 5, WM_APP + 5) && message.wParam == 1007 && message_seen == 1);
    Check(203, UnhookWindowsHookEx(message_hook));
    PostMessageA(hooked_window, WM_APP + 5, 7, 0);
    Check(204, GetMessageA(&message, hooked_window, WM_APP + 5, WM_APP + 5) && message.wParam == 7 && message_seen == 1);

    send_hook = SetWindowsHookExA(WH_CALLWNDPROC, SendHook, NULL, thread);
    Check(210, send_hook != NULL);
    SendMessageA(hooked_window, WM_APP + 6, 11, -22);
    Check(211, send_seen == 1 && UnhookWindowsHookEx(send_hook));

    create_hook = SetWindowsHookExA(WH_CBT, CreateHook, NULL, thread);
    Check(220, create_hook != NULL);
    child = CreateWindowExA(0, "ModulesFixture", "hooked child", WS_CHILD | WS_VISIBLE, 5, 5, 50, 20, hooked_window, NULL,
                            wc.hInstance, NULL);
    Check(221, child && create_seen == 1 && !lstrcmpA(created_class, "ModulesFixture"));
    Check(222, GetWindowRect(child, &rect) && rect.right - rect.left == 123 && rect.bottom - rect.top == 20);
    ShowWindow(hooked_window, SW_SHOW);
    SetActiveWindow(hooked_window);
    Check(223, activate_seen >= 1 && next_result_ok);
    Check(224, UnhookWindowsHookEx(create_hook));

    /* A hook for every thread of the session lives in a DLL, and the system
       calls it in the other ARM64 processes of the session: a window a child
       process creates is sized by the hook this process set. */
    SetLastError(0);
    Check(230, SetWindowsHookExA(WH_CBT, CreateHook, NULL, 0) == NULL && GetLastError() == 1428);
    {
        char folder[MAX_PATH], dll[MAX_PATH], command[MAX_PATH + 16];
        STARTUPINFOA startup;
        PROCESS_INFORMATION process;
        DWORD code = 0;
        HMODULE module;
        HHOOK session;
        GetTempPathA(MAX_PATH - 32, folder);
        snprintf(dll, sizeof dll, "%sMODDLL.DLL", folder);
        snprintf(command, sizeof command, "\"%sIMPORTER.EXE\" window", folder);
        module = LoadLibraryA(dll);
        session = module ? SetWindowsHookExA(WH_CBT, (HOOKPROC)GetProcAddress(module, "ModuleCbt"), module, 0) : NULL;
        Check(231, session != NULL);
        memset(&startup, 0, sizeof startup);
        startup.cb = sizeof startup;
        Check(232, CreateProcessA(NULL, command, NULL, NULL, FALSE, 0, NULL, folder, &startup, &process));
        Check(233, WaitForSingleObject(process.hProcess, 30000) == WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess, &code) &&
                       code == 101);
        if (code != 101) printf("hooked child exit code %lu\n", (unsigned long)code);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        /* Without the hook the child keeps its own size. */
        Check(234, session && UnhookWindowsHookEx(session));
        code = 0;
        Check(235, CreateProcessA(NULL, command, NULL, NULL, FALSE, 0, NULL, folder, &startup, &process) &&
                       WaitForSingleObject(process.hProcess, 30000) == WAIT_OBJECT_0 &&
                       GetExitCodeProcess(process.hProcess, &code) && code == 91);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        if (module) FreeLibrary(module);
    }
    DestroyWindow(other);
    DestroyWindow(hooked_window);
}

/* --- the shell helper library of the SDK (sdk/lib/shellhelp) ------------------- */
static int IconOk(HICON icon)
{
    ICONINFO parts;
    int ok;
    if (!icon) return 0;
    memset(&parts, 0, sizeof parts);
    ok = GetIconInfo(icon, &parts) && parts.fIcon && parts.hbmMask;
    if (parts.hbmMask) DeleteObject(parts.hbmMask);
    if (parts.hbmColor) DeleteObject(parts.hbmColor);
    DestroyIcon(icon);
    return ok;
}
static void Shell(void)
{
    char folder[MAX_PATH], path[MAX_PATH];
    ShellHelpLink link;
    HANDLE process = NULL;
    DWORD code = 0;
    UINT_PTR result;
    /* Icons out of a PE file for x86, a 16-bit NE file and a DLL with many. */
    Check(300, ShellHelpIconCount("C:\\WINDOWS\\NOTEPAD.EXE") >= 1 && IconOk(ShellHelpIcon("C:\\WINDOWS\\NOTEPAD.EXE", 0, 32, 32)));
    Check(301, IconOk(ShellHelpIcon("C:\\WINDOWS\\NOTEPAD.EXE", 0, 16, 16)));
    Check(302, ShellHelpIconCount("C:\\WINDOWS\\PROGMAN.EXE") >= 1 && IconOk(ShellHelpIcon("C:\\WINDOWS\\PROGMAN.EXE", 0, 32, 32)));
    Check(303, ShellHelpIconCount("C:\\WINDOWS\\SYSTEM\\SHELL32.DLL") > 40 &&
                   IconOk(ShellHelpIcon("C:\\WINDOWS\\SYSTEM\\SHELL32.DLL", 3, 32, 32)));
    Check(304, ShellHelpIcon("C:\\WINDOWS\\WIN.INI", 0, 32, 32) == NULL && ShellHelpIcon("C:\\NOSUCH.EXE", 0, 32, 32) == NULL);
    Check(305, ShellHelpIcon("C:\\WINDOWS\\NOTEPAD.EXE", 99, 32, 32) == NULL);
    /* The icon of this program itself, an ARM64 PE32+ without icons: none. */
    GetModuleFileNameA(NULL, path, MAX_PATH);
    Check(306, ShellHelpIconCount(path) == 0);
    /* A shortcut of the start menu. */
    Check(310, ShellHelpReadLink("C:\\WINDOWS\\Start Menu\\Programs\\Accessories\\Calculator.lnk", &link));
    Check(311, !lstrcmpiA(link.target, "C:\\WINDOWS\\CALC.EXE"));
    if (lstrcmpiA(link.target, "C:\\WINDOWS\\CALC.EXE")) printf("link target [%s] dir [%s] args [%s]\n", link.target, link.directory, link.arguments);
    Check(312, !ShellHelpReadLink("C:\\WINDOWS\\WIN.INI", &link));
    /* Programs and documents. */
    GetTempPathA(MAX_PATH - 32, folder);
    snprintf(path, sizeof path, "%sIMPORTER.EXE", folder);
    result = ShellHelpOpen(path, NULL, folder, SW_SHOWNORMAL, &process);
    Check(320, result > 32 && process != NULL);
    Check(321, process && WaitForSingleObject(process, 30000) == WAIT_OBJECT_0 && GetExitCodeProcess(process, &code) && code == 100);
    if (process) CloseHandle(process);
    Check(322, ShellHelpOpen("C:\\NOSUCH.EXE", NULL, NULL, SW_SHOWNORMAL, NULL) == 2);
    Check(323, ShellHelpOpen("C:\\WINDOWS\\SYSTEM.DAT", NULL, NULL, SW_SHOWNORMAL, NULL) == 31);
    /* A text file goes to the program registered for.txt: the original Notepad. */
    process = NULL;
    result = ShellHelpOpen("C:\\WINDOWS\\WIN.INI", NULL, NULL, SW_SHOWNORMAL, &process);
    Check(324, result > 32 && process != NULL);
    if (process)
    {
        HWND notepad = NULL;
        for (int i = 0; i < 200 && !notepad; ++i)
        {
            Sleep(50);
            notepad = FindWindowA("Notepad", NULL);
        }
        Check(325, notepad != NULL);
        TerminateProcess(process, 0);
        CloseHandle(process);
    }
}

/* --- SHELL32 and COMDLG32 names, linked statically (sdk/lib) -------------------- */
static const char *dialog_name;
/* Plays the person: types a name into the file dialog and presses OK. */
static DWORD WINAPI DialogUser(LPVOID parameter)
{
    (void)parameter;
    for (int i = 0; i < 400; ++i)
    {
        HWND dialog = FindWindowA("Neo98FileDialog", NULL);
        Sleep(25);
        if (!dialog || !IsWindowVisible(dialog)) continue;
        if (dialog_name)
        {
            SetWindowTextA(GetDlgItem(dialog, 101), dialog_name);
            PostMessageA(dialog, WM_COMMAND, 103, 0);
        }
        else PostMessageA(dialog, WM_COMMAND, 104, 0);
        return 1;
    }
    return 0;
}
static BOOL AskFile(int save, const char *type, char *path, const char *extension, DWORD flags)
{
    OPENFILENAMEA ofn;
    HANDLE user;
    DWORD id;
    BOOL result;
    memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.lpstrFilter = "Settings (*.ini)\0*.ini\0All files\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrInitialDir = "C:\\WINDOWS";
    ofn.lpstrDefExt = extension;
    ofn.Flags = flags;
    dialog_name = type;
    user = CreateThread(NULL, 0, DialogUser, NULL, 0, &id);
    result = save ? GetSaveFileNameA(&ofn) : GetOpenFileNameA(&ofn);
    WaitForSingleObject(user, 15000);
    CloseHandle(user);
    if (result && (ofn.nFileOffset != 11 || lstrcmpiA(path + ofn.nFileExtension, "ini"))) return 2;
    return result;
}
static void ShellNames(void)
{
    SHFILEINFOA info;
    char path[MAX_PATH] = "", folder[MAX_PATH], program[MAX_PATH];
    HICON icon;
    Check(400, (INT_PTR)ExtractIconA(NULL, "C:\\WINDOWS\\SYSTEM\\SHELL32.DLL", (UINT)-1) > 40);
    icon = ExtractIconA(NULL, "C:\\WINDOWS\\NOTEPAD.EXE", 0);
    Check(401, IconOk(icon));
    memset(&info, 0, sizeof info);
    Check(402, SHGetFileInfoA("C:\\WINDOWS\\Start Menu\\Programs\\Accessories\\Calculator.lnk", 0, &info, sizeof info,
                              SHGFI_ICON | SHGFI_DISPLAYNAME) && !lstrcmpA(info.szDisplayName, "Calculator") && IconOk(info.hIcon));
    memset(&info, 0, sizeof info);
    Check(403, SHGetFileInfoA("C:\\WINDOWS\\WIN.INI", 0, &info, sizeof info, SHGFI_TYPENAME) && info.szTypeName[0]);
    Check(404, !SHGetFileInfoA("C:\\NOSUCH.TXT", 0, &info, sizeof info, SHGFI_ICON));
    Check(405, (INT_PTR)ShellExecuteA(NULL, "open", "C:\\NOSUCH.EXE", NULL, NULL, SW_SHOW) == SE_ERR_FNF);
    Check(406, (INT_PTR)ShellExecuteA(NULL, "print", "C:\\WINDOWS\\WIN.INI", NULL, NULL, SW_SHOW) == SE_ERR_NOASSOC);
    GetTempPathA(MAX_PATH - 32, folder);
    snprintf(program, sizeof program, "%sIMPORTER.EXE", folder);
    Check(407, (INT_PTR)ShellExecuteA(NULL, NULL, program, NULL, folder, SW_SHOW) > 32);
    /* The file dialog: a name without extension gets the default one; a file
       that must exist is asked for again until the person gives up. */
    Check(410, AskFile(0, "win", path, "ini", OFN_FILEMUSTEXIST) == 1 && !lstrcmpiA(path, "C:\\WINDOWS\\win.ini"));
    path[0] = 0;
    Check(411, AskFile(0, NULL, path, "ini", OFN_FILEMUSTEXIST) == 0 && !path[0]);
    /* Saving under a full path of another folder. */
    Check(412, AskFile(1, "C:\\NEWFILE.INI", path, "ini", OFN_OVERWRITEPROMPT) != 0 && !lstrcmpiA(path, "C:\\NEWFILE.INI"));
}

int main(void)
{
    Modules();
    Hooks();
    Shell();
    ShellNames();
    printf("MODULES checks=%d failures=%d\n", checks, failures);
    return failures;
}
