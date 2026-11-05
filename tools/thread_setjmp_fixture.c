/* SPDX-License-Identifier: GPL-2.0-or-later
 * MSVCRT _setjmp3 reads the active thread SEH frame's try level.
 * Test the original CRT on both the initial thread and a CreateThread stack,
 * including longjmp back through that registration and normal thread exit. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U LoadLibraryA(const char *);
API void *GetProcAddress(U, const char *);
API U CreateThread(void *, U, U (__stdcall *)(void *), void *, U, U *);
API U WaitForSingleObject(U, U);
API U GetExitCodeThread(U, U *);
API U CloseHandle(U);
API U SetUnhandledExceptionFilter(void *);
API void RaiseException(U, U, U, const U *);
static int (__cdecl *save)(U *, int, ...);
static void (__cdecl *jump)(U *, int);
static volatile U handled;
static int __stdcall filter(U **pointers)
{
    if (pointers[0][0] != 0xe0006181) return 0;
    ++handled;
    return -1;
}
static U __stdcall exercise(void *unused)
{
    U buffer[16];
    volatile U seen = 0;
    int value;
    (void)unused;
    value = save(buffer, 0);
    if (value == 0) {
        seen = 1;
        jump(buffer, 37);
        return 1;
    }
    if (value != 37 || seen != 1) return 2;
    RaiseException(0xe0006181, 0, 0, 0);
    return 0x6181;
}
void mainCRTStartup(void)
{
    U crt = LoadLibraryA("msvcrt.dll"), code = 0, thread;
    save = (void *)GetProcAddress(crt, "_setjmp3");
    jump = (void *)GetProcAddress(crt, "longjmp");
    if (!save || !jump) ExitProcess(10);
    SetUnhandledExceptionFilter((void *)filter);
    if (exercise(0) != 0x6181) ExitProcess(11);
    thread = CreateThread(0, 0, exercise, 0, 0, 0);
    if (!thread || WaitForSingleObject(thread, 5000) != 0) ExitProcess(12);
    if (!GetExitCodeThread(thread, &code) || code != 0x6181) ExitProcess(13);
    CloseHandle(thread);
    if (handled != 2) ExitProcess(14);
    ExitProcess(0);
}
