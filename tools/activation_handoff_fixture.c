/* SPDX-License-Identifier: GPL-2.0-or-later
 * a window that goes hands the activation on the way USER
 * chooses it. The modal dialog "Handoff" opens a visible modeless pop-up it
 * owns; destroying it (DestroyWindow, code 2 on the owner) and
 * hiding it (ShowWindow, code 3) make the owner active again. An
 * unowned pop-up that goes gives the activation to the next top-level
 * window, the dialog again. the dialog font of 8 points is
 * -MulDiv(8, 96, 72) = -11 pixels high. When all hold the caption
 * reads "Handoff ok", otherwise the dialog ends with the failing step. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U GetModuleHandleA(const char *);
API void ExitProcess(U);
API int DialogBoxParamA(U, U, U, void *, U);
API U CreateDialogParamA(U, U, U, void *, U);
API U EndDialog(U, int);
API U SetTimer(U, U, U, void *);
API U KillTimer(U, U);
API U GetActiveWindow(void);
API U DestroyWindow(U);
API U ShowWindow(U, int);
API U SendMessageA(U, U, U, U);
API U SetWindowTextA(U, const char *);
API U SendDlgItemMessageA(U, int, U, U, U);
API int GetObjectA(U, int, void *);
static U __stdcall tool(U dialog, U message, U wp, U lp)
{
    (void)dialog; (void)wp; (void)lp;
    return message == 0x110;
}
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
        const U instance = GetModuleHandleA(0);
        int font[15] = {0};
        const U handle = SendDlgItemMessageA(dialog, 100, 0x31, 0, 0); /* WM_GETFONT */
        if (!handle) { EndDialog(dialog, 7101); return 1; }
        if (SendMessageA(dialog, 0x31, 0, 0) != handle) { EndDialog(dialog, 7112); return 1; }
        if (!GetObjectA(handle, 60, font)) { EndDialog(dialog, 7111); return 1; }
        if (font[0] != -11) { EndDialog(dialog, 7102); return 1; }
        if (GetActiveWindow() != dialog) { EndDialog(dialog, 7103); return 1; }
        U owned = CreateDialogParamA(instance, 2, dialog, (void *)tool, 0);
        if (!owned || GetActiveWindow() != owned) { EndDialog(dialog, 7104); return 1; }
        DestroyWindow(owned);
        if (GetActiveWindow() != dialog) { EndDialog(dialog, 7105); return 1; }
        owned = CreateDialogParamA(instance, 2, dialog, (void *)tool, 0);
        if (!owned || GetActiveWindow() != owned) { EndDialog(dialog, 7106); return 1; }
        ShowWindow(owned, 0);
        if (GetActiveWindow() != dialog) { EndDialog(dialog, 7107); return 1; }
        DestroyWindow(owned);
        if (GetActiveWindow() != dialog) { EndDialog(dialog, 7108); return 1; }
        const U alone = CreateDialogParamA(instance, 2, 0, (void *)tool, 0);
        if (!alone || GetActiveWindow() != alone) { EndDialog(dialog, 7109); return 1; }
        DestroyWindow(alone);
        if (GetActiveWindow() != dialog) { EndDialog(dialog, 7110); return 1; }
        SetWindowTextA(dialog, "Handoff ok");
        return 1;
    }
    return 0;
}
void mainCRTStartup(void)
{
    const int result = DialogBoxParamA(GetModuleHandleA(0), 1, 0, (void *)procedure, 0);
    ExitProcess(result == -1 ? 7100 : (U)result);
}
