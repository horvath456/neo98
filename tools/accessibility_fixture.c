/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The accessibility settings of SystemParametersInfo.

   Without an argument: every structure answers with its flags, a wrong
   cbSize is refused, StickyKeys, ShowSounds, the keyboard preference and
   High Contrast are set with SPIF_UPDATEINIFILE | SPIF_SENDCHANGE, and the
   window of the program counts the WM_SETTINGCHANGE that come. High Contrast
   on makes the window colour black, off brings the colour before back. With
   the argument "again", a second start reads StickyKeys, ShowSounds and the
   keyboard preference back.

   C:\ACCESS.TXT gets 32 ints, tools/test_accessibility_host.py names them. */
#include <windows.h>

static int results[32];
static int setting_changes, last_change;

static LRESULT CALLBACK WindowProcedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == WM_SETTINGCHANGE)
    {
        ++setting_changes;
        last_change = (int)wp;
    }
    return DefWindowProcA(hwnd, message, wp, lp);
}

static int Flags(UINT action, void *structure, UINT size)
{
    *(UINT *)structure = size;
    if (!SystemParametersInfoA(action, size, structure, 0)) return -1;
    return (int)((DWORD *)structure)[1];
}

void mainCRTStartup(void)
{
    const char *line = GetCommandLineA();
    int again = 0;
    for (const char *p = line; *p; ++p)
        if (p[0] == 'a' && p[1] == 'g' && p[2] == 'a' && p[3] == 'i' && p[4] == 'n') again = 1;
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WindowProcedure;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "AccessibilityFixture";
    RegisterClassA(&wc);
    CreateWindowA("AccessibilityFixture", "Accessibility", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL,
                  wc.hInstance, NULL);

    FILTERKEYS filter;
    TOGGLEKEYS toggle;
    MOUSEKEYS mouse;
    STICKYKEYS sticky;
    ACCESSTIMEOUT timeout;
    SERIALKEYSA serial;
    SOUNDSENTRYA sentry;
    HIGHCONTRASTA contrast;
    char port[MAX_PATH] = "", active[MAX_PATH] = "", scheme[256] = "";
    BOOL value = FALSE;
    results[0] = again;
    results[1] = Flags(SPI_GETFILTERKEYS, &filter, sizeof filter);
    results[2] = Flags(SPI_GETTOGGLEKEYS, &toggle, sizeof toggle);
    results[3] = Flags(SPI_GETMOUSEKEYS, &mouse, sizeof mouse);
    results[4] = Flags(SPI_GETSTICKYKEYS, &sticky, sizeof sticky);
    results[5] = Flags(SPI_GETACCESSTIMEOUT, &timeout, sizeof timeout);
    serial.lpszActivePort = active;
    serial.lpszPort = port;
    results[6] = Flags(SPI_GETSERIALKEYS, &serial, sizeof serial);
    sentry.lpszWindowsEffectDLL = NULL;
    results[7] = Flags(SPI_GETSOUNDSENTRY, &sentry, sizeof sentry);
    contrast.lpszDefaultScheme = scheme;
    results[8] = Flags(SPI_GETHIGHCONTRAST, &contrast, sizeof contrast);
    results[9] = (int)filter.iWaitMSec;
    results[10] = (int)filter.iRepeatMSec;
    results[11] = (int)timeout.iTimeOutMSec;
    results[12] = (int)mouse.iMaxSpeed;
    results[13] = (int)active[0];
    results[14] = (int)scheme[0];
    /* A STICKYKEYS of the wrong size is refused. */
    sticky.cbSize = sizeof sticky + 4;
    results[15] = SystemParametersInfoA(SPI_GETSTICKYKEYS, sizeof sticky, &sticky, 0);
    results[16] = SystemParametersInfoA(SPI_GETSHOWSOUNDS, 0, &value, 0) ? value : -1;
    results[17] = GetSystemMetrics(SM_SHOWSOUNDS);
    value = FALSE;
    results[18] = SystemParametersInfoA(SPI_GETKEYBOARDPREF, 0, &value, 0) ? value : -1;
    if (!again)
    {
        Flags(SPI_GETSTICKYKEYS, &sticky, sizeof sticky);
        sticky.dwFlags |= SKF_STICKYKEYSON;
        results[19] = SystemParametersInfoA(SPI_SETSTICKYKEYS, sizeof sticky, &sticky,
                                            SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
        results[20] = last_change;
        results[21] = SystemParametersInfoA(SPI_SETSHOWSOUNDS, TRUE, NULL, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
        results[22] = GetSystemMetrics(SM_SHOWSOUNDS);
        results[23] = SystemParametersInfoA(SPI_SETKEYBOARDPREF, TRUE, NULL, SPIF_UPDATEINIFILE);
        /* High Contrast with its scheme, then off again. */
        results[24] = (int)GetSysColor(COLOR_WINDOW);
        contrast.cbSize = sizeof contrast;
        contrast.dwFlags |= HCF_HIGHCONTRASTON;
        contrast.lpszDefaultScheme = "High Contrast Black";
        results[25] = SystemParametersInfoA(SPI_SETHIGHCONTRAST, sizeof contrast, &contrast,
                                            SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
        results[26] = (int)GetSysColor(COLOR_WINDOW);
        results[27] = (int)GetSysColor(COLOR_WINDOWTEXT);
        contrast.dwFlags &= ~HCF_HIGHCONTRASTON;
        results[28] = SystemParametersInfoA(SPI_SETHIGHCONTRAST, sizeof contrast, &contrast,
                                            SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
        results[29] = (int)GetSysColor(COLOR_WINDOW);
        results[30] = setting_changes;
    }
    HANDLE file = CreateFileA(again ? "C:\\ACCESS2.TXT" : "C:\\ACCESS.TXT", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                              0, NULL);
    DWORD written = 0;
    WriteFile(file, results, sizeof results, &written, NULL);
    CloseHandle(file);
    ExitProcess(0);
}
