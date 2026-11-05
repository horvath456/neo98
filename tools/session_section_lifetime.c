/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>

#ifdef BUILD_DLL
__declspec(dllexport) __attribute__((section(".shared"))) DWORD value = 0x31415926;
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved) { return TRUE; }
#else
static void check(int okay, UINT code) { if (!okay) ExitProcess(code); }

void mainCRTStartup(void)
{
    const char *command = GetCommandLineA(), *arg = command;
    while (*arg && *arg != '!') ++arg;
    if (*arg)
    {
        unsigned mode = *++arg - '0', address = 0;
        for (++arg; *arg; ++arg)
            address = (address << 4) | (*arg <= '9' ? *arg - '0' : *arg - 'a' + 10);
        HANDLE ready = OpenEventA(EVENT_MODIFY_STATE, FALSE, "SectionLifetimeReady");
        HANDLE go = OpenEventA(SYNCHRONIZE, FALSE, "SectionLifetimeGo");
        check(ready && go, 10);
        check(!GetModuleHandleA("SECTLIFE.DLL"), 11);
        check(!IsBadReadPtr((void *)address, 4), 12); // Passive session mapping.
        HMODULE library = mode ? LoadLibraryA("SECTLIFE.DLL") : 0;
        check(!mode || library, 13);
        check(*(DWORD *)address == 0x12345678, 14);
        SetEvent(ready);
        check(WaitForSingleObject(go, 5000) == WAIT_OBJECT_0, 15);
        if (!mode)
            check(IsBadReadPtr((void *)address, 4), 16); // Last real owner freed it.
        else
        {
            check(*(DWORD *)address == 0x12345678, 17); // This owner still holds it.
            if (mode == 1) check(FreeLibrary(library), 18);
            // Mode 2 leaves the last module reference to process teardown.
        }
        ExitProcess(0);
    }
    HANDLE ready = CreateEventA(0, TRUE, FALSE, "SectionLifetimeReady");
    HANDLE go = CreateEventA(0, TRUE, FALSE, "SectionLifetimeGo");
    check(ready && go, 20);
    char exe[MAX_PATH], child[MAX_PATH + 32];
    GetModuleFileNameA(0, exe, sizeof exe);
    for (unsigned mode = 0; mode < 3; ++mode)
    {
        HMODULE library = LoadLibraryA("SECTLIFE.DLL");
        check(library != 0, 21);
        DWORD *value = (DWORD *)GetProcAddress(library, "value");
        check(value && *value == 0x31415926, 22);
        *value = 0x12345678;
        check(LoadLibraryA("SECTLIFE.DLL") == library, 23);
        check(FreeLibrary(library) && *value == 0x12345678, 24);
        ResetEvent(ready); ResetEvent(go);
        wsprintfA(child, "\"%s\" !%u%08lx", exe, mode, (DWORD)value);
        STARTUPINFOA si = {0}; PROCESS_INFORMATION pi = {0}; si.cb = sizeof si;
        check(CreateProcessA(0, child, 0, 0, FALSE, 0, 0, 0, &si, &pi), 25);
        check(WaitForSingleObject(ready, 5000) == WAIT_OBJECT_0, 26);
        check(FreeLibrary(library) && !GetModuleHandleA("SECTLIFE.DLL"), 27);
        SetEvent(go);
        check(WaitForSingleObject(pi.hProcess, 5000) == WAIT_OBJECT_0, 28);
        DWORD code = 99; check(GetExitCodeProcess(pi.hProcess, &code) && !code, 40 + code);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        Sleep(50); // Win98 signals exit before completing module teardown.
        library = LoadLibraryA("SECTLIFE.DLL");
        value = (DWORD *)GetProcAddress(library, "value");
        check(value && *value == 0x31415926, 29); // Fresh backing after the last owner.
        check(FreeLibrary(library), 30);
    }
    OutputDebugStringA("SESSION SECTION LIFETIME PASS");
    ExitProcess(0);
}
#endif
