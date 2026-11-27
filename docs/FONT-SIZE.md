# The font size of the display

Windows has one setting for how large everything is: the font size of the
display, in dots per inch. "Small Fonts" is 96, "Large Fonts" 120, and
"Other..." takes any size from 20 to 500 percent of the small one. Neo98
reads the setting like Windows does, so the fonts, the dialogs and the parts
of a window are as large as on a PC with the same setting.

## Setting it

In Windows 98: Display Properties, page Settings, Advanced..., page General,
"Font Size". In Windows 95 the list is on the page Settings itself. Windows
asks for a restart, and so does Neo98: the size holds from the next start.

## What Windows stores

| Place | Value |
| --- | --- |
| `HKEY_CURRENT_CONFIG\Display\Settings` | `DPILogicalX`, `DPILogicalY`, `DPIPhysicalX`, `DPIPhysicalY`: the size, four times; `fonts.fon`, `fixedfon.fon`, `oemfonts.fon`: the System, Fixedsys and Terminal fonts of the size |
| `HKEY_CURRENT_CONFIG\Display\Fonts` | the raster fonts of the size: MS Sans Serif, MS Serif, Courier, Symbol, Small Fonts |
| `SYSTEM.INI` | the three system fonts once more in `[boot]`, `aspect=100,size,size` in `[boot.description]` |

Windows has two sets of raster fonts, listed under
`HKLM\Software\Microsoft\Windows\CurrentVersion\FontSize`: one drawn for 96
dots per inch (`VGASYS.FON`, `SSERIFE.FON`...) and one for 120 (`8514SYS.FON`,
`SSERIFF.FON`...). Display Properties installs the set whose number is nearer
to the size, so every size above 108 uses the fonts of 120.

## What follows the size

- `GetDeviceCaps`: `LOGPIXELSX` and `LOGPIXELSY` are the logical size,
  `HORZSIZE` and `VERTSIZE` the pixels of the screen over the physical size
  in millimetres. An inch of the metric and English mapping modes
  (`MM_LOMETRIC`, `MM_TWIPS`...) is that many pixels. `dmLogPixels` of
  `EnumDisplaySettings` is the size too.
- Every font asked for in points: a dialog's font
  (`-MulDiv(points, size, 72)`), `DEFAULT_GUI_FONT`, TrueType fonts.
  A dialog measures its units from its font, so a dialog in 8 point MS Sans
  Serif has units of 6 by 13 pixels at 96 dots per inch, 8 by 16 at 120 and
  10 by 20 at 144. `GetDialogBaseUnits` is the cell of the System font, 8 by
  16 or 10 by 20.
- The parts of a window. Windows keeps their sizes in twips and their fonts
  in points under `HKCU\Control Panel\Desktop\WindowMetrics` and turns them
  into pixels at its start: a caption of -270 twips is 18 pixels at 96 dots
  per inch and 23 at 120. `SystemParametersInfo` with
  `SPI_SETNONCLIENTMETRICS` or `SPI_SETICONMETRICS` and `SPIF_UPDATEINIFILE`
  stores them there the same way. A caption, a small caption and a menu bar
  are never lower than their font and one edge.
- The spacing of icons, 75 pixels at 96 dots per inch.
- Scroll bars, and with them the button of a drop-down combo box, which is
  as wide as a scroll bar.

The stock fonts `ANSI_VAR_FONT`, `ANSI_FIXED_FONT` and `OEM_FIXED_FONT` ask
for 12 pixels at every size; they get larger only because the fonts of 120
dots per inch start at 16 pixels. Check boxes and radio buttons stay 13
pixels, and toolbar pictures keep their size, as on Windows.

## Fonts between two sizes

A raster font has only the sizes of its file. The mapper weighs them with
the penalties GDI's own table has for the height: a size above the request
costs 600 and 150 for every pixel, one below it 150 for every pixel missing.
A request between two sizes therefore takes the lower one. GDI also enlarges
a raster size by a whole factor, at the price of 50 and 20 times the factor:
24 point MS Sans Serif at 144 dots per inch, 48 pixels, is the 14 point size
of the 120 set drawn twice as large. `PROOF_QUALITY` in the `LOGFONT` asks
for the sizes of the file only. The other penalties of GDI's table (family,
pitch, width) are not weighed: a face is found by its name.

## In the source

| File | What it does |
| --- | --- |
| `src/gdi_display_dpi.h` | the four numbers, and points and twips in pixels |
| `src/x86_font_catalog.cpp` | reads the numbers and the system fonts at the start, the stock fonts |
| `src/gdi_font_catalog.cpp` | the mapper's choice of a raster size, the enlarged font |
| `src/x86_user.cpp`, `LoadWindowMetrics` | the sizes and fonts of a window's parts at the start |
| `src/x86_parameters_write.inc` | stores them for `SystemParametersInfo` |
| `src/user.cpp`, `ApplyNonClientMetrics` | makes the caption and menu fonts and holds the sizes to them |

`tools/test_font_size_host.py` sets 120 and 144 the way Display Properties
does and compares what Windows programs are told at each size; it needs an
installed Windows.

## Limits

A window that is open while `SPI_SETNONCLIENTMETRICS` changes the height of
captions or menus keeps its frame until it is resized; Windows adjusts every
window at once. Minimized windows have no width setting (`MinWidth`).
