/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Where USER puts an MDI child that names no place and no size. An MDI client is given a row of sizes, among them the one File
   Manager has on a screen of 720 by 1560; at each size three children are
   created with CW_USEDEFAULT in all four fields, and at the end two are
   destroyed and two more created. D:\MDIDEF.TXT gets the four system metrics
   the calculation uses and, per child, the client rectangle of the MDI
   client, the number of children created before it in this client, and the
   rectangle the child got. tools/test_mdi_default_original.py runs USER on the same numbers. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U CreateFileA(const char *, U, U, U, U, U, U);
API U WriteFile(U, const void *, U, U *, U);
API U CloseHandle(U);
API int lstrlenA(const char *);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
API U DefFrameProcA(U, U, U, U, U);
API U DefMDIChildProcA(U, U, U, U);
API U SendMessageA(U, U, U, U);
API int MoveWindow(U, int, int, int, int, int);
API int GetClientRect(U, int *);
API int GetWindowRect(U, int *);
API int ScreenToClient(U, int *);
API int GetSystemMetrics(int);
__declspec(dllimport) int __cdecl wsprintfA(char *, const char *, ...);

struct Class { U style, proc; int class_extra, window_extra; U instance, icon, cursor, brush; const char *menu, *name; };
static U client;
static char text[4000];
static int created;
static U children[32];

static U __stdcall Frame(U window, U message, U wparam, U lparam)
{
    return DefFrameProcA(window, client, message, wparam, lparam);
}
static void Child(void)
{
    U create[9] = {(U) "Neo98MdiChild", (U) "child", 0x400000, 0x80000000u, 0x80000000u, 0x80000000u, 0x80000000u, 0, 0};
    int area[4], r[4];
    U child;
    GetClientRect(client, area);
    child = SendMessageA(client, 0x220, 0, (U)create); /* WM_MDICREATE */
    GetWindowRect(child, r);
    ScreenToClient(client, r);
    ScreenToClient(client, r + 2);
    wsprintfA(text + lstrlenA(text), "child area=%d,%d created=%d rect=%d,%d,%d,%d\r\n", area[2], area[3], created, r[0], r[1],
              r[2] - r[0], r[3] - r[1]);
    children[created++ % 32] = child;
}
void mainCRTStartup(void)
{
    static const int sizes[][2] = {{716, 1380}, {640, 400}, {300, 700}, {200, 60}, {50, 10}, {716, 1380}};
    struct Class klass = {0};
    U frame, written = 0, file, ccs[2] = {0, 100};
    klass.proc = (U)Frame;
    klass.instance = 0x400000;
    klass.name = "Neo98MdiFrame";
    RegisterClassA(&klass);
    klass.proc = (U)DefMDIChildProcA;
    klass.name = "Neo98MdiChild";
    RegisterClassA(&klass);
    frame = CreateWindowExA(0, "Neo98MdiFrame", "F", 0x10cf0000u, 0, 0, 400, 300, 0, 0, 0x400000, 0);
    client = CreateWindowExA(0, "MDICLIENT", 0, 0x52000000u, 0, 0, 100, 100, frame, 0, 0x400000, ccs);
    wsprintfA(text, "metrics size=%d,%d frame=%d,%d\r\n", GetSystemMetrics(30), GetSystemMetrics(31), GetSystemMetrics(32),
              GetSystemMetrics(33));
    for (int i = 0; i < 6; ++i)
    {
        MoveWindow(client, 0, 0, sizes[i][0], sizes[i][1], 0);
        if (i == 5)
        {
            /* Two children go; the count of those created stays ( only counts up). */
            SendMessageA(client, 0x221, children[0], 0); /* WM_MDIDESTROY */
            SendMessageA(client, 0x221, children[1], 0);
        }
        for (int n = 0; n < (i == 5 ? 2 : 3); ++n) Child();
    }
    file = CreateFileA("D:\\MDIDEF.TXT", 0x40000000u, 0, 0, 2, 0x80, 0);
    WriteFile(file, text, (U)lstrlenA(text), &written, 0);
    CloseHandle(file);
    ExitProcess(0);
}
