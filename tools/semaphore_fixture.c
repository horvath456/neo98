/* SPDX-License-Identifier: GPL-2.0-or-later
 * Own x86 public-API contract for the original KERNEL32/VWIN32 semaphore core
 * and NEO98's native object, handle, access and cooperative-thread providers.
 */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U CreateSemaphoreA(const void *, int, int, const char *);
API U OpenSemaphoreA(U, U, const char *);
API U ReleaseSemaphore(U, int, int *);
API U CreateEventA(const void *, U, U, const char *);
API U OpenEventA(U, U, const char *);
API U SetEvent(U);
API U CreateMutexA(const void *, U, const char *);
API U OpenMutexA(U, U, const char *);
API U CreateThread(U, U, U(__stdcall *)(U), U, U, U *);
API U GetExitCodeThread(U, U *);
API U WaitForSingleObject(U, U);
API U CloseHandle(U);
API U GetLastError(void);
API void SetLastError(U);
API U InterlockedIncrement(volatile U *);
API void Sleep(U);
API void ExitProcess(U);

enum
{
    TIMEOUT = 258,
    SYNCHRONIZE = 0x100000,
    MODIFY_STATE = 2,
    ALL_ACCESS = 0x1f0003
};
static const char shared_name[] = "neo98-semaphore-workers";
static U ready[3], completed, finish;
static volatile U entered;

static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}

static void counter_contract(void)
{
    U h = CreateSemaphoreA(0, 2, 3, 0);
    check(h && !GetLastError(), 3750);
    check(WaitForSingleObject(h, 0) == 0 && WaitForSingleObject(h, 0) == 0, 3751);
    check(WaitForSingleObject(h, 0) == TIMEOUT && WaitForSingleObject(h, 2) == TIMEOUT, 3752);
    int previous = 0x12345678;
    check(ReleaseSemaphore(h, 2, &previous) && previous == 0, 3753);
    previous = 0x12345678;
    check(!ReleaseSemaphore(h, 2, &previous) && GetLastError() == 298 && previous == 0x12345678, 3754);
    check(ReleaseSemaphore(h, 1, &previous) && previous == 2, 3755);
    previous = 0x12345678;
    check(!ReleaseSemaphore(h, 1, &previous) && GetLastError() == 298 && previous == 0x12345678, 3756);
    /* Public nonpositive-release rejection is the explicit ARM service policy. */
    check(!ReleaseSemaphore(h, 0, &previous) && GetLastError() == 87 && previous == 0x12345678, 3757);
    check(!ReleaseSemaphore(h, -1, &previous) && GetLastError() == 87 && previous == 0x12345678, 3758);
    for (U i = 0; i < 3; ++i)
        check(WaitForSingleObject(h, 0) == 0, 3759);
    check(WaitForSingleObject(h, 0) == TIMEOUT, 3760);
    check(ReleaseSemaphore(h, 1, 0) && WaitForSingleObject(h, 0) == 0, 3761);
    check(WaitForSingleObject(h, 0) == TIMEOUT && CloseHandle(h), 3762);
    check(WaitForSingleObject(h, 0) == ~0u && GetLastError() == 6, 3763);
    check(!ReleaseSemaphore(h, 1, &previous) && GetLastError() == 6 && previous == 0x12345678, 3764);
    check(!CloseHandle(h) && GetLastError() == 6, 3765);
    check(!ReleaseSemaphore(0, 1, 0) && GetLastError() == 6, 3766);
}

