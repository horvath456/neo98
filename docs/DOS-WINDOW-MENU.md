# The system menu of the MS-DOS window

WINOLDAP, the part of Windows 95 and 98 that shows an MS-DOS program in a
window, gives the window's system menu items of its own when it makes the
window. The texts come from WINOA386.MOD: its string tables and its menu 500.

| Position | Item | Command |
| --- | --- | --- |
| 0 to 4 | USER's Restore, Move, Size, Minimize, Maximize | F120h, F010h, F000h, F020h, F030h |
| 5 | separator | |
| 6 | "&Close", WINOLDAP's text in place of USER's, without "Alt+F4" (the keys go to the program) | F060h |
| 7 | "&Edit", the popup of menu 500: "Mar&k", "Cop&y" with "Enter", "&Paste", "Scro&ll" | E002h, E000h, E001h, E004h |
| 8 | "&Toolbar", checked while the strip shows | E00Eh |
| 9 | separator | |
| 10 | "&Properties" | E005h |

Before the menu opens WINOLDAP sets the items: Copy only with a marked block,
Paste only with text on the clipboard and the window in a text mode, Mark only
in a text mode, Scroll only while the window shows less than the whole
screen. Its table of commands also takes the ids of the strip's buttons:
E003h (Mark), E00Ah (Font), E00Dh (Properties), E00Fh (Full screen), E011h
(Background).

What the commands do, measured in Windows 95 OSR2 and Windows 98 SE with
`tools/dos_system_menu_fixture.c`:

- Toolbar shows or hides the strip; the client area of the window keeps its
  picture, so the window grows or shrinks by the strip's 28 pixels, and the
  item is checked or not. WINOLDAP changes the bit of its settings in memory;
  the PIF stays as it is.
- Mark puts "Mark - " before the title, and the mouse marks a block of text
  cells instead of reaching the program; Enter copies the block (Copy) and
  ends it, Escape ends it.
- Scroll puts "Scroll - " before the title; the keys move the view instead of
  reaching the program, until Escape or Enter. Neo98's window always shows
  the whole screen, so there is nothing to move.
- Paste types the clipboard's text into the program.
- Properties opens the window's property sheet ("MS-DOS Prompt Properties").

Alt+Space opens the menu, as the menu key of any window, unless the Misc
page of the properties leaves the key to the program (`KBD_NOALTSPACE`,
80h). Neo98 gave the key to the program, so where the window's caption has no
system menu box, the menu could not be reached.

In Neo98 the MS-DOS window (`src/x86_dos_vm.cpp`) builds the same menu from
the same module (`DosVmSystemMenu`), sets its items on `WM_INITMENU`
(`DosVmMenuStates`) and takes the commands with `WM_SYSCOMMAND`
(`DosVmSystemCommand`), through the same actions as the buttons of the strip.
Before, the window had USER's system menu only. Its titles ("Mark - ",
"Scroll - ", "Finished - ", the name of the program that runs) go through
`SetWindowText` as WINOLDAP's do (`DosVmTitle`), so `WM_SETTEXT` passes the
window's procedure, and a procedure put before the window's own sees each
new title. Before, Neo98 set the text without the message.

`tools/test_dos_system_menu_host.py` compares the menu and the commands with
Windows. Two things differ there and are not compared: Windows calls the
window of `C:\WINDOWS\COMMAND.COM` started by WinExec "MS-DOS Prompt" where Neo98
shows "COMMAND", and after `exit` Windows keeps the window as "Finished -"
where Neo98 closes it.
