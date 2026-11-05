/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* GetOpenFileNameA and GetSaveFileNameA for a program without COMDLG32. An
   ARM64 process of NEO98 does not load COMDLG32.DLL, which is x86 code; the
   SDK links a file dialog of its own statically instead (sdk/lib/filedialog.c):
   one list with folders, drives and the files of the chosen filter, a name
   field, a button that steps through the filters, OK and Cancel. It honours
   lpstrFilter, nFilterIndex, lpstrFile, nMaxFile, lpstrInitialDir,
   lpstrTitle, lpstrDefExt, hwndOwner and the flags OFN_FILEMUSTEXIST,
   OFN_PATHMUSTEXIST and OFN_OVERWRITEPROMPT, and fills nFileOffset and
   nFileExtension. Hooks, templates and multiple selection are not served
   (the call fails for OFN_ENABLEHOOK, OFN_ENABLETEMPLATE and
   OFN_ALLOWMULTISELECT). docs/ARM64-APPS.md. */
#ifndef NEO98_COMMDLG_H
#define NEO98_COMMDLG_H
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif

#define OFN_READONLY 0x00000001
#define OFN_OVERWRITEPROMPT 0x00000002
#define OFN_HIDEREADONLY 0x00000004
#define OFN_NOCHANGEDIR 0x00000008
#define OFN_ENABLEHOOK 0x00000020
#define OFN_ENABLETEMPLATE 0x00000040
#define OFN_ALLOWMULTISELECT 0x00000200
#define OFN_PATHMUSTEXIST 0x00000800
#define OFN_FILEMUSTEXIST 0x00001000
#define OFN_EXPLORER 0x00080000

typedef struct tagOFNA
{
    DWORD lStructSize;
    HWND hwndOwner;
    HINSTANCE hInstance;
    LPCSTR lpstrFilter;
    LPSTR lpstrCustomFilter;
    DWORD nMaxCustFilter;
    DWORD nFilterIndex;
    LPSTR lpstrFile;
    DWORD nMaxFile;
    LPSTR lpstrFileTitle;
    DWORD nMaxFileTitle;
    LPCSTR lpstrInitialDir;
    LPCSTR lpstrTitle;
    DWORD Flags;
    WORD nFileOffset;
    WORD nFileExtension;
    LPCSTR lpstrDefExt;
    LPARAM lCustData;
    void *lpfnHook;
    LPCSTR lpTemplateName;
} OPENFILENAMEA, *LPOPENFILENAMEA, OPENFILENAME, *LPOPENFILENAME;

BOOL WINAPI GetOpenFileNameA(LPOPENFILENAMEA);
BOOL WINAPI GetSaveFileNameA(LPOPENFILENAMEA);
#define GetOpenFileName GetOpenFileNameA
#define GetSaveFileName GetSaveFileNameA

#ifdef __cplusplus
}
#endif
#endif