static void named_contract(void)
{
    const char name[] = "neo98-semaphore-named";
    U attributes[] = {12, 0, 1};
    SetLastError(98);
    U first = CreateSemaphoreA(attributes, 1, 3, name);
    check(first && !GetLastError(), 3767);
    /* Existing objects bypass the constructor, including invalid new limits. */
    U same = CreateSemaphoreA(0, 99, -10, name);
    check(same && same != first && GetLastError() == 183, 3768);
    U read = OpenSemaphoreA(SYNCHRONIZE, 0, name);
    U write = OpenSemaphoreA(MODIFY_STATE, 1, name);
    U none = OpenSemaphoreA(0, 0, name);
    U masked = OpenSemaphoreA(~0u, 0, name);
    check(read && write && none && masked && read != first && write != read, 3769);
    check(!OpenSemaphoreA(ALL_ACCESS, 0, "NEO98-SEMAPHORE-NAMED") && GetLastError() == 123, 3770);
    int previous = 0x12345678;
    check(!ReleaseSemaphore(read, 1, &previous) && GetLastError() == 5 && previous == 0x12345678, 3771);
    /* Waits need no SYNCHRONIZE: KERNEL32 looks the handle up with required
       access 0. The unit taken goes back through `write`. */
    check(WaitForSingleObject(write, 0) == 0 && ReleaseSemaphore(write, 1, 0), 3772);
    check(WaitForSingleObject(none, 0) == 0 && ReleaseSemaphore(write, 1, 0), 3773);
    check(!ReleaseSemaphore(none, 1, 0) && GetLastError() == 5, 3774);
    check(WaitForSingleObject(read, 0) == 0 && WaitForSingleObject(same, 0) == TIMEOUT, 3775);
    check(ReleaseSemaphore(write, 2, &previous) && previous == 0, 3776);
    check(WaitForSingleObject(masked, 0) == 0 && WaitForSingleObject(first, 0) == 0, 3777);
    check(CloseHandle(first), 3778);
    check(WaitForSingleObject(first, 0) == ~0u && GetLastError() == 6, 3779);
    U reopened = OpenSemaphoreA(ALL_ACCESS, 0, name);
    check(reopened && reopened != first && ReleaseSemaphore(reopened, 1, &previous) && previous == 0, 3780);
    check(WaitForSingleObject(same, 0) == 0 && WaitForSingleObject(read, 0) == TIMEOUT, 3781);
    check(CloseHandle(same) && CloseHandle(read) && CloseHandle(write) && CloseHandle(none) &&
              CloseHandle(masked) && CloseHandle(reopened),
          3782);
    check(!OpenSemaphoreA(ALL_ACCESS, 0, name) && GetLastError() == 123, 3783);
    U replacement = CreateSemaphoreA(0, 0, 1, name);
    check(replacement && replacement != first && !GetLastError(), 3784);
    check(WaitForSingleObject(replacement, 0) == TIMEOUT, 3785);
    check(!ReleaseSemaphore(reopened, 1, 0) && GetLastError() == 6, 3786);
    check(CloseHandle(replacement), 3787);
    /* Reuse both object and handle slots beyond their native table capacities. */
    for (U i = 0; i < 140; ++i)
    {
        U h = CreateSemaphoreA(attributes, 1, 1, name);
        check(h && !GetLastError() && WaitForSingleObject(h, 0) == 0 && CloseHandle(h), 3788);
    }
    U a = CreateSemaphoreA(0, 1, 1, ""), b = CreateSemaphoreA(0, 0, 1, "");
    check(a && b && a != b && WaitForSingleObject(a, 0) == 0 && WaitForSingleObject(b, 0) == TIMEOUT, 3789);
    check(CloseHandle(a) && CloseHandle(b), 3790);
    check(!OpenSemaphoreA(ALL_ACCESS, 0, "") && GetLastError() == 123, 3791);
}

static void namespace_contract(void)
{
    const char name[] = "neo98-semaphore-namespace";
    U sem = CreateSemaphoreA(0, 0, 1, name);
    check(sem != 0, 3792);
    check(!CreateEventA(0, 0, 0, name) && GetLastError() == 6, 3793);
    check(!OpenEventA(ALL_ACCESS, 0, name) && GetLastError() == 6, 3794);
    check(!CreateMutexA(0, 0, name) && GetLastError() == 6, 3795);
    check(!OpenMutexA(ALL_ACCESS, 0, name) && GetLastError() == 6, 3796);
    check(CloseHandle(sem), 3797);
    U event = CreateEventA(0, 0, 0, name);
    check(event && !GetLastError(), 3798);
    check(!CreateSemaphoreA(0, 0, 1, name) && GetLastError() == 6, 3799);
    check(!OpenSemaphoreA(ALL_ACCESS, 0, name) && GetLastError() == 6, 3800);
    int previous = 0x12345678;
    check(!ReleaseSemaphore(event, 1, &previous) && GetLastError() == 6 && previous == 0x12345678, 3801);
    check(CloseHandle(event), 3802);
    U mutex = CreateMutexA(0, 0, name);
    check(mutex && !GetLastError(), 3803);
    check(!CreateSemaphoreA(0, 0, 1, name) && GetLastError() == 6, 3804);
    check(!OpenSemaphoreA(ALL_ACCESS, 0, name) && GetLastError() == 6, 3805);
    check(!ReleaseSemaphore(mutex, 1, 0) && GetLastError() == 6 && CloseHandle(mutex), 3806);
}

static void original_constructor_edges(void)
{
    /* Windows 98 checks only signed maximum >= initial, not nonnegative counts. */
    U h = CreateSemaphoreA(0, -1, 2, 0);
    check(h && !GetLastError() && WaitForSingleObject(h, 0) == TIMEOUT, 3807);
    int previous = 0;
    check(ReleaseSemaphore(h, 1, &previous) && previous == -1 && WaitForSingleObject(h, 0) == TIMEOUT, 3808);
    check(ReleaseSemaphore(h, 2, &previous) && previous == 0, 3809);
    check(WaitForSingleObject(h, 0) == 0 && WaitForSingleObject(h, 0) == 0 && CloseHandle(h), 3810);
    h = CreateSemaphoreA(0, 0, 0, 0);
    check(h && WaitForSingleObject(h, 0) == TIMEOUT, 3811);
    previous = 0x12345678;
    check(!ReleaseSemaphore(h, 1, &previous) && GetLastError() == 298 && previous == 0x12345678, 3812);
    check(CloseHandle(h), 3813);
    h = CreateSemaphoreA(0, -2, -1, 0);
    check(h && ReleaseSemaphore(h, 1, &previous) && previous == -2, 3814);
    previous = 0x12345678;
    check(!ReleaseSemaphore(h, 1, &previous) && GetLastError() == 298 && previous == 0x12345678, 3815);
    check(WaitForSingleObject(h, 0) == TIMEOUT && CloseHandle(h), 3816);
    check(CreateSemaphoreA(0, 2, 1, 0) == 0 && GetLastError() == 87, 3817);
    /* An absent named lookup leaves FFFFFFFF as the original failure result. */
    check(CreateSemaphoreA(0, 2, 1, "neo98-semaphore-bad-constructor") == ~0u && GetLastError() == 87, 3818);
    check(!OpenSemaphoreA(ALL_ACCESS, 0, "neo98-semaphore-bad-constructor") && GetLastError() == 123, 3819);
    U attributes[] = {8, 0, 1};
    check(!CreateSemaphoreA(attributes, 0, 1, 0) && GetLastError() == 87, 3820);
    attributes[0] = 12;
    /* The original security helper ignores the descriptor pointer. */
    attributes[1] = 0xdeadbeef;
    h = CreateSemaphoreA(attributes, 0, 1, 0);
    check(h && ReleaseSemaphore(h, 1, 0) && WaitForSingleObject(h, 0) == 0 && CloseHandle(h), 3821);
}

