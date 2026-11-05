/* SPDX-License-Identifier: GPL-2.0-or-later
 * Public guest contract for the original CRT PTD family. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
#define CRT __declspec(dllimport) __cdecl
CRT U _beginthread(void (__cdecl *)(void *), U, void *);
CRT void _endthread(void);
CRT void _endthreadex(U);
CRT int *_errno(void);
CRT U *__doserrno(void);
CRT U __threadid(void);
CRT U __threadhandle(void);
CRT U _msize(void *);
CRT void srand(U);
CRT int rand(void);
CRT char *strtok(char *, const char *);
API U GetCurrentThreadId(void);
API U GetLastError(void);
API void SetLastError(U);
API void Sleep(U);
API U GetExitCodeThread(U, U *);
API U CloseHandle(U);
API U TlsAlloc(void);
API U TlsFree(U);
API void ExitProcess(U);
static void check(int condition, U code) { if (!condition) ExitProcess(code); }
static int equal(const char *a, const char *b)
{
    if (!a || !b) return a == b;
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static const U expected[3][4] = {{38,7719,21238,2437},{41,18467,6334,26500},{35,29739,3374,11141}};
struct Worker
{
    U index, handle, saved_handle;
    volatile U visits, ptd, after_end;
    char text[32];
};
static void __cdecl worker(void *p)
{
    struct Worker *w = p;
    const U i = w->index, code = 0xd0980000 + i;
    SetLastError(code);
    int *error = _errno();
    U *dos = __doserrno(), *ptd = (U *)((char *)error - 8);
    check(error && (U)dos == (U)error + 4 && GetLastError() == code, 5370);
    check(ptd[0] == GetCurrentThreadId() && ptd[1] && ptd[1] != ~0u && ptd[5] == 1, 5371);
    w->saved_handle = ptd[1];
    check(__threadid() == ptd[0] && __threadhandle() == 0xfffffffeu, 5393);
    check(!_errno()[0] && !*dos && _msize(ptd) >= 116, 5372);
    w->ptd = (U)ptd;
    *error = 51 + i; *dos = 71 + i;
    srand(i == 2 ? ~0u : i);
    check((U)rand() == expected[i][0], 5373);
    check(equal(strtok(w->text, ":"), "one"), 5374);
    w->visits = 1;
    for (U step = 1; step < 4; ++step)
    {
        SetLastError(code);
        Sleep(0);
        check(GetLastError() == code && _errno() == error && *error == (int)(51 + i) &&
              __doserrno() == dos && *dos == 71 + i, 5375);
        check((U)rand() == expected[i][step], 5376);
        check(equal(strtok(0, ":"), step == 1 ? "two" : step == 2 ? "three" : 0), 5377);
    }
    w->visits = 2;
    if (i == 1) _endthread();
    if (i == 2) _endthreadex(0x98abc123);
    if (i) { w->after_end = 1; check(0, 5378); }
}
void run_thread_crt_family_test(void)
{
    int *error = _errno();
    U *dos = __doserrno(), *data = (U *)((char *)error - 8);
    const int old_error = *error;
    const U old_dos = *dos, old_seed = data[5], old_next = data[6];
    check((U)dos == (U)error + 4, 5379);
    *error = 91; *dos = 92;
    srand(1);
    char text[] = ",alpha,,beta,gamma,";
    check(equal(strtok(text, ","), "alpha") && rand() == 41, 5380);
    /* Occupy every public slot that is still free: the CRT keeps its per-thread
     * data behind a private token, not in the 80 public slots. A DLL that took
     * its own index while the process loaded it (COMDLG32 at original )
     * keeps that slot, so the count reached here is not always 80. */
    U slots[80], held = 0;
    while (held < 80 && (slots[held] = TlsAlloc()) != ~0u) ++held;
    check(held && TlsAlloc() == ~0u, 5381);
    struct Worker workers[3] = {{0,0,0,0,0,0,":one::two:three:"},
                                {1,0,0,0,0,0,":one::two:three:"},
                                {2,0,0,0,0,0,":one::two:three:"}};
    for (U i = 0; i < 3; ++i)
    {
        workers[i].handle = _beginthread(worker, 0, &workers[i]);
        check(workers[i].handle != ~0u && workers[i].handle != 0, 5382);
    }
    for (U i = 0; i < 1000 && (workers[0].visits != 2 || workers[1].visits != 2 || workers[2].visits != 2); ++i)
        Sleep(0);
    for (U i = 0; i < 3; ++i)
    {
        check(workers[i].visits == 2 && !workers[i].after_end && workers[i].saved_handle == workers[i].handle, 5383);
        check(!_msize((void *)workers[i].ptd) && GetLastError() == 87, 5384);
        U result = 0;
        if (i < 2)
        {
            check(!GetExitCodeThread(workers[i].handle, &result) && GetLastError() == 6, 5385);
            check(!CloseHandle(workers[i].handle) && GetLastError() == 6, 5386);
        }
        else
        {
            /* EndEx keeps even a legacy thread's handle open, matching the
             * original distinction between these two CRT cleanup routines. */
            check(GetExitCodeThread(workers[i].handle, &result) && result == 0x98abc123, 5387);
            check(CloseHandle(workers[i].handle), 5388);
        }
    }
    check(*error == 91 && *dos == 92 && rand() == 18467 && equal(strtok(0, ","), "beta"), 5389);
    check(rand() == 6334 && equal(strtok(0, ","), "gamma") && !strtok(0, ","), 5390);
    check(_beginthread(worker, 0x200001, &workers[0]) == ~0u && *error == 12 && *dos == 8, 5391);
    for (U i = 0; i < held; ++i) check(TlsFree(slots[i]), 5392);
    *error = old_error; *dos = old_dos; data[5] = old_seed; data[6] = old_next;
}
