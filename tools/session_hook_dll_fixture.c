/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A hook DLL for the whole session, x86: the system loads it into every
   process a window is created in. Its WH_CBT procedure gives each new
   top-level window of class "Notepad" a place of its own, in that program's
   process; its WH_GETMESSAGE procedure counts. Every process the DLL was
   loaded into leaves a file C:\HOOK<process id>.TXT when it sees a window
   created. The frame of the 16-bit File Manager ("WFS_Frame") is placed the
   same way, in the process of that 16-bit program, and subclassed with a
   32-bit procedure when it is activated; the file then also says which CBT
   codes the procedure saw there and how many messages the subclass passed on. tools/test_session_hooks_host.py,
   tools/test_session_hooks_win16_native.py. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U GetCurrentProcessId(void);
API U CreateFileA(const char *, U, U, U, U, U, U);
API U WriteFile(U, const void *, U, U *, U);
API U CloseHandle(U);
API int lstrlenA(const char *);
API int lstrcmpA(const char *, const char *);
API int GetClassNameA(U, char *, int);
API int CallNextHookEx(U, int, U, int);
API U GetWindowLongA(U, int);
API U SetWindowLongA(U, int, U);
API U CallWindowProcA(U, U, U, U, U);
__declspec(dllimport) int __cdecl wsprintfA(char *, const char *, ...);

static U messages, created, codes, target, original, passed, minmax, sized;
static void Note(const char *class_name)
{
    char path[64], text[320];
    U written = 0, file;
    wsprintfA(path, "C:\\HOOK%u.TXT", GetCurrentProcessId());
    wsprintfA(text, "process=%u\r\nclass=%s\r\ncreated=%u\r\nmessages=%u\r\ncodes=%u\r\nsubclassed=%u\r\npassed=%u\r\nminmax=%u\r\nsized=%u\r\n",
              GetCurrentProcessId(), class_name, created, messages, codes, original != 0, passed, minmax, sized);
    file = CreateFileA(path, 0x40000000u, 0, 0, 2, 0x80, 0);
    WriteFile(file, text, (U)lstrlenA(text), &written, 0);
    CloseHandle(file);
}
/* The 32-bit procedure in front of the 16-bit one of File Manager. */
static U __stdcall Subclass(U window, U message, U wparam, U lparam)
{
    ++passed;
    if (message == 0x24) ++minmax;                       /* WM_GETMINMAXINFO: a MINMAXINFO of this side */
    if (message == 0x05 && (lparam & 0xffff) > 0) ++sized; /* WM_SIZE */
    return CallWindowProcA(original, window, message, wparam, lparam);
}
__declspec(dllexport) int __stdcall SessionCbt(int code, U wp, int lp)
{
    if (code >= 0 && code < 32) codes |= 1u << code;
    if (code == 5 && wp == target && !original) /* HCBT_ACTIVATE */
    {
        original = GetWindowLongA(target, -4);
        SetWindowLongA(target, -4, (U)Subclass);
    }
    if (code == 4 && wp == target) /* HCBT_DESTROYWND: what this process saw */
    {
        SetWindowLongA(target, -4, original);
        Note("WFS_Frame");
        target = 0;
    }
    if (code == 3) /* HCBT_CREATEWND: lp -> { CREATESTRUCT *, HWND after } */
    {
        U *create = *(U **)lp; /* params, instance, menu, parent, cy, cx, y, x, style, name, class, exstyle */
        char name[64];
        GetClassNameA(wp, name, sizeof name);
        if (!create[3] && !lstrcmpA(name, "Notepad"))
        {
            ++created;
            create[7] = 33;  /* x */
            create[6] = 44;  /* y */
            create[5] = 555; /* cx */
            create[4] = 333; /* cy */
            Note(name);
        }
        if (!create[3] && !lstrcmpA(name, "WFS_Frame"))
        {
            ++created;
            target = wp;
            create[7] = 21;  /* x */
            create[6] = 32;  /* y */
            create[5] = 543; /* cx */
            create[4] = 321; /* cy */
            Note(name);
        }
    }
    return CallNextHookEx(0, code, wp, lp);
}
__declspec(dllexport) int __stdcall SessionMessages(int code, U wp, int lp)
{
    if (code >= 0) ++messages;
    return CallNextHookEx(0, code, wp, lp);
}
int __stdcall DllEntry(U instance, U reason, U reserved)
{
    (void)instance; (void)reason; (void)reserved;
    return 1;
}
