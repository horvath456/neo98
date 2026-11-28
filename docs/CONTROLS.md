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
