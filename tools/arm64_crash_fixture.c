/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* A program that fails on purpose, in the way its argument names. The run
   must end with the failure of the process and nothing else. Without an
   argument the last letter of the program's own name chooses (CRASHN.EXE
   reads through a null pointer, CRASHU.EXE runs an undefined instruction),
   for a start by double click. tools/test_arm64_apps_native.py,
   tools/test_arm64_start_host.py. */
#include <windows.h>

static volatile int *nowhere;
static const int constant = 5;

int main(int argc, char **argv)
{
    DWORD written;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "before\r\n", 8, &written, NULL);
    if (argc < 2)
    {
        char path[MAX_PATH];
        const int length = (int)GetModuleFileNameA(NULL, path, sizeof path);
        const char letter = length > 5 ? path[length - 5] : 0;
        if (letter == 'N' || letter == 'n') *nowhere = 1;
        if (letter == 'U' || letter == 'u') __asm__("udf #0");
        return 1;
    }
    if (!lstrcmpA(argv[1], "null")) *nowhere = 1;
    if (!lstrcmpA(argv[1], "readonly")) *(volatile int *)&constant = 6;
    if (!lstrcmpA(argv[1], "undefined")) __asm__("udf #0");
    /* KERNEL32 catches an invalid string access and returns zero. */
    if (!lstrcmpA(argv[1], "badpointer") && lstrlenA((const char *)0x12340000) != 0) return 2;
    if (!lstrcmpA(argv[1], "wild")) ((void (*)(void))0x66660000)();
    if (!lstrcmpA(argv[1], "high")) *(volatile int *)0x123456780000ull = 1;
    if (!lstrcmpA(argv[1], "struct")) RegisterClassA((const WNDCLASSA *)0x45670000);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), "after\r\n", 7, &written, NULL);
    return 0;
}
