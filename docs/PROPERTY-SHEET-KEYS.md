# The keys of a property sheet

A property sheet takes some keys before the dialog manager does. COMCTL32's
IsDialogMessage of a sheet, which the loop of a modal `PropertySheet` calls
and `PSM_ISDIALOGMESSAGE` answers for a program with a modeless sheet,
looks at a key going down with CTRL held:

| Key | An ordinary sheet | A wizard |
| --- | --- | --- |
| CTRL+TAB, CTRL+PAGE DOWN | the next page, after the last the first | the step of Next |
| CTRL+SHIFT+TAB, CTRL+PAGE UP | the page before, before the first the last | the step of Back |

Everything else goes to `IsDialogMessage`, where TAB alone moves the focus to
the next control of the page. Neo98's sheet does this in the branch of
`DialogMessage` (`src/x86_dialog.cpp`) for a sheet's window.

The two versions of COMCTL32 differ in how they ask whether CTRL is held.
Windows 98's asks `GetKeyState`, the state that goes with the message being
handled, so CTRL+TAB turns the page however fast the keys came. Windows 95's
asks `GetAsyncKeyState`, the state of the moment: CTRL+TAB turns the page
only while CTRL is still down when the sheet reads the TAB, as when a person
holds CTRL; keys sent as one burst (CTRL down, TAB down and up, CTRL up)
before the program reads them move the focus as TAB alone does. That holds
for a program of the time that runs Windows 95's COMCTL32 as well.

`tools/test_sheet_keys_host.py` presses the four keys and TAB alone in a
modal and in a modeless sheet of three pages (`tools/sheet_keys_fixture.c`).
