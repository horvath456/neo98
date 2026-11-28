/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Check boxes and radio buttons of every kind that draws a cell, for
   tools/test_check_cell_host.py: a popup window "CheckCell" at 0,0 of 360 by
   300 pixels with, 50 pixels apart from y 10 on and 300 by 40 pixels each, a
   checked check box (with the focus), a checked radio button, a three-state
   box in its third state, a disabled checked box and a box with
   BS_LEFTTEXT. They use the dialog font, DEFAULT_GUI_FONT. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U GetModuleHandleA(const char *);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
API U DefWindowProcA(U, U, U, U);
API int GetMessageA(U *, U, U, U);
API int TranslateMessage(const U *);
API U DispatchMessageA(const U *);
API U SendMessageA(U, U, U, U);
API U SetFocus(U);
API int EnableWindow(U, int);
API U GetStockObject(int);
API U LoadCursorA(U, U);

struct Class { U style, proc; int class_extra, window_extra; U instance, icon, cursor, brush; const char *menu, *name; };

static U __stdcall Procedure(U window, U message, U wparam, U lparam)
{
    return DefWindowProcA(window, message, wparam, lparam);
}
static U Button(U parent, const char *text, U style, int index, U check)
{
    const U font = GetStockObject(17); /* DEFAULT_GUI_FONT */
    const U button = CreateWindowExA(0, "BUTTON", text, 0x50010000u | style, 10, 10 + 50 * index, 300, 40, parent,
                                     (U)(100 + index), GetModuleHandleA(0), 0);
    SendMessageA(button, 0x30, font, 0);    /* WM_SETFONT */
    SendMessageA(button, 0xf1, check, 0);   /* BM_SETCHECK */
    return button;
}
void __stdcall mainCRTStartup(void)
{
    struct Class c = {0, (U)Procedure, 0, 0, GetModuleHandleA(0), 0, LoadCursorA(0, 0x7f00), 16, 0, "CheckCell"};
    U message[7], window, first, disabled;
    RegisterClassA(&c);
    window = CreateWindowExA(0, "CheckCell", "CheckCell", 0x90000000u, 0, 0, 360, 300, 0, 0, c.instance, 0);
    first = Button(window, "Check box", 3, 0, 1);         /* BS_AUTOCHECKBOX */
    Button(window, "Radio button", 9, 1, 1);              /* BS_AUTORADIOBUTTON */
    Button(window, "Three states", 6, 2, 2);              /* BS_AUTO3STATE, indeterminate */
    disabled = Button(window, "Disabled", 3, 3, 1);
    EnableWindow(disabled, 0);
    Button(window, "Text left", 3 | 0x20, 4, 1);          /* BS_LEFTTEXT */
    SetFocus(first);
    while (GetMessageA(message, 0, 0, 0) > 0)
    {
        TranslateMessage(message);
        DispatchMessageA(message);
    }
    ExitProcess(0);
}
