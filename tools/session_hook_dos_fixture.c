/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 *
 * Sets the hooks of HOOKMEAS.DLL (tools/session_hook_dos_dll_fixture.c) for
 * the session, opens an MS-DOS window (C:\COMMAND.COM), its property sheet
 * (from the system menu, else from the Properties button of the toolbar),
 * types into a field of the sheet and into the window, and writes what it
 * did into C:\MEAS\RUN.TXT (and COM1, when it opens). Run the same way in
 * Windows 95 OSR2 and Windows 98 SE; tools/test_session_hooks_dos_host.py.
 */
#include <windows.h>
void *memset(void *p,int c,unsigned n){unsigned char*q=p;while(n--)*q++=(unsigned char)c;return p;}
static HANDLE com;
static void put(const char *s){DWORD n;HANDLE f;WriteFile(com,s,lstrlenA(s),&n,0);
 f=CreateFileA("C:\\MEAS\\RUN.TXT",GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,0,OPEN_ALWAYS,0,0);if(f!=INVALID_HANDLE_VALUE){SetFilePointer(f,0,0,FILE_END);WriteFile(f,s,lstrlenA(s),&n,0);CloseHandle(f);}}
static HWND found; static const char *want_class, *want_title;
static BOOL CALLBACK find(HWND w, LPARAM l){char c[64],t[128];if(!IsWindowVisible(w))return TRUE;GetClassNameA(w,c,64);GetWindowTextA(w,t,128);
 if((!want_class||!lstrcmpiA(c,want_class))&&(!want_title||(t[0]&&lstrcmpiA(t,"Program Manager")&&(!*want_title||!lstrcmpA(t,want_title)))))found=w;return TRUE;}
static HWND wait_for(const char *klass,const char *title,int seconds){int i;for(i=0;i<seconds*5;i++){found=0;want_class=klass;want_title=title;EnumWindows(find,0);if(found)return found;Sleep(200);}return 0;}
static BOOL CALLBACK list(HWND w, LPARAM l){char c[64],t[128],line[300];if(!IsWindowVisible(w))return TRUE;GetClassNameA(w,c,64);GetWindowTextA(w,t,128);wsprintfA(line,"WINDOW %s|%s\r\n",c,t);put(line);return TRUE;}
void start(void)
{
    HMODULE dll; void (*install)(void); HWND tty, sheet; HMENU menu; int i, count; char text[100], line[300];
    static char data[60000]; DWORD n=0; HANDLE f;
    com = CreateFileA("COM1", GENERIC_WRITE, 0, 0, OPEN_EXISTING, 0, 0);
    DeleteFileA("C:\\MEAS\\HOOK.TXT");
    dll = LoadLibraryA("C:\\MEAS\\HOOKMEAS.DLL");
    install = (void (*)(void))GetProcAddress(dll, "Install");
    if (!install) { put("NO DLL\r\nMEASURE_DONE\r\n"); ExitProcess(0); }
    install();
    WinExec("C:\\COMMAND.COM", SW_SHOWNORMAL);
    tty = wait_for("tty", 0, 60);
    wsprintfA(line, "tty=%lx\r\n", (DWORD)tty); put(line);
    Sleep(3000);
    if (tty) {
        menu = GetSystemMenu(tty, FALSE);
        count = GetMenuItemCount(menu);
        for (i = 0; i < count; i++) {
            UINT id = GetMenuItemID(menu, i);
            text[0] = 0; GetMenuStringA(menu, i, text, sizeof text, MF_BYPOSITION);
            wsprintfA(line, "MENU %d id=%x %s\r\n", i, id, text); put(line);
            if (text[0] && (text[0]=='P'||text[1]=='P'||lstrcmpiA(text, "&Properties") == 0) && id != (UINT)-1) {
                if (text[0]=='&'?text[1]=='P':text[0]=='P') { PostMessageA(tty, WM_SYSCOMMAND, id, 0); put("posted properties\r\n"); }
            }
        }
        /* Without Properties in the system menu: the Properties button of
           the toolbar (the sixth control of the strip, at 185,14). */
        if (!wait_for("#32770", "", 5)) {
            put("toolbar properties\r\n");
            PostMessageA(tty, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(185, 14));
            PostMessageA(tty, WM_LBUTTONUP, 0, MAKELPARAM(185, 14));
        }
        sheet = wait_for("#32770", "", 30);
        wsprintfA(line, "sheet=%lx\r\n", (DWORD)sheet); put(line);
        Sleep(5000);
        EnumWindows(list, 0);
        if (sheet) {
            HWND edit = FindWindowExA(sheet, 0, "#32770", 0);
            HWND field = edit ? FindWindowExA(edit, 0, "Edit", 0) : 0;
            wsprintfA(line, "page=%lx field=%lx\r\n", (DWORD)edit, (DWORD)field); put(line);
            if (field) { PostMessageA(field, WM_KEYDOWN, 'A', 0); PostMessageA(field, WM_CHAR, 'a', 0); Sleep(1500); }
            PostMessageA(sheet, WM_COMMAND, IDCANCEL, 0);
        }
        Sleep(2000);
        PostMessageA(tty, WM_CHAR, 'x', 0);
        Sleep(2000);
    }
    f = CreateFileA("C:\\MEAS\\HOOK.TXT", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, 0, OPEN_EXISTING, 0, 0);
    if (f != INVALID_HANDLE_VALUE) { ReadFile(f, data, sizeof data - 1, &n, 0); CloseHandle(f); data[n] = 0; put(data); }
    put("MEASURE_DONE\r\n");
    ExitProcess(0);
}
