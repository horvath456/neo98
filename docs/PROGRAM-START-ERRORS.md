# When a program does not start

Windows has five ways to start a program, and each answers in its own way
when the program does not start: `ShellExecute` with a number of 32 or less,
`ShellExecuteEx` with that number in `hInstApp` and a Windows error in
`GetLastError`, `WinExec` and `LoadModule` with the numbers of 16-bit
Windows, `CreateProcess` with `FALSE` and a Windows error. The shell's Run
dialog shows a message for the answer of `ShellExecuteEx`.

`SHELL32.DLL` runs as the original in Neo98 (`src/dll_routes.h`); the
answers of the shell are Windows' own as long as KERNEL32 answers like
Windows below it. The table is what Windows 95 OSR2 and Windows 98 SE
answer, measured with the program of `tools/start_errors_fixture.c`; both
answer alike in every row. A number above 32 means the program started.

| What is started | `GetFileAttributes` | `ShellExecute` | `ShellExecuteEx` | `WinExec` | `CreateProcess` | `LoadModule` |
| --- | --- | --- | --- | --- | --- | --- |
| a file that does not exist | -1, 2 | 2, 2 | 2, 2 | 2, 2 | 2 | 2, 2 |
| a file in a folder that does not exist | -1, 3 | 2, 2 | 2, 2 | 2, 2 | 2 | 3, 3 |
| a file on a drive that does not exist, also in a folder there | -1, 3 | 2, 2 | 2, 2 | 2, 2 | 2 | 3, 3 |
| a name found nowhere on the path | -1, 2 | 2, 2 | 2, 2 | 2, 2 | 2 | 2, 2 |
| a text file named `.EXE`, or an empty one | | 5, 193 | 5, 193 | 0, 193 | 193 | 0, 193 |
| a document without a program | | 31, 1155 | 31, 1155 | 0, 193 | 193 | 0, 193 |
| a folder | | not tried | not tried | 5, 5 | 5 | 5, 5 |
| a program another program has open without sharing | | 26, 32 | 26, 32 | 0, 32 | 32 | 0, 32 |
| a program whose DLL is missing | | 32, 1157 | 32, 1157 | 0, 1157 | 1157 | 0, 1157 |
| a program that imports a name its DLL does not export | | 5, 31 | 5, 31 | 0, 31 | 31 | 31, 31 |

The second number is `GetLastError` afterwards; for `ShellExecuteEx` the
first is `hInstApp`. Error 193 is `ERROR_BAD_EXE_FORMAT`, 1155 "no
association", 1157 `ERROR_DLL_NOT_FOUND`, 31 `ERROR_GEN_FAILURE`.
`ShellExecute` turns a Windows error of 26 or more into 5 except those it
has a number of its own for, and `WinExec` hands on 2, 3, 4, 5, 8 and 11
and answers 0 for every other error. A folder that does not exist is
"file not found" for every way of starting but `LoadModule`; only the file
functions say "path not found". A drive that does not exist is a folder
that does not exist, for the ways of starting and for the file functions.

## A drive that does not exist

Every function that takes a path answers for a path on a drive that does
not exist (Q:) what it answers for a folder that does not exist: "path
not found" (3). Measured with the same program in Windows 95 OSR2 and
Windows 98 SE, which answer alike:

| Function | Q: | C:\SE\NODIR (a folder that does not exist) |
| --- | --- | --- |
| `CreateFile`, opening and creating | 3 | 3 |
| `GetFileAttributes` of a file, of the root or folder | 3, 3 | 3, 2 |
| `FindFirstFile` | 3 | 3 |
| `SetCurrentDirectory` | 3 | 2 |
| `CreateDirectory`, `DeleteFile` | 3 | 3 |
| `OpenFile` (`OF_EXIST`), its `nErrCode` too | 3 | 3 |
| `GetDiskFreeSpace` | 15 | 123 |
| `GetVolumeInformation` | 13 | 161 |
| `GetFullPathName` | the path, no error | the path |
| `GetDriveType` | 1 (no root) | 1 |

Only `GetDiskFreeSpace`, which takes a drive, says "invalid drive" (15).
Neo98's file system answered 15 for every path on a drive that does not
exist. For a 16-bit program DOS answers the same: open (3Dh), change
directory (3Bh), find first (4Eh), make directory (39h), the attributes
(4300h) and the long-name attributes (7143h) set the carry and answer 3,
the free space of the drive (36h) answers FFFFh, and its current
directory (47h) sets the carry and answers 15. Neo98 answered 2 from find
first and the attributes, and the current directory of the missing
drive.

The shell's Run dialog says "Cannot find the file 'Q:\NOFILE.EXE' (or one
of its components)...", as for a file that does not exist, because
`ShellExecute` answers 2. "The device is not ready" belongs to a drive
that exists without a disk in it (error 21, an empty CD-ROM drive).

