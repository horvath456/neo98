/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Two processes that never wait: each calls Windows in a loop for a second
   and a half and counts its rounds in shared memory. The program starts
   itself a second time; the parent then waits for the child and compares.
   The exit code names what failed, 0 is success. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U GetModuleFileNameA(U, char *, U);
API U CreateProcessA(const char *, char *, void *, void *, U, U, void *, const char *, void *, void *);
API U CreateFileMappingA(U, void *, U, U, U, const char *);
API U OpenFileMappingA(U, U, const char *);
API U *MapViewOfFile(U, U, U, U, U);
API U WaitForSingleObject(U, U);
API U GetExitCodeProcess(U, U *);
API U GetTickCount(void);
API U GetFileAttributesA(const char *);
enum { ParentRounds, ChildRounds };
enum { Milliseconds = 1500, Enough = 100 };
static void Work(volatile U *rounds)
{
    U start = GetTickCount();
    while (GetTickCount() - start < Milliseconds) {
        if (GetFileAttributesA("C:\\WINDOWS") == 0xffffffff) ExitProcess(20);
        ++*rounds;
    }
}
void mainCRTStartup(void)
{
    char path[260];
    U startup[17] = {68}, process[4] = {0}, code = 99;
    U mapping = OpenFileMappingA(0xf001f, 0, "NEO98TurnProgress");
    if (mapping) {
        U *shared = MapViewOfFile(mapping, 0xf001f, 0, 0, 0);
        if (!shared) ExitProcess(30);
        Work(&shared[ChildRounds]);
        ExitProcess(0);
    }
    mapping = CreateFileMappingA(0xffffffff, 0, 4, 0, 4096, "NEO98TurnProgress");
    U *shared = mapping ? MapViewOfFile(mapping, 0xf001f, 0, 0, 0) : 0;
    if (!shared) ExitProcess(1);
    if (!GetModuleFileNameA(0, path, sizeof path)) ExitProcess(2);
    if (!CreateProcessA(path, 0, 0, 0, 0, 0, 0, 0, startup, process)) ExitProcess(3);
    Work(&shared[ParentRounds]);
    if (WaitForSingleObject(process[0], 30000)) ExitProcess(4);
    if (!GetExitCodeProcess(process[0], &code) || code) ExitProcess(code ? code : 5);
    /* Both were at work at the same time and both got on. */
    if (shared[ParentRounds] < Enough) ExitProcess(10);
    if (shared[ChildRounds] < Enough) ExitProcess(11);
    ExitProcess(0);
}
