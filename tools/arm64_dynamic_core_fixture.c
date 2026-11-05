/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* dynamic core exports use the same ARM64 gates as imports. */
#include <windows.h>
#include <stdio.h>
static int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; printf("FAIL %d\n", __LINE__); } } while (0)
int main(void)
{
    HMODULE kernel = GetModuleHandleA("KERNEL32.DLL");
    CHECK(kernel != NULL);
#define CORE(name) CHECK(GetProcAddress(kernel, #name) == (LPVOID)name)
    CORE(RaiseException);
    CORE(RtlCaptureContext);
    CORE(RtlLookupFunctionEntry);
    CORE(RtlVirtualUnwind);
    CORE(RtlUnwindEx);
    CORE(RtlUnwind);
    CORE(RtlRestoreContext);
    CORE(RtlPcToFileHeader);
    CORE(SetUnhandledExceptionFilter);
    HMODULE user = LoadLibraryA("USER32.DLL");
    CHECK(user != NULL);
    CHECK(GetProcAddress(user, "GetWindowLongPtrA") == (LPVOID)GetWindowLongPtrA);
    DWORD (WINAPI *ticks)(void) = (DWORD (WINAPI *)(void))GetProcAddress(kernel, "GetTickCount");
    CHECK(ticks != NULL);
    if (ticks) { DWORD before = ticks(); CHECK(GetTickCount() - before < 1000); }
    PVOID (WINAPI *header)(PVOID, PVOID *) = (PVOID (WINAPI *)(PVOID, PVOID *))GetProcAddress(kernel, "RtlPcToFileHeader");
    PVOID base = NULL;
    if (header) CHECK(header((PVOID)main, &base) == GetModuleHandleA(NULL) && base == GetModuleHandleA(NULL));
    CHECK(GetProcAddress(kernel, "Neo98NonexistentExport") == NULL);
    CHECK(GetLastError() == 127);
    printf("DYNAMIC_CORE checks=%d failures=%d\n", checks, failures);
    return failures;
}
