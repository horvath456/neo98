/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* Start code of an ARM64 application: the entry of a DLL. The system calls it
   with the reason; the initializers of static C++ objects run before DllMain
   sees DLL_PROCESS_ATTACH, their destructors after it saw DLL_PROCESS_DETACH.
   A DLL without a DllMain of its own gets one that agrees to everything.
   docs/ARM64-APPS.md. */
#include <windows.h>

void __neo98_run_initializers(void);
void __neo98_run_exit_functions(void);
void __neo98_no_mappings(void) {}
void __neo98_release_mappings(void);
#pragma comment(linker, "/alternatename:__neo98_release_mappings=__neo98_no_mappings")
void __neo98_no_thread_cleanup(void) {}
void __neo98_thread_cleanup(void);
#pragma comment(linker, "/alternatename:__neo98_thread_cleanup=__neo98_no_thread_cleanup")
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved);
BOOL WINAPI __neo98_DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)instance; (void)reason; (void)reserved;
    return TRUE;
}
#pragma comment(linker, "/alternatename:DllMain=__neo98_DllMain")

BOOL WINAPI _DllMainCRTStartup(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    BOOL result;
    if (reason == DLL_PROCESS_ATTACH) __neo98_run_initializers();
    result = DllMain(instance, reason, reserved);
    if (reason == DLL_THREAD_DETACH || reason == DLL_PROCESS_DETACH) __neo98_thread_cleanup();
    if (reason == DLL_PROCESS_DETACH)
    {
        __neo98_run_exit_functions();
        __neo98_release_mappings();
    }
    return result;
}
