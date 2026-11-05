/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Where a message box stands among the topmost windows of another program. A message box is a dialog; USER gives it WS_EX_TOPMOST only for
   MB_SYSTEMMODAL without MB_TASKMODAL ( picks the dialog style 0347h
   with DS_SYSMODAL, turns DS_SYSMODAL into WS_EX_TOPMOST), and a
   window whose owner is topmost becomes topmost. Any other
   new window goes behind the topmost ones.
   Without an argument the program shows a topmost window T over the middle
   of the screen and starts itself once per case; the other process shows
     plain MessageBox(NULL,..., MB_OK)
     task MessageBox(NULL,..., MB_TASKMODAL)
     system MessageBox(NULL,..., MB_SYSTEMMODAL)
     both MessageBox(NULL,..., MB_SYSTEMMODAL | MB_TASKMODAL)
     owned MessageBox(O,...), O an ordinary window of that process
     ownedtop MessageBox(O,...), O a topmost window
     maximized no box: an ordinary window, maximized over the whole screen
               (what the box of File Manager previously used)
   With the argument "hold" the first process only keeps T and the maximized
   window on the screen for eight seconds, for a picture.
   The first process finds the box, writes down whether it is topmost,
   whether it stands before T in the order of the top-level windows and
   whether WindowFromPoint finds it (or its button) at its centre, where T
   lies too, and closes it. D:\MSGORD.TXT. tools/test_msgbox_order_native.py. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API void Sleep(U);
API char *GetCommandLineA(void);
API U WinExec(const char *, U);
API U CreateFileA(const char *, U, U, U, U, U, U);
API U WriteFile(U, const void *, U, U *, U);
API U CloseHandle(U);
API int lstrlenA(const char *);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
API U DefWindowProcA(U, U, U, U);
API int PeekMessageA(U *, U, U, U, U);
API int DispatchMessageA(const U *);
API int PostMessageA(U, U, U, U);
API U FindWindowA(const char *, const char *);
API U GetWindow(U, U);
API U GetWindowLongA(U, int);
API int IsWindow(U);
API int IsWindowVisible(U);
API int MessageBoxA(U, const char *, const char *, U);
API int ShowWindow(U, int);
API int GetWindowRect(U, int *);
API U WindowFromPoint(int, int);
API U GetParent(U);
__declspec(dllimport) int __cdecl wsprintfA(char *, const char *, ...);

struct Class { U style, proc; int class_extra, window_extra; U instance, icon, cursor, brush; const char *menu, *name; };
static const char *const cases[] = {"plain", "task", "system", "both", "owned", "ownedtop", "maximized"};
static const U styles[] = {0, 0x2000, 0x1000, 0x3000, 0, 0, 0};
enum { Cases = 7 };
static char text[1200];

static void Pump(void)
{
    U message[7];
    while (PeekMessageA(message, 0, 0, 0, 1)) DispatchMessageA(message);
}
static int Has(const char *line, const char *word)
{
    for (; *line; ++line)
    {
        int i = 0;
        while (word[i] && line[i] == word[i]) ++i;
        if (!word[i] && (line[i] == 0 || line[i] == ' ')) return 1;
    }
    return 0;
}
void mainCRTStartup(void)
{
    struct Class klass = {0};
    const char *line = GetCommandLineA();
    char title[40], command[60];
    U t, written = 0, file;
    klass.proc = (U)DefWindowProcA;
    klass.instance = 0x400000;
    klass.brush = 6;
    klass.name = "Neo98MsgOrder";
    RegisterClassA(&klass);
    for (int i = Cases - 1; i >= 0; --i) /* "ownedtop" before "owned" */
        if (Has(line, cases[i]))
        {
            U owner = 0;
            wsprintfA(title, "Box %s", cases[i]);
            if (i == 6)
            {
                U window = CreateWindowExA(0, klass.name, title, 0x10cf0000u, 100, 100, 300, 200, 0, 0, 0x400000, 0);
                ShowWindow(window, 3);
                while (IsWindow(window)) { Pump(); Sleep(50); }
                ExitProcess(0);
            }
            if (i >= 4) owner = CreateWindowExA(i == 5 ? 8 : 0, klass.name, "O", 0x10c80000u, 500, 20, 200, 120, 0, 0, 0x400000, 0);
            Pump();
            MessageBoxA(owner, "text", title, styles[i]);
            ExitProcess(0);
        }
    /* WS_EX_TOPMOST | WS_EX_TOOLWINDOW, a pop-up with a border over the place of a centred box,
       in the colour of the selection so that a picture tells it from the white windows. */
    klass.brush = 14; /* COLOR_HIGHLIGHT + 1 */
    klass.name = "Neo98MsgOrderTop";
    RegisterClassA(&klass);
    t = CreateWindowExA(0x88, klass.name, "T", 0x90800000u, 150, 100, 500, 400, 0, 0, 0x400000, 0);
    Pump();
    if (Has(line, "hold"))
    {
        /* For a picture: T and the maximized window of the other process, for a while. */
        U window = 0;
        WinExec("D:\\MSGORD.EXE maximized", 1);
        for (int n = 0; n < 300 && !(window && IsWindowVisible(window)); ++n)
        {
            Pump();
            Sleep(50);
            window = FindWindowA(0, "Box maximized");
        }
        for (int n = 0; n < 160; ++n) { Pump(); Sleep(50); }
        if (window) PostMessageA(window, 0x10, 0, 0);
        for (int n = 0; n < 100 && window && IsWindow(window); ++n) { Pump(); Sleep(50); }
        ExitProcess(0);
    }
    for (int i = 0; i < Cases; ++i)
    {
        U box = 0;
        int above = -1, topmost = -1, hit = -1;
        wsprintfA(title, "Box %s", cases[i]);
        wsprintfA(command, "D:\\MSGORD.EXE %s", cases[i]);
        WinExec(command, 1);
        for (int n = 0; n < 300 && !(box && IsWindowVisible(box)); ++n)
        {
            Pump();
            Sleep(50);
            box = FindWindowA(0, title);
        }
        Pump();
        Sleep(300);
        Pump();
        if (box)
        {
            above = 0;
            for (U h = GetWindow(t, 0); h && h != t; h = GetWindow(h, 2)) /* GW_HWNDFIRST, GW_HWNDNEXT */
                if (h == box) above = 1;
            topmost = (GetWindowLongA(box, -20) & 8) != 0;
            {
                int r[4];
                U at;
                GetWindowRect(box, r);
                at = WindowFromPoint((r[0] + r[2]) / 2, (r[1] + r[3]) / 2);
                hit = at == box || (at && GetParent(at) == box);
            }
            /* WM_COMMAND: the one button of an MB_OK box carries IDCANCEL; WM_CLOSE for the window. */
            PostMessageA(box, i == 6 ? 0x10 : 0x111, i == 6 ? 0 : 2, 0);
        }
        for (int n = 0; n < 200 && box && IsWindow(box); ++n) { Pump(); Sleep(50); }
        wsprintfA(text + lstrlenA(text), "%s found=%d topmost=%d before_T=%d hit=%d closed=%d\r\n", cases[i], box != 0, topmost,
                  above, hit, !box || !IsWindow(box));
        Sleep(300);
    }
    file = CreateFileA("D:\\MSGORD.TXT", 0x40000000u, 0, 0, 2, 0x80, 0);
    WriteFile(file, text, (U)lstrlenA(text), &written, 0);
    CloseHandle(file);
    ExitProcess(0);
}
