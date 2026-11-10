// SPDX-License-Identifier: GPL-2.0-or-later
// Own PE32 program and DLL for tools/test_toolhelp_modules.py, not Windows
// binaries: a process with more modules than a small snapshot once held.
#ifdef LIBRARY
// An address in its data gives the DLL a relocation table: 48 copies of it
// cannot all load at the base they were linked for.
void *anchor = &anchor;
int __stdcall DllMain(void *instance, unsigned reason, void *reserved) { (void)instance; (void)reason; (void)reserved; return anchor != 0; }
#else
__declspec(dllimport) void *__stdcall LoadLibraryA(const char *);
__declspec(dllimport) void *__stdcall CreateToolhelp32Snapshot(unsigned, unsigned);
__declspec(dllimport) int __stdcall Module32First(void *, void *);
__declspec(dllimport) int __stdcall Module32Next(void *, void *);
__declspec(dllimport) int __stdcall CloseHandle(void *);
__declspec(dllimport) void __stdcall ExitProcess(unsigned);
enum { Libraries = 48 };
void mainCRTStartup(void)
{
    static void *loaded[Libraries];
    static unsigned entry[137];
    char name[] = "M00.DLL";
    for (unsigned i = 0; i < Libraries; ++i)
    {
        name[1] = '0' + i / 10; name[2] = '0' + i % 10;
        if (!(loaded[i] = LoadLibraryA(name))) ExitProcess(20);
    }
    // Three snapshots, so a list written past its end meets the next one.
    for (unsigned round = 0; round < 3; ++round)
    {
        void *snapshot = CreateToolhelp32Snapshot(8, 0);
        if (snapshot == (void *)-1) ExitProcess(21);
        unsigned rows = 0, found = 0;
        entry[0] = sizeof entry;
        for (int more = Module32First(snapshot, entry); more; more = Module32Next(snapshot, entry))
        {
            ++rows;
            for (unsigned i = 0; i < Libraries; ++i) if ((void *)entry[5] == loaded[i]) ++found;
            entry[0] = sizeof entry;
        }
        if (found != Libraries) ExitProcess(22);
        if (rows < Libraries + 2) ExitProcess(23);
        if (!CloseHandle(snapshot)) ExitProcess(24);
    }
    ExitProcess(7);
}
#endif
