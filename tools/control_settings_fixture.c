// SPDX-License-Identifier: GPL-2.0-or-later
// The settings the Keyboard, Mouse, Sounds and Display pages of the Control
// Panel set, as a program sets them (tools/test_control_settings_host.py).
//
//   CTLSET.EXE set    sets every value with SystemParametersInfo and
//                     SPIF_UPDATEINIFILE | SPIF_SENDCHANGE, the caret time
//                     and three system colours the way the applets do, and
//                     starts itself as "child"; writes C:\SET.TXT
//   CTLSET.EXE child  what a second process sees at once: C:\CHILD.TXT
//   CTLSET.EXE get    what a new session starts with: C:\GET.TXT
//   CTLSET.EXE keys SPEED DELAY
//                     sets the typematic values without storing them, opens
//                     a window and writes the times of the WM_KEYDOWN of one
//                     held key to C:\KEYS.TXT when the key goes up
#include <windows.h>

#ifndef SPI_GETWHEELSCROLLLINES
#define SPI_GETWHEELSCROLLLINES 104
#define SPI_SETWHEELSCROLLLINES 105
#endif
#ifndef SPI_GETMENUSHOWDELAY
#define SPI_GETMENUSHOWDELAY 106
#define SPI_SETMENUSHOWDELAY 107
#endif

static char text[8192];
static int used;

static void line(const char *name, long value)
{
    used += wsprintfA(text + used, "%s=%ld\r\n", name, value);
}
static void save(const char *path)
{
    DWORD written = 0;
    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    WriteFile(file, text, used, &written, 0);
    CloseHandle(file);
}
static int has(const char *command, const char *word)
{
    int i, n = lstrlenA(word);
    for (; *command; ++command)
    {
        for (i = 0; i < n && command[i] == word[i]; ++i)
            ;
        if (i == n && command[-1] == ' ' && (command[n] == 0 || command[n] == ' '))
            return 1;
    }
    return 0;
}
static long number_after(const char *command, const char *word, int which)
{
    int n = lstrlenA(word), i;
    long value = 0;
    for (; *command; ++command)
    {
        for (i = 0; i < n && command[i] == word[i]; ++i)
            ;
        if (i != n)
            continue;
        command += n;
        for (;;)
        {
            while (*command == ' ')
                ++command;
            value = 0;
            while (*command >= '0' && *command <= '9')
                value = value * 10 + (*command++ - '0');
            if (!which--)
                return value;
        }
    }
    return -1;
}

static int changes, last_change;
static DWORD key_times[256];
static int key_count, key_repeats;
static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_SETTINGCHANGE)
    {
        ++changes;
        last_change = (int)wparam;
    }
    if (message == WM_KEYDOWN && wparam == 'A' && key_count < 256)
    {
        key_times[key_count++] = GetTickCount();
        if (lparam & 0x40000000)
            ++key_repeats;
    }
    if (message == WM_KEYUP && wparam == 'A')
        PostQuitMessage(0);
    return DefWindowProcA(window, message, wparam, lparam);
}
static HWND window(int visible)
{
    WNDCLASSA c = {0};
    c.lpfnWndProc = procedure;
    c.hInstance = GetModuleHandleA(0);
    c.lpszClassName = "ControlSettings";
    c.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassA(&c);
    return CreateWindowExA(0, c.lpszClassName, "Control settings", WS_OVERLAPPEDWINDOW | (visible ? WS_VISIBLE : 0),
                           40, 40, 300, 200, 0, 0, c.hInstance, 0);
}
// The applets write what USER does not store itself.
static void store(const char *key, const char *name, const char *value)
{
    HKEY handle;
    if (RegCreateKeyA(HKEY_CURRENT_USER, key, &handle))
        return;
    RegSetValueExA(handle, name, 0, REG_SZ, (const BYTE *)value, lstrlenA(value) + 1);
    RegCloseKey(handle);
}
static void read_all(void)
{
    int mouse[3] = {-1, -1, -1};
    UINT value = 0xdead;
    line("keyboard speed result", SystemParametersInfoA(SPI_GETKEYBOARDSPEED, 0, &value, 0));
    line("keyboard speed", value);
    value = 0xdead;
    line("keyboard delay result", SystemParametersInfoA(SPI_GETKEYBOARDDELAY, 0, &value, 0));
    line("keyboard delay", value);
    line("double click time", GetDoubleClickTime());
    line("caret blink time", GetCaretBlinkTime());
    line("mouse result", SystemParametersInfoA(SPI_GETMOUSE, 0, mouse, 0));
    line("mouse threshold 1", mouse[0]);
    line("mouse threshold 2", mouse[1]);
    line("mouse speed", mouse[2]);
    value = 0xdead;
    line("beep result", SystemParametersInfoA(SPI_GETBEEP, 0, &value, 0));
    line("beep", value);
    line("double click width", GetSystemMetrics(SM_CXDOUBLECLK));
    line("double click height", GetSystemMetrics(SM_CYDOUBLECLK));
    line("drag width", GetSystemMetrics(SM_CXDRAG));
    line("drag height", GetSystemMetrics(SM_CYDRAG));
    line("menu drop alignment", GetSystemMetrics(SM_MENUDROPALIGNMENT));
    value = 0xdead;
    line("wheel lines result", SystemParametersInfoA(SPI_GETWHEELSCROLLLINES, 0, &value, 0));
    line("wheel lines", value);
    value = 0xdead;
    line("menu show delay result", SystemParametersInfoA(SPI_GETMENUSHOWDELAY, 0, &value, 0));
    line("menu show delay", value);
    line("swapped", GetSystemMetrics(SM_SWAPBUTTON));
    line("colour background", GetSysColor(COLOR_BACKGROUND));
    line("colour window", GetSysColor(COLOR_WINDOW));
    line("colour button face", GetSysColor(COLOR_BTNFACE));
    line("colour active caption", GetSysColor(COLOR_ACTIVECAPTION));
}
static void pump(DWORD milliseconds)
{
    MSG message;
    const DWORD end = GetTickCount() + milliseconds;
    while (GetTickCount() < end)
    {
        while (PeekMessageA(&message, 0, 0, 0, PM_REMOVE))
            DispatchMessageA(&message);
        Sleep(10);
    }
}

