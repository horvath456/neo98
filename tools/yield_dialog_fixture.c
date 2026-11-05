/* SPDX-License-Identifier: GPL-2.0-or-later */
/* End a modeless dialog during control creation. The creation loop must never write through a released dialog slot or create controls on a destroyed window. Failure codes 41000..41099. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U CreateDialogIndirectParamA(U, const void *, U, U, U);
API U RegisterClassA(U *);
API U DefWindowProcA(U, U, U, U);
API U GetModuleHandleA(const char *);
API U DestroyWindow(U);
API U EndDialog(U, U);
API U IsWindow(U);
API U GetParent(U);
API U GetDlgItem(U, U);
API U GetLastError(void);
API void ExitProcess(U);

static void check(int okay, U code)
{
    if (!okay)
        ExitProcess(code);
}
static U mode, control_hits, font_hits, init_hits, control_parent;
static U __stdcall control_proc(U h, U m, U w, U l)
{
    if (m == 1) /* WM_CREATE */
    {
        ++control_hits;
        if (mode == 1 && control_hits == 1)
        {
            control_parent = GetParent(h);
            check(EndDialog(control_parent, 0x1234) != 0, 41010);
        }
    }
    /* Without DS_SETFONT Win98 SE sends no WM_SETFONT to the controls. */
    if (m == 0x30)
        ++font_hits;
    return DefWindowProcA(h, m, w, l);
}
static U __stdcall dialog_proc(U h, U m, U w, U l)
{
    (void)h;
    (void)w;
    (void)l;
    if (m == 0x110)
    {
        ++init_hits;
        return 1;
    }
    return 0;
}
static void word(unsigned char *b, U *at, U v)
{
    b[(*at)++] = (unsigned char)v;
    b[(*at)++] = (unsigned char)(v >> 8);
}
static void dword(unsigned char *b, U *at, U v)
{
    word(b, at, v & 65535);
    word(b, at, v >> 16);
}
static void string(unsigned char *b, U *at, const char *s)
{
    do
    {
        word(b, at, (unsigned char)*s);
    } while (*s++);
}
/* Template header and two controls of the custom class. */
static U make(unsigned char *b)
{
    U at = 0;
    dword(b, &at, 0x90c80080);
    dword(b, &at, 0);
    word(b, &at, 2);
    word(b, &at, 0);
    word(b, &at, 0);
    word(b, &at, 100);
    word(b, &at, 60);
    word(b, &at, 0);
    word(b, &at, 0);
    string(b, &at, "YieldDialog");
    for (U item = 0; item < 2; ++item)
    {
        while (at & 3)
            b[at++] = 0;
        dword(b, &at, 0x50000000);
        dword(b, &at, 0);
        word(b, &at, 5);
        word(b, &at, (U)(5 + item * 16));
        word(b, &at, 40);
        word(b, &at, 14);
        word(b, &at, (U)(11 + item));
        string(b, &at, "Yield98Ctl");
        string(b, &at, item ? "two" : "one");
        word(b, &at, 0);
    }
    return at;
}
void mainCRTStartup(void)
{
    U instance = GetModuleHandleA(0);
    U cls[10] = {0, (U)control_proc, 0, 0, instance, 0, 0, 0, 0, (U) "Yield98Ctl"};
    check(RegisterClassA(cls) != 0, 41001);
    unsigned char data[512];
    check(make(data) <= sizeof data, 41002);

    /* Normal path: create both controls, then send WM_INITDIALOG. */
    mode = 0;
    control_hits = font_hits = init_hits = 0;
    U plain = CreateDialogIndirectParamA(instance, data, 0, (U)dialog_proc, 0);
    check(plain != 0 && IsWindow(plain), 41003);
    check(control_hits == 2 && font_hits == 0 && init_hits == 1, 41004);
    check(GetDlgItem(plain, 11) != 0 && GetDlgItem(plain, 12) != 0, 41005);
    check(DestroyWindow(plain) != 0, 41006);

    /* The first control calls EndDialog from WM_CREATE. Windows 98 SE leaves this modeless dialog alive: both controls are created, WM_INITDIALOG runs and CreateDialog returns the hidden window. Creation must not write a freed dialog slot. */
    mode = 1;
    control_hits = font_hits = init_hits = 0;
    U ended = CreateDialogIndirectParamA(instance, data, 0, (U)dialog_proc, 0);
    check(control_hits == 2 && font_hits == 0, 41007);
    check(init_hits == 1, 41008);
    check(ended == control_parent && IsWindow(ended) && DestroyWindow(ended), 41009);

    /* The same slot can subsequently hold a complete new dialog. */
    mode = 0;
    control_hits = font_hits = init_hits = 0;
    U again = CreateDialogIndirectParamA(instance, data, 0, (U)dialog_proc, 0);
    check(again != 0 && IsWindow(again), 41011);
    check(control_hits == 2 && font_hits == 0 && init_hits == 1, 41012);
    check(GetDlgItem(again, 11) != 0 && GetDlgItem(again, 12) != 0, 41013);
    check(DestroyWindow(again) != 0, 41014);
    ExitProcess(0);
}
