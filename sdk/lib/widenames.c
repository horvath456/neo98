/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* GetModuleHandleW without a W function of the system: the name is converted
   and GetModuleHandleA answers. Windows 98 exports the W function and fails
   it; a program built with the SDK gets this one, statically, and the same
   file works on a Windows that has the real one. Own code. docs/ARM64-APPS.md. */
#include <windows.h>

HMODULE WINAPI GetModuleHandleW(LPCWSTR name)
{
    char narrow[MAX_PATH];
    if (!name) return GetModuleHandleA(NULL);
    if (!WideCharToMultiByte(CP_ACP, 0, name, -1, narrow, sizeof narrow, NULL, NULL)) return NULL;
    return GetModuleHandleA(narrow);
}
/* For a source that declares the function as an import itself. */
void *__imp_GetModuleHandleW = (void *)GetModuleHandleW;
