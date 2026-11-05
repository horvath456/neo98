/* SPDX-License-Identifier: GPL-2.0-or-later */
/* WM_MOUSEACTIVATE at a 16-bit window. A WH_CBT hook for the
   session out of this 32-bit DLL runs in the process of the 16-bit File
   Manager: it places the frame "WFS_Frame" at 21,32 with 543 by 321
   before it exists and puts a 32-bit procedure in front of its 16-bit one
   when it is activated. That procedure writes down every WM_MOUSEACTIVATE
   that reaches the frame: whether wParam names the frame, lParam, and what
   the 16-bit procedure (DefFrameProc, USER) answered. A press in the
   client area arrives there only because each 16-bit child passes the
   question to its parent. C:\MA16.TXT is written when the frame goes.
   tools/test_mouse_activate_cases_native.py. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
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

static U target, original;
static char text[1200];

static U __stdcall Subclass(U window, U message, U wparam, U lparam)
{
    const U answer = CallWindowProcA(original, window, message, wparam, lparam);
    if (message == 0x21 && lstrlenA(text) < 1100) /* WM_MOUSEACTIVATE */
        wsprintfA(text + lstrlenA(text), "MA top=%d lp=%08x answer=%u\r\n", wparam == window, lparam, answer);
    return answer;
}
__declspec(dllexport) int __stdcall MouseActivateCbt(int code, U wp, int lp)
{
    if (code == 5 && wp == target && !original) /* HCBT_ACTIVATE */
    {
        original = GetWindowLongA(target, -4);
        SetWindowLongA(target, -4, (U)Subclass);
    }
    if (code == 4 && wp == target) /* HCBT_DESTROYWND */
    {
        U written = 0, file = CreateFileA("C:\\MA16.TXT", 0x40000000u, 0, 0, 2, 0x80, 0);
        SetWindowLongA(target, -4, original);
        WriteFile(file, text, (U)lstrlenA(text), &written, 0);
        CloseHandle(file);
        target = 0;
    }
    if (code == 3) /* HCBT_CREATEWND: lp -> { CREATESTRUCT *, HWND after } */
    {
        U *create = *(U **)lp; /* params, instance, menu, parent, cy, cx, y, x, style, name, class, exstyle */
        char name[64];
        GetClassNameA(wp, name, sizeof name);
        if (!create[3] && !lstrcmpA(name, "WFS_Frame"))
        {
            target = wp;
            create[7] = 21;  /* x */
            create[6] = 32;  /* y */
            create[5] = 543; /* cx */
            create[4] = 321; /* cy */
        }
    }
    return CallNextHookEx(0, code, wp, lp);
}
int __stdcall DllEntry(U instance, U reason, U reserved)
{
    (void)instance; (void)reason; (void)reserved;
    return 1;
}