static U __stdcall worker(U id)
{
    U h = OpenSemaphoreA(SYNCHRONIZE, 0, shared_name);
    check(h && WaitForSingleObject(h, 1) == TIMEOUT, 3822);
    check(SetEvent(ready[id]), 3823);
    check(WaitForSingleObject(h, 10000) == 0, 3824);
    InterlockedIncrement(&entered);
    check(ReleaseSemaphore(completed, 1, 0), 3825);
    check(WaitForSingleObject(finish, 10000) == 0 && CloseHandle(h), 3826);
    return 98 + id;
}

static void blocking_workers(void)
{
    entered = 0;
    U owner = CreateSemaphoreA(0, 0, 3, shared_name);
    U writer = OpenSemaphoreA(MODIFY_STATE, 0, shared_name);
    completed = CreateSemaphoreA(0, 0, 3, 0);
    finish = CreateEventA(0, 1, 0, 0);
    check(owner && writer && completed && finish, 3827);
    U threads[3], ids[3];
    for (U i = 0; i < 3; ++i)
    {
        ready[i] = CreateEventA(0, 1, 0, 0);
        threads[i] = CreateThread(0, 0, worker, i, 0, &ids[i]);
        check(ready[i] && threads[i] && ids[i], 3828);
    }
    check(ids[0] != ids[1] && ids[1] != ids[2] && ids[0] != ids[2], 3829);
    for (U i = 0; i < 3; ++i)
        check(WaitForSingleObject(ready[i], 10000) == 0, 3830);
    Sleep(0);
    check(!entered && WaitForSingleObject(completed, 0) == TIMEOUT, 3831);
    for (U i = 0; i < 3; ++i)
        check(WaitForSingleObject(threads[i], 0) == TIMEOUT, 3832);
    /* Workers' own handles keep the object alive after its creating handle closes. */
    check(CloseHandle(owner), 3833);
    int previous = 0x12345678;
    check(ReleaseSemaphore(writer, 1, &previous) && previous == 0, 3834);
    check(WaitForSingleObject(completed, 10000) == 0 && entered == 1, 3835);
    check(WaitForSingleObject(completed, 0) == TIMEOUT, 3836);
    check(ReleaseSemaphore(writer, 2, &previous) && previous == 0, 3837);
    check(WaitForSingleObject(completed, 10000) == 0 && WaitForSingleObject(completed, 10000) == 0 &&
              entered == 3,
          3838);
    check(WaitForSingleObject(completed, 0) == TIMEOUT, 3839);
    check(CloseHandle(writer) && SetEvent(finish), 3840);
    for (U i = 0; i < 3; ++i)
    {
        U result = 0;
        check(WaitForSingleObject(threads[i], 10000) == 0 && GetExitCodeThread(threads[i], &result) &&
                  result == 98 + i,
              3841);
        check(CloseHandle(threads[i]) && CloseHandle(ready[i]), 3842);
    }
    check(!OpenSemaphoreA(ALL_ACCESS, 0, shared_name) && GetLastError() == 123, 3843);
    check(CloseHandle(completed) && CloseHandle(finish), 3844);
}

void run_semaphore_test(void)
{
    counter_contract();
    named_contract();
    namespace_contract();
    original_constructor_edges();
    blocking_workers();
    U large = CreateSemaphoreA(0, 0x7ffffffe, 0x7fffffff, 0);
    int previous = 0x12345678;
    check(large, 3845);
    /* Native public service validation must catch overflow before the original
       modulo-32 VWIN32 counter core. The internal original still wraps. */
    check(!ReleaseSemaphore(large, 2, &previous) && GetLastError() == 298 && previous == 0x12345678, 3846);
    check(ReleaseSemaphore(large, 1, &previous) && previous == 0x7ffffffe, 3847);
    check(!WaitForSingleObject(large, 0) && CloseHandle(large), 3848);
}
