// SPDX-License-Identifier: GPL-2.0-or-later
// The sizes an MS-DOS window offers, and what it does with one that is
// chosen (tools/test_dos_font_host.py).
//
//   DOSFONT.EXE    starts C:\WINDOWS\COMMAND.COM, waits for its window (class
//                  "tty") and the size box in its toolbar, and writes to
//                  C:\DOSFONT.TXT: the sizes of the box (its item data: 0xffff
//                  for Auto, else width | height << 8, with 0x8000 for a
//                  TrueType size); the size of the picture after the TrueType
//                  size 15 pixels high was chosen; and the size of the picture
//                  with Auto, once in the work area as it is and once after
//                  the work area was made 300 pixels high. Then it ends the
//                  MS-DOS window.
#include <windows.h>

static char text[4096];
static int used;

static void line(const char *name, long value)
{
    used += wsprintfA(text + used, "%s=%ld\r\n", name, value);
}
static void pump(int milliseconds)
{
    MSG message;
    DWORD until = GetTickCount() + milliseconds;
    do
    {
        while (PeekMessageA(&message, 0, 0, 0, PM_REMOVE))
            DispatchMessageA(&message);
        Sleep(20);
    } while ((int)(until - GetTickCount()) > 0);
}
// Chooses an item of the box and waits until the window has a new size, or
// for five seconds when it keeps the one it has.
static void choose(HWND window, HWND box, int index, const char *name)
{
    RECT before, after;
    char key[64];
    int waited;
    GetClientRect(window, &before);
    SendMessageA(box, CB_SETCURSEL, index, 0);
    SendMessageA(window, WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(box), CBN_SELCHANGE), (LPARAM)box);
    for (waited = 0; waited < 5000; waited += 100)
    {
        pump(100);
        GetClientRect(window, &after);
        if (after.right != before.right || after.bottom != before.bottom)
            break;
    }
    pump(300);
    GetClientRect(window, &after);
    wsprintfA(key, "%s width", name);
    line(key, after.right);
    wsprintfA(key, "%s height", name);
    line(key, after.bottom);
}
void mainCRTStartup(void)
{
    STARTUPINFOA start = {sizeof start};
    PROCESS_INFORMATION child;
    char command[] = "C:\\WINDOWS\\COMMAND.COM";
    HWND window = 0, box = 0;
    RECT work, frame, client, small;
    DWORD written = 0;
    HANDLE file;
    int waited, count = 0, i, truetype = -1;
    if (!CreateProcessA(0, command, 0, 0, FALSE, 0, 0, 0, &start, &child))
    {
        line("start error", GetLastError());
        goto done;
    }
    for (waited = 0; waited < 30000 && !box; waited += 100)
    {
        pump(100);
        window = FindWindowA("tty", 0);
        box = window ? FindWindowExA(window, 0, "COMBOBOX", 0) : 0;
    }
    line("window", window != 0);
    line("box", box != 0);
    if (!box)
        goto end;
    pump(1500);
    count = (int)SendMessageA(box, CB_GETCOUNT, 0, 0);
    line("sizes", count);
    for (i = 0; i < count && i < 64; ++i)
    {
        char key[16];
        const unsigned data = (unsigned)SendMessageA(box, CB_GETITEMDATA, i, 0);
        wsprintfA(key, "size %d", i);
        line(key, data);
        if (data != 0xffff && (data & 0x8000) && ((data >> 8) & 0x7f) == 15)
            truetype = i;
    }
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0);
    line("work width", work.right - work.left);
    line("work height", work.bottom - work.top);
    GetWindowRect(window, &frame);
    GetClientRect(window, &client);
    line("frame width", (frame.right - frame.left) - client.right);
    line("frame height", (frame.bottom - frame.top) - client.bottom);
    if (truetype >= 0)
        choose(window, box, truetype, "truetype");
    choose(window, box, 0, "auto");
    small = work;
    small.bottom = small.top + 300;
    line("work area set", SystemParametersInfoA(SPI_SETWORKAREA, 0, &small, 0));
    // Another size and Auto again, so that Auto is worked out anew.
    choose(window, box, 1, "first");
    choose(window, box, 0, "auto small");
    SystemParametersInfoA(SPI_SETWORKAREA, 0, &work, 0);
end:
    TerminateProcess(child.hProcess, 0);
    WaitForSingleObject(child.hProcess, 10000);
done:
    line("done", 1);
    file = CreateFileA("C:\\DOSFONT.TXT", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    WriteFile(file, text, used, &written, 0);
    CloseHandle(file);
    ExitProcess(0);
}
