# Regional Settings: what is stored, and typed text

## What SetLocaleInfoA stores

`SetLocaleInfoA` sets a value of the user's locale under
`HKCU\Control Panel\International` (`src/x86_nls_extended.inc`). With some
values KERNEL32 stores what follows from them as well: a time format gives
`iTime`, `iTLZero`, `iTimePrefix` and `sTime`, a short date `iDate` and
`sDate`; a time or date separator set alone rewrites its picture
(`sTimeFormat`, `sShortDate`), and `LOCALE_ITIME` rewrites the time format,
every `H` to `h` for 0 and every `h` to `H` for 1.

Every value it would write, the one given and those that follow, is first
compared with what the locale itself has for that type
(`GetLocaleInfoA` with `LOCALE_NOUSEROVERRIDE`, then `CompareStringA`
without flags). When they are equal the stored value is removed instead of
written: a setting put back to the locale's own leaves nothing in the
registry, and a later change of the locale is not held back by it.

The first day of the week (`LOCALE_IFIRSTDAYOFWEEK`, `iFirstDayOfWeek`) is
one digit from 0 (Monday) to 6 (Sunday). `GetLocaleInfoA` answers the stored
value of the user's locale, and without one the locale's own: English
(United States) begins on Sunday (6), German (Germany) on Monday (0). A new
locale chosen on the first page of Regional Settings is written as `Locale`
under the same key, followed by `InvalidateNLSCache`; the next
`GetUserDefaultLCID` and every value then follow it.

## Typed text

`INTL.CPL` reads each of its editable combo boxes as "the selected item, or
else the text" (`CB_GETCURSEL`, then `CB_GETLBTEXT` or `GetDlgItemText`) and
marks a box as changed on `CBN_SELCHANGE` and on `CBN_EDITCHANGE`; USER
clears the selection of the list when the user changes the text. The Date
page sets its two styles when it is left (`PSN_KILLACTIVE`, which OK sends to
the page in front) and only clears its flags on `PSN_APPLY`. The Time page
sets a changed time style first and then fills its separator box anew from
the new style and sets that, so with the style and the separator changed
together the style wins.

A drop-down list closed by a click outside it ends with the list's
cancellation: the list takes back the item that was selected when it
dropped down and tells the combo box, which puts that item's text into the
field. Text typed while the list is open is therefore replaced by the old
item when the next click lands outside the list (on OK, say), and that
click does nothing else. With the list closed the typed text is taken.

## Tests

- `tools/test_locale_settings_host.py`: what follows from a time and a date
  picture, the separators, the removal of values equal to the locale's own,
  `LOCALE_ITIME`, the first day of the week, a new user locale.
- `tools/test_regional_typed_host.py`: the real applet, typed into with the
  input script (both date styles, AM and PM symbols, the time separator, the
  decimal, grouping and list separators, the currency symbol and its
  separators, then the time style alone), read back in a new session with
  `GetLocaleInfoA` and the four format functions.
- `tools/test_combo_typed_host.py`: the selection of a combo box after typed
  text.
