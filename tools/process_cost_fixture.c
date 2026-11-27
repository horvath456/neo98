// SPDX-License-Identifier: GPL-2.0-or-later
// Starts programs in stages and holds them, so that the memory of the host
// can be read while they run (tools/test_process_cost_host.py).
//
//   PCOST.EXE C:\PCOST.LST
//   PCOST.EXE child        the smallest program with a window: it registers a
//                          class, makes a window and waits for messages
//   PCOST.EXE plain        allocates 64 pages of data, writes a RET at the
//                          start of every second one and ends
//   PCOST.EXE jump         the same, and calls each of those 32 pages before
//                          it ends: code in memory that was allocated as data
//
// Every line of the list is a command line to start, and a line "---" ends a
// stage: the fixture waits until the programs of the stage are idle, writes
// C:\PCOST.Mn (n counts the stages from one) and waits for C:\PCOST.Gn. A line
// "===" ends every program started so far and waits for them before it ends
// the stage the same way. After the last stage the fixture ends every program
// it started, waits for them, and holds once more (the stage after the last)
// before it ends itself.
#include <windows.h>

static char list[8192];
static HANDLE processes[256];
static int started;

static void hold(int stage)
{
    char name[32];
    HANDLE file;
    int waited;
    wsprintfA(name, "C:\\PCOST.M%d", stage);
    file = CreateFileA(name, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    if (file != INVALID_HANDLE_VALUE)
    {
        DWORD written = 0;
        char text[32];
        WriteFile(file, text, wsprintfA(text, "%d\r\n", started), &written, 0);
        CloseHandle(file);
    }
    wsprintfA(name, "C:\\PCOST.G%d", stage);
    for (waited = 0; waited < 600000 && GetFileAttributesA(name) == 0xffffffffu; waited += 50)
        Sleep(50);
}
static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    return DefWindowProcA(window, message, wparam, lparam);
}
static void end_all(void)
{
    int i;
    for (i = 0; i < started; ++i)
        TerminateProcess(processes[i], 0);
    for (i = 0; i < started; ++i)
    {
        WaitForSingleObject(processes[i], 30000);
        CloseHandle(processes[i]);
    }
    started = 0;
}
void mainCRTStartup(void)
{
    const char *command = GetCommandLineA();
    DWORD size = 0;
    HANDLE file;
    char *line, *end;
    int stage = 1, first = 0, i;
    while (*command && *command != ' ')
        ++command;
    while (*command == ' ')
        ++command;
    if (!lstrcmpA(command, "plain") || !lstrcmpA(command, "jump"))
    {
        unsigned char *pages = (unsigned char *)VirtualAlloc(0, 64 * 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        int result = pages ? 0 : 3;
        for (i = 0; pages && i < 64; i += 2)
        {
            pages[i * 4096] = 0xc3;
            if (command[0] == 'j')
                ((void (*)(void))(pages + i * 4096))();
        }
        ExitProcess(result);
    }
    if (!lstrcmpA(command, "child"))
    {
        WNDCLASSA c = {0};
        MSG message;
        c.lpfnWndProc = procedure;
        c.hInstance = GetModuleHandleA(0);
        c.lpszClassName = "ProcessCostChild";
        RegisterClassA(&c);
        CreateWindowExA(0, c.lpszClassName, "child", WS_OVERLAPPED, 0, 0, 100, 50, 0, 0, c.hInstance, 0);
        while (GetMessageA(&message, 0, 0, 0) > 0)
            DispatchMessageA(&message);
        ExitProcess(0);
    }
    file = CreateFileA(command, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    if (file == INVALID_HANDLE_VALUE)
        ExitProcess(2);
    ReadFile(file, list, sizeof list - 1, &size, 0);
    CloseHandle(file);
    list[size] = 0;
    for (line = list; *line; line = end)
    {
        for (end = line; *end && *end != '\r' && *end != '\n'; ++end)
            ;
        while (*end == '\r' || *end == '\n')
            *end++ = 0;
        if (!lstrcmpA(line, "==="))
        {
            end_all();
            first = 0;
            hold(stage++);
        }
        else if (!lstrcmpA(line, "---"))
        {
            for (i = first; i < started; ++i)
                WaitForInputIdle(processes[i], 30000);
            first = started;
            hold(stage++);
        }
        else if (*line && started < 256)
        {
            STARTUPINFOA start = {sizeof start};
            PROCESS_INFORMATION child;
            if (CreateProcessA(0, line, 0, 0, FALSE, 0, 0, 0, &start, &child))
            {
                CloseHandle(child.hThread);
                processes[started++] = child.hProcess;
            }
        }
    }
    end_all();
    hold(stage);
    ExitProcess(0);
}
