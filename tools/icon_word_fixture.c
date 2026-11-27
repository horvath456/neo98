// SPDX-License-Identifier: GPL-2.0-or-later
// The low word of an icon handle names the icon
// (tools/test_icon_word_host.py).
//
// Under Windows 9x an HICON is a 16-bit handle of USER, and a Win32 program
// may keep one in a word: the Explorer of Windows 95 sets the icon of a
// folder window with WM_SETICON and the low word of the handle. The program
// makes an icon and does with its low word what it does with the handle.
// C:\ICONWORD.TXT gets one line "name=value" per step.
#include <windows.h>

static char text[2048];
static int used;

static void line(const char *name, long value)
{
    used += wsprintfA(text + used, "%s=%ld\r\n", name, value);
}
// How many pixels DrawIconEx changes on magenta, and what it answers.
static int drawn(HICON icon, BOOL *answer)
{
    HDC screen = GetDC(0), dc = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, 32, 32), old = SelectObject(dc, bitmap);
    HBRUSH brush = CreateSolidBrush(RGB(255, 0, 255));
    RECT all = {0, 0, 32, 32};
    int x, y, changed = 0;
    FillRect(dc, &all, brush);
    *answer = DrawIconEx(dc, 0, 0, icon, 32, 32, 0, 0, DI_NORMAL);
    for (y = 0; y < 32; ++y)
        for (x = 0; x < 32; ++x)
            if (GetPixel(dc, x, y) != RGB(255, 0, 255)) ++changed;
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteObject(brush);
    DeleteDC(dc);
    ReleaseDC(0, screen);
    return changed;
}

void mainCRTStartup(void)
{
    static BYTE and_bits[32 * 4], xor_bits[32 * 4];
    WNDCLASSA c = {0};
    ICONINFO info;
    HICON icon, copy, word, copy_word;
    HWND window;
    HANDLE file;
    DWORD written = 0;
    BOOL answer = FALSE;
    int i, whole;
    for (i = 0; i < 32 * 4; ++i) and_bits[i] = i < 64 ? 0xff : 0, xor_bits[i] = i < 64 ? 0 : 0xaa;
    icon = CreateIcon(GetModuleHandleA(0), 32, 32, 1, 1, and_bits, xor_bits);
    word = (HICON)(DWORD_PTR)LOWORD((DWORD_PTR)icon);
    line("icon made", icon != 0);
    whole = drawn(icon, &answer);
    line("the handle draws", answer && whole > 0);
    line("the word draws the same", drawn(word, &answer) == whole && answer);
    line("the word has its bitmaps", GetIconInfo(word, &info) && info.fIcon && info.hbmMask);

    c.lpfnWndProc = DefWindowProcA;
    c.hInstance = GetModuleHandleA(0);
    c.lpszClassName = "IconWord";
    c.hIcon = word; // a class may be registered with the word
    line("class registered", RegisterClassA(&c) != 0);
    window = CreateWindowExA(0, "IconWord", "Icon word", WS_OVERLAPPEDWINDOW, 0, 0, 200, 100, 0, 0, c.hInstance, 0);
    line("class icon is the handle", (HICON)GetClassLongA(window, GCL_HICON) == icon);
    // WM_SETICON with the word: WM_GETICON and the next WM_SETICON answer the
    // handle the icon was made with, so a program finds its icon again.
    SendMessageA(window, WM_SETICON, ICON_BIG, (LPARAM)word);
    line("window icon is the handle", (HICON)SendMessageA(window, WM_GETICON, ICON_BIG, 0) == icon);
    line("previous icon is the handle", (HICON)SendMessageA(window, WM_SETICON, ICON_BIG, 0) == icon);
    SetClassLongA(window, GCL_HICONSM, (LONG)(DWORD_PTR)word);
    line("small class icon is the handle", (HICON)GetClassLongA(window, GCL_HICONSM) == icon);
    SetClassLongA(window, GCL_HICONSM, 0);

    copy = CopyIcon(word);
    line("the word is copied", copy && copy != icon && drawn(copy, &answer) == whole);
    copy_word = (HICON)(DWORD_PTR)LOWORD((DWORD_PTR)copy);
    line("the word is destroyed", DestroyIcon(copy_word) != 0);
    line("the destroyed handle is gone", !GetIconInfo(copy, &info));
    line("the destroyed word is gone", !GetIconInfo(copy_word, &info) && !drawn(copy_word, &answer) && !answer);
    line("the icon is still there", drawn(word, &answer) == whole);
    line("no icon by another word", !GetIconInfo((HICON)(DWORD_PTR)(LOWORD((DWORD_PTR)icon) ^ 0x400), &info));
    DestroyWindow(window);
    file = CreateFileA("C:\\ICONWORD.TXT", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    WriteFile(file, text, used, &written, 0);
    CloseHandle(file);
    ExitProcess(0);
}
