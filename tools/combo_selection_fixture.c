/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The selection of a combo box's edit field without the focus, for
   tools/test_combo_selection_host.py. A popup window "ComboSel" at 0,0 of 400
   by 200 pixels has a drop-down combo box at 10,10 (200 wide) with the text
   "Selected text" and a button "Other" at 10,60. The combo box gets the
   focus, which selects its text; a second later the button gets it. The
   window writes the style of the combo's edit field and its selection after
   the move into C:\COMBOSEL.TXT. */
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
API U SetTimer(U, U, U, U);
API int KillTimer(U, U);
API U GetWindow(U, U);
API U GetWindowLongA(U, int);
API U GetStockObject(int);
API U LoadCursorA(U, U);
API U CreateFileA(const char *, U, U, U, U, U, U);
API U WriteFile(U, const void *, U, U *, U);
API U CloseHandle(U);
API int lstrlenA(const char *);
__declspec(dllimport) int __cdecl wsprintfA(char *, const char *, ...);

struct Class { U style, proc; int class_extra, window_extra; U instance, icon, cursor, brush; const char *menu, *name; };
static U combo, other;

static U __stdcall Procedure(U window, U message, U wparam, U lparam)
{
    if (message == 0x113) /* WM_TIMER */
    {
        KillTimer(window, 1);
        SetFocus(other);
        {
            char text[100];
            U start = 0, end = 0, written = 0, file;
            const U edit = GetWindow(combo, 5); /* GW_CHILD */
            SendMessageA(edit, 0xb0, (U)&start, (U)&end); /* EM_GETSEL */
            wsprintfA(text, "edit style %08x selection %u %u\r\n", GetWindowLongA(edit, -16), start, end);
            file = CreateFileA("C:\\COMBOSEL.TXT", 0x40000000u, 0, 0, 2, 0x80, 0);
            WriteFile(file, text, (U)lstrlenA(text), &written, 0);
            CloseHandle(file);
        }
        return 0;
    }
    return DefWindowProcA(window, message, wparam, lparam);
}
void __stdcall mainCRTStartup(void)
{
    struct Class c = {0, (U)Procedure, 0, 0, GetModuleHandleA(0), 0, LoadCursorA(0, 0x7f00), 16, 0, "ComboSel"};
    U message[7], window;
    const U font = GetStockObject(17); /* DEFAULT_GUI_FONT */
    RegisterClassA(&c);
    window = CreateWindowExA(0, "ComboSel", "ComboSel", 0x90000000u, 0, 0, 400, 200, 0, 0, c.instance, 0);
    combo = CreateWindowExA(0, "COMBOBOX", "", 0x50010042u, 10, 10, 200, 120, window, (U)100, c.instance, 0); /* CBS_DROPDOWN | CBS_AUTOHSCROLL */
    other = CreateWindowExA(0, "BUTTON", "Other", 0x50010000u, 10, 60, 100, 24, window, (U)101, c.instance, 0);
    SendMessageA(combo, 0x30, font, 0);
    SendMessageA(other, 0x30, font, 0);
    SendMessageA(combo, 0xc, 0, (U)"Selected text"); /* WM_SETTEXT */
    SetFocus(combo);
    SetTimer(window, 1, 1000, 0);
    while (GetMessageA(message, 0, 0, 0) > 0)
    {
        TranslateMessage(message);
        DispatchMessageA(message);
    }
    ExitProcess(0);
}
