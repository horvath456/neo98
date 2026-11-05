// SPDX-License-Identifier: GPL-2.0-or-later
// Private Win98 console adapter for libc++'s unchanged print.cpp.
#pragma once
#include <windows.h>
#include <stdlib.h>
#include <limits.h>

inline BOOL Neo98WriteConsoleW(HANDLE handle, const wchar_t *text, DWORD count, LPDWORD written, LPVOID)
{
    if (!count) { if (written) *written = 0; return TRUE; }
    if (count > INT_MAX) { SetLastError(87); return FALSE; }
    const int bytes = WideCharToMultiByte(CP_OEMCP, 0, text, (int)count, nullptr, 0, nullptr, nullptr);
    if (!bytes) return FALSE;
    char *buffer = (char *)malloc(bytes);
    if (!buffer) { SetLastError(8); return FALSE; }
    BOOL ok = WideCharToMultiByte(CP_OEMCP, 0, text, (int)count, buffer, bytes, nullptr, nullptr) != 0;
    DWORD done = 0;
    if (ok) ok = WriteFile(handle, buffer, bytes, &done, nullptr);
    const DWORD error = GetLastError();
    free(buffer);
    if (written) *written = ok && done == (DWORD)bytes ? count : 0;
    SetLastError(error);
    return ok && done == (DWORD)bytes;
}
#define WriteConsoleW Neo98WriteConsoleW
