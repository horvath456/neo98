/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A 32-bit window procedure and a 16-bit one in front of each other, on a
   window of a 16-bit program. Built twice: with BUILD_DLL as C:\SUBMIX.DLL,
   otherwise as the program C:\SUBMIX.EXE that sets the WH_CBT hook of the
   DLL for the session, starts the 16-bit C:\SUBMIX16.EXE
   (tools/subclass_mix_win16_fixture.asm) and waits until it has written its
   results into C:\SUBMIX16.TXT (it makes C:\SUBMIX16.END after them).

   The hook runs in the process of the 16-bit program. It puts Sub32 in front
   of a window of class "SubMixA" when the window is made (HCBT_CREATEWND),
   before the 16-bit program puts its own procedure in front of it; and in
   front of a window of class "SubMixB" when the program minimises it
   (HCBT_MINMAX), after the 16-bit procedure. Sub32 answers message 401h with
   16 times what the procedure behind it answered, plus 3; 403h with 1 when
   GetWindowLongA(GWL_WNDPROC) names Sub32 itself and 2 otherwise; and 404h
   by writing back what GetWindowLongA(GWL_WNDPROC) read.
   tools/test_win16_subclass_mix_host.py. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API void Sleep(U);
API U LoadLibraryA(const char *);
API U GetProcAddress(U, const char *);
API U WinExec(const char *, U);
API U CreateFileA(const char *, U, U, U, U, U, U);
API U CloseHandle(U);
API U SetWindowsHookExA(int, U, U, U);
API int UnhookWindowsHookEx(U);
API int CallNextHookEx(U, int, U, int);
API int GetClassNameA(U, char *, int);
API int lstrcmpA(const char *, const char *);
API U GetWindowLongA(U, int);
API U SetWindowLongA(U, int, U);
API U CallWindowProcA(U, U, U, U, U);

#ifdef BUILD_DLL
static U window_a, original_a, window_b, original_b;

static U __stdcall Sub32(U window, U message, U wparam, U lparam)
{
    const U original = window == window_a ? original_a : original_b;
    if (message == 0x401)
        return CallWindowProcA(original, window, message, wparam, lparam) * 16 + 3;
    if (message == 0x403)
        return GetWindowLongA(window, -4) == (U)Sub32 ? 1 : 2;
    if (message == 0x404)
    {
        SetWindowLongA(window, -4, GetWindowLongA(window, -4));
        return 1;
    }
    return CallWindowProcA(original, window, message, wparam, lparam);
}
static int Named(U window, const char *name)
{
    char text[32];
    return GetClassNameA(window, text, sizeof text) && !lstrcmpA(text, name);
}
__declspec(dllexport) int __stdcall SubclassCbt(int code, U wp, int lp)
{
    if (code == 3 && !window_a && Named(wp, "SubMixA")) /* HCBT_CREATEWND */
    {
        window_a = wp;
        original_a = SetWindowLongA(wp, -4, (U)Sub32);
    }
    if (code == 1 && !window_b && Named(wp, "SubMixB")) /* HCBT_MINMAX */
    {
        window_b = wp;
        original_b = SetWindowLongA(wp, -4, (U)Sub32);
    }
    return CallNextHookEx(0, code, wp, lp);
}
int __stdcall DllMain(U instance, U reason, U reserved)
{
    (void)instance; (void)reason; (void)reserved;
    return 1;
}
#else
void __stdcall mainCRTStartup(void)
{
    const U library = LoadLibraryA("C:\\SUBMIX.DLL");
    const U procedure = library ? GetProcAddress(library, "SubclassCbt") : 0;
    const U hook = procedure ? SetWindowsHookExA(5, procedure, library, 0) : 0; /* WH_CBT, the session */
    if (hook && WinExec("C:\\SUBMIX16.EXE", 1) > 31)
        for (int i = 0; i < 300; ++i)
        {
            const U file = CreateFileA("C:\\SUBMIX16.END", 0x80000000u, 1, 0, 3, 0x80, 0);
            if (file != 0xffffffffu)
            {
                CloseHandle(file);
                break;
            }
            Sleep(100);
        }
    if (hook)
        UnhookWindowsHookEx(hook);
    ExitProcess(0);
}
#endif
