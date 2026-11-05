/* SPDX-License-Identifier: GPL-2.0-or-later */
/* the top item of a list box never lies beyond count minus page.
   USER sets the new top and runs, which hands range (items
   minus one), page (whole items shown) and position to SetScrollInfo; the
   answer is the position the bar allows, and SetScrollInfo hides the bar
   when every item fits. A list without a bar cuts the position to the same
   limit itself. A drop-down combo box makes its selection the top
   of the list and sizes the open list to its items plus
   SM_CYEDGE, cut to whole items. DESK.CPL's colour list
   opened with "True Color (32 bit)" on the top line, three empty rows and a
   scroll bar. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U SendMessageA(U, U, U, U);
API U GetWindowLongA(U, int);
API U GetClientRect(U, int *);
API U GetWindowRect(U, int *);
API U GetComboBoxInfo(U, void *);
API int GetSystemMetrics(int);
API U DestroyWindow(U);

static void check(int ok, U code) { if (!ok) ExitProcess(code); }
static U __stdcall plain(U h, U m, U w, U l) { return DefWindowProcA(h, m, w, l); }
static const char *names[4] = {"256 Colors", "High Color (16 bit)", "True Color (24 bit)", "True Color (32 bit)"};

void mainCRTStartup(void)
{
    U c[10] = {0, (U)plain, 0, 0, 0, 0, 0, 0, 0, (U)"Top98"};
    check(RegisterClassA(c) != 0, 10);
    U parent = CreateWindowExA(0, "Top98", "top", 0x90c00000u, 20, 20, 400, 400, 0, 0, 0, 0);
    check(parent != 0, 11);
    /* A list with room for more than four items. */
    U list = CreateWindowExA(0, "LISTBOX", "", 0x50a00101u, 10, 10, 150, 120, parent, 100, 0, 0);
    check(list != 0, 12);
    for (U i = 0; i < 4; ++i)
        check(SendMessageA(list, 0x180, 0, (U)names[i]) == i, 13);
    SendMessageA(list, 0x186, 3, 0);
    check(SendMessageA(list, 0x18e, 0, 0) == 0, 14);            /* LB_GETTOPINDEX */
    check(SendMessageA(list, 0x197, 3, 0) == 0, 15);            /* LB_SETTOPINDEX */
    check(SendMessageA(list, 0x18e, 0, 0) == 0, 16);
    check(!(GetWindowLongA(list, -16) & 0x200000u), 17);        /* everything fits: no bar */
    /* Twenty items: the bar comes back and the last page is the limit. */
    for (U i = 4; i < 20; ++i)
        SendMessageA(list, 0x180, 0, (U)names[i & 3]);
    check((GetWindowLongA(list, -16) & 0x200000u) != 0, 18);
    int r[4];
    GetClientRect(list, r);
    const U item = SendMessageA(list, 0x1a1, 0, 0), page = (U)r[3] / item;
    check(item && page && page < 20, 19);
    SendMessageA(list, 0x197, 19, 0);
    check(SendMessageA(list, 0x18e, 0, 0) == 20 - page, 20);
    SendMessageA(list, 0x197, 2, 0);
    check(SendMessageA(list, 0x18e, 0, 0) == 2, 21);
    /* A list without a bar cuts the same way. */
    U bare = CreateWindowExA(0, "LISTBOX", "", 0x50800101u, 170, 10, 150, 120, parent, 101, 0, 0);
    for (U i = 0; i < 4; ++i)
        SendMessageA(bare, 0x180, 0, (U)names[i]);
    SendMessageA(bare, 0x197, 3, 0);
    check(SendMessageA(bare, 0x18e, 0, 0) == 0, 22);
    /* The drop-down list of DESK.CPL: CBS_DROPDOWNLIST with WS_VSCROLL. */
    U combo = CreateWindowExA(0, "COMBOBOX", "", 0x50210003u, 10, 150, 150, 200, parent, 102, 0, 0);
    check(combo != 0, 23);
    for (U i = 0; i < 4; ++i)
        SendMessageA(combo, 0x143, 0, (U)names[i]);
    check(SendMessageA(combo, 0x14e, 3, 0) == 3, 24);           /* CB_SETCURSEL */
    SendMessageA(combo, 0x14f, 1, 0);                           /* CB_SHOWDROPDOWN */
    U info[16] = {0};
    info[0] = 52;
    check(GetComboBoxInfo(combo, info) && info[12], 25);
    const U drop = info[12];
    check(SendMessageA(drop, 0x18e, 0, 0) == 0, 26);
    check(SendMessageA(drop, 0x188, 0, 0) == 3, 27);
    int w[4];
    GetWindowRect(drop, w);
    const U row = SendMessageA(drop, 0x1a1, 0, 0);
    check(row && (U)(w[3] - w[1]) == 4 * row + (U)GetSystemMetrics(46), 28);
    check(!(GetWindowLongA(drop, -16) & 0x200000u), 29);
    SendMessageA(combo, 0x14f, 0, 0);
    DestroyWindow(parent);
    ExitProcess(0);
}
