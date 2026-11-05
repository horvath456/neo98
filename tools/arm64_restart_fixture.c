/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* An ARM64 program that restarts Windows once: the first time it runs it
   leaves a mark and calls ExitWindowsEx(EWX_REBOOT); started again after the
   restart, it finds the mark, says so and stays. The host re-executes itself
   between the two, with the machine of the ARM64 process taken down first. tools/test_arm64_restart_host.py. */
#include <windows.h>
#include <stdio.h>

static LRESULT CALLBACK Proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_ENDSESSION && wParam)
    {
        FILE *file = fopen("C:\\RESTART.TXT", "a");
        if (file) { fprintf(file, "endsession=1\n"); fclose(file); }
    }
    return DefWindowProcA(window, message, wParam, lParam);
}
int main(void)
{
    WNDCLASSA wc;
    MSG message;
    FILE *file;
    const int second = GetFileAttributesA("C:\\RESTART.TXT") != 0xffffffffu;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = Proc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "Arm64Restart";
    RegisterClassA(&wc);
    CreateWindowExA(0, "Arm64Restart", "restart", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 50, 50, 300, 200, NULL, NULL, wc.hInstance, NULL);
    file = fopen("C:\\RESTART.TXT", "a");
    if (file)
    {
        fprintf(file, second ? "second=1\n" : "first=1\n");
        fclose(file);
    }
    if (!second)
    {
        const BOOL asked = ExitWindowsEx(EWX_REBOOT, 0);
        file = fopen("C:\\RESTART.TXT", "a");
        if (file) { fprintf(file, "asked=%d\n", asked); fclose(file); }
    }
    while (GetMessageA(&message, NULL, 0, 0) > 0) DispatchMessageA(&message);
    return 0;
}
