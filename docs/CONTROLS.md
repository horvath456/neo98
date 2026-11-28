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
