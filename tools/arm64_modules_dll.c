/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* A DLL of an ARM64 program, for tools/arm64_modules_fixture.c: exports,
   DllMain, a call into KERNEL32, a call back into the program.
   docs/ARM64-APPS.md. */
#include <windows.h>

static int attached, detached;
static HINSTANCE instance_seen;

static char marker[MAX_PATH + 16];
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
    {
        ++attached;
        instance_seen = instance;
        GetModuleFileNameA(instance, marker, MAX_PATH);
        lstrcatA(marker, ".detached");
    }
    if (reason == DLL_PROCESS_DETACH)
    {
        /* The program looks for this file after FreeLibrary. */
        DWORD written;
        const HANDLE file = CreateFileA(marker, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        ++detached;
        WriteFile(file, "1", 1, &written, NULL);
        CloseHandle(file);
    }
    return TRUE;
}
__declspec(dllexport) int ModuleAdd(int a, int b) { return a + b; }
__declspec(dllexport) int ModuleAttached(void) { return attached; }
__declspec(dllexport) HINSTANCE ModuleInstance(void) { return instance_seen; }
__declspec(dllexport) int ModuleLength(const char *text) { return lstrlenA(text); }
__declspec(dllexport) long long ModuleCall(long long (*function)(long long, double), long long value)
{
    return function(value, 0.5) + 1;
}
__declspec(dllexport) int module_counter = 7;

/* A WH_CBT procedure for the whole session: the system calls it in every
   ARM64 process a window is created in. */
__declspec(dllexport) LRESULT CALLBACK ModuleCbt(int code, WPARAM wParam, LPARAM lParam)
{
    if (code == HCBT_CREATEWND)
    {
        CBT_CREATEWNDA *create = (CBT_CREATEWNDA *)lParam;
        if (create->lpcs && create->lpcs->lpszName && !lstrcmpA(create->lpcs->lpszName, "session child")) create->lpcs->cx = 321;
    }
    return CallNextHookEx(NULL, code, wParam, lParam);
}