void mainCRTStartup(void)
{
    const char *command = GetCommandLineA();
    const UINT keep = SPIF_UPDATEINIFILE | SPIF_SENDCHANGE;
    if (has(command, "set"))
    {
        int mouse[3] = {4, 12, 2};
        int elements[3] = {COLOR_BACKGROUND, COLOR_WINDOW, COLOR_BTNFACE};
        COLORREF colours[3] = {RGB(10, 20, 30), RGB(250, 240, 230), RGB(100, 110, 120)};
        STARTUPINFOA start = {sizeof start};
        PROCESS_INFORMATION child;
        char own[MAX_PATH + 16];
        window(0);
        line("set keyboard speed", SystemParametersInfoA(SPI_SETKEYBOARDSPEED, 20, 0, keep));
        line("changes after keyboard speed", changes);
        line("change was", last_change);
        line("set keyboard delay", SystemParametersInfoA(SPI_SETKEYBOARDDELAY, 2, 0, keep));
        line("set double click time", SystemParametersInfoA(SPI_SETDOUBLECLICKTIME, 777, 0, keep));
        line("set mouse", SystemParametersInfoA(SPI_SETMOUSE, 0, mouse, keep));
        line("set beep", SystemParametersInfoA(SPI_SETBEEP, 0, 0, keep));
        line("set double click width", SystemParametersInfoA(SPI_SETDOUBLECLKWIDTH, 9, 0, keep));
        line("set double click height", SystemParametersInfoA(SPI_SETDOUBLECLKHEIGHT, 7, 0, keep));
        line("set drag width", SystemParametersInfoA(SPI_SETDRAGWIDTH, 6, 0, keep));
        line("set drag height", SystemParametersInfoA(SPI_SETDRAGHEIGHT, 5, 0, keep));
        line("set menu drop alignment", SystemParametersInfoA(SPI_SETMENUDROPALIGNMENT, 1, 0, keep));
        line("set wheel lines", SystemParametersInfoA(SPI_SETWHEELSCROLLLINES, 5, 0, keep));
        line("set menu show delay", SystemParametersInfoA(SPI_SETMENUSHOWDELAY, 250, 0, keep));
        line("set mouse button swap", SystemParametersInfoA(SPI_SETMOUSEBUTTONSWAP, 1, 0, keep));
        line("changes", changes);
        // The Keyboard page: SetCaretBlinkTime, and the value USER reads at
        // its next start.
        line("set caret blink time", SetCaretBlinkTime(333));
        store("Control Panel\\Desktop", "CursorBlinkRate", "333");
        // The Appearance page: SetSysColors, and each colour under its name.
        line("set colours", SetSysColors(3, elements, colours));
        store("Control Panel\\Colors", "Background", "10 20 30");
        store("Control Panel\\Colors", "Window", "250 240 230");
        store("Control Panel\\Colors", "ButtonFace", "100 110 120");
        read_all();
        save("C:\\SET.TXT");
        GetModuleFileNameA(0, own, MAX_PATH);
        lstrcatA(own, " child");
        if (CreateProcessA(0, own, 0, 0, 0, 0, 0, 0, &start, &child))
            WaitForSingleObject(child.hProcess, 20000);
    }
    else if (has(command, "child"))
    {
        read_all();
        save("C:\\CHILD.TXT");
    }
    else if (has(command, "get"))
    {
        read_all();
        save("C:\\GET.TXT");
    }
    else if (has(command, "keys"))
    {
        MSG message;
        int i;
        HWND shown;
        line("set keyboard speed", SystemParametersInfoA(SPI_SETKEYBOARDSPEED, number_after(command, "keys", 0), 0, 0));
        line("set keyboard delay", SystemParametersInfoA(SPI_SETKEYBOARDDELAY, number_after(command, "keys", 1), 0, 0));
        shown = window(1);
        SetForegroundWindow(shown);
        SetFocus(shown);
        while (GetMessageA(&message, 0, 0, 0) > 0)
            DispatchMessageA(&message);
        line("key downs", key_count);
        line("repeats", key_repeats);
        for (i = 1; i < key_count; ++i)
            line("after", key_times[i] - key_times[i - 1]);
        save("C:\\KEYS.TXT");
    }
    pump(0);
    ExitProcess(0);
}
