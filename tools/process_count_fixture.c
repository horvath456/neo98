// SPDX-License-Identifier: GPL-2.0-or-later
// As many processes at once as the caller asks for
// (tools/test_process_count_host.py).
//
//   PROCS.EXE N     starts itself N times as "child", each child with a
//                   window of its own, and writes to C:\PROCS.TXT how many
//                   starts succeeded, the error of the first that did not,
//                   what WinExec answers then, and how many children said
//                   they run; then it lets them all end
//   PROCS.EXE child registers a window, counts itself in the shared section
//                   and waits for the event the first process sets
#include <windows.h>

static char text[1024];
static int used;

static void line(const char *name, long value)
{
    used += wsprintfA(text + used, "%s=%ld\r\n", name, value);
}
static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    return DefWindowProcA(window, message, wparam, lparam);
}

void mainCRTStartup(void)
{
    const char *command = GetCommandLineA();
    HANDLE done = CreateEventA(0, TRUE, FALSE, "ProcessCountDone");
    HANDLE mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, 0, PAGE_READWRITE, 0, 4096, "ProcessCountShared");
    volatile LONG *running = (volatile LONG *)MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, 0);
    int length = lstrlenA(command), wanted = 0, started = 0, first_error = 0, i;
    if (length > 6 && !lstrcmpA(command + length - 6, " child"))
    {
        WNDCLASSA c = {0};
        MSG message;
        c.lpfnWndProc = procedure;
        c.hInstance = GetModuleHandleA(0);
        c.lpszClassName = "ProcessCountChild";
        RegisterClassA(&c);
        CreateWindowExA(0, c.lpszClassName, "child", WS_OVERLAPPED, 0, 0, 100, 50, 0, 0, c.hInstance, 0);
        InterlockedIncrement((LONG *)running);
        // The event, with the messages of the window in between.
        while (MsgWaitForMultipleObjects(1, &done, FALSE, INFINITE, QS_ALLINPUT) != WAIT_OBJECT_0)
            while (PeekMessageA(&message, 0, 0, 0, PM_REMOVE))
                DispatchMessageA(&message);
        ExitProcess(0);
    }
    for (i = length; i > 0 && command[i - 1] >= '0' && command[i - 1] <= '9'; --i)
        ;
    for (; command[i]; ++i)
        wanted = wanted * 10 + (command[i] - '0');
    {
        static HANDLE processes[256];
        char own[MAX_PATH + 16], short_own[MAX_PATH + 16];
        DWORD written = 0, waited;
        HANDLE file;
        GetModuleFileNameA(0, own, MAX_PATH);
        lstrcpyA(short_own, own);
        lstrcatA(own, " child");
        lstrcatA(short_own, " child");
        for (i = 0; i < wanted && i < 256; ++i)
        {
            STARTUPINFOA start = {sizeof start};
            PROCESS_INFORMATION child;
            SetLastError(0);
            if (!CreateProcessA(0, own, 0, 0, FALSE, 0, 0, 0, &start, &child))
            {
                first_error = GetLastError();
                break;
            }
            CloseHandle(child.hThread);
            processes[started++] = child.hProcess;
        }
        // Every child that started says so before the count is taken.
        for (waited = 0; waited < 60000 && *running < started; waited += 50)
            Sleep(50);
        line("wanted", wanted);
        line("started", started);
        line("first error", first_error);
        line("running", *running);
        if (started < wanted)
        {
            SetLastError(0);
            line("WinExec", WinExec(short_own, SW_SHOW));
            line("WinExec error", GetLastError());
        }
        SetEvent(done);
        for (i = 0; i < started; ++i)
            WaitForSingleObject(processes[i], 30000);
        for (i = 0, waited = 0; i < started; ++i)
        {
            DWORD code = 1;
            GetExitCodeProcess(processes[i], &code);
            if (!code)
                ++waited;
        }
        line("ended", waited);
        file = CreateFileA("C:\\PROCS.TXT", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
        WriteFile(file, text, used, &written, 0);
        CloseHandle(file);
    }
    ExitProcess(0);
}
