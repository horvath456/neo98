/* SPDX-License-Identifier: GPL-2.0-or-later */
#define fixture_entry unused_fixture_entry
#include "host_clipboard_fixture.c"
#undef fixture_entry
API void InitCommonControls(void);
API H ImageList_Create(int, int, U, int, int);
API int ImageList_ReplaceIcon(H, int, H);
API H CreateIcon(H, int, int, unsigned char, unsigned char, const void *, const void *);
API int DestroyIcon(H);
API U SendMessageA(H, U, U, U);
API int SetShellWindow(H);
API int GetSystemMetrics(int);
API int InvalidateRect(H, const void *, int);
static H list;
static U tick;
static void require(int ok, U error) { if (!ok) ExitProcess(error); }
static void geometry(void)
{
    for (U part = 1; part <= 2; ++part) {
        int r[4] = {(int)part};
        require(SendMessageA(list, 0x100e, 0, (U)r), 71);
        for (U k = 0; k < 4; ++k) { put_number((U)r[k]); put(" "); }
    }
    put("\r\n"); write_output();
}
static int __stdcall procedure(H w, U m, U wp, U lp)
{
    if (m == 0x113) {
        if (++tick == 1) {
            SendMessageA(list, 0x100f, 0, 100 | (150u << 16)); // LVM_SETITEMPOSITION
            SendMessageA(list, 0x1001, 0, 0x332211); // distinct wallpaper/background colour
            InvalidateRect(list, 0, 1);
            geometry();
        } else if (tick == 2) {
            SendMessageA(list, 0x1009, 0, 0); // LVM_DELETEALLITEMS
            InvalidateRect(list, 0, 1);
        }
        return 0;
    }
    return DefWindowProcA(w, m, wp, lp);
}
void fixture_entry(void)
{
    GetModuleFileNameA(0, path, sizeof path);
    H instance = GetModuleHandleA(0);
    struct { U style; void *procedure; int a, b; H instance, icon, cursor, brush; const char *menu, *name; } cls = {0};
    cls.procedure = (void *)procedure; cls.instance = instance; cls.brush = (H)2; cls.name = "NEO98MaskShell";
    require(RegisterClassA(&cls), 72);
    int width = GetSystemMetrics(0), height = GetSystemMetrics(1);
    H shell = CreateWindowExA(0, cls.name, "mask shell", 0x90000000, 0, 0, width, height, 0, 0, instance, 0);
    require(shell && SetShellWindow(shell), 73);
    cls.name = "SHELLDLL_DefView"; require(RegisterClassA(&cls), 74);
    H view = CreateWindowExA(0, cls.name, "", 0x50000000, 0, 0, width, height, shell, 0, instance, 0);
    InitCommonControls();
    list = CreateWindowExA(0, "SysListView32", "", 0x50000000, 0, 0, width, height, view, 0, instance, 0);
    require(list != 0, 75);
    SendMessageA(list, 0x1001, 0, 0x808000); // COLORREF for desktop teal
    SendMessageA(list, 0x1026, 0, 0x808000); // LVM_SETTEXTBKCOLOR
    static unsigned char mask[128]; static U colour[1024];
    memset(mask, 255, sizeof mask); memset(colour, 0, sizeof colour);
    mask[3 * 4] &= ~(128u >> 3); colour[3 * 32 + 3] = 0x008080;
    H icon = CreateIcon(instance, 32, 32, 1, 32, mask, colour);
    H images = ImageList_Create(32, 32, 0x21, 1, 1);
    require(icon && images && ImageList_ReplaceIcon(images, -1, icon) == 0, 76);
    DestroyIcon(icon);
    SendMessageA(list, 0x1003, 0, (U)images);
    U item[10] = {3, 0, 0, 0, 0, (U)"Label", 0, 0};
    require(SendMessageA(list, 0x1007, 0, (U)item) == 0, 77);
    SendMessageA(list, 0x100f, 0, 32 | (40u << 16));
    geometry();
    SetTimer(shell, 1, 4000, 0);
    U message[8];
    while (GetMessageA(message, 0, 0, 0) > 0) DispatchMessageA(message);
    ExitProcess(0);
}
