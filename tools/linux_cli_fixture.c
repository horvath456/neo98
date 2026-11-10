// SPDX-License-Identifier: GPL-2.0-or-later
// Own PE32 program for the Linux terminal path, not a Windows binary.
__declspec(dllimport) unsigned __stdcall GetStdHandle(unsigned);
__declspec(dllimport) int __stdcall WriteFile(unsigned, const void *, unsigned, unsigned *, void *);
__declspec(dllimport) int __stdcall ReadFile(unsigned, void *, unsigned, unsigned *, void *);
__declspec(dllimport) void __stdcall ExitProcess(unsigned);
__declspec(dllimport) char *__stdcall GetCommandLineA(void);
__declspec(dllimport) unsigned __stdcall GetCurrentDirectoryA(unsigned, char *);
void mainCRTStartup(void)
{
    static const char text[] = "NEO98 native Linux CLI\r\n";
    unsigned written = 0, count = 0;
    unsigned output = GetStdHandle(-11), input = GetStdHandle(-10);
    char line[64];
    if (!WriteFile(output, text, sizeof(text) - 1, &written, 0) || written != sizeof(text) - 1) ExitProcess(21);
    // "cwd" as the last word of the command line: name the directory the
    // program started in, for the host's --directory.
    const char *command = GetCommandLineA(), *end = command;
    while (*end) ++end;
    if (end - command >= 4 && end[-4] == ' ' && end[-3] == 'c' && end[-2] == 'w' && end[-1] == 'd') {
        char folder[260];
        unsigned length = GetCurrentDirectoryA(sizeof folder - 2, folder);
        if (!length || length > sizeof folder - 2) ExitProcess(25);
        folder[length++] = '\r'; folder[length++] = '\n';
        if (!WriteFile(output, folder, length, &written, 0) || written != length) ExitProcess(26);
    }
    static const char diagnostic[] = "native stderr\r\n";
    if (!WriteFile(GetStdHandle(-12), diagnostic, sizeof(diagnostic) - 1, &written, 0)) ExitProcess(24);
    for (;;) {
        if (!ReadFile(input, line, sizeof line, &count, 0)) ExitProcess(22);
        if (!count) break;
        if (!WriteFile(output, line, count, &written, 0) || written != count) ExitProcess(23);
    }
    ExitProcess(7);
}
