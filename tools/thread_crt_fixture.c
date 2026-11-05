/* SPDX-License-Identifier: GPL-2.0-or-later
 * Own Win32 guest contract for the original MSVCRT thread/PTD port.
 * Not linked into COMMON until the runtime adapter is ready. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
#define CRT __declspec(dllimport) __cdecl
CRT U _beginthreadex(void *, U, U(__stdcall *)(void *), void *, U, U *);
/* Deliberately omit noreturn: the assertions after this call detect an
 * incorrectly returning native _endthreadex adapter. */
CRT void _endthreadex(U);
CRT int *_errno(void);
CRT U _msize(void *);
API U CreateThread(void *, U, U(__stdcall *)(void *), void *, U, U *);
API void ExitThread(U);
API U CreateEventA(void *, U, U, const char *);
API U SetEvent(U);
API U CloseHandle(U);
API U WaitForSingleObject(U, U);
API U ResumeThread(U);
API U GetExitCodeThread(U, U *);
API U GetCurrentThreadId(void);
API U GetLastError(void);
API void SetLastError(U);
API void Sleep(U);
API void ExitProcess(U);

static void check(int value, U code)
{
    if (!value)
        ExitProcess(code);
}
struct Worker
{
    U mode, index, id, gate, completed;
    volatile U visits, errno_pointer, after_end;
};
static volatile U forbidden_visits;
static U main_errno_pointer;
static U __stdcall worker(void *argument)
{
    struct Worker *w = argument;
    const U tag = 0xa9800000 + w->index;
    SetLastError(tag);
    int *error = _errno();
    check(error && GetLastError() == tag && !*error, 5300);
    check((U)error != main_errno_pointer && GetCurrentThreadId() == w->id, 5301);
    U *data = (U *)((unsigned char *)error - 8);
    check(data[0] == w->id && data[1] == ~0u && data[5] == 1 && _msize(data) >= 116, 5302);
    w->errno_pointer = (U)error;
    *error = 100 + w->index;
    w->visits = 1;
    if (w->gate)
        check(WaitForSingleObject(w->gate, 10000) == 0, 5303);
    for (U i = 0; i < 6; ++i)
    {
        SetLastError(tag);
        Sleep(0);
        check(_errno() == error && *error == (int)(100 + w->index) && GetLastError() == tag, 5304);
    }
    w->visits = 2;
    if (w->completed)
        check(SetEvent(w->completed), 5305);
    if (w->mode == 1)
    {
        _endthreadex(0x98765432);
        w->after_end = 1;
        check(0, 5306);
    }
    if (w->mode == 2)
    {
        ExitThread(0xabcdef01);
        w->after_end = 1;
        check(0, 5307);
    }
    return 0x9000 + w->index;
}
static U __stdcall forbidden(void *argument)
{
    (void)argument;
    ++forbidden_visits;
    return 91;
}
static void reclaimed(U error_pointer, U code)
{
    /* This test intentionally checks the ported MSVCRT PTD layout. The pointer
     * is queried immediately after thread completion, before further heap use. */
    check(error_pointer && !_msize((void *)(error_pointer - 8)) && GetLastError() == 87, code);
}
void run_thread_crt_test(void)
{
    int *main_error = _errno();
    const int saved_errno = *main_error;
    U *main_data = (U *)((unsigned char *)main_error - 8);
    const U saved_doserrno = main_data[3];
    main_errno_pointer = (U)main_error;
    SetLastError(0x419819);
    check(_errno() == main_error && GetLastError() == 0x419819, 5310);
    check(main_data[0] == GetCurrentThreadId() && main_data[1] == ~0u && main_data[5] == 1, 5311);
    check(_msize(main_data) >= 116, 5312);
    *main_error = 77;
    const U gate = CreateEventA(0, 1, 0, 0);
    check(gate != 0, 5313);
    struct Worker workers[3] = {{0, 1, 0, gate, 0, 0, 0, 0},
                                {1, 2, 0, gate, 0, 0, 0, 0},
                                {2, 3, 0, gate, 0, 0, 0, 0}};
    U handles[3];
    handles[0] = _beginthreadex(0, 0, worker, &workers[0], 4, &workers[0].id);
    handles[1] = _beginthreadex(0, 0x10000, worker, &workers[1], 0, &workers[1].id);
    /* A plain Win32 thread obtains PTD lazily. Its DLL-detach cleanup must
     * release it even though _threadstartex and _endthreadex were not used. */
    handles[2] = CreateThread(0, 0, worker, &workers[2], 0, &workers[2].id);
    check(handles[0] && handles[1] && handles[2], 5314);
    check(workers[0].id && workers[1].id && workers[2].id && workers[0].id != workers[1].id &&
              workers[0].id != workers[2].id && workers[1].id != workers[2].id, 5315);
    Sleep(0);
    U result = 0;
    check(!workers[0].visits && GetExitCodeThread(handles[0], &result) && result == 259 &&
              WaitForSingleObject(handles[0], 0) == 258, 5316);
    check(ResumeThread(handles[0]) == 1 && ResumeThread(handles[0]) == 0xffffffffu, 5317);
    for (U attempt = 0; attempt < 10000 &&
                          (!workers[0].visits || !workers[1].visits || !workers[2].visits); ++attempt)
        Sleep(0);
    check(workers[0].visits == 1 && workers[1].visits == 1 && workers[2].visits == 1, 5327);
    check(SetEvent(gate), 5318);
    const U expected[] = {0x9001, 0x98765432, 0xabcdef01};
    for (U i = 0; i < 3; ++i)
    {
        check(WaitForSingleObject(handles[i], 10000) == 0, 5319);
        check(GetExitCodeThread(handles[i], &result) && result == expected[i], 5320);
        check(workers[i].visits == 2 && !workers[i].after_end, 5321);
        reclaimed(workers[i].errno_pointer, 5322);
        /* _endthreadex leaves the caller's handle open after completion. */
        check(CloseHandle(handles[i]) && !CloseHandle(handles[i]) && GetLastError() == 6, 5323);
        check(_errno() == main_error && *main_error == 77, 5324);
    }
    check(workers[0].errno_pointer != workers[1].errno_pointer &&
              workers[0].errno_pointer != workers[2].errno_pointer &&
              workers[1].errno_pointer != workers[2].errno_pointer, 5325);
    check(CloseHandle(gate), 5326);

    /* Closing the handle before the new thread first runs must leave the
     * startup PTD alive. The callback reports completion through its event. */
    struct Worker closed = {0, 4, 0, 0, 0, 0, 0, 0};
    closed.completed = CreateEventA(0, 0, 0, 0);
    check(closed.completed != 0, 5330);
    const U closed_handle = _beginthreadex(0, 0, worker, &closed, 0, &closed.id);
    check(closed_handle && CloseHandle(closed_handle), 5331);
    check(WaitForSingleObject(closed.completed, 10000) == 0 && closed.visits == 2, 5332);
    reclaimed(closed.errno_pointer, 5333);
    check(CloseHandle(closed.completed) && *main_error == 77, 5334);

    U untouched = 0x12349876;
    SetLastError(98);
    check(!_beginthreadex(0, 0, forbidden, 0, 8, &untouched), 5340);
    check(GetLastError() == 87 && *main_error == 22 && main_data[3] == 87 && untouched == 0x12349876,
          5341);
    *main_error = 77;
    check(!_beginthreadex(0, 0x200001, forbidden, 0, 0, &untouched), 5342);
    check(GetLastError() == 8 && *main_error == 12 && main_data[3] == 8 && untouched == 0x12349876,
          5343);
    Sleep(0);
    check(!forbidden_visits, 5344);

    /* Runtime provider limit, not an original Windows handle-count claim.
     * Repeated handles to one event exhaust handles without exhausting the
     * independent 64-event table. Failed CreateThread must publish no worker. */
    for (U round = 0; round < 2; ++round)
    {
        static U events[1024];
        U count = 0;
        while (count < 1024)
        {
            const U event = CreateEventA(0, 1, 0, "NEO98-CRT-HANDLE-LIMIT");
            if (!event)
                break;
            events[count++] = event;
        }
        check(count > 128 && count < 1024, 5350);
        *main_error = 77;
        check(!_beginthreadex(0, 0, forbidden, 0, 0, &untouched), 5351);
        check(GetLastError() == 8 && *main_error == 12 && main_data[3] == 8 && untouched == 0x12349876,
              5352);
        for (U i = 0; i < count; ++i)
            check(CloseHandle(events[i]), 5353);
        Sleep(0);
        check(!forbidden_visits, 5354);
    }
    /* A successful worker after both failures also checks reused thread slots. */
    struct Worker reused = {0, 5, 0, 0, 0, 0, 0, 0};
    const U again = _beginthreadex(0, 0, worker, &reused, 0, &reused.id);
    check(again && WaitForSingleObject(again, 10000) == 0, 5360);
    check(GetExitCodeThread(again, &result) && result == 0x9005 && CloseHandle(again), 5361);
    reclaimed(reused.errno_pointer, 5362);
    check(_errno() == main_error && !forbidden_visits, 5363);
    *main_error = saved_errno;
    main_data[3] = saved_doserrno;
}
