/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
int _fltused; /* Compiler marker for this freestanding floating-point test. */
#define API __declspec(dllimport) __stdcall
API U SetErrorMode(U);
API void SetLastError(U);
API U GetLastError(void);
API U CreateThread(U, U, U, U, U, U);
API U WaitForSingleObject(U, U);
API U CloseHandle(U);
API void Sleep(U);
API void ExitProcess(U);
__declspec(dllimport) U __cdecl _controlfp(U, U);
__declspec(dllimport) U __cdecl _control87(U, U);
static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}
static int convert(double value)
{
    int result;
    __asm__("fldl %1; fistpl %0" : "=m"(result) : "m"(value));
    return result;
}
static U __stdcall worker(U unused)
{
    (void)unused;
    check(SetErrorMode(0x8002) == 0x1234, 2600);
    _control87(0x100, 0x300);
    Sleep(0);
    check((_control87(0, 0) & 0x300) == 0x100 && convert(-1.75) == -2, 2601);
    return 0;
}
void run_crt_control_test(void)
{
    U saved = _control87(0, 0);
    _control87(0x80000, 0x80000);
    check((_controlfp(0, 0x80000) & 0x80000) != 0, 2602);
    check((_control87(0, 0x80000) & 0x80000) == 0, 2603);
    check((_controlfp(0x40000, 0x40000) & 0x40000) != 0, 2604);
    check((_control87(0, 0) & 0x40000) != 0, 2605);
    check(_controlfp(0xdeadbeef, ~0u) == 0xdea5beef, 2606);
    _control87(0x9001f, ~0u);
    static const int positive[] = {2, 1, 2, 1}, negative[] = {-2, -2, -1, -1};
    for (U mode = 0; mode < 4; ++mode)
    {
        _controlfp(mode << 8, 0x300);
        check(convert(1.75) == positive[mode] && convert(-1.75) == negative[mode], 2607);
    }
    _control87(0x30000, 0x30000);
    unsigned short word;
    __asm__("fnstcw %0" : "=m"(word));
    check((word & 0x300) == 0 && (_control87(0, 0) & 0x30000) == 0x20000, 2608);
    SetLastError(98);
    U previous = SetErrorMode(0xdead1234);
    check(GetLastError() == 98, 2609);
    _control87(0x200, 0x300);
    U thread = CreateThread(0, 0, (U)worker, 0, 0, 0);
    check(thread && !WaitForSingleObject(thread, ~0u), 2610);
    check(CloseHandle(thread) && SetErrorMode(previous) == 0x8002, 2611);
    check((_control87(0, 0) & 0x300) == 0x200 && convert(-1.75) == -1, 2612);
    _control87(saved, ~0u);
}
