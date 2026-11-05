/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Guest fixture. A second thread opens a dialog out of the program's
   resources; its WM_INITDIALOG sends a message to a window of the main
   thread, which alternates between pumping, LoadCursorA and FindResourceA. The loader lock
   of the port covered the whole dialog creation, so the main thread waited
   for it in LoadCursorA while the dialog thread waited for the main thread's
   answer: Explorer's Run dialog stood invisible that way. Exit code zero
   means every check held; codes 4820..4839. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U GetModuleHandleA(const char *);
API U CreateThread(U, U, U (__stdcall *)(U), U, U, U *);
API U WaitForSingleObject(U, U);
API void Sleep(U);
API U RegisterClassA(const U *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U PeekMessageA(U *, U, U, U, U);
API U DispatchMessageA(const U *);
API U SendMessageA(U, U, U, U);
API U DialogBoxParamA(U, U, U, U (__stdcall *)(U, U, U, U), U);
API U EndDialog(U, U);
API U LoadCursorA(U, U);
API U FindResourceA(U, U, U);

static U failure, window, answered, result = 0xffffffff;
static void check(int ok, U code)
{
    if (!ok && !failure)
        failure = code;
}
static U __stdcall procedure(U h, U m, U w, U l)
{
    if (m == 0x401)
    {
        ++answered;
        return 0x1234;
    }
    return DefWindowProcA(h, m, w, l);
}
static U __stdcall dialog(U h, U m, U w, U l)
{
    (void)w; (void)l;
    if (m == 0x110) /* WM_INITDIALOG */
    {
        /* Let the main thread run into FindResourceA first, as Explorer's
           tray thread was in LoadCursorA when the Run dialog sent to it. */
        Sleep(20);
        check(SendMessageA(window, 0x401, 0, 0) == 0x1234, 4820);
        EndDialog(h, 7);
        return 1;
    }
    return 0;
}
static U __stdcall worker(U unused)
{
    (void)unused;
    result = DialogBoxParamA(GetModuleHandleA(0), 100, 0, dialog, 0);
    return 0;
}
void mainCRTStartup(void)
{
    U wc[10] = {0};
    wc[1] = (U) procedure;
    wc[4] = GetModuleHandleA(0);
    wc[9] = (U) "LockTest";
    check(RegisterClassA(wc) != 0, 4821);
    window = CreateWindowExA(0, "LockTest", "lock", 0x00cf0000, 0, 0, 100, 100, 0, 0, GetModuleHandleA(0), 0);
    check(window != 0, 4822);
    U id = 0;
    const U thread = CreateThread(0, 0, worker, 0, 0, &id);
    check(thread != 0, 4823);
    U msg[7];
    for (U round = 0; round < 2000 && WaitForSingleObject(thread, 0); ++round)
    {
        while (PeekMessageA(msg, 0, 0, 0, 1))
            DispatchMessageA(msg);
        LoadCursorA(0, 32512);
        FindResourceA(GetModuleHandleA(0), 100, 5);
        Sleep(1);
    }
    check(WaitForSingleObject(thread, 0) == 0, 4824);
    check(result == 7, 4825);
    check(answered == 1, 4826);
    ExitProcess(failure);
}
