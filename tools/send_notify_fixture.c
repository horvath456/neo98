/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SendNotifyMessage to a window of another thread returns at once.
   The main thread owns a window and waits in WaitForSingleObject for an event
   the second thread sets only after its SendNotifyMessage returned. A notify
   that waited for the reply deadlocked here, as Explorer's shell start did.
   Afterwards the main thread pumps and gets the message; InSendMessageEx
   inside says ISMEX_NOTIFY (2), and the window procedure's answer goes
   nowhere. Exit code 0 is a pass, otherwise the failed check. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U CreateThread(U, U, U(__stdcall *)(U), U, U, U *);
API U CreateEventA(U, U, U, U);
API U SetEvent(U);
API U WaitForSingleObject(U, U);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U SendNotifyMessageA(U, U, U, U);
API U PeekMessageA(U *, U, U, U, U);
API U DispatchMessageA(const U *);
API U InSendMessageEx(U);
API U GetLastError(void);

static U window, event, notified, answer = 1, got, flags;
static void check(int ok, U code) { if (!ok) ExitProcess(code); }
static U __stdcall proc(U h, U m, U w, U l)
{
    if (m == 0x8123) { ++got; flags = InSendMessageEx(0); check(w == 7 && l == 9, 30); return 0x55; }
    return DefWindowProcA(h, m, w, l);
}
static U __stdcall notifier(U unused)
{
    answer = SendNotifyMessageA(window, 0x8123, 7, 9);
    notified = 1;
    SetEvent(event);
    return 0;
}
void mainCRTStartup(void)
{
    U cls[10] = {0, (U)proc, 0, 0, 0, 0, 0, 0, 0, (U)"SendNotify98"};
    U id = 0, message[7];
    check(RegisterClassA(cls) != 0, 10);
    window = CreateWindowExA(0, "SendNotify98", "notify", 0, 0, 0, 50, 50, 0, 0, 0, 0);
    event = CreateEventA(0, 1, 0, 0);
    check(window && event, 11);
    /* Own thread: a plain send, the procedure runs before the call returns. */
    check(SendNotifyMessageA(window, 0x8123, 7, 9) == 1, 12);
    check(got == 1, 19);
    check(flags == 0, 20);
    check(CreateThread(0, 0, notifier, 0, 0, &id) != 0, 13);
    check(WaitForSingleObject(event, 10000) == 0, 14);
    check(notified && answer == 1, 15);
    check(got == 1, 16);   /* not delivered while this thread did not pump */
    for (U i = 0; i < 200 && got < 2; ++i)
        while (PeekMessageA(message, 0, 0, 0, 1)) DispatchMessageA(message);
    check(got == 2, 17);
    check((flags & 3) == 2, 18);   /* ISMEX_NOTIFY */
    ExitProcess(0);
}
