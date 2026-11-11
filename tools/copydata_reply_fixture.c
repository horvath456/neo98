// SPDX-License-Identifier: GPL-2.0-or-later
// WM_COPYDATA between two processes: the copy in the receiver stays until
// its window procedure returns, also after ReplyMessage has let the sender
// go on. TAPISRV answers a request that way and reads dwData afterwards.
// Started without arguments this is the receiver; it starts itself again
// with "S" as the sender. The sender sends the block, then a WM_SETTEXT whose
// string wants memory of the same size in the receiver. The exit code of the
// receiver names the first step that failed.
typedef unsigned U;
typedef struct { int x, y; } POINT;
typedef struct { U hwnd, message, wParam, lParam, time; POINT pt; } MSG;
typedef struct { U style; void *proc; int cls, wnd; U instance, icon, cursor, brush; const char *menu, *name; } WNDCLASS;
typedef struct { U dwData, cbData; void *lpData; } COPYDATASTRUCT;
typedef struct { U cb; char *reserved, *desktop, *title; U x, y, xs, ys, xc, yc, fill, flags; unsigned short show, r2; void *r3; U in, out, err; } STARTUPINFO;
typedef struct { U process, thread, pid, tid; } PROCESS_INFORMATION;
#define API __declspec(dllimport) __stdcall
API U RegisterClassA(const WNDCLASS *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
API U DefWindowProcA(U, U, U, U);
API int GetMessageA(MSG *, U, U, U);
API U DispatchMessageA(const MSG *);
API int PostMessageA(U, U, U, U);
API U SendMessageA(U, U, U, U);
API U FindWindowA(const char *, const char *);
API int ReplyMessage(U);
API U SetTimer(U, U, U, void *);
API U GetModuleHandleA(const char *);
API U GetModuleFileNameA(U, char *, U);
API char *GetCommandLineA(void);
API int CreateProcessA(const char *, char *, void *, void *, int, U, void *, const char *, STARTUPINFO *, PROCESS_INFORMATION *);
API void Sleep(U);
API void ExitProcess(U);

static const char klass[] = "Neo98CopyDataReply";
static const char payload[16] = "HELLO-COPYDATA!";
static U verdict, checked, answered;

static U __stdcall proc(U hwnd, U message, U wparam, U lparam)
{
    if (message == 0x4a)
    {
        const COPYDATASTRUCT *copy = (const COPYDATASTRUCT *)lparam;
        ReplyMessage(1);
        // The sender runs on and sends its string meanwhile.
        Sleep(400);
        const char *data = (const char *)copy->lpData;
        U same = copy->dwData == 0x12345678 && copy->cbData == sizeof payload;
        for (U i = 0; same && i < sizeof payload; ++i) same = data[i] == payload[i];
        if (!same && !verdict) verdict = 5;
        checked = 1;
        PostMessageA(hwnd, 0x8001, 0, 0);
        return 0;
    }
    if (message == 0x8002)
    {
        // What the first send returned to the sender: the reply, 1.
        if (wparam != 1 && !verdict) verdict = 7;
        answered = 1;
        return 0;
    }
    if (message == 0x113 && !verdict) verdict = 6;
    return DefWindowProcA(hwnd, message, wparam, lparam);
}

static void sender(void)
{
    U target = 0;
    for (U i = 0; i < 100 && !(target = FindWindowA(klass, 0)); ++i) Sleep(50);
    if (!target) ExitProcess(1);
    COPYDATASTRUCT copy = {0x12345678, sizeof payload, (void *)payload};
    const U result = SendMessageA(target, 0x4a, 0, (U)&copy);
    // 12 bytes of COPYDATASTRUCT and 16 of data: a string of the same size.
    static const char text[28] = "XXXXXXXXXXXXXXXXXXXXXXXXXXX";
    SendMessageA(target, 0x0c, 0, (U)text);
    PostMessageA(target, 0x8002, result, 0);
    ExitProcess(0);
}

void mainCRTStartup(void)
{
    const char *line = GetCommandLineA();
    U length = 0;
    while (line[length]) ++length;
    if (length > 2 && line[length - 1] == 'S' && line[length - 2] == ' ') sender();
    WNDCLASS c = {0};
    c.proc = proc;
    c.instance = GetModuleHandleA(0);
    c.name = klass;
    if (!RegisterClassA(&c)) ExitProcess(2);
    const U window = CreateWindowExA(0, klass, "receiver", 0, 0, 0, 10, 10, 0, 0, c.instance, 0);
    if (!window) ExitProcess(3);
    static char command[300];
    U n = GetModuleFileNameA(0, command, 280);
    command[n++] = ' '; command[n++] = 'S'; command[n] = 0;
    STARTUPINFO start = {sizeof start};
    PROCESS_INFORMATION made;
    if (!CreateProcessA(0, command, 0, 0, 0, 0, 0, 0, &start, &made)) ExitProcess(4);
    SetTimer(window, 1, 15000, 0);
    MSG m;
    while (!(checked && answered) && verdict != 6 && GetMessageA(&m, 0, 0, 0) > 0) DispatchMessageA(&m);
    ExitProcess(verdict);
}
