/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A message-pumping process for host-requested DOS property sheets. */
#include <windows.h>
void mainCRTStartup(void)
{
    WNDCLASSA wc = {0};
    MSG message;
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "DosSettingsHostFixture";
    RegisterClassA(&wc);
    CreateWindowA(wc.lpszClassName, "DOS settings fixture", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                  450, 50, 300, 200, NULL, NULL, wc.hInstance, NULL);
    while (GetMessageA(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    ExitProcess(0);
}
