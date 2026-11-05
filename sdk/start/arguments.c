/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* Start code of an ARM64 application: the arguments of the program.
   docs/ARM64-APPS.md. */
#include <windows.h>

/* __argc and __argv, as the C runtimes of Windows name them: the arguments
   of the program, split in a copy of the command line. Quotes group, a
   backslash before a quote keeps the quote. A program with main receives
   them as parameters; one with WinMain may read them here. */
int __argc;
char **__argv;
void __neo98_arguments(void)
{
    char *line = GetCommandLineA();
    int length = lstrlenA(line), argc = 0, capacity = length / 2 + 2;
    HANDLE heap = GetProcessHeap();
    char *copy = (char *)HeapAlloc(heap, 0, (SIZE_T)length + 1);
    char **argv = (char **)HeapAlloc(heap, 0, sizeof(char *) * (SIZE_T)capacity);
    if (!copy || !argv) ExitProcess(255);
    char *in = line, *out = copy;
    while (*in)
    {
        while (*in == ' ' || *in == '\t') ++in;
        if (!*in) break;
        argv[argc++] = out;
        int quoted = 0;
        while (*in && (quoted || (*in != ' ' && *in != '\t')))
        {
            if (*in == '\\' && in[1] == '"') { *out++ = '"'; in += 2; }
            else if (*in == '"') { quoted = !quoted; ++in; }
            else *out++ = *in++;
        }
        *out++ = 0;
    }
    argv[argc] = NULL;
    __argc = argc;
    __argv = argv;
}
