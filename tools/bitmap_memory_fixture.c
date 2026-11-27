// SPDX-License-Identifier: GPL-2.0-or-later
// Device bitmaps until GDI has no memory for another one
// (tools/test_bitmap_memory_host.py).
//
//   BMPMEM.EXE N    creates up to N compatible bitmaps of 1024 x 1024 pixels,
//                   writes to C:\BMPMEM.TXT how many it got, what the first
//                   failure answered and GetLastError after it, whether a
//                   bitmap created before still takes a pixel and gives it
//                   back, then deletes all of them and creates as many again
#include <windows.h>

static char text[1024];
static int used;
static HBITMAP bitmaps[1024];

static void line(const char *name, long value)
{
    used += wsprintfA(text + used, "%s=%ld\r\n", name, value);
}
static int fill(HDC screen, int wanted, int *error)
{
    int count = 0;
    *error = 0;
    for (; count < wanted; ++count)
    {
        SetLastError(0);
        bitmaps[count] = CreateCompatibleBitmap(screen, 1024, 1024);
        if (!bitmaps[count])
        {
            *error = GetLastError();
            break;
        }
    }
    return count;
}
void mainCRTStartup(void)
{
    const char *command = GetCommandLineA();
    HDC screen = GetDC(0), memory = CreateCompatibleDC(screen);
    int wanted = 0, i, first, second, error, error_again, pixel = -1;
    DWORD written = 0;
    HANDLE file;
    BITMAP shape = {0};
    for (i = lstrlenA(command); i > 0 && command[i - 1] >= '0' && command[i - 1] <= '9'; --i)
        ;
    for (; command[i]; ++i)
        wanted = wanted * 10 + (command[i] - '0');
    if (wanted > 1024)
        wanted = 1024;
    first = fill(screen, wanted, &error);
    if (first)
    {
        HGDIOBJ old = SelectObject(memory, bitmaps[0]);
        GetObjectA(bitmaps[0], sizeof shape, &shape);
        SetPixel(memory, 1000, 1000, RGB(255, 0, 0));
        pixel = GetPixel(memory, 1000, 1000) == RGB(255, 0, 0);
        SelectObject(memory, old);
    }
    for (i = 0; i < first; ++i)
        DeleteObject(bitmaps[i]);
    second = fill(screen, wanted, &error_again);
    for (i = 0; i < second; ++i)
        DeleteObject(bitmaps[i]);
    line("wanted", wanted);
    line("created", first);
    line("error", error);
    line("bytes each", shape.bmWidthBytes * shape.bmHeight);
    line("pixel", pixel);
    line("created again", second);
    line("error again", error_again);
    line("done", 1);
    file = CreateFileA("C:\\BMPMEM.TXT", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    WriteFile(file, text, used, &written, 0);
    CloseHandle(file);
    ExitProcess(0);
}
