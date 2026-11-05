/* SPDX-License-Identifier: GPL-2.0-or-later
 * GetMenuItemRect/MenuItemFromPoint for an open popup menu, given its owner. */
#include <windows.h>
#include "wine/test.h"

static HMENU popup;
static int done;

static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_ENTERIDLE && w == MSGF_MENU && !done)
    {
        RECT item, window;
        HWND menu = FindWindowA("#32768", NULL);
        POINT center;
        int hit;
        done = 1;
        ok(menu != NULL, "popup window\n");
        GetWindowRect(menu, &window);
        ok(GetMenuItemRect(h, popup, 1, &item), "GetMenuItemRect\n");
        center.x = (item.left + item.right) / 2; center.y = (item.top + item.bottom) / 2;
        hit = MenuItemFromPoint(h, popup, center);
        trace("popup window %ld,%ld-%ld,%ld item1 %ld,%ld-%ld,%ld hit=%d\n", window.left, window.top,
              window.right, window.bottom, item.left, item.top, item.right, item.bottom, hit);
        /* Win98 SE measures the item against the client origin of the window
           passed (USER switches only for a #32768 window), so the
           owner's answer lies outside the popup and does not hit test. */
        ok(!(item.left >= window.left && item.top >= window.top), "owner-relative item rectangle\n");
        ok(hit == -1, "MenuItemFromPoint at the owner-relative centre misses (got %d)\n", hit);
        ok(GetMenuItemRect(menu, popup, 1, &item), "GetMenuItemRect with the popup window\n");
        trace("with the popup window: item1 %ld,%ld-%ld,%ld\n", item.left, item.top, item.right, item.bottom);
        ok(item.left >= window.left && item.right <= window.right && item.top >= window.top &&
           item.bottom <= window.bottom, "popup-window item rectangle lies inside the popup\n");
        center.x = (item.left + item.right) / 2; center.y = (item.top + item.bottom) / 2;
        ok(MenuItemFromPoint(h, popup, center) == 1, "MenuItemFromPoint there answers 1\n");
        EndMenu();
        return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

START_TEST(menu_item_rect)
{
    WNDCLASSA cls = {0};
    HWND owner;
    cls.lpfnWndProc = proc; cls.hInstance = GetModuleHandleA(NULL); cls.lpszClassName = "MenuItemRect98";
    ok(RegisterClassA(&cls) != 0, "class\n");
    owner = CreateWindowExA(0, "MenuItemRect98", "owner", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 50, 50, 300, 200, 0, 0, cls.hInstance, 0);
    SetForegroundWindow(owner);
    popup = CreatePopupMenu();
    AppendMenuA(popup, MF_STRING, 10, "first");
    AppendMenuA(popup, MF_STRING, 11, "second");
    AppendMenuA(popup, MF_STRING, 12, "third");
    TrackPopupMenu(popup, TPM_LEFTALIGN | TPM_TOPALIGN, 120, 120, 0, owner, NULL);
    ok(done, "menu loop ran\n");
    DestroyMenu(popup);
    DestroyWindow(owner);
}
