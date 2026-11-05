/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A real 32-bit LoadLibrary16 call remains on the stack while NE LibMain waits. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U LoadLibrary16(const char *);
API void FreeLibrary16(U);
API int MessageBoxA(U, const char *, const char *, U);
API void ExitProcess(U);
API U GetCurrentThreadId(void);
API U SetWindowsHookExA(int, void *, U, U);
API int UnhookWindowsHookEx(U);
#ifndef CHECK_HOSTED_ALIAS
#define CHECK_HOSTED_ALIAS 0
#endif
static U target_window, destroyed;
static U __stdcall hook(int code, U window, U data)
{
    (void)data;
    if (code == 3 && !target_window) target_window = window;
    if (code == 4 && window == target_window) ++destroyed;
    return 0;
}
#ifndef EXPECT_FAILURE
#define EXPECT_FAILURE 0
#endif
#ifndef RELOAD_WAIT
#define RELOAD_WAIT 0
#endif
void mainCRTStartup(void)
{
    U registration = CHECK_HOSTED_ALIAS ? SetWindowsHookExA(5, (void *)hook, 0, GetCurrentThreadId()) : 0;
    if (CHECK_HOSTED_ALIAS && !registration) {
        MessageBoxA(0, "Cannot install CBT hook", "FAIL hosted alias", 0);
        ExitProcess(3);
    }
    U module = LoadLibrary16("C:\\HELD16.DLL");
    if (EXPECT_FAILURE ? module != 20 : module < 32) {
        MessageBoxA(0, "LoadLibrary failed", "FAIL hosted call", 0);
        ExitProcess(1);
    }
    if (!EXPECT_FAILURE) FreeLibrary16(module);
    if (CHECK_HOSTED_ALIAS) {
        UnhookWindowsHookEx(registration);
        if (destroyed != 1) {
            MessageBoxA(0, "Flat hook did not return once from the saved hosted context", "FAIL hosted alias", 0);
            ExitProcess(4);
        }
    }
    MessageBoxA(0, "NE LibMain and flat caller returned once", "PASS hosted call", 0);
    if (RELOAD_WAIT) {
        module = LoadLibrary16("C:\\HELD16.DLL");
        if (module < 32) {
            MessageBoxA(0, "Second load failed", "FAIL hosted reload", 0);
            ExitProcess(2);
        }
        FreeLibrary16(module);
        MessageBoxA(0, "Second initializer and flat caller returned once", "PASS hosted reload", 0);
    }
    ExitProcess(0);
}
