/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The resource functions of USER with a module read as a data file. The
   program loads itself with LOAD_LIBRARY_AS_DATAFILE and takes each of its
   resources (tools/test_datafile_resource_host.py writes them) through that
   handle: icon 1 (red), cursor 2 (blue, hot spot 5,7), bitmap 3 (green, 8
   by 4), string 101, menus 4 and NAMEDMENU and dialog 5. C:\DATAFILE.TXT
   gets one line per call as soon as it is made, so a call that stops the
   program leaves the lines before it. */
#include <windows.h>

static HANDLE out;

static void put(const char *name, const char *format, ...)
{
    char line[256];
    va_list list;
    va_start(list, format);
    int n = wsprintfA(line, "%s=", name);
    n += wvsprintfA(line + n, format, list);
    va_end(list);
    line[n++] = '\r';
    line[n++] = '\n';
    DWORD written;
    WriteFile(out, line, n, &written, 0);
}

/* The colour in the middle of an icon or a cursor drawn on white. */
static COLORREF centre(HICON icon)
{
    HDC screen = GetDC(0), dc = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, 32, 32);
    HGDIOBJ old = SelectObject(dc, bitmap);
    RECT rect = {0, 0, 32, 32};
    FillRect(dc, &rect, (HBRUSH)GetStockObject(WHITE_BRUSH));
    COLORREF colour = icon && DrawIconEx(dc, 0, 0, icon, 32, 32, 0, 0, DI_NORMAL) ? GetPixel(dc, 16, 16) : CLR_INVALID;
    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    ReleaseDC(0, screen);
    return colour;
}

static void bitmap_line(const char *name, HBITMAP bitmap)
{
    BITMAP info = {0};
    COLORREF colour = CLR_INVALID;
    if (bitmap && GetObjectA(bitmap, sizeof info, &info))
    {
        HDC dc = CreateCompatibleDC(0);
        HGDIOBJ old = SelectObject(dc, bitmap);
        colour = GetPixel(dc, 1, 1);
        SelectObject(dc, old);
        DeleteDC(dc);
    }
    put(name, "%dx%d %06lx", (int)info.bmWidth, (int)info.bmHeight, (unsigned long)colour);
}

static INT_PTR CALLBACK modeless(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    (void)window; (void)wparam; (void)lparam;
    return message == WM_INITDIALOG;
}

static INT_PTR CALLBACK modal(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    (void)wparam; (void)lparam;
    if (message != WM_INITDIALOG)
        return FALSE;
    EndDialog(window, 42);
    return TRUE;
}

void mainCRTStartup(void)
{
    out = CreateFileA("C:\\DATAFILE.TXT", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    char path[MAX_PATH];
    GetModuleFileNameA(0, path, sizeof path);
    HMODULE data = LoadLibraryExA(path, 0, LOAD_LIBRARY_AS_DATAFILE);
    put("datafile", "%d", data && ((UINT_PTR)data & 1));
    put("find", "%d", FindResourceA(data, MAKEINTRESOURCE(1), RT_GROUP_ICON) != 0);

    HICON icon = (HICON)LoadImageA(data, MAKEINTRESOURCE(1), IMAGE_ICON, 32, 32, 0);
    put("loadimage-icon", "%06lx", (unsigned long)centre(icon));
    put("loadicon", "%06lx", (unsigned long)centre(LoadIconA(data, MAKEINTRESOURCE(1))));
    HCURSOR cursor = (HCURSOR)LoadImageA(data, MAKEINTRESOURCE(2), IMAGE_CURSOR, 32, 32, 0);
    ICONINFO hot = {0};
    if (cursor)
        GetIconInfo(cursor, &hot);
    put("loadimage-cursor", "%06lx %lu,%lu", (unsigned long)centre(cursor), (unsigned long)hot.xHotspot,
        (unsigned long)hot.yHotspot);
    put("loadcursor", "%06lx", (unsigned long)centre(LoadCursorA(data, MAKEINTRESOURCE(2))));
    bitmap_line("loadimage-bitmap", (HBITMAP)LoadImageA(data, MAKEINTRESOURCE(3), IMAGE_BITMAP, 0, 0, 0));
    bitmap_line("loadbitmap", LoadBitmapA(data, MAKEINTRESOURCE(3)));

    char text[64] = {0};
    int length = LoadStringA(data, 101, text, sizeof text);
    put("loadstring", "%d %s", length, text);

    HMENU menu = LoadMenuA(data, MAKEINTRESOURCE(4));
    char item[32] = {0};
    if (menu)
        GetMenuStringA(menu, 0, item, sizeof item, MF_BYPOSITION);
    put("loadmenu", "%d %s", menu ? GetMenuItemCount(menu) : -1, item);
    HMENU named = LoadMenuA(data, "NAMEDMENU");
    put("loadmenu-named", "%d", named ? GetMenuItemCount(named) : -1);

    HWND dialog = CreateDialogParamA(data, MAKEINTRESOURCE(5), 0, modeless, 0);
    char title[64] = {0};
    if (dialog)
        GetWindowTextA(dialog, title, sizeof title);
    put("createdialog", "%s %d", title, dialog && GetDlgItem(dialog, 100) != 0);
    if (dialog)
        DestroyWindow(dialog);
    put("dialogbox", "%d", (int)DialogBoxParamA(data, MAKEINTRESOURCE(5), 0, modal, 0));

    FreeLibrary(data);
    CloseHandle(out);
    ExitProcess(0);
}
