// SPDX-License-Identifier: GPL-2.0-or-later
typedef unsigned U;
typedef void *H;
__declspec(dllimport) H __stdcall GetStdHandle(U);
__declspec(dllimport) int __stdcall ReadFile(H, void *, U, U *, void *);
__declspec(dllimport) int __stdcall WriteFile(H, const void *, U, U *, void *);
__declspec(dllimport) void __stdcall ExitProcess(U);
void mainCRTStartup(void)
{
    H input = GetStdHandle((U)-10), output = GetStdHandle((U)-11);
    U done = 0;
    static const char welcome[] = "NEO98 console window\r\n";
    WriteFile(output, welcome, sizeof(welcome) - 1, &done, 0);
    char text[256];
    while (ReadFile(input, text, sizeof(text), &done, 0) && done)
        WriteFile(output, text, done, &done, 0);
    ExitProcess(0);
}
