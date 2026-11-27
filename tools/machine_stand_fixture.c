/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A program whose sound keeps playing (tools/test_machine_stand_host.py):
   TADA.WAV in a loop. Its window says "playing" once PlaySound has taken
   the sound, "silent" when it refused. */
#include <windows.h>
#include <mmsystem.h>

static LRESULT CALLBACK frame(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_DESTROY) { PlaySoundA(0, 0, 0); PostQuitMessage(0); return 0; }
    return DefWindowProcA(h, m, w, l);
}

void mainCRTStartup(void)
{
    HINSTANCE instance = GetModuleHandleA(0);
    WNDCLASSA k = {0};
    k.hInstance = instance; k.lpfnWndProc = frame; k.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    k.lpszClassName = "MachineStand";
    RegisterClassA(&k);
    const BOOL playing = PlaySoundA("C:\\WINDOWS\\MEDIA\\TADA.WAV", 0, SND_FILENAME | SND_ASYNC | SND_LOOP);
    CreateWindowA("MachineStand", playing ? "playing" : "silent", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40, 300, 150,
                  0, 0, instance, 0);
    MSG message;
    while (GetMessageA(&message, 0, 0, 0)) { TranslateMessage(&message); DispatchMessageA(&message); }
    ExitProcess(0);
}
