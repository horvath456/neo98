/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"

START_TEST(rtl_memory)
{
    typedef void (WINAPI *move_fn)(void *, const void *, SIZE_T);
    typedef void (WINAPI *fill_fn)(void *, SIZE_T, BYTE);
    typedef void (WINAPI *zero_fn)(void *, SIZE_T);
    HMODULE kernel = GetModuleHandleA("kernel32.dll");
    move_fn move = (move_fn)GetProcAddress(kernel, "RtlMoveMemory");
    fill_fn fill = (fill_fn)GetProcAddress(kernel, "RtlFillMemory");
    zero_fn zero = (zero_fn)GetProcAddress(kernel, "RtlZeroMemory");
    BYTE *memory = VirtualAlloc(NULL, 16384, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    DWORD previous, error;
    unsigned i;
    ok(move && fill && zero && memory, "memory fixture available\n");
    if (!move || !fill || !zero || !memory) return;

    for (i = 0; i < 16384; ++i) memory[i] = (BYTE)i;
    SetLastError(0xdeadbeef);
    move(memory + 1, memory, 8192);
    error = GetLastError();
    for (i = 0; i < 8192 && memory[i+1] == (BYTE)i; ++i) {}
    ok(i == 8192 && error == 0xdeadbeef, "backward overlap bytes=%u error=%lu\n", i, error);
    move(memory, memory + 1, 8192);
    for (i = 0; i < 8192 && memory[i] == (BYTE)i; ++i) {}
    ok(i == 8192, "forward overlap bytes=%u\n", i);

    SetLastError(0xdeadbeef); move(NULL, memory, 16);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "NULL destination error=%lu\n", GetLastError());
    SetLastError(0xdeadbeef); move(memory, NULL, 16);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "NULL source error=%lu\n", GetLastError());
    SetLastError(0xdeadbeef); move(NULL, NULL, 0);
    ok(GetLastError() == 0xdeadbeef, "zero-length move error=%lu\n", GetLastError());
    SetLastError(0xdeadbeef); fill(NULL, 16, 0x5a);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "NULL fill error=%lu\n", GetLastError());
    SetLastError(0xdeadbeef); zero(NULL, 16);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "NULL zero error=%lu\n", GetLastError());
    SetLastError(0xdeadbeef); fill(NULL, 0, 0x5a); zero(NULL, 0);
    ok(GetLastError() == 0xdeadbeef, "zero-length fill/zero error=%lu\n", GetLastError());

    ok(VirtualProtect(memory + 4096, 4096, PAGE_NOACCESS, &previous), "protect middle page\n");
    SetLastError(0xdeadbeef); move(memory, memory + 4096, 16);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "inaccessible source error=%lu\n", GetLastError());
    SetLastError(0xdeadbeef); move(memory + 4096, memory, 16);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "inaccessible destination error=%lu\n", GetLastError());
    memset(memory + 4088, 0, 8);
    SetLastError(0xdeadbeef); fill(memory + 4088, 16, 0x5a);
    error = GetLastError();
    for (i = 0; i < 8 && memory[4088+i] == 0x5a; ++i) {}
    trace("boundary fill bytes=%u error=%lu\n", i, error);
    ok(error == ERROR_INVALID_PARAMETER, "cross-page fill error=%lu\n", error);
    ok(i == 8, "aligned fill preserves %u completed bytes\n", i);
    memset(memory + 4093, 0, 3);
    SetLastError(0xdeadbeef); fill(memory + 4093, 6, 0x5a);
    ok(GetLastError() == ERROR_INVALID_PARAMETER && !memory[4093] && !memory[4094] && !memory[4095],
       "faulting unaligned dword must not become three byte writes\n");
    memset(memory + 4088, 0x5a, 8);
    memset(memory + 8192, 0, 16);
    SetLastError(0xdeadbeef); move(memory + 8192, memory + 4088, 16);
    error = GetLastError();
    for (i = 0; i < 8 && memory[8192+i] == 0x5a; ++i) {}
    ok(error == ERROR_INVALID_PARAMETER && i == 8 && !memory[8200],
       "nonoverlapping move copies forward before source fault: bytes=%u error=%lu\n", i, error);
    memset(memory + 4088, 0, 8);
    SetLastError(0xdeadbeef); move(memory + 4088, memory + 8192, 16);
    error = GetLastError();
    for (i = 0; i < 8 && memory[4088+i] == 0x5a; ++i) {}
    ok(error == ERROR_INVALID_PARAMETER && i == 8, "partial destination copy bytes=%u error=%lu\n", i, error);
    ok(VirtualProtect(memory + 4096, 4096, PAGE_READONLY, &previous), "read-only middle page\n");
    SetLastError(0xdeadbeef); zero(memory + 4096, 16);
    ok(GetLastError() == ERROR_INVALID_PARAMETER, "read-only zero error=%lu\n", GetLastError());
    VirtualFree(memory, 0, MEM_RELEASE);
}
