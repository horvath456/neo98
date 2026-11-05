/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
typedef struct { U style, proc; int ce, we; U instance, icon, cursor, background; const char *menu, *name; } Class;
typedef struct { int left, top, right, bottom; } Rect;
API U GetModuleHandleA(const char *);
API void ExitProcess(U);
API U GetClassInfoA(U, const char *, Class *);
API U RegisterClassA(const Class *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U CallWindowProcA(U, U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U SendMessageA(U, U, U, U);
API U InvalidateRect(U, const Rect *, U);
API U FillRect(U, const Rect *, U);
API U CreateSolidBrush(U);
API U DeleteObject(U);
static U original, draws, brush;
static U __stdcall subclass(U h, U m, U w, U l) { return CallWindowProcA(original, h, m, w, l); }
static U __stdcall parent_proc(U h, U m, U w, U l)
{
    if (m == 0x2c) { ((U *)l)[4] = 20; return 1; }
    if (m == 0x2b) {
        U *item = (U *)l;
        if (item[0] != 2 || item[2] != 0 || item[11] != 123) ExitProcess(3);
        if (!FillRect(item[6], (Rect *)(item + 7), brush)) ExitProcess(4);
        ++draws;
        return 1;
    }
    return DefWindowProcA(h, m, w, l);
}
void mainCRTStartup(void)
{
    U instance = GetModuleHandleA(0);
    Class parent = {0, (U)parent_proc, 0, 0, instance, 0, 0, 0, 0, "PaintParent"}, list;
    brush = CreateSolidBrush(0x008000);
    if (!RegisterClassA(&parent) || !GetClassInfoA(0, "LISTBOX", &list)) ExitProcess(1);
    original = list.proc;
    list.name = "SubclassedListBox"; list.instance = instance; list.proc = (U)subclass;
    if (!RegisterClassA(&list)) ExitProcess(2);
    U p = CreateWindowExA(0, parent.name, "Subclass listbox paint", 0x10000000, 0, 0, 200, 150, 0, 0, instance, 0);
    U h = CreateWindowExA(0, list.name, "", 0x50000020, 10, 10, 120, 90, p, 17, instance, 0);
    if (!p || !h || SendMessageA(h, 0x180, 0, 123) != 0) ExitProcess(5);
    InvalidateRect(h, 0, 1);
    SendMessageA(h, 0xf, 0, 0);
    if (!draws) ExitProcess(6);
    DeleteObject(brush);
    ExitProcess(0);
}