A program with a missing DLL or a missing import does not start, and
KERNEL32 says why before the creator gets its answer: "A required .DLL
file, X.DLL, was not found." or "The X.EXE file is linked to missing
export Y.DLL:Name.", both with the caption "Error Starting Program". For a
DLL of Neo98's own (KERNEL32, USER32, GDI32, ADVAPI32 and WINMM) the
export directory of the original is the list: a name that is not in it is
missing, a name that is in it and that Neo98 does not implement is bound
and stops the run when it is called.

## The Run dialog

| What was typed | The message of the shell |
| --- | --- |
| a missing file, also in a missing folder or on a missing drive, also with arguments | "Cannot find the file 'X' (or one of its components). Make sure the path and filename are correct and that all required libraries are available." |
| no program | "X is not a valid Win32 application." |
| a document without a program | "This file does not have a program associated with it for performing this action. Create an association in My Computer by clicking View and then clicking Options." (Windows 98: "Folder Options") |
| a document whose program is missing | the dialog "Program Not Found", which asks where the program is |
| a missing import | KERNEL32's box, then the file, an empty line and "A device attached to the system is not functioning." |

## From a 16-bit program

A 16-bit program starts others through SHELL's `ShellExecute` (File
Manager's Run does), KERNEL's `WinExec` or `LoadModule`. All find a bare
name ("calc") as Windows does, in the current directory, the Windows and
System directories and along PATH, and all take a full path.

What they answer, measured in Windows 95 OSR2 and Windows 98 SE with the
same 16-bit program (`tools/start_answers_win16_fixture.asm`):

| | `WinExec` | `LoadModule` | `ShellExecute` |
| --- | --- | --- | --- |
| a 16-bit program | its instance | its instance | 42 |
| a 32-bit program | an instance | an instance | an instance |
| a file on a missing drive, or in a missing folder | 3 | 3 | 2 |
| a file that does not exist | 2 | 2 | 2 |

The instance of a 16-bit program is the one its `InitTask` hands it.
`ShellExecute` is SHELL32's: it waits for the new program's input and
answers the instance KERNEL32 keeps for its process, or 42 when there is
none, and a 16-bit program started by a 16-bit one has not run yet. For a
32-bit program Windows answers the instance of the 16-bit module it keeps
for every process: `GetWindowWord(GWW_HINSTANCE)` of its windows answers
the same from a 16-bit program, and `GetModuleUsage` of it is 1 while the
program runs and 0 after; a 16-bit program can wait for the end of what
it started that way. `KRNL386`'s `LoadModule` opens the program by its
path, so a missing folder or drive is "path not found", where the 32-bit
`WinExec` says "file not found".

A 32-bit program that starts a 16-bit one gets its instance from
`WinExec` and `ShellExecute` too, and 32 from `LoadModule` (which answers
32 for every program that started). Windows' `LoadModule` appends the
command tail of its parameter block through its terminating zero, whatever
its count says.

Neo98 answers the instance of a 16-bit program as Windows does (it reads
it from the new program's task database). A 32-bit program has no 16-bit
module here: `WinExec`, `LoadModule` and `ShellExecute` of a 16-bit
program answer a word of their own for it (above 32, never 42, one per
process), and `GetModuleUsage` of that word is 1 while the program runs
and 0 after.

File Manager's Run seemed to start nothing ("cannot find the specified
file"): its dialog puts the selected file into the Command Line field, and
the dialog manager gives the first field the focus the way it always does,
with its whole text selected, so what the user types replaces the file.
Neo98 gave that first focus without the selection, and the typed name went
in before the file ("calcC:\"). Measured in Windows 95 OSR2 and Windows 98
SE: the field's text is selected from its first to its last character.

## Not the same in Neo98

- A 32-bit program started by a 16-bit one: Windows answers the instance
  of the program's 16-bit module, which `GetWindowWord(GWW_HINSTANCE)` of
  its windows answers too; Neo98 answers a word of its own, and
  `GWW_HINSTANCE` of such a window is 0 for a 16-bit program. A 32-bit
  program started by a 32-bit one gets that word from `WinExec`, the image
  base from `ShellExecute`.
- Values are local to a process: the instance of a 16-bit program another
  one started can be the same number as the starter's own instance.

## Tests

`tools/test_start_errors_host.py` runs the fixture: every row of the
table, the file functions for a missing drive and a missing folder, a
16-bit program started by a 32-bit one, and the Run dialog for a missing
folder, a missing drive, a file that is no program and a missing import,
whose messages the log of the run holds.
`tools/test_start_win16_host.py` starts Calculator from a 16-bit program by
its name and by its path through `ShellExecute` and `WinExec`, then runs
`tools/start_answers_win16_fixture.asm`: the answers of the three ways for
a 16-bit and a 32-bit program, `GetModuleUsage` of them, the errors for a
missing drive, folder and file, `OpenFile` and the DOS calls on a missing
drive. `tools/test_winfile_run_host.py` opens File Manager's Run and checks
the selection of its field and that a typed character replaces the text.
The fixtures write their lines to COM1 as well, so the same programs run in
the real Windows under QEMU.
