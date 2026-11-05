/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Guest side of tools/test_menu_capacity_guest.py: a menu grows with its items
   and a process may load more menu resources than a fixed table would hold. Win98 keeps a menu in the USER heap and lets AppendMenu grow it
   until that 64-KB heap is full; 48 items and 32 shared menu resources were
   numbers of the port. Every check has its own exit code. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U CreateMenu(void);
API U CreatePopupMenu(void);
API U AppendMenuA(U, U, U, const char *);
API U InsertMenuA(U, U, U, U, const char *);
API U RemoveMenu(U, U, U);
API U DestroyMenu(U);
API U GetMenuItemCount(U);
API U GetMenuItemID(U, int);
API U GetSubMenu(U, int);
API U LoadMenuA(U, const char *);
API U GetMenu(U);
API U IsMenu(U);
API U CreateDialogParamA(U, const char *, U, void *, U);
API U DestroyWindow(U);
API U GetModuleHandleA(const char *);
API U DefWindowProcA(U, U, U, U);
API void ExitProcess(U);

static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}
static U __stdcall Proc(U window, U message, U wparam, U lparam)
{
    (void)window;
    (void)message;
    (void)wparam;
    (void)lparam;
    return 0;
}
int mainCRTStartup(void)
{
    const U instance = GetModuleHandleA(0);
    check(instance != 0, 1);
    /* A menu grows past the old ceiling of 48 items. */
    const U popup = CreatePopupMenu();
    check(popup != 0, 2);
    for (U i = 0; i < MENU_ITEMS; ++i)
        check(AppendMenuA(popup, 0, 6000 + i, "item") != 0, 3);
    check(GetMenuItemCount(popup) == MENU_ITEMS, 4);
    check(GetMenuItemID(popup, 47) == 6047, 5);
    check(GetMenuItemID(popup, MENU_ITEMS - 1) == 6000 + MENU_ITEMS - 1, 6);
    /* Inserting behind the old ceiling keeps the order. */
    check(InsertMenuA(popup, 100, 0x400, 7000, "inserted") != 0, 7);
    check(GetMenuItemCount(popup) == MENU_ITEMS + 1, 8);
    check(GetMenuItemID(popup, 100) == 7000, 9);
    check(GetMenuItemID(popup, 101) == 6100, 10);
    /* Removing shrinks it again, item by item, down to empty. */
    check(RemoveMenu(popup, 100, 0x400) != 0, 11);
    check(GetMenuItemCount(popup) == MENU_ITEMS, 12);
    for (U i = 0; i < MENU_ITEMS; ++i)
        check(RemoveMenu(popup, 0, 0x400) != 0, 13);
    check(GetMenuItemCount(popup) == 0, 14);
    check(DestroyMenu(popup) != 0, 15);
    /* A menu template with more than 48 items is read completely. */
    const U bar = LoadMenuA(instance, (const char *)TEMPLATE_MENU);
    check(bar != 0, 16);
    check(GetMenuItemCount(bar) == 1, 17);
    const U sub = GetSubMenu(bar, 0);
    check(sub != 0, 18);
    check(GetMenuItemCount(sub) == TEMPLATE_ITEMS, 19);
    check(GetMenuItemID(sub, TEMPLATE_ITEMS - 1) == 2000 + TEMPLATE_ITEMS - 1, 20);
    check(DestroyMenu(bar) != 0, 21);
    /* More shared menu resources than the old table of 32 held: every dialog
       of the fixture names a menu resource of its own, and the dialog path is
       the one that keeps the shared list. */
    /* The dialogs are opened one at a time: the shared list keeps its entry
       when the dialog goes, and the table of live dialogs is a separate
       bound. */
    U first = 0, second = 0;
    for (U i = 0; i < DIALOG_COUNT; ++i)
    {
        const U window = CreateDialogParamA(instance, (const char *)(FIRST_DIALOG + i), 0, (void *)Proc, 0);
        check(window != 0, 22);
        const U menu = GetMenu(window);
        check(menu != 0, 23);
        if (i == 0)
            first = menu;
        if (i == 1)
            second = menu;
        check(DestroyWindow(window) != 0, 24);
    }
    /* DestroyWindow destroyed each dialog's menu with it (measured on Win98
       SE, menu_destroy contract); the same resource loads a new menu. */
    const U again = CreateDialogParamA(instance, (const char *)FIRST_DIALOG, 0, (void *)Proc, 0);
    check(again != 0, 25);
    check(!IsMenu(first) && IsMenu(GetMenu(again)), 26);
    check(second != first, 27);
    check(DestroyWindow(again) != 0, 28);
    /* Every dialog of the fixture open at the same time. Win98 keeps a dialog
       in the extra window bytes of class #32770 and has no table of live
       dialogs; NEO98 held sixteen and stopped with "Dialog table full". Opening them all also proves that the pointer a dialog record
       hands out stays valid while the set grows, because WM_INITDIALOG of a
       later dialog runs while the earlier records are alive. */
    U open[DIALOG_COUNT];
    for (U i = 0; i < DIALOG_COUNT; ++i)
    {
        open[i] = CreateDialogParamA(instance, (const char *)(FIRST_DIALOG + i), 0, (void *)Proc, 0);
        check(open[i] != 0, 29);
    }
    for (U i = 0; i < DIALOG_COUNT; ++i)
    {
        /* Each one still answers its own menu, so no record was overwritten. */
        check(IsMenu(GetMenu(open[i])), 30);
        for (U j = 0; j < i; ++j)
            check(GetMenu(open[i]) != GetMenu(open[j]), 31);
    }
    for (U i = 0; i < DIALOG_COUNT; ++i)
        check(DestroyWindow(open[i]) != 0, 32);
    ExitProcess(0);
    return 0;
}
