/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The sizes of the window parts: NONCLIENTMETRICS and GetSystemMetrics.
   "read" writes C:\METRICS.TXT with them; "registry" first sets
   HKCU\Control Panel\Desktop\WindowMetrics ScrollWidth to -300 (20 pixels)
   for the next start; "set" first applies a menu bar of 22 pixels with
   SPI_SETNONCLIENTMETRICS and SPIF_UPDATEINIFILE.
   tools/test_window_metrics_host.py */
#define WINVER 0x0400
#define _WIN32_WINNT 0x0400
#include <windows.h>

void mainCRTStartup(void)
{
    const char *command = GetCommandLineA();
    NONCLIENTMETRICSA n;
    n.cbSize = sizeof n;
    if (lstrcmpA(command + lstrlenA(command) - 8, "registry") == 0)
    {
        HKEY key;
        if (!RegCreateKeyA(HKEY_CURRENT_USER, "Control Panel\\Desktop\\WindowMetrics", &key))
        {
            RegSetValueExA(key, "ScrollWidth", 0, REG_SZ, (const BYTE *)"-300", 5);
            RegCloseKey(key);
        }
    }
    else if (lstrcmpA(command + lstrlenA(command) - 3, "set") == 0 &&
             SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof n, &n, 0))
    {
        n.iMenuHeight = 22;
        SystemParametersInfoA(SPI_SETNONCLIENTMETRICS, sizeof n, &n, SPIF_UPDATEINIFILE);
    }
    char line[400];
    DWORD written = 0;
    n.cbSize = sizeof n;
    BOOL ok = SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof n, &n, 0);
    wsprintfA(line, "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\r\n", ok, n.iBorderWidth, n.iScrollWidth,
              n.iScrollHeight, n.iCaptionWidth, n.iCaptionHeight, n.iSmCaptionWidth, n.iSmCaptionHeight,
              n.iMenuWidth, n.iMenuHeight, GetSystemMetrics(SM_CXVSCROLL), GetSystemMetrics(SM_CYHSCROLL),
              GetSystemMetrics(SM_CYCAPTION), GetSystemMetrics(SM_CYMENU), GetSystemMetrics(SM_CXSMSIZE),
              GetSystemMetrics(SM_CYSMCAPTION));
    HANDLE file = CreateFileA("C:\\METRICS.TXT", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    WriteFile(file, line, lstrlenA(line), &written, NULL);
    CloseHandle(file);
    ExitProcess(0);
}
