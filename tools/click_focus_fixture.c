/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A click inside the active window leaves the focus where it is.

   One top-level window at 100,100 of 400x300 pixels: a plain window, or with
   the argument "dialog" a dialog of its own template. In both an EDIT with
   "abc def" and the caret after "abc" (client 10,10, 200x24) has the focus,
   and a STATIC (client 10,60, 100x20) and the empty rest of the client area
   take no focus. A second window, Other (600,100, 200x120), is made first.
   tools/test_click_focus_host.py clicks the empty client area, the static
   and the caption, then Other, and the empty client area again: a real
   activation.

   A second after the start the counts begin from zero, with the focus on the
   edit and the caret after "abc" again. Every fifth of a second
   C:\FOCUS.TXT gets ten ints: whether this is the
   dialog, the presses of the left button the window saw in its client area
   and on its frame, the WM_SETFOCUS and WM_ACTIVATE (active) the window got,
   the WM_KILLFOCUS and WM_SETFOCUS the edit got, whether the edit has the
   focus, and the start and end of its selection; then the same ten as they
   were when the window lost the activation. */
#include <windows.h>

static int results[20];
static HWND window, edit;
static WNDPROC edit_procedure;

static LRESULT CALLBACK EditProcedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
{
    if (message == WM_KILLFOCUS) ++results[5];
    if (message == WM_SETFOCUS) ++results[6];
    return CallWindowProcA(edit_procedure, hwnd, message, wp, lp);
}

static void Sample(int *out)
{
    DWORD start = 0, end = 0;
    SendMessageA(edit, EM_GETSEL, (WPARAM)&start, (LPARAM)&end);
    out[7] = GetFocus() == edit;
    out[8] = (int)start;
    out[9] = (int)end;
}

static void Note(UINT message, WPARAM wp)
{
    /* Deactivation comes before the focus leaves: the state after the
       clicks inside the active window. */
    if (message == WM_ACTIVATE && !LOWORD(wp) && edit)
    {
        Sample(results);
        for (int i = 0; i < 10; ++i) results[10 + i] = results[i];
    }
    if (message == WM_LBUTTONDOWN) ++results[1];
    if (message == WM_NCLBUTTONDOWN) ++results[2];
    if (message == WM_SETFOCUS) ++results[3];
    if (message == WM_ACTIVATE && LOWORD(wp)) ++results[4];
}

static void Write(void)
{
    static int ticks;
    DWORD written = 0;
    if (++ticks == 5)
    {
        SetFocus(edit);
        SendMessageA(edit, EM_SETSEL, 3, 3);
        for (int i = 1; i < 7; ++i) results[i] = 0;
    }
    Sample(results);
    HANDLE file = CreateFileA("C:\\FOCUS.TXT", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    WriteFile(file, results, sizeof results, &written, NULL);
    CloseHandle(file);
}

/* The controls in pixels, wherever the dialog units put them, and the focus
   on the edit with the caret after "abc". */
static void Arrange(HWND hwnd, HWND child_edit, HWND label)
{
    window = hwnd;
    edit = child_edit;
    MoveWindow(hwnd, 100, 100, 400, 300, TRUE);
    MoveWindow(edit, 10, 10, 200, 24, TRUE);
    MoveWindow(label, 10, 60, 100, 20, TRUE);
    edit_procedure = (WNDPROC)SetWindowLongA(edit, GWL_WNDPROC, (LONG)EditProcedure);
    SetFocus(edit);
    SendMessageA(edit, EM_SETSEL, 3, 3);
    SetTimer(hwnd, 1, 200, NULL);
}

static LRESULT CALLBACK WindowProcedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
{
    Note(message, wp);
    if (message == WM_TIMER) Write();
    if (message == WM_DESTROY) PostQuitMessage(0);
    return DefWindowProcA(hwnd, message, wp, lp);
}

static BOOL CALLBACK DialogProcedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp)
{
    Note(message, wp);
    if (message == WM_INITDIALOG)
    {
        Arrange(hwnd, GetDlgItem(hwnd, 1), GetDlgItem(hwnd, 2));
        return FALSE; /* the focus is set */
    }
    if (message == WM_TIMER) Write();
    if (message == WM_CLOSE) DestroyWindow(hwnd);
    if (message == WM_DESTROY) PostQuitMessage(0);
    return FALSE;
}

/* A DLGTEMPLATE with an EDIT (1) and a STATIC (2), words in the order of the
   template. */
static WORD template_words[128];
static unsigned at;
static void Word(WORD w) { template_words[at++] = w; }
static void Long(DWORD d) { Word(LOWORD(d)); Word(HIWORD(d)); }
static void Text(const char *s) { do Word((WORD)(unsigned char)*s); while (*s++); }
static void Align(void) { if (at & 1) Word(0); }

void mainCRTStartup(void)
{
    MSG message;
    HINSTANCE instance = GetModuleHandleA(NULL);
    const char *line = GetCommandLineA();
    int dialog = 0;
    for (const char *p = line; *p; ++p)
        if (p[0] == 'd' && p[1] == 'i' && p[2] == 'a' && p[3] == 'l' && p[4] == 'o' && p[5] == 'g') dialog = 1;
    results[0] = dialog;
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "ClickFocusOther";
    RegisterClassA(&wc);
    CreateWindowA("ClickFocusOther", "Other", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 600, 100, 200, 120, NULL, NULL,
                  instance, NULL);
    if (dialog)
    {
        Long(WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE); Long(0); Word(2);
        Word(50); Word(50); Word(200); Word(150);
        Word(0); Word(0); Text("Click focus dialog");
        Align(); Long(WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL); Long(0);
        Word(5); Word(5); Word(100); Word(12); Word(1); Word(0xffff); Word(0x81); Text("abc def"); Word(0);
        Align(); Long(WS_CHILD | WS_VISIBLE); Long(0);
        Word(5); Word(30); Word(50); Word(10); Word(2); Word(0xffff); Word(0x82); Text("Label"); Word(0);
        CreateDialogIndirectParamA(instance, (LPCDLGTEMPLATEA)template_words, NULL, DialogProcedure, 0);
    }
    else
    {
        wc.lpfnWndProc = WindowProcedure;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = "ClickFocus";
        RegisterClassA(&wc);
        HWND hwnd = CreateWindowA("ClickFocus", "Click focus", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 400, 300,
                                  NULL, NULL, instance, NULL);
        HWND child_edit = CreateWindowA("EDIT", "abc def", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                                        0, 0, 10, 10, hwnd, (HMENU)1, instance, NULL);
        HWND label = CreateWindowA("STATIC", "Label", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, hwnd, (HMENU)2,
                                   instance, NULL);
        Arrange(hwnd, child_edit, label);
    }
    while (GetMessageA(&message, NULL, 0, 0) > 0)
    {
        if (dialog && IsDialogMessageA(window, &message)) continue;
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    ExitProcess(0);
}
