# Locales

A locale is a language with the customs of a country: how dates, times,
numbers and money are written, which day begins the week. Windows keeps
the values of every locale in `C:\WINDOWS\SYSTEM\LOCALE.NLS`, and the
user's choice under `HKEY_CURRENT_USER\Control Panel\International`.
Neo98 reads both like KERNEL32 does (`src/x86_nls_extended.inc`,
`src/x86_nls.cpp`, `tools/host_windows_locale.h`).

## Which locales there are

`EnumSystemLocalesA` takes the locales out of the registry, not out of
LOCALE.NLS: the values of
`HKLM\System\CurrentControlSet\Control\Nls\Locale` and then those of
`Nls\AddLocales`, in the order the registry keeps them. A value counts when
its name is eight hexadecimal digits. With `LCID_INSTALLED` only the values
with a name as their data count, with `LCID_SUPPORTED` the empty ones too.
The callback gets the name as the registry spells it (`00000C07`); when it
answers anything but `TRUE`, the values of that key end, and those of the
next key still come.

Setup writes the locales of the installed language group with their names
and the others empty. Windows 95 OSR2 in English names 50 (the Western
European and American ones of code page 1252), Windows 98 SE 74 (also
those of code pages 1250, 1251, 1253, 1254 and 1257). The first page of
Regional Settings lists exactly the installed ones.

## The texts of a locale

`GetLocaleInfoA` answers the texts of a locale in that locale's own ANSI
code page: Monday in Russian comes as bytes of code page 1251, the currency
of Greece in 1253. A locale whose code page has no table among the files of
Windows (`CP_1251.NLS` and so on in `C:\WINDOWS\SYSTEM`) has no data at
all: in Windows 95, which installs code page 1252 alone, `GetLocaleInfoA`
answers nothing for Russian or Greek. The name of a locale
(`LOCALE_SLANGUAGE`) comes from KERNEL32's own string table.

## Choosing a locale

On OK, Regional Settings writes the chosen locale as `Locale`
(`00000C07` for German (Austrian)) under
`HKCU\Control Panel\International`, calls `InvalidateNLSCache` and asks
for a restart. It writes nothing else: every value then comes from the new
locale, the first day of the week (`LOCALE_IFIRSTDAYOFWEEK`, 0 for
Monday), the dates, numbers and money. A value the user set before with
`SetLocaleInfoA` (a currency symbol, say) stays under the key and is
answered for the new locale as well, until the user changes it.

`SetLocaleInfoA` writes the registry and is answered at once. A value a
program writes into the registry by itself is not: Windows 95 and 98 go on
answering the old one in that session, also after `InvalidateNLSCache`.
Neo98 reads the registry for every answer and answers it at once.

KERNEL32 keeps the user's locale in its shared data, so
`InvalidateNLSCache` in one process empties it for all of them: a program
that is running asks `GetUserDefaultLCID` and gets the new locale at once.
Neo98 counts the calls of `InvalidateNLSCache` for the whole session, and a
process that sees a count it has not seen reads the locale again.

For German (Austrian), with a currency symbol `US$` set before:

| Asked | Windows 95 and 98 answer |
| --- | --- |
| `GetUserDefaultLCID` | `0C07` |
| short date | `dd.MM.yy` |
| long date | `dddd, dd. MMMM yyyy`: "Samstag, 28. November 2026" |
| first day of the week | 0, Monday |
| currency, decimal, thousands | `US$` (the user's), `,`, `.`: "-US$ 1.234.567,89" |
| time format | `HH:mm:ss` |

## Not the same in Neo98

- A value written into the registry by a program is answered at once (see
  above).
- `LOCALE_SCOUNTRY` answers nothing: Windows takes the country's name out
  of a string table of KERNEL32 Neo98 does not read.
- `EnumSystemCodePagesA` hands out a list of its own, not the code pages of
  `Nls\Codepage`.
- Windows 98 answers `IsValidLocale(..., LCID_INSTALLED)` with 0 for ten
  Spanish locales of America whose names the registry has (Panama to
  Paraguay); Neo98 answers 1, as for the other named ones.

## Tests

`tools/test_regional_locale_host.py` runs `tools/regional_locale_fixture.c`
in three sessions: the enumeration against the registry, the texts of
other code pages; then the real Regional Settings with German (Austrian)
chosen on its first page; then the answers in a new session. The same
program was run in Windows 95 OSR2 and Windows 98 SE.
