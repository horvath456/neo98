# Controls

Small rules of USER's controls that programs see, one section each.

## A combo box without the focus

The edit field of a combo box (`CBS_DROPDOWN`, `CBS_SIMPLE`) is made with
`ES_NOHIDESEL`, so the edit itself keeps drawing its selection when it loses
the focus. The combo box takes the selection away: when its edit reports
`EN_KILLFOCUS`, USER's kill-focus helper of the combo sets the selection to
0,0 (`EM_SETSEL`), closes the list unless the focus stayed in the combo, and
sends `CBN_KILLFOCUS`. When the focus comes back, `EN_SETFOCUS` selects the
whole text again. Neo98 sent only `CBN_KILLFOCUS`, and the text the focus had
selected stayed highlighted in every combo box a dialog's focus had passed
through (`src/x86_combobox.cpp`). `tools/test_combo_selection_host.py`.

## Buttons of several lines

A button with `BS_MULTILINE` breaks its caption into lines. USER measures the
caption with DrawText (`DT_CALCRECT`, `DT_WORDBREAK`, `DT_EDITCONTROL`) in
the button's content rectangle (for a check box or radio button the client
less the check cell and its gap, for a push button the client less its edge)
less `SM_CXEDGE` at both sides and `SM_CYBORDER` above and below. The block
it answers, as wide as its widest line (a line broken at a space counts the
space) and as high as its lines, is placed as a single line would be:
`BS_LEFT`, `BS_RIGHT` and `BS_CENTER` across, `BS_TOP`, `BS_BOTTOM` and the
centre down, with the height plus one. DrawState gets it as a complex picture
whose callback draws the text again with `DT_WORDBREAK` and `DT_EDITCONTROL`
in a rectangle of the block's size, the accelerator underlined. The focus
rectangle frames the block as it frames a single line, and the check cell
keeps its own place: centred in the button, or in the first or last line of
5/4 of the text height with `BS_TOP` and `BS_BOTTOM`. A caption with more
lines than the button has room for overhangs it at both ends and is cut.

The two versions differ in one byte of the callback: Windows 98 reads the
button's horizontal alignment and draws each line of a right-aligned block
at its right and of a centred block (a push button, `BS_CENTER`) centred,
without the space at the break; Windows 95 reads the empty byte beside it and
draws every line from the left of the block. The vertical bits the callback
looks for are never there in either version, so the lines start at the top
of the block. Neo98 follows Windows 98; `User::SetMultilineButtonLinesLeft`
gives the rule of Windows 95.

A caption without `BS_MULTILINE` stays on one line, however wide, and the
button's edge cuts it. Neo98 drew every caption that way, so Passwords >
User Profiles of Windows 95, whose radio buttons and check boxes have
`BS_TOP` and `BS_MULTILINE`, showed each on one cut line
(`src/user.cpp`, `ButtonCaption`, `DrawMultiline`).

`tools/button_multiline_fixture.c` shows ten such buttons (Passwords' own
three among them) and writes the rows of ink of each, which
`tools/test_button_multiline_host.py` compares row by row with the same
program measured in Windows 98 SE; in Windows 95 OSR2 only the lines of the
right-aligned and centred blocks differ, as above. With
`CheckBoxesFollowFontSize` at 144 dots per inch ([FONT-SIZE.md](FONT-SIZE.md))
the cell is 20 pixels and the block moves with it.

## The children of a message box

USER builds a message box as a dialog template and runs it like any dialog.
After the buttons the template has a STATIC with `SS_ICON` for the icon,
id 10, style `50020003h`, at 7,7 dialog units and of no size (the control
takes the size of its icon), whose name is the icon's resource id (FFh and
the word 7F01h to 7F04h: the hand, the question, the exclamation, the
asterisk; `MB_USERICON` gets 7F03h and the program's icon afterwards), and a
STATIC for the text, id FFFFh, style `50022080h` (`SS_LEFT`, `SS_NOPREFIX`,
`SS_EDITCONTROL`) whatever `MB_RIGHT` says, at the rectangle the box
measured for the text. The icon's control answers `GetWindowText` with those
three bytes, the text's with the whole text. A box without an icon has no
icon control, one with a null text no text control.

The template holds dialog units. USER turns the pixels of its layout into
them with MulDiv (and gives the left-aligned text one unit more each way),
the dialog manager turns them back, so every place and size is rounded on
the way. The header gets the box's client area, at its place inside the
frame and caption; the dialog manager of Windows 95 and 98 takes that place
for the window's own corner, so a box stands a frame to the right and a
frame and a caption lower than USER centred it. `MB_SETFOREGROUND` adds
`DS_SETFOREGROUND`. The controls draw the icon and the text; the box paints
only its face. A STATIC draws with `DT_EDITCONTROL` when it has
`SS_EDITCONTROL` (but not with `SS_LEFTNOWORDWRAP`), so the text breaks its
lines as the box measured them.

Neo98 made the buttons only and painted icon and text into the box itself:
`EnumChildWindows` found no STATIC, `GetDlgItemText(box, 0xFFFF)` nothing,
and a `WH_CBT` hook saw no control for the text (`src/x86_message_box.cpp`).
The hook still sees two things differently: Windows' dialog manager hands it
the class as an atom and Neo98 by name, and Neo98 adds `WS_CLIPSIBLINGS` to
a popup before the hook and Windows after.

A dialog's modal loop, the message box's among them, dispatches what it
takes from the queue: a `WM_TIMER` with a procedure (SetTimer's `TIMERPROC`)
calls the procedure, also for the timer of a thread, which has no window.
Neo98's loop sent every message to its window, and such a timer did not run
while a dialog was open (`src/x86_dialog.cpp`, `RunModal`).

`tools/message_box_children_fixture.c` opens five boxes and records what its
CBT hook and, from such a timer, `EnumChildWindows`, `GetDlgItem` and
`GetDlgItemText` find; `tools/test_message_box_children_host.py` compares
every line with the same program in Windows 98 SE (Windows 95 OSR2 answers
the same).

## The entries of a directory listing

`LB_DIR` (and `CB_DIR`, `DlgDirList`) puts each file and directory into the
list through USER's insertion with the flag of `LB_ADDSTRINGLOWER`: the
entries show in lower case, "readme.txt" and "[windows]"; the drives,
"[-c-]", come at the end. A sorted list box compares an entry in brackets
as greater than one without, whatever the letters say, so the files come
first, then the directories and drives. `LB_ADDFILE` adds one file the
same way: USER looks the name up as DOS does (files of any attribute, no
directory) and answers `LB_ERR` when there is none. The four messages
`LB_INSERTSTRINGUPPER`, `LB_INSERTSTRINGLOWER`, `LB_ADDSTRINGUPPER` and
`LB_ADDSTRINGLOWER` (1AAh to 1ADh) are the insertion with the case flag for
a window of a 4.0 module. Neo98 listed the names as the drive spells them,
sorted brackets among the letters, and ended the list box on `LB_ADDFILE`
("unsupported listbox message") (`src/x86_listbox.cpp`,
`src/x86_dlgdir.cpp`). Measured in Windows 95 OSR2 and Windows 98 SE with
`tools/cross_process_lists_fixture.c`.

