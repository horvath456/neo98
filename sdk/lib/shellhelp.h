/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* What a program without SHELL32 and OLE32 needs from them most: start a
   program or a document, read the icon of a file, read a shortcut. A small
   static library of the NEO98 SDK, own code over KERNEL32, USER32 and
   ADVAPI32, so a program built with it runs under Windows on ARM as well.
   docs/ARM64-APPS.md. */
#ifndef NEO98_SHELLHELP_H
#define NEO98_SHELLHELP_H
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Opens a file the way ShellExecute does with the verb "open": a program is
   started with the parameters; a shortcut leads to its target; a folder goes
   to Explorer; a document or an address ("http://...") goes to the program
   the registry names for it (HKEY_CLASSES_ROOT, shell\open\command). The
   result is above 32 on success, like the one of ShellExecute; otherwise 2
   (file not found), 31 (nothing is registered for it) or the error of
   CreateProcess below 32. `process`, if not NULL, receives the handle of the
   new process, which the caller closes. */
UINT_PTR ShellHelpOpen(LPCSTR file, LPCSTR parameters, LPCSTR directory, int show, HANDLE *process);

/* The icon number `index` of an EXE or DLL (PE for any processor, or a
   16-bit NE file) or of an ICO file, in the size asked for; a negative index
   names the resource -index, as ExtractIcon has it. NULL when the file has
   no such icon. The caller destroys the icon. */
HICON ShellHelpIcon(LPCSTR path, int index, int width, int height);
/* How many icons the file holds. */
int ShellHelpIconCount(LPCSTR path);

/* A shortcut (.lnk). */
typedef struct
{
    char target[MAX_PATH];
    char arguments[MAX_PATH];
    char directory[MAX_PATH];
    char description[MAX_PATH];
    char icon[MAX_PATH]; /* empty: the icon of the target */
    int icon_index;
    int show;            /* SW_SHOWNORMAL, SW_SHOWMAXIMIZED or SW_SHOWMINNOACTIVE */
} ShellHelpLink;
BOOL ShellHelpReadLink(LPCSTR path, ShellHelpLink *link);

#ifdef __cplusplus
}
#endif
#endif
