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
functions say "path not found".

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
| a missing file, also in a missing folder, also with arguments | "Cannot find the file 'X' (or one of its components). Make sure the path and filename are correct and that all required libraries are available." |
| no program | "X is not a valid Win32 application." |
| a document without a program | "This file does not have a program associated with it for performing this action. Create an association in My Computer by clicking View and then clicking Options." (Windows 98: "Folder Options") |
| a document whose program is missing | the dialog "Program Not Found", which asks where the program is |
| a missing import | KERNEL32's box, then the file, an empty line and "A device attached to the system is not functioning." |

## Not the same in Neo98

- A drive letter that does not exist: Windows answers "path not found" (3)
  from `GetFileAttributes` and `LoadModule`, Neo98 "invalid drive" (15).
  The ways of starting answer 2 in both.
- A program that started: Windows 95 and 98 answer the 16-bit instance or
  task of the new program from `ShellExecute` and `WinExec`, Neo98 another
  number above 32.

## Tests

`tools/test_start_errors_host.py` runs the fixture: every row of the
table, and the Run dialog for a missing folder, a file that is no program
and a missing import, whose messages the log of the run holds.
