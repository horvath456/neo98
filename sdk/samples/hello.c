/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* The smallest console program: a line on standard output, the arguments,
   and an exit code. */
#include <windows.h>

static void Print(const char *text)
{
    DWORD written;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, (DWORD)lstrlenA(text), &written, NULL);
}

int main(int argc, char **argv)
{
    char line[256];
    Print("Hello from ARM64\r\n");
    for (int i = 1; i < argc; ++i)
    {
        wsprintfA(line, "argument %d: %s\r\n", i, argv[i]);
        Print(line);
    }
    return argc - 1;
}
