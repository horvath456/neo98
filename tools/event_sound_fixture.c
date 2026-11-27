// SPDX-License-Identifier: GPL-2.0-or-later
// The sounds of the events of a sound scheme
// (tools/test_event_sounds_host.py).
//
//   EVSND.EXE assign EVENT FILE  what the Sounds page of the Control Panel
//                                writes for an event: the file as the default
//                                value of AppEvents\Schemes\Apps\.Default\
//                                EVENT\.Current of the user
//   EVSND.EXE beep TYPE          MessageBeep(TYPE), then two seconds of quiet
//   EVSND.EXE quiet TYPE         the same with the beep turned off first
//                                (SPI_SETBEEP, not stored)
//   EVSND.EXE alias EVENT        PlaySound(EVENT, SND_ALIAS | SND_NODEFAULT),
//                                to its end; the answer goes to C:\EVSND.TXT
//   EVSND.EXE window             a window with a menu bar, for the events of
//                                USER: the menu, and Maximize, Restore and
//                                Minimize as items that send the system
//                                command; a minimized window restores itself
//                                after a second
// TYPE is decimal.
#include <windows.h>
#include <mmsystem.h>

static const char *after(const char *command, const char *word)
{
    int i, n = lstrlenA(word);
    for (; *command; ++command)
    {
        for (i = 0; i < n && command[i] == word[i]; ++i)
            ;
        if (i == n && command[-1] == ' ' && (command[n] == ' ' || !command[n]))
        {
            command += n;
            while (*command == ' ')
                ++command;
            return command;
        }
    }
    return 0;
}
static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_COMMAND && LOWORD(wparam) == 2)
        DestroyWindow(window);
    if (message == WM_COMMAND && LOWORD(wparam) == 3)
        return SendMessageA(window, WM_SYSCOMMAND, SC_MAXIMIZE, 0);
    if (message == WM_COMMAND && LOWORD(wparam) == 4)
        return SendMessageA(window, WM_SYSCOMMAND, SC_RESTORE, 0);
    if (message == WM_COMMAND && LOWORD(wparam) == 5)
    {
        SendMessageA(window, WM_SYSCOMMAND, SC_MINIMIZE, 0);
        SetTimer(window, 1, 1000, 0);
        return 0;
    }
    if (message == WM_TIMER)
    {
        KillTimer(window, 1);
        return SendMessageA(window, WM_SYSCOMMAND, SC_RESTORE, 0);
    }
    if (message == WM_DESTROY)
        PostQuitMessage(0);
    return DefWindowProcA(window, message, wparam, lparam);
}

void mainCRTStartup(void)
{
    const char *command = GetCommandLineA(), *rest;
    char text[64];
    DWORD written = 0;
    HANDLE file;
    if ((rest = after(command, "assign")) != 0)
    {
        char key[200];
        int n;
        HKEY handle;
        lstrcpyA(key, "AppEvents\\Schemes\\Apps\\.Default\\");
        n = lstrlenA(key);
        while (*rest && *rest != ' ')
            key[n++] = *rest++;
        key[n] = 0;
        lstrcatA(key, "\\.Current");
        while (*rest == ' ')
            ++rest;
        if (!RegCreateKeyA(HKEY_CURRENT_USER, key, &handle))
        {
            RegSetValueExA(handle, 0, 0, REG_SZ, (const BYTE *)rest, lstrlenA(rest) + 1);
            RegCloseKey(handle);
        }
    }
    else if ((rest = after(command, "beep")) != 0 || (rest = after(command, "quiet")) != 0)
    {
        UINT type = 0;
        if (after(command, "quiet"))
            SystemParametersInfoA(SPI_SETBEEP, 0, 0, 0);
        while (*rest >= '0' && *rest <= '9')
            type = type * 10 + (*rest++ - '0');
        MessageBeep(type);
        Sleep(2000);
    }
    else if ((rest = after(command, "alias")) != 0)
    {
        const BOOL played = PlaySoundA(rest, 0, SND_ALIAS | SND_NODEFAULT | SND_SYNC);
        file = CreateFileA("C:\\EVSND.TXT", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
        WriteFile(file, text, wsprintfA(text, "played=%d\r\n", played), &written, 0);
        CloseHandle(file);
    }
    else if (after(command, "window"))
    {
        WNDCLASSA c = {0};
        MSG message;
        HMENU bar = CreateMenu(), popup = CreatePopupMenu();
        AppendMenuA(popup, MF_STRING, 1, "&Nothing");
        AppendMenuA(popup, MF_STRING, 3, "Ma&ximize");
        AppendMenuA(popup, MF_STRING, 4, "&Restore");
        AppendMenuA(popup, MF_STRING, 5, "M&inimize");
        AppendMenuA(popup, MF_STRING, 2, "&Exit");
        AppendMenuA(bar, MF_POPUP, (UINT_PTR)popup, "&File");
        c.lpfnWndProc = procedure;
        c.hInstance = GetModuleHandleA(0);
        c.lpszClassName = "EventSounds";
        c.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        c.hCursor = LoadCursorA(0, IDC_ARROW);
        RegisterClassA(&c);
        CreateWindowExA(0, c.lpszClassName, "Event sounds", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 20, 20, 300, 200, 0, bar,
                        c.hInstance, 0);
        while (GetMessageA(&message, 0, 0, 0) > 0)
        {
            TranslateMessage(&message);
            DispatchMessageA(&message);
        }
    }
    ExitProcess(0);
}
