/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* GetOpenFileNameA and GetSaveFileNameA without COMDLG32: see commdlg.h. A
   window of its own with the controls of USER (LISTBOX, EDIT, BUTTON,
   STATIC) and a modal loop. On a large screen held upright (a phone) the
   controls are larger, for a finger. Own code. docs/ARM64-APPS.md. */
#include "commdlg.h"

enum { IdList = 100, IdName, IdFilter, IdOk, IdCancel, IdFolder };
#define FILTERS 16

typedef struct
{
    OPENFILENAMEA *ofn;
    int save, done, result;
    HWND window, list, name, filter, folder;
    HFONT font;
    char directory[MAX_PATH];
    const char *filter_name[FILTERS], *filter_pattern[FILTERS];
    int filter_count, filter_index;
} Dialog;
static const char dialog_class[] = "Neo98FileDialog";

/* One pattern of a filter ("*.wav") against a name; a filter holds several,
   separated by semicolons. */
static int MatchOne(const char *pattern, const char *end, const char *name)
{
    for (; pattern < end; ++pattern, ++name)
    {
        if (*pattern == '*')
        {
            for (;; ++name)
            {
                if (MatchOne(pattern + 1, end, name)) return 1;
                if (!*name) return 0;
            }
        }
        if (!*name) return 0;
        if (*pattern != '?')
        {
            char a = *pattern, b = *name;
            if (a >= 'a' && a <= 'z') a -= 32;
            if (b >= 'a' && b <= 'z') b -= 32;
            if (a != b) return 0;
        }
    }
    return !*name;
}
static int Matches(const char *patterns, const char *name)
{
    if (!patterns || !*patterns) return 1;
    for (const char *p = patterns; *p;)
    {
        const char *end = p;
        while (*end && *end != ';') ++end;
        /* "*.*" means every file, also one without a dot. */
        if ((end - p == 3 && p[0] == '*' && p[1] == '.' && p[2] == '*') || MatchOne(p, end, name)) return 1;
        p = *end ? end + 1 : end;
    }
    return 0;
}
static void AddSlash(char *path)
{
    const int n = lstrlenA(path);
    if (n && path[n - 1] != '\\' && n < MAX_PATH - 1) lstrcatA(path, "\\");
}
/* Fills the list: the way up, the folders, the files of the filter, the drives. */
static void Fill(Dialog *d)
{
    WIN32_FIND_DATAA found;
    char pattern[MAX_PATH + 4], line[MAX_PATH + 8];
    HANDLE search;
    DWORD drives;
    SendMessageA(d->list, LB_RESETCONTENT, 0, 0);
    SetWindowTextA(d->folder, d->directory);
    if (lstrlenA(d->directory) > 3) SendMessageA(d->list, LB_ADDSTRING, 0, (LPARAM) "[..]");
    lstrcpyA(pattern, d->directory);
    AddSlash(pattern);
    lstrcatA(pattern, "*.*");
    for (int pass = 0; pass < 2; ++pass)
    {
        search = FindFirstFileA(pattern, &found);
        if (search == INVALID_HANDLE_VALUE) break;
        do
        {
            const int folder = (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (found.cFileName[0] == '.' && (!found.cFileName[1] || (found.cFileName[1] == '.' && !found.cFileName[2]))) continue;
            if (folder != (pass == 0)) continue;
            if (folder) wsprintfA(line, "[%s]", found.cFileName);
            else if (Matches(d->filter_count ? d->filter_pattern[d->filter_index] : NULL, found.cFileName))
                lstrcpyA(line, found.cFileName);
            else continue;
            SendMessageA(d->list, LB_ADDSTRING, 0, (LPARAM)line);
        } while (FindNextFileA(search, &found));
        FindClose(search);
    }
    drives = GetLogicalDrives();
    for (int i = 0; i < 26; ++i)
        if (drives >> i & 1)
        {
            wsprintfA(line, "[-%c-]", 'a' + i);
            SendMessageA(d->list, LB_ADDSTRING, 0, (LPARAM)line);
        }
    if (d->filter_count)
    {
        wsprintfA(line, "%.200s", d->filter_name[d->filter_index]);
        SetWindowTextA(d->filter, line);
    }
}
static void Enter(Dialog *d, const char *folder)
{
    char path[MAX_PATH], full[MAX_PATH], *part = NULL;
    if (folder[0] && folder[1] == ':') lstrcpynA(path, folder, MAX_PATH);
    else
    {
        lstrcpyA(path, d->directory);
        AddSlash(path);
        if (lstrlenA(path) + lstrlenA(folder) >= MAX_PATH) return;
        lstrcatA(path, folder);
    }
    if (!GetFullPathNameA(path, MAX_PATH, full, &part)) return;
    if (GetFileAttributesA(full) == 0xffffffffu || !(GetFileAttributesA(full) & FILE_ATTRIBUTE_DIRECTORY)) return;
    lstrcpyA(d->directory, full);
    Fill(d);
}
/* The name in the field becomes the answer, or the dialog stays open. */
static void Accept(Dialog *d)
{
    OPENFILENAMEA *ofn = d->ofn;
    char name[MAX_PATH], path[MAX_PATH], full[MAX_PATH], *part = NULL;
    DWORD attributes;
    int dot = 0, n;
    GetWindowTextA(d->name, name, MAX_PATH);
    if (!name[0]) return;
    if ((name[0] && name[1] == ':') || name[0] == '\\') lstrcpyA(path, name);
    else
    {
        lstrcpyA(path, d->directory);
        AddSlash(path);
        if (lstrlenA(path) + lstrlenA(name) >= MAX_PATH) return;
        lstrcatA(path, name);
    }
    if (!GetFullPathNameA(path, MAX_PATH, full, &part)) return;
    attributes = GetFileAttributesA(full);
    if (attributes != 0xffffffffu && (attributes & FILE_ATTRIBUTE_DIRECTORY))
    {
        /* A folder typed into the field is entered. */
        lstrcpyA(d->directory, full);
        SetWindowTextA(d->name, "");
        Fill(d);
        return;
    }
    for (const char *p = part ? part : full; *p; ++p) dot = dot || *p == '.';
    if (!dot && ofn->lpstrDefExt && ofn->lpstrDefExt[0] && lstrlenA(full) + lstrlenA(ofn->lpstrDefExt) + 1 < MAX_PATH)
    {
        lstrcatA(full, ".");
        lstrcatA(full, ofn->lpstrDefExt);
        attributes = GetFileAttributesA(full);
    }
    if (attributes == 0xffffffffu && !d->save && (ofn->Flags & OFN_FILEMUSTEXIST))
    {
        MessageBoxA(d->window, "The file cannot be found.", ofn->lpstrTitle ? ofn->lpstrTitle : "Open", MB_OK | MB_ICONEXCLAMATION);
        return;
    }
    if (attributes != 0xffffffffu && d->save && (ofn->Flags & OFN_OVERWRITEPROMPT) &&
        MessageBoxA(d->window, "This file already exists. Replace it?", ofn->lpstrTitle ? ofn->lpstrTitle : "Save As",
                    MB_YESNO | MB_ICONEXCLAMATION) != IDYES)
        return;
    n = lstrlenA(full);
    if (!ofn->lpstrFile || (DWORD)n + 1 > ofn->nMaxFile) return;
    lstrcpyA(ofn->lpstrFile, full);
    ofn->nFileOffset = ofn->nFileExtension = 0;
    for (int i = 0; i < n; ++i)
    {
        if (full[i] == '\\') { ofn->nFileOffset = (WORD)(i + 1); ofn->nFileExtension = 0; }
        else if (full[i] == '.') ofn->nFileExtension = (WORD)(i + 1);
    }
    if (ofn->lpstrFileTitle && ofn->nMaxFileTitle) lstrcpynA(ofn->lpstrFileTitle, full + ofn->nFileOffset, (int)ofn->nMaxFileTitle);
    ofn->nFilterIndex = d->filter_count ? (DWORD)d->filter_index + 1 : 0;
    d->result = 1;
    d->done = 1;
}
/* A line of the list was chosen (twice: entered or accepted). */
static void Choose(Dialog *d, int enter)
{
    char line[MAX_PATH + 8];
    const LRESULT at = SendMessageA(d->list, LB_GETCURSEL, 0, 0);
    int n;
    if (at < 0 || SendMessageA(d->list, LB_GETTEXT, (WPARAM)at, (LPARAM)line) <= 0) return;
    n = lstrlenA(line);
    if (line[0] != '[')
    {
        SetWindowTextA(d->name, line);
        if (enter) Accept(d);
        return;
    }
    if (!enter) return;
    line[n - 1] = 0;
    if (n == 5 && line[1] == '-' && line[3] == '-')
    {
        char root[4] = {line[2], ':', '\\', 0};
        Enter(d, root);
    }
    else Enter(d, line + 1);
}
static LRESULT CALLBACK DialogProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    Dialog *d = (Dialog *)GetWindowLongPtrA(window, GWLP_USERDATA);
    switch (message)
    {
    case WM_COMMAND:
        if (!d) break;
        switch (LOWORD(wParam))
        {
        case IdList:
            if (HIWORD(wParam) == LBN_SELCHANGE) Choose(d, 0);
            if (HIWORD(wParam) == LBN_DBLCLK) Choose(d, 1);
            return 0;
        case IdFilter:
            if (d->filter_count > 1)
            {
                d->filter_index = (d->filter_index + 1) % d->filter_count;
                Fill(d);
            }
            return 0;
        case IdOk:
        {
            /* With a folder or drive selected and no name typed, OK enters it. */
            char name[4];
            if (!GetWindowTextA(d->name, name, sizeof name)) Choose(d, 1);
            else Accept(d);
            return 0;
        }
        case IdCancel:
            d->done = 1;
            return 0;
        }
        break;
    case WM_CLOSE:
        if (d) d->done = 1;
        return 0;
    }
    return DefWindowProcA(window, message, wParam, lParam);
}
static HWND Control(Dialog *d, const char *kind, const char *text, DWORD style, int id, int x, int y, int w, int h)
{
    HWND control = CreateWindowExA(!lstrcmpiA(kind, "EDIT") || !lstrcmpiA(kind, "LISTBOX") ? WS_EX_CLIENTEDGE : 0, kind, text,
                                   WS_CHILD | WS_VISIBLE | style, x, y, w, h, d->window, (HMENU)(INT_PTR)id,
                                   GetModuleHandleA(NULL), NULL);
    if (control) SendMessageA(control, WM_SETFONT, (WPARAM)d->font, 0);
    return control;
}
static BOOL Run(OPENFILENAMEA *ofn, int save)
{
    Dialog dialog, *d = &dialog;
    WNDCLASSA wc;
    RECT work, client;
    MSG message;
    HWND owner;
    int width, height, x, y, row, margin, large;

    if (!ofn || (ofn->Flags & (OFN_ENABLEHOOK | OFN_ENABLETEMPLATE | OFN_ALLOWMULTISELECT))) return FALSE;
    memset(d, 0, sizeof *d);
    d->ofn = ofn;
    d->save = save;
    owner = ofn->hwndOwner;
    /* The filters: pairs of name and patterns, each ended by a zero byte. */
    for (const char *p = ofn->lpstrFilter; p && *p && d->filter_count < FILTERS;)
    {
        d->filter_name[d->filter_count] = p;
        p += lstrlenA(p) + 1;
        if (!*p) break;
        d->filter_pattern[d->filter_count++] = p;
        p += lstrlenA(p) + 1;
    }
    if (ofn->nFilterIndex && (int)ofn->nFilterIndex <= d->filter_count) d->filter_index = (int)ofn->nFilterIndex - 1;
    if (!ofn->lpstrInitialDir || !ofn->lpstrInitialDir[0] || GetFileAttributesA(ofn->lpstrInitialDir) == 0xffffffffu ||
        !GetFullPathNameA(ofn->lpstrInitialDir, MAX_PATH, d->directory, NULL))
        GetCurrentDirectoryA(MAX_PATH, d->directory);

    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = DialogProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(INT_PTR)(COLOR_BTNFACE + 1);
    wc.lpszClassName = dialog_class;
    RegisterClassA(&wc); /* fails the second time, harmlessly */

    SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0);
    /* Upright and large: a phone. Rows a finger can hit. */
    large = work.bottom - work.top > work.right - work.left && work.right - work.left >= 600;
    row = large ? 64 : 24;
    margin = large ? 16 : 8;
    width = large ? work.right - work.left : 440;
    height = large ? work.bottom - work.top : 340;
    if (width > work.right - work.left) width = work.right - work.left;
    if (height > work.bottom - work.top) height = work.bottom - work.top;
    x = work.left + (work.right - work.left - width) / 2;
    y = work.top + (work.bottom - work.top - height) / 2;
    d->font = large ? CreateFontA(-row / 2, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, 0, 0, "MS Sans Serif")
                    : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    d->window = CreateWindowExA(WS_EX_DLGMODALFRAME, dialog_class, ofn->lpstrTitle ? ofn->lpstrTitle : save ? "Save As" : "Open",
                                WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, width, height, owner, NULL, wc.hInstance, NULL);
    if (!d->window) return FALSE;
    SetWindowLongPtrA(d->window, GWLP_USERDATA, (LONG_PTR)d);
    GetClientRect(d->window, &client);
    {
        const int w = client.right - 2 * margin, button = (w - margin) / 2;
        int top = margin;
        d->folder = Control(d, "STATIC", "", SS_LEFTNOWORDWRAP, IdFolder, margin, top, w, row * 2 / 3);
        top += row * 2 / 3 + margin / 2;
        d->list = Control(d, "LISTBOX", "", LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_VSCROLL | WS_TABSTOP, IdList, margin, top, w,
                          client.bottom - top - 3 * row - 4 * margin);
        if (large) SendMessageA(d->list, LB_SETITEMHEIGHT, 0, row);
        top = client.bottom - 3 * row - 3 * margin;
        d->name = Control(d, "EDIT", ofn->lpstrFile ? ofn->lpstrFile : "", ES_AUTOHSCROLL | WS_TABSTOP, IdName, margin, top, w, row);
        top += row + margin;
        d->filter = Control(d, "BUTTON", "", BS_PUSHBUTTON | WS_TABSTOP | (d->filter_count ? 0 : WS_DISABLED), IdFilter, margin, top,
                            w, row);
        top += row + margin;
        Control(d, "BUTTON", save ? "Save" : "Open", BS_DEFPUSHBUTTON | WS_TABSTOP, IdOk, margin, top, button, row);
        Control(d, "BUTTON", "Cancel", BS_PUSHBUTTON | WS_TABSTOP, IdCancel, margin + button + margin, top, button, row);
    }
    Fill(d);
    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(d->window, SW_SHOW);
    SetFocus(d->name);
    while (!d->done && GetMessageA(&message, NULL, 0, 0) > 0)
    {
        if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN && GetParent(message.hwnd) == d->window)
        {
            if (message.hwnd == d->list) Choose(d, 1);
            else Accept(d);
            continue;
        }
        if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE &&
            (message.hwnd == d->window || GetParent(message.hwnd) == d->window))
        {
            d->done = 1;
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    /* The owner is enabled before the dialog goes, so it gets the activation. */
    if (owner) EnableWindow(owner, TRUE);
    DestroyWindow(d->window);
    if (large && d->font) DeleteObject(d->font);
    if (owner) SetForegroundWindow(owner);
    return d->result;
}
BOOL WINAPI GetOpenFileNameA(LPOPENFILENAMEA ofn) { return Run(ofn, 0); }
BOOL WINAPI GetSaveFileNameA(LPOPENFILENAMEA ofn) { return Run(ofn, 1); }
/* A source that declares the two itself, as imports of COMDLG32, finds them
   here: the cell an import is called through. */
void *__imp_GetOpenFileNameA = (void *)GetOpenFileNameA;
void *__imp_GetSaveFileNameA = (void *)GetSaveFileNameA;
