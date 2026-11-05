/* SPDX-License-Identifier: GPL-2.0-or-later
   Starts three DOS programs, each in a DOS machine of its own, and stays:
   several DOS machines at once for tools/test_pause_graphics_host.py. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API int CreateProcessA(const char *, char *, void *, void *, int, U, void *, const char *, void *, void *);
API void Sleep(U);
API void ExitProcess(U);

void __stdcall mainCRTStartup(void)
{
    static char programs[3][16] = {"D:\\PDOSA.COM", "D:\\PDOSB.COM", "D:\\PDOSC.COM"};
    U i;
    /* CreateProcess, not WinExec: that one waits until the program it started
       rests, and a DOS program that polls its timer never does. */
    for (i = 0; i < 3; ++i)
    {
        U startup[17] = {68}, process[4];
        if (!CreateProcessA(0, programs[i], 0, 0, 0, 0, 0, 0, startup, process)) ExitProcess(10 + i);
        Sleep(400);
    }
    for (;;) Sleep(1000);
}
