/* SPDX-License-Identifier: GPL-2.0-or-later */
/* a window that destroys itself on WM_NCACTIVATE(FALSE).
   DESK.CPL's full-screen cover DeskSaysNoPeekingItsASurprise does this. When
   the program destroys the active cover, USER hands the activation on
    and the cover gets WM_NCACTIVATE(FALSE); its procedure calls
   DestroyWindow again. USER marks the task (byte 15h, bit 0) around
   that message, and DestroyWindow does not hand the activation on while the
   mark stands, so the nested call only takes the window down. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U DestroyWindow(U);
API U IsWindow(U);
API U SetActiveWindow(U);
API U GetActiveWindow(void);

static void check(int ok, U code) { if (!ok) ExitProcess(code); }
static U destroys, deactivations;
static U __stdcall cover(U h, U m, U w, U l)
{
    if (m == 0x86 && !w)
    {
        ++deactivations;
        U r = DefWindowProcA(h, m, w, l);
        ++destroys;
        DestroyWindow(h);
        return r;
    }
    return DefWindowProcA(h, m, w, l);
}
static U __stdcall plain(U h, U m, U w, U l) { return DefWindowProcA(h, m, w, l); }

void mainCRTStartup(void)
{
    U a[10] = {0, (U)cover, 0, 0, 0, 0, 0, 0, 0, (U)"Cover98"};
    U b[10] = {0, (U)plain, 0, 0, 0, 0, 0, 0, 0, (U)"Sheet98"};
    check(RegisterClassA(a) && RegisterClassA(b), 10);
    U sheet = CreateWindowExA(0, "Sheet98", "sheet", 0x90c00000u, 50, 50, 300, 200, 0, 0, 0, 0);
    U shade = CreateWindowExA(0x88, "Cover98", "cover", 0x90000000u, 0, 0, 800, 600, 0, 0, 0, 0);
    check(sheet && shade, 11);
    SetActiveWindow(shade);
    check(GetActiveWindow() == shade, 12);
    DestroyWindow(shade);
    check(!IsWindow(shade), 13);
    check(deactivations >= 1 && deactivations <= 2, 14);
    check(IsWindow(sheet), 15);
    ExitProcess(0);
}
