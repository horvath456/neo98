# The system menu

Every window with `WS_SYSMENU` has a system menu: Restore, Move, Size,
Minimize, Maximize and Close. USER sets the states of its items from the
window each time it is about to be shown (`PrepareSystemMenu` in
`src/user_menu_tracking.inc`). This page says where it is opened from.

## From the icon of the caption

A press on the icon at the left of the caption reaches `DefWindowProc` as
`WM_NCLBUTTONDOWN` with `HTSYSMENU`. Like a press on the menu bar, it becomes
the system command `SC_MOUSEMENU`, here with `HTSYSMENU` in its low bits
(F093h), so a program may take it. `DefWindowProc` answers that command by
starting the menu mode of the window with the system menu selected and open:
the menu drops below the icon, the icon shows selected, and the keys of the
menu mode work from there (left and right to the menu bar). That is the menu
mode Alt+Space starts, entered by the mouse (`User::MouseSystemMenu`).

A second press on the icon while the menu is open closes the menu. When it
comes within the double-click time and the double-click box of the press
that opened it, the menu loop closes the menu and posts `WM_SYSCOMMAND` with
`SC_CLOSE`: the window closes, as a double click on the icon does. Before,
the press opened nothing, and the double click closed the window only
because the first press had left no menu loop in the way.

## From the taskbar

A right click on a window's button in the taskbar makes Explorer send the
window message 0313h with `SendMessageCallback`, `lParam` the point of the
click on the screen. `DefWindowProc` answers it: nothing for a disabled
window; otherwise it prepares the system menu, makes Maximize its default
item (Restore for a minimized or maximized window, where the caption's own
menu has Close) and tracks it at the point with the right button as well
(`TrackPopupMenuEx` with the flags of the system menu and
`TPM_RIGHTBUTTON`). The message was not answered at all.

With `lParam` -1, from the keyboard, USER asks the shell hook for the
rectangle of the window's button (`HSHELL_GETMINRECT`) and opens the menu
along it; when no shell hook answers, it opens nothing. Neo98's shell hook
only notifies (`NotifyShell` in `src/user.cpp`) and answers no rectangle, so
the keyboard form opens nothing.

## A maximized MDI child

A maximized MDI child gives its frame's menu bar four items: at the start its
system menu, shown as its small icon (`HBMMENU_SYSTEM`, the child in the
item's data), and at the end Minimize, Restore and Close (`HBMMENU_MBAR_*`,
the commands `SC_MINIMIZE`, `SC_RESTORE` and `SC_CLOSE`). USER takes
`WS_SYSMENU` off the child meanwhile, so the child has no system menu of its
own until it is restored (`GetSystemMenu` of the child answers none). The bar
laid the four items out but drew none of them: it draws the icon now, centred
in its item, and the three as the caption buttons `DrawFrameControl` draws,
pushed while selected and inactive while grayed (`PaintBarBitmap` in
`src/user_caption_draw.inc`).

A press on the icon drops the child's system menu, as before; its items send
the frame `WM_COMMAND`, which `DefFrameProc` hands to the child as
`WM_SYSCOMMAND`. A double click on the icon is the Close of that menu.

## Tests

`tools/test_system_menu_icon_host.py`: a window
(`tools/system_menu_icon_fixture.c`) gets presses and a double click on its
icon and 0313h at a point, and an MDI frame (`tools/mdi_system_icon_fixture.c`)
with two maximized children shows the icon and buttons in its bar, opens the
child's system menu from the icon, closes one child from that menu and the
other by a double click. `tools/test_system_menu_host.py` covers the states
of the items and the caption's context menu.
