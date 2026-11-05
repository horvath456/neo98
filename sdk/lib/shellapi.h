/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* ShellExecuteA, ExtractIconA and SHGetFileInfoA for a program without
   SHELL32. An ARM64 process of NEO98 does not load SHELL32.DLL, which is x86
   code; the SDK links these three statically instead, own code over
   shellhelp (sdk/lib/shellhelp.h). They do the common part of the originals:
   see each function. Under Windows on ARM the same program uses these too.
   docs/ARM64-APPS.md. */
#ifndef NEO98_SHELLAPI_H
#define NEO98_SHELLAPI_H
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif

#define SE_ERR_FNF 2
#define SE_ERR_NOASSOC 31
#define SHGFI_ICON 0x00000100
#define SHGFI_DISPLAYNAME 0x00000200
#define SHGFI_TYPENAME 0x00000400
#define SHGFI_LARGEICON 0x00000000
#define SHGFI_SMALLICON 0x00000001

typedef struct _SHFILEINFOA
{
    HICON hIcon;
    int iIcon;
    DWORD dwAttributes;
    CHAR szDisplayName[MAX_PATH];
    CHAR szTypeName[80];
} SHFILEINFOA, SHFILEINFO;

/* The verbs "open" and NULL: a program, a shortcut, a folder, a document or
   an address, as ShellHelpOpen starts them. Any other verb answers
   SE_ERR_NOASSOC. The result is above 32 on success. */
HINSTANCE WINAPI ShellExecuteA(HWND hwnd, LPCSTR lpOperation, LPCSTR lpFile, LPCSTR lpParameters, LPCSTR lpDirectory, INT nShowCmd);
/* The icon number nIconIndex of an EXE, DLL or ICO file at 32 by 32; with
   index -1 the number of icons, as a handle. NULL without such an icon. */
HICON WINAPI ExtractIconA(HINSTANCE hInst, LPCSTR pszExeFileName, UINT nIconIndex);
/* SHGFI_ICON (with SHGFI_SMALLICON), SHGFI_DISPLAYNAME and SHGFI_TYPENAME
   for a file: the icon of a program or of the target of a shortcut, the name
   without folder (and without ".lnk"), the type name the registry has for
   the extension. Other flags are not served. */
DWORD_PTR WINAPI SHGetFileInfoA(LPCSTR pszPath, DWORD dwFileAttributes, SHFILEINFOA *psfi, UINT cbFileInfo, UINT uFlags);
#define ShellExecute ShellExecuteA
#define ExtractIcon ExtractIconA
#define SHGetFileInfo SHGetFileInfoA

#ifdef __cplusplus
}
#endif
#endif
