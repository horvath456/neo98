/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
#define CRT __declspec(dllimport) __cdecl
CRT U _onexit(U);
CRT int atexit(void(__cdecl *)(void));
CRT void _cexit(void);
CRT void _c_exit(void);
CRT void exit(int);
API void ExitProcess(U);
API void Sleep(U);
API U CreateFileA(const char *, U, U, void *, U, U, U);
API U WriteFile(U, const void *, U, U *, void *);
API U CloseHandle(U);
static U count, round;
static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}
static void __cdecl a(void)
{
    check(count % 2 == 1, 2970);
    ++count;
    Sleep(0); /* Reverse x86 callbacks can yield while the native CRT lock is held. */
}
static void __cdecl b(void)
{
    check(count % 2 == 0, 2971);
    ++count;
}
static void __cdecl finish(void)
{
    check(count == 20, 2972);
    if (++round == 2)
    {
        U f = CreateFileA("C:\\CRTEXIT.DAT", 0x40000000, 0, 0, 2, 0, 0), written = 0, proof = 0x98989898;
        check(f != ~0u && WriteFile(f, &proof, 4, &written, 0) && written == 4 && CloseHandle(f), 2973);
    }
}
void run_crt_exit_test(void)
{
    check(_onexit((U)finish) == (U)finish, 2974);
    for (U i = 0; i < 20; ++i)
        check(!atexit(i & 1 ? b : a), 2975);
    check(!_onexit(0), 2976); /* Null entries stay in the table but are skipped during cleanup. */
    _c_exit();
    check(!count && !round, 2977);
    _cexit();
    check(count == 20 && round == 1, 2978);
    count = 0;
}
void finish_crt_exit_test(void)
{
    exit(0);
    ExitProcess(2979); /* exit must never return to its x86 caller. */
}
