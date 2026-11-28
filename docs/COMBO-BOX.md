# The combo box

The combo box of Neo98 is its own implementation of USER's
(`src/x86_combobox.cpp`, `src/win98_combobox.h`): a box with an edit field
or a static field, and a list of the class ComboLBox that drops down. This
page collects what it does the way USER does and how that was found out.

## Enabled and disabled

`EnableWindow` on a combo box sends it `WM_ENABLE`, and the combo box hands
the state on to its edit field and its list. A box made with `WS_DISABLED`
gives the style to both when it creates them. A disabled field takes no
input, and when it paints it asks its parent for the colours of a static
control (`WM_CTLCOLORSTATIC`, from Windows 95 on), so it shows grey; a
field that takes input asks with `WM_CTLCOLOREDIT` and is white. Windows
sends two or three of these messages for one paint, Neo98 one.

Measured with `tools/combo_enable_fixture.c` in Windows 95 OSR2 and
Windows 98 SE. Before, Neo98 left the field enabled: the Name box of the
Sounds applet, which Windows disables while a group of events is selected,
looked like a field to type into.

## Typed text

Text typed into the field selects no item of the list: USER sends
`CBN_EDITCHANGE` and lets the list follow the text (`LB_SETCURSEL` with
-1, the caret and the top of the list at the first item that begins with
the text), and `CB_GETCURSEL` answers `CB_ERR` until an item is chosen
again.

## Tests

- `tools/test_combo_enable_host.py`: enabled, disabled and enabled again.
- `tools/test_combo_typed_host.py`: the selection after typed text.
- `tools/test_combo_savebits_host.py`: the open list saves what it covers.
