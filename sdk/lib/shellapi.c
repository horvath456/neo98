/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* ShellExecuteA, ExtractIconA, SHGetFileInfoA without SHELL32: see
   shellapi.h. Own code. docs/ARM64-APPS.md. */
#include "shellapi.h"
#include "shellhelp.h"

HINSTANCE WINAPI ShellExecuteA(HWND hwnd, LPCSTR operation, LPCSTR file, LPCSTR parameters, LPCSTR directory, INT show)
{
    (void)hwnd;
    if (operation && operation[0] && lstrcmpiA(operation, "open")) return (HINSTANCE)(INT_PTR)SE_ERR_NOASSOC;
    return (HINSTANCE)(INT_PTR)ShellHelpOpen(file, parameters, directory, show, NULL);
}
HICON WINAPI ExtractIconA(HINSTANCE instance, LPCSTR file, UINT index)
{
    (void)instance;
    if (index == (UINT)-1) return (HICON)(INT_PTR)ShellHelpIconCount(file);
    return ShellHelpIcon(file, (int)index, 32, 32);
}
DWORD_PTR WINAPI SHGetFileInfoA(LPCSTR path, DWORD attributes, SHFILEINFOA *info, UINT size, UINT flags)
{
    static ShellHelpLink link; /* large; calls come from the thread of the window */
    const char *name = path, *dot = NULL;
    int is_link = 0;
    (void)attributes;
    if (!path || !info || size < sizeof *info) return 0;
    if (GetFileAttributesA(path) == 0xffffffffu) return 0;
    for (const char *p = path; *p; ++p)
    {
        if (*p == '\\' || *p == '/' || *p == ':') { name = p + 1; dot = NULL; }
        else if (*p == '.') dot = p;
    }
    is_link = dot && !lstrcmpiA(dot, ".lnk");
    if (flags & SHGFI_ICON)
    {
        const int edge = flags & SHGFI_SMALLICON ? 16 : 32;
        info->hIcon = NULL;
        info->iIcon = 0;
        if (is_link && ShellHelpReadLink(path, &link))
            info->hIcon = ShellHelpIcon(link.icon[0] ? link.icon : link.target, link.icon_index, edge, edge);
        else if (!is_link)
            info->hIcon = ShellHelpIcon(path, 0, edge, edge);
        if (!info->hIcon) return 0;
    }
    if (flags & SHGFI_DISPLAYNAME)
    {
        lstrcpynA(info->szDisplayName, name, MAX_PATH);
        if (is_link) info->szDisplayName[dot - name] = 0;
    }
    if (flags & SHGFI_TYPENAME)
    {
        /* HKEY_CLASSES_ROOT\.ext names the class, whose default value is the type name. */
        char type[128] = "", text[128] = "";
        HKEY key;
        DWORD bytes = sizeof type;
        info->szTypeName[0] = 0;
        if (dot && RegOpenKeyExA(HKEY_CLASSES_ROOT, dot, 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS)
        {
            if (RegQueryValueExA(key, NULL, NULL, NULL, (LPBYTE)type, &bytes) != ERROR_SUCCESS) type[0] = 0;
            RegCloseKey(key);
        }
        bytes = sizeof text;
        if (type[0] && RegOpenKeyExA(HKEY_CLASSES_ROOT, type, 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS)
        {
            if (RegQueryValueExA(key, NULL, NULL, NULL, (LPBYTE)text, &bytes) == ERROR_SUCCESS) lstrcpynA(info->szTypeName, text, 80);
            RegCloseKey(key);
        }
    }
    return 1;
}
/* A source that declares these three itself, as imports of SHELL32, finds
   them here: the cell an import is called through. */
void *__imp_ShellExecuteA = (void *)ShellExecuteA;
void *__imp_ExtractIconA = (void *)ExtractIconA;
void *__imp_SHGetFileInfoA = (void *)SHGetFileInfoA;
