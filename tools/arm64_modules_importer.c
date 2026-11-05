/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* A program that imports from a DLL of its own at load time, for
   tools/arm64_modules_fixture.c. The exit code is 100 when everything held,
   else the number of the first failed check. docs/ARM64-APPS.md. */
#include <windows.h>

__declspec(dllimport) int ModuleAdd(int a, int b);
__declspec(dllimport) int ModuleAttached(void);
__declspec(dllimport) HINSTANCE ModuleInstance(void);
__declspec(dllimport) int ModuleLength(const char *text);
__declspec(dllimport) long long ModuleCall(long long (*function)(long long, double), long long value);
__declspec(dllimport) extern int module_counter;

static long long Twice(long long value, double half) { return value * 2 + (half == 0.5 ? 0 : 1000); }

/* With the argument "window": a window is created while another process
   holds a WH_CBT hook for the session; 101 when that hook sized it. */
static int HookedWindow(void)
{
    RECT rect = {0, 0, 0, 0};
    HWND window = CreateWindowExA(0, "STATIC", "session child", WS_POPUP, 10, 10, 100, 50, NULL, NULL, GetModuleHandleA(NULL), NULL);
    if (!window) return 90;
    GetWindowRect(window, &rect);
    DestroyWindow(window);
    return rect.right - rect.left == 321 ? 101 : rect.right - rect.left == 100 ? 91 : 92;
}

int main(int argc, char **argv)
{
    if (argc > 1 && !lstrcmpA(argv[1], "window")) return HookedWindow();
    if (ModuleAttached() != 1) return 1;            /* DllMain ran before main */
    if (ModuleAdd(2, 3) != 5) return 2;
    if (ModuleInstance() != GetModuleHandleA("MODDLL.DLL") || !ModuleInstance()) return 3;
    if (ModuleLength("twelve chars") != 12) return 4; /* the DLL calls KERNEL32 */
    if (ModuleCall(Twice, 0x100000000ll) != 0x200000001ll) return 5; /* 64 bits both ways */
    if (module_counter != 7) return 6;               /* exported data */
    if (GetProcAddress(GetModuleHandleA("MODDLL.DLL"), "ModuleAdd") != (LPVOID)ModuleAdd) return 7;
    return 100;
}
