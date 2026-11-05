/* SPDX-License-Identifier: GPL-2.0-or-later
 * Kaggz's start dialog: a dialog of a 4.0 program with DS_CENTER
 * stands in the middle of the work area (USER), a modal dialog
 * that DialogBox shows becomes the active window, and an SS_BITMAP control
 * whose template names the bitmap by number (FFFFh, 114) shows it. When all
 * three hold the caption reads "Center ok"; the runner checks the frame in
 * the screenshot (no caption buttons without WS_SYSMENU). */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U GetModuleHandleA(const char *);
API void ExitProcess(U);
API int DialogBoxParamA(U, U, U, void *, U);
API U EndDialog(U, int);
API U SetTimer(U, U, U, void *);
API U KillTimer(U, U);
API U GetWindowRect(U, int *);
API U SystemParametersInfoA(U, U, void *, U);
API U GetActiveWindow(void);
API U SendDlgItemMessageA(U, int, U, U, U);
API U SetWindowTextA(U, const char *);
static U __stdcall procedure(U dialog, U message, U wp, U lp)
{
    (void)wp; (void)lp;
    if (message == 0x110) /* WM_INITDIALOG */
    {
        SetTimer(dialog, 1, 200, 0);
        return 1;
    }
    if (message == 0x113) /* WM_TIMER */
    {
        KillTimer(dialog, 1);
        int r[4] = {0}, work[4] = {0};
        if (!GetWindowRect(dialog, r) || !SystemParametersInfoA(0x30, 0, work, 0)) EndDialog(dialog, 7101);
        else if (r[0] != (work[0] + work[2] - (r[2] - r[0])) / 2 ||
                 r[1] != (work[1] + work[3] - (r[3] - r[1])) / 2) EndDialog(dialog, 7102);
        else if (GetActiveWindow() != dialog) EndDialog(dialog, 7103);
        else if (!SendDlgItemMessageA(dialog, 100, 0x173, 0, 0)) EndDialog(dialog, 7104); /* STM_GETIMAGE */
        else SetWindowTextA(dialog, "Center ok");
        return 1;
    }
    return 0;
}
void mainCRTStartup(void)
{
    const int result = DialogBoxParamA(GetModuleHandleA(0), 1, 0, (void *)procedure, 0);
    ExitProcess(result == -1 ? 7100 : (U)result);
}
