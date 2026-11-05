/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* An ARM64 program that starts another ARM64 program many times, one after
   the other, more often than the machine has places for processes at once:
   a process that has ended gives its place back. Started with an argument it
   is the child and returns that number. tools/test_arm64_apps_native.py. */
#include <windows.h>

int main(int argc, char **argv)
{
    char path[MAX_PATH], command[MAX_PATH + 16], line[96];
    DWORD written;
    int good = 0;
    if (argc > 1)
    {
        int value = 0;
        for (const char *p = argv[1]; *p >= '0' && *p <= '9'; ++p) value = value * 10 + (*p - '0');
        return value;
    }
    GetModuleFileNameA(NULL, path, sizeof path);
    for (int round = 1; round <= 40; ++round)
    {
        STARTUPINFOA startup;
        PROCESS_INFORMATION process;
        DWORD code = 0;
        memset(&startup, 0, sizeof startup);
        startup.cb = sizeof startup;
        wsprintfA(command, "\"%s\" %d", path, round);
        if (!CreateProcessA(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process)) break;
        if (WaitForSingleObject(process.hProcess, 10000) == WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess, &code) &&
            code == (DWORD)round)
            ++good;
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
    wsprintfA(line, "children with the right exit code: %d\r\n", good);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), line, (DWORD)lstrlenA(line), &written, NULL);
    return good == 40 ? 0 : 1;
}
