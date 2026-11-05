/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "capture_handshake.h"
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
typedef struct
{
    U hwnd, message, wp, lp, time;
    int x, y;
} CaptureMessage;
API U GetMessageA(CaptureMessage *, U, U, U);
API U TranslateMessage(const CaptureMessage *);
API U DispatchMessageA(const CaptureMessage *);
API U PeekMessageA(CaptureMessage *, U, U, U, U);
/* Paint messages run only after other queued work. Owner-draw callbacks execute in the owning thread's message loop; a wall-clock delay is not synchronization. */
static void pump(void)
{
    CaptureMessage message;
    unsigned n;
    for (n = 0; n < 8; ++n)
        PeekMessageA(&message, 0, 0, 0, 0);
}
typedef struct
{
    int left, top, right, bottom;
} Rect;
typedef struct
{
    U style;
    U(__stdcall *proc)(U, U, U, U);
    int ce, we;
    U instance, icon, cursor, background;
    const char *menu, *name;
} Class;
API U RegisterClassA(const Class *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DestroyWindow(U);
API U DefWindowProcA(U, U, U, U);
API U CallWindowProcA(U, U, U, U, U);
API U SetWindowLongA(U, int, U);
API U GetClassInfoA(U, const char *, Class *);
API U GetDlgItem(U, U);
API U IsWindow(U);
API U SendMessageA(U, U, U, U);
API U SetFocus(U);
API U CreateDialogParamA(U, U, U, U(__stdcall *)(U, U, U, U), U);
API U CreateSolidBrush(U);
API U DeleteObject(U);
API U FillRect(U, const Rect *, U);
API U TextOutA(U, int, int, const char *, int);
API U SetTextColor(U, U);
API U SetBkMode(U, int);
API void Sleep(U);
API void ExitProcess(U);
static U measured, drawn, deleted, compared, changed, subclassed, original, blue, grey, doomed, instance;
static U inherited, inherited_proc;
static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}
static U send(U h, U m, U w, U l) { return SendMessageA(h, m, w, l); }
static int equal(const char *a, const char *b)
{
    while (*a && *a == *b)
    {
        ++a;
        ++b;
    }
    return *a == *b;
}
static U __stdcall subclass(U h, U m, U w, U l)
{
    ++subclassed;
    return CallWindowProcA(original, h, m, w, l);
}
/* A Delphi-style class inherits the LISTBOX procedure under another name and forwards through it. Control behavior must follow the procedure, not just the class name. */
static U __stdcall inherit(U h, U m, U w, U l)
{
    ++inherited;
    return CallWindowProcA(inherited_proc, h, m, w, l);
}
static U __stdcall procedure(U h, U m, U w, U l)
{
    if(m==0x2e&&(w&65535)==0x71)return 1;
    if (m == 0x2c)
    {
        U *v = (U *)l;
        check(v[0] == 2 && v[1] == w, 1800);
        ++measured;
        if (w == 105)
        {
            DestroyWindow(GetDlgItem(h, 105));
            return 1;
        }
        v[3] = 100;
        v[4] = w == 103 ? 11 + 7 * v[2] : 20;
        return 1;
    }
    if (m == 0x2b)
    {
        U *v = (U *)l;
        check(v[0] == 2 && v[1] == w && IsWindow(v[5]), 1801);
        ++drawn;
        if (w == 106)
        {
            doomed = v[5];
            DestroyWindow(v[5]);
            return 1;
        }
        Rect *r = (Rect *)(v + 7), expected;
        check(send(v[5], 0x198, v[2], (U)&expected) != ~0u, 1802);
        check(r->left == expected.left && r->top == expected.top && r->right == expected.right &&
                  r->bottom == expected.bottom,
              1803);
        check(FillRect(v[6], r, v[4] & 1 ? blue : grey), 1804);
        SetTextColor(v[6], v[4] & 1 ? 0xffffff : 0);
        SetBkMode(v[6], 1);
        char text[80];
        U n = w == 103 ? 0 : send(v[5], 0x189, v[2], (U)text);
        if (w == 103)
        {
            text[0] = 'A' + v[2];
            text[1] = 0;
            n = 1;
        }
        check(n < 80 && TextOutA(v[6], r->left + 3, r->top + 2, text, n), 1805);
        return 1;
    }
    if (m == 0x2d)
    {
        U *v = (U *)l;
        check(v[0] == 2 && v[1] == w, 1806);
        ++deleted;
        return 1;
    }
    if (m == 0x39)
    {
        U *v = (U *)l;
        check(v[0] == 2 && v[1] == w, 1807);
        ++compared;
        if (compared == 1)
            Sleep(0);
        return (int)v[4] - (int)v[6];
    }
    if (m == 0x111)
    {
        if ((w >> 16) == 1)
            ++changed;
        return 0;
    }
    return DefWindowProcA(h, m, w, l);
}
static U create(U parent, U id, U flags, int x, int width)
{
    return CreateWindowExA(0, "LISTBOX", "", flags, x, 8, width, 190, parent, id, instance, 0);
}
static U __stdcall dialog(U h, U m, U w, U l)
{
    if (m == 0x110)
    {
        U list = GetDlgItem(h, 201);
        check(list && send(list, 0x180, 0, (U) "Dialog list") == 0, 1855);
        return 1;
    }
    return 0;
}
void run_listbox_test(U inst)
{
    instance = inst;
    Class cls = {0, procedure, 0, 0, inst, 0, 0, 0, 0, "NEO98ListBoxContract"};
    check(RegisterClassA(&cls), 1810);
    blue = CreateSolidBrush(0x800000);
    grey = CreateSolidBrush(0xc0c0c0);
    check(blue && grey, 1811);
    U parent = CreateWindowExA(0, cls.name, "Original USER listbox geometry", 0x10000000, 50, 40, 660, 270, 0,
                               0, inst, 0);
    check(parent, 1812);
    U plain = create(parent, 101, 0x50800403, 8, 180);
    check(plain, 1813);
    check(send(plain, 0x180, 0, (U) "Gamma") == 0 && send(plain, 0x180, 0, (U) "alpha") == 0 &&
              send(plain, 0x180, 0, (U) "Beta") == 1,
          1814);
    check(send(plain, 0x18b, 0, 0) == 3, 1815);
    char text[64];
    check(send(plain, 0x189, 1, (U)text) == 4 && equal(text, "Beta"), 1816);
    check(send(plain, 0x18f, ~0u, (U) "ga") == 2 && send(plain, 0x1a2, ~0u, (U) "ga") == ~0u, 1817);
    check(send(plain, 0x19a, ~0u, 98) == 1 && send(plain, 0x199, 2, 0) == 98, 1818);
    check(send(plain, 0x186, 1, 0) == 1 && send(plain, 0x188, 0, 0) == 1, 1819);
    check(send(plain, 0x181, 0, (U) "unsorted") == 0 && send(plain, 0x188, 0, 0) == 2, 1820);
    check(send(plain, 0x182, 0, 0) == 3 && send(plain, 0x188, 0, 0) == 1, 1821);
    check(send(plain, 0x190, 0, 0) == ~0u, 1822);
    original = SetWindowLongA(plain, -4, (U)subclass);
    check(original && send(plain, 0x18b, 0, 0) == 3 && subclassed, 1823);
    SetFocus(plain);
    send(plain, 0x100, 0x28, 0);
    check(send(plain, 0x188, 0, 0) == 2 && changed, 1824);
    send(plain, 0x102, 'a', 0);
    check(send(plain, 0x188, 0, 0) == 0, 1859);
    send(plain,0x100,0x71,0);check(send(plain,0x188,0,0)==1,1860);
    SetWindowLongA(plain, -4, original);
    {
        // USER: a line is the average character width of
        // the font, a page two thirds of the 178 client pixels, and
        // cuts the origin to the extent 400 less the client width. The origin
        // widens the item rectangle.
        Rect at;
        check(send(plain, 0x198, 0, (U)&at) != ~0u, 18870);
        const int right = at.right;
        check(send(plain, 0x194, 400, 0) == 0 && send(plain, 0x193, 0, 0) == 400, 18871);
        send(plain, 0x114, 1, 0);
        send(plain, 0x198, 0, (U)&at);
        const int line = at.right - right;
        check(line > 0 && line < 16, 18872);
        send(plain, 0x114, 3, 0);
        send(plain, 0x198, 0, (U)&at);
        check(at.right - right == line + 118, 18873);
        send(plain, 0x114, 7, 0);
        send(plain, 0x198, 0, (U)&at);
        check(at.right - right == 222, 18874);
        send(plain, 0x114, 50 << 16 | 5, 0);
        send(plain, 0x114, 8, 0);
        send(plain, 0x198, 0, (U)&at);
        check(at.right - right == 50, 18875);
        send(plain, 0x114, 2, 0);
        send(plain, 0x198, 0, (U)&at);
        check(at.right - right == 0, 18876);
        send(plain, 0x114, 300 << 16 | 4, 0);
        send(plain, 0x198, 0, (U)&at);
        check(at.right - right == 222, 18877);
        send(plain, 0x114, 6, 0);
        send(plain, 0x194, 0, 0);
        send(plain, 0x198, 0, (U)&at);
        check(at.right == right, 18878);
    }
    U multi = create(parent, 102, 0x50800b51, 200, 210);
    check(multi && measured == 1, 1825);
    for (U i = 0; i < 24; ++i)
    {
        char name[] = "Entry 00";
        name[6] = '0' + i / 10;
        name[7] = '0' + i % 10;
        check(send(multi, 0x180, 0, (U)name) == i, 1826);
    }
    check(send(multi, 0x19b, 1, (2 << 16)) == 0 && send(multi, 0x185, 0, 1) == 0, 1827);
    U selected[4] = {};
    check(send(multi, 0x190, 0, 0) == 2 && send(multi, 0x191, 4, (U)selected) == 2 && selected[0] == 0 &&
              selected[1] == 2,
          1828);
    check(send(multi, 0x186, 0, 0) == ~0u, 1829);
    Rect rect;
    check(send(multi, 0x198, 9, (U)&rect) != ~0u && rect.left == 100 && rect.top == 0 && rect.bottom == 20,
          1830);
    check(send(multi, 0x197, 9, 0) == 0 && send(multi, 0x18e, 0, 0) == 9, 1831);
    check(send(multi, 0x198, 0, (U)&rect) == 0 && rect.left == -100, 1832);
    check(send(multi, 0x197, 0, 0) == 0, 1833);
    // USER pages by the two whole columns shown and cuts the
    // top to the last page: three columns of nine rows end at item 9.
    send(multi,0x114,3,0);check(send(multi,0x18e,0,0)==9,1861);
    send(multi,0x114,6,0);check(send(multi,0x18e,0,0)==0,1862);
    U variable = create(parent, 103, 0x50000020, 430, 180);
    check(variable, 1834);
    for (U i = 0; i < 5; ++i)
        check(send(variable, 0x180, 0, 98 + i) == i, 1835);
    check(measured == 6 && send(variable, 0x1a1, 1, 0) == 18, 1836);
    check(send(variable, 0x198, 1, (U)&rect) == 1 && rect.top == 11 && rect.bottom == 29, 1837);
    U value = 0;
    check(send(variable, 0x189, 2, (U)&value) == 4 && value == 100, 1838);
    check(send(variable, 0x1a0, 1, 25) == 0 && send(variable, 0x1a1, 1, 0) == 25, 1839);
    check(send(variable, 0x1a0, 1, 256) == ~0u, 1840);
    // All five items (132 pixels) fit the 190 pixels, so USER counts
    // five whole items and Windows 98 keeps the top at 0.
    send(variable,0x115,1,0);check(send(variable,0x18e,0,0)==0,1863);
    send(variable,0x115,6,0);check(send(variable,0x18e,0,0)==0,1864);
    NEO98_WAIT_FOR_CAPTURE(CaptureMessage); // The host takes its picture of the three list boxes here.
    U sorted = create(parent, 104, 0x40000012, 0, 100);
    check(sorted, 1841);
    check(send(sorted, 0x180, 0, 9) == 0 && send(sorted, 0x180, 0, 2) == 0 && send(sorted, 0x180, 0, 7) == 1,
          1842);
    check(compared && send(sorted, 0x199, 0, 0) == 2 && send(sorted, 0x199, 2, 0) == 9, 1843);
    U before = deleted;
    check(DestroyWindow(sorted) && deleted == before + 3, 1844);
    check(!create(parent, 105, 0x40000010, 0, 100) && !GetDlgItem(parent, 105), 1845);
    U destroy_draw = create(parent, 106, 0x50000010, 0, 100);
    check(destroy_draw, 1846);
    check(send(destroy_draw, 0x180, 0, 7) == 0, 1847);
    pump();
    check(drawn && doomed == destroy_draw && !IsWindow(doomed), 1848);
    check(send(multi, 0x197, 9, 0) == 0, 1849);
    pump();
    check(send(multi, 0x197, 0, 0) == 0, 1850);
    before = deleted;
    check(send(variable, 0x184, 0, 0) == 0 && deleted == before + 5 && send(variable, 0x18b, 0, 0) == 0,
          1851);
    check(DestroyWindow(parent) && !IsWindow(plain) && !IsWindow(multi) && !IsWindow(variable), 1852);
    U dlg = CreateDialogParamA(inst, 24, 0, dialog, 0);
    check(dlg, 1856);
    U dlg_list = GetDlgItem(dlg, 201);
    check(send(dlg_list, 0x189, 0, (U)text) == 11 && equal(text, "Dialog list"), 1857);
    check(DestroyWindow(dlg) && !IsWindow(dlg_list), 1858);
    for (U i = 0; i < 20; ++i)
    {
        U h = create(0, 107, 0x10000000, 0, 180);
        check(h && DestroyWindow(h), 1853);
    }
    /* Copy LISTBOX class metadata, rename/register it, then subclass the window. Both inherited class procedures and CallWindowProc must retain list-box behavior. */
    {
        Class inherited_class;
        U super, before;
        check(GetClassInfoA(0, "LISTBOX", &inherited_class), 1860);
        inherited_proc = (U)inherited_class.proc;
        check(inherited_proc, 1861);
        inherited_class.name = "Neo98InheritedListBox";
        inherited_class.instance = instance;
        check(RegisterClassA(&inherited_class), 1862);
        super = CreateWindowExA(0, "Neo98InheritedListBox", "", 0, 8, 8, 120, 90, 0, 0,
                                instance, 0);
        check(super, 1863);
        /* Call the inherited class procedure. */
        check(send(super, 0x180, 0, (U)"eins") == 0 && send(super, 0x180, 0, (U)"zwei") == 1, 1864);
        check(send(super, 0x18b, 0, 0) == 2, 1865);
        /* Call through CallWindowProc after subclassing the window. */
        inherited = 0;
        before = SetWindowLongA(super, -4, (U)inherit);
        check(before, 1866);
        check(send(super, 0x180, 0, (U)"drei") == 2 && inherited == 1, 1867);
        check(send(super, 0x18b, 0, 0) == 3 && inherited == 2, 1868);
        {
            char text[16];
            text[0] = 0;
            check(send(super, 0x189, 1, (U)text) == 4 && equal(text, "zwei"), 1869);
        }
        check(DestroyWindow(super), 1870);
    }
    check(DeleteObject(blue) && DeleteObject(grey), 1854);
}
