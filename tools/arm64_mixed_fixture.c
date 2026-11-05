/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* A mixed session: this ARM64 program on one desktop with two x86 programs.
   It starts tools/arm64_mixed_peer_fixture.c (x86), which sends messages to
   this window, and the original Notepad, whose edit control it fills, reads
   and pastes into through the clipboard. The results go into a file beside
   the program. tools/test_arm64_mixed_native.py, docs/ARM64-APPS.md. */
#include <windows.h>

static HWND main_window;
static PROCESS_INFORMATION notepad, peer;
static int peer_number, peer_copydata, peer_mask = -1, notepad_done, ticks;
static int notepad_set, notepad_paste, notepad_copy, notepad_title, notepad_prop, peer_prop;
static char directory[MAX_PATH];

static void Report(void)
{
    char path[MAX_PATH], text[520];
    DWORD written, exit_code = 0;
    GetExitCodeProcess(peer.hProcess, &exit_code);
    wsprintfA(text,
              "peer_started=%d\r\npeer_mask=%d\r\npeer_exit=%u\r\npeer_number=%d\r\npeer_copydata=%d\r\n"
              "notepad_started=%d\r\nnotepad_title=%d\r\nnotepad_set=%d\r\nnotepad_paste=%d\r\nnotepad_copy=%d\r\n"
              "notepad_prop=%d\r\npeer_prop=%d\r\n",
              peer.hProcess != NULL, peer_mask, exit_code, peer_number, peer_copydata, notepad.hProcess != NULL,
              notepad_title, notepad_set, notepad_paste, notepad_copy, notepad_prop, peer_prop);
    wsprintfA(path, "%sMIXED.TXT", directory);
    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    WriteFile(file, text, (DWORD)lstrlenA(text), &written, NULL);
    CloseHandle(file);
}

/* Fill, read and paste into the edit control of the x86 Notepad. */
static void DriveNotepad(void)
{
    char text[128];
    HWND frame = FindWindowA("Notepad", NULL);
    if (!frame) return;
    HWND edit = FindWindowExA(frame, NULL, "Edit", NULL);
    if (!edit) return;
    notepad_done = 1;
    GetWindowTextA(frame, text, sizeof text);
    notepad_title = lstrlenA(text) > 7 && !lstrcmpA(text + lstrlenA(text) - 7, "Notepad");
    /* A property on a window of the x86 process: USER keeps it at the window. */
    notepad_prop = SetPropA(frame, "Arm64Parked", (HANDLE)(INT_PTR)1) && GetPropA(frame, "Arm64Parked") == (HANDLE)(INT_PTR)1 &&
                   RemovePropA(frame, "Arm64Parked") == (HANDLE)(INT_PTR)1 && !GetPropA(frame, "Arm64Parked");
    SendMessageA(edit, WM_SETTEXT, 0, (LPARAM)"from arm64");
    text[0] = 0;
    /* The length crosses the processes; the text itself is read back through
       the clipboard below (a WM_GETTEXT sent to another process is not
       carried by this runtime, between x86 processes either). */
    notepad_set = SendMessageA(edit, WM_GETTEXTLENGTH, 0, 0) == 10;
    /* Text of this process on the clipboard, pasted by the x86 control. */
    if (OpenClipboard(main_window))
    {
        EmptyClipboard();
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, 16);
        lstrcpyA((char *)GlobalLock(memory), " pasted");
        GlobalUnlock(memory);
        SetClipboardData(CF_TEXT, memory);
        CloseClipboard();
    }
    SendMessageA(edit, EM_SETSEL, 10, 10);
    SendMessageA(edit, WM_PASTE, 0, 0);
    notepad_paste = SendMessageA(edit, WM_GETTEXTLENGTH, 0, 0) == 17;
    /* And back: the x86 control copies everything, this process reads the
       clipboard. */
    SendMessageA(edit, EM_SETSEL, 0, -1);
    SendMessageA(edit, WM_COPY, 0, 0);
    if (OpenClipboard(main_window))
    {
        HANDLE data = GetClipboardData(CF_TEXT);
        const char *copied = data ? (const char *)GlobalLock(data) : NULL;
        notepad_copy = copied && !lstrcmpA(copied, "from arm64 pasted");
        if (data) GlobalUnlock(data);
        CloseClipboard();
    }
}

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_USER + 1:
        peer_number = wParam == 0x1234 && lParam == -9;
        return -3;
    case WM_USER + 2:
        SetWindowTextA(hwnd, "set by arm64");
        return 77;
    case WM_COPYDATA:
    {
        COPYDATASTRUCT *data = (COPYDATASTRUCT *)lParam;
        peer_copydata = data->dwData == 4711 && data->cbData == 9 && !lstrcmpA((const char *)data->lpData, "x86 data");
        return 1;
    }
    case WM_USER + 9:
        peer_mask = (int)wParam;
        /* The property the x86 process set on this window. */
        peer_prop = GetPropA(hwnd, "x86 prop") == (HANDLE)(INT_PTR)0x55;
        return 0;
    case WM_TIMER:
        ++ticks;
        if (!notepad_done) DriveNotepad();
        if ((peer_mask >= 0 && notepad_done) || ticks > 150)
        {
            KillTimer(hwnd, 1);
            WaitForSingleObject(peer.hProcess, 2000);
            Report();
            if (notepad.hProcess) TerminateProcess(notepad.hProcess, 0);
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, message, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show)
{
    WNDCLASSA wc;
    STARTUPINFOA startup;
    MSG message;
    char path[MAX_PATH];
    (void)previous; (void)command; (void)show;
    GetModuleFileNameA(NULL, directory, sizeof directory);
    for (int i = lstrlenA(directory); i > 0 && directory[i - 1] != '\\'; --i) directory[i - 1] = 0;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "Arm64Mixed";
    if (!RegisterClassA(&wc)) return 1;
    main_window = CreateWindowA("Arm64Mixed", "arm64 mixed", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 300, 40, 240, 120, NULL,
                                NULL, instance, NULL);
    if (!main_window) return 2;
    SetPropA(main_window, "arm64 prop", (HANDLE)(INT_PTR)0x66);
    memset(&startup, 0, sizeof startup);
    startup.cb = sizeof startup;
    wsprintfA(path, "%sPEER.EXE", directory);
    CreateProcessA(path, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &peer);
    lstrcpyA(path, "C:\\WINDOWS\\NOTEPAD.EXE");
    CreateProcessA(NULL, path, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &notepad);
    SetTimer(main_window, 1, 100, NULL);
    while (GetMessageA(&message, NULL, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    return 0;
}
