// SPDX-License-Identifier: GPL-2.0-or-later
// A window that is dragged while its program sends WM_CANCELMODE to other
// windows (tools/test_cancel_mode_drag_host.py).
//
// Window A ("Drag me", 100,100, 300 by 200) has a timer of 100 ms. Each tick
// disables and enables a push button in the hidden window B, as NEO98HW's
// status refresh did with the buttons of its hidden wireless window
// (EnableWindow sends WM_CANCELMODE, USER), and sends WM_CANCELMODE to
// B itself, which leaves it to DefWindowProc. Neither window
// holds the capture of A's move loop, so under Win98 the drag goes on.
typedef unsigned U;
typedef int I;
typedef void *H;
typedef const char *S;
typedef I(__stdcall *PROC)(H, U, U, I);
typedef struct { U style; PROC proc; I a, b; H instance, icon, cursor, brush; S menu, name; } CLASS;
typedef struct { H hwnd; U message, wparam; I lparam; U time; I x, y; } MSG;
#define API __declspec(dllimport)
API H __stdcall GetModuleHandleA(S);
API void __stdcall ExitProcess(U);
API unsigned short __stdcall RegisterClassA(CLASS *);
API H __stdcall CreateWindowExA(U, S, S, U, I, I, I, I, H, H, H, void *);
API I __stdcall DefWindowProcA(H, U, U, I);
API I __stdcall GetMessageA(MSG *, H, U, U);
API I __stdcall TranslateMessage(MSG *);
API I __stdcall DispatchMessageA(MSG *);
API U __stdcall SetTimer(H, U, U, void *);
API I __stdcall EnableWindow(H, I);
API I __stdcall SendMessageA(H, U, U, I);
API void __stdcall PostQuitMessage(I);
API H __stdcall LoadCursorA(H, S);

static H hidden, button;
static U ticks;

static I __stdcall proc(H hwnd, U msg, U wp, I lp)
{
    if (msg == 0x113)
    {
        EnableWindow(button, 0);
        EnableWindow(button, 1);
        SendMessageA(hidden, 0x1f, 0, 0); // WM_CANCELMODE
        ++ticks;
        return 0;
    }
    if (msg == 2) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

void mainCRTStartup(void)
{
    H instance = GetModuleHandleA(0);
    CLASS c = {0, proc, 0, 0, instance, 0, LoadCursorA(0, (S)32512), (H)16, 0, "CancelModeDrag"};
    RegisterClassA(&c);
    hidden = CreateWindowExA(0, "CancelModeDrag", "Hidden", 0x00c80000, 0, 0, 200, 100, 0, 0, instance, 0);
    button = CreateWindowExA(0, "BUTTON", "Connect", 0x50010000, 10, 10, 80, 23, hidden, (H)1, instance, 0);
    H window = CreateWindowExA(0, "CancelModeDrag", "Drag me", 0x10cf0000, 100, 100, 300, 200, 0, 0, instance, 0);
    SetTimer(window, 1, 100, 0);
    MSG m;
    while (GetMessageA(&m, 0, 0, 0) > 0) { TranslateMessage(&m); DispatchMessageA(&m); }
    ExitProcess(0);
}
