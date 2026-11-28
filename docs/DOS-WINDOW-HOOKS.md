# Hooks of the session in the MS-DOS window

A program that calls `SetWindowsHookEx` with a procedure of a DLL and thread
0 hooks the whole session: Windows loads the DLL into every process an event
of the hook's type happens in and calls the procedure there. That includes
the MS-DOS window.

## What Windows does

In Windows 95 and 98 the MS-DOS window belongs to WINOLDAP, a task of the
system VM, and the property sheet of the window runs in the same task.
Measured with `tools/session_hook_dos_fixture.c` and
`tools/session_hook_dos_dll_fixture.c` (a program sets `WH_CBT`,
`WH_CALLWNDPROC` and `WH_GETMESSAGE` for the session, opens
`C:\COMMAND.COM`, its property sheet from the system menu and types into a
field): the DLL is loaded into the process of that task, and every
procedure is called there, in one process and one thread:

| Window | Hook | Events |
| --- | --- | --- |
| the MS-DOS window (class `tty`) | `WH_CBT` | created, activated, focus |
| | `WH_CALLWNDPROC` | `WM_SETFOCUS` |
| | `WH_GETMESSAGE` | its clicks and keys |
| the font size box of its toolbar | `WH_CBT` | created |
| the property sheet and its pages, fields and boxes | `WH_CBT` | created, the sheet activated, the first field focused |
| | `WH_CALLWNDPROC` | `WM_INITDIALOG`, `WM_SETFOCUS` of the field |
| | `WH_GETMESSAGE` | the keys typed into a field |

## In Neo98

The MS-DOS window is the window of the DOS process (`src/x86_dos_vm.cpp`),
and its property sheet runs in the one thread of that process, as in
Windows. The DOS process runs native code until it loads its first x86
module, which brings the processor of the process (`AttachProcessor`). The
DLL of a hook of the session is loaded there like everywhere else and is
that first module (`SyncSessionHooks`, `src/x86_hooks.cpp`); the loop of
the window runs `WH_GETMESSAGE` after each message, as the native modal
loops do.

Not the same:

- The property sheet opens from the system menu's Properties and from the
  button of the toolbar ([DOS-WINDOW-MENU.md](DOS-WINDOW-MENU.md)).
- `WM_SETFOCUS` to a field of the sheet passes no `WH_CALLWNDPROC`; the
  `WH_CBT` focus event comes.

## Tests

`tools/test_session_hooks_dos_host.py` runs the two programs on a copy of
the pristine drive C: and checks every row of the table but the last
`WM_SETFOCUS`, all in the process of the window (9 checks; before, no hook
was called in the DOS process).
