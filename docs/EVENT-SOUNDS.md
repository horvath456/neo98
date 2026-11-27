# The sounds of events

A sound scheme of the Control Panel (Sounds) gives an event a file: the
default value of `HKCU\AppEvents\Schemes\Apps\.Default\EVENT\.Current`.
USER plays the events of the system through the play routine MMSYSTEM
registers with it, with flags of its own; the port's
`Application::PlayEventSound` (`src/x86_wave_out.cpp`) takes them, and every
event it plays leaves a line `SOUND event name=EVENT flags=... result=...`
in the log (`result=0` played, `4294967295` no file for the event).

## Who raises which event

| Event (label in Sounds) | Raised by | In the port |
| --- | --- | --- |
| `.Default` (Default sound) | `MessageBeep` with type 0 or a type above 40h; a message box without an icon; `Beep` of KERNEL32 | all three |
| `SystemHand` (Critical Stop), `SystemQuestion`, `SystemExclamation`, `SystemAsterisk` | `MessageBeep` with the icon bits 10h, 20h, 30h, 40h; a message box with that icon | both |
| `Open`, `Close` (Open program, Close program) | USER, when a process starts and ends | yes |
| `Maximize`, `Minimize`, `RestoreUp`, `RestoreDown` | `DefWindowProc`, `WM_SYSCOMMAND`, before the window changes | yes |
| `MenuPopup`, `MenuCommand` | the menu code, when a popup opens and an item is chosen | yes |
| `SystemStart`, `SystemExit` (Start Windows, Exit Windows) | the start and the end of the session | the hosts play them |
| `AppGPFault` (Program error) | KERNEL32, right before the box that ends a program after a fault | where the host shows that box (`Application::SetFaultBox`) |
| `EmptyRecycleBin` of Explorer | SHELL32 itself, `PlaySound` with `SND_ALIAS`, `SND_APPLICATION` and `SND_ASYNC` when the bin is emptied, if the scheme of Explorer has a file | SHELL32's own code over the port's `PlaySound` |
| `MailBeep` (New Mail Notification) | the mail program | not the system's work |

## A message box

USER plays the event of a box's icon when the box's template is ready,
before the dialog is made: the default sound without an icon, the four icon
events by bits 4 to 7 of the type, nothing for `MB_USERICON` or a value
above the asterisk (50h to 70h). It calls the play routine itself, not
`MessageBeep`: a box sounds also while the beep is off (`SPI_SETBEEP`), and
an event without a file is quiet, where `MessageBeep` falls back to the
speaker. Windows 98 and Windows 95 do the same. The 32-bit calls
(`MessageBoxA`, `MessageBoxExA`, `MessageBoxW`, `MessageBoxIndirectA`) and
the 16-bit `MessageBox` end in USER's one routine, and so do the boxes the
port opens itself (`Application::BuildMessageBox`, `src/x86_message_box.cpp`).
The system-modal box of a hard error plays nothing.

## Beep of KERNEL32

`Beep` plays the event of the default sound and, only when that does not
sound, the event FFFFh, which is the speaker routine of USER. FFFFh sounds
only while no play routine is registered, on a machine without a wave
device; with one, USER looks the event's string up and has none for FFFFh.
So `Beep` is quiet on a machine with a sound card and no default sound, and
never reads its frequency or duration.

## Tests

`tools/test_event_sounds_host.py` (with `tools/event_sound_fixture.c` and
`tools/box_sound_win16_fixture.asm`): `MessageBeep`, `PlaySound` with
`SND_ALIAS`, the events of the menu and of the window states, a box of each
of the four 32-bit calls and of the 16-bit `MessageBox`, a box with the beep
off, with an empty entry, with `MB_USERICON` and with 50h, `Beep` with and
without a default sound, and the fault box in `neo98-host`. The samples the
device plays are recorded (`NEO98_AUDIO_TAP`).
