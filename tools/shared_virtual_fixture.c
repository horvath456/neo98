/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Win98 MEM_SHARED buffers, including an already running peer. */
#include <windows.h>
static const char ready_name[] = "neo98-b6270-ready", go_name[] = "neo98-b6270-go";
static void require(int ok, unsigned code) { if (!ok) ExitProcess(code); }
static unsigned parse(const char *s)
{
    unsigned v = 0;
    for (unsigned i = 0; i < 8; ++i) v = (v << 4) | (s[i] <= '9' ? s[i] - '0' : s[i] - 'a' + 10);
    return v;
}
static void step(HANDLE ready, HANDLE go)
{
    require(SetEvent(ready) && WaitForSingleObject(go, 10000) == WAIT_OBJECT_0, 30);
}
void mainCRTStartup(void)
{
    const char *cmd = GetCommandLineA();
    MEMORY_BASIC_INFORMATION info;
    if (cmd[0] == 'c') {
        volatile DWORD *p = (void *)parse(cmd + 1);
        HANDLE ready = OpenEventA(EVENT_ALL_ACCESS, FALSE, ready_name);
        HANDLE go = OpenEventA(EVENT_ALL_ACCESS, FALSE, go_name);
        require(ready && go, 19);
        require(!IsBadWritePtr((void *)p, 16), 20);
        require(p[0] == 0x12345678 && p[1] == (DWORD)(p + 2) && p[2] == 0xabcdef01, 21);
        p[2] = 73;
        step(ready, go);
        require(VirtualQuery((void *)p, &info, sizeof info) && info.Protect == PAGE_READONLY && IsBadWritePtr((void *)p, 1), 22);
        step(ready, go);
        require(VirtualQuery((void *)p, &info, sizeof info) && info.State == MEM_RESERVE, 23);
        step(ready, go);
        require(!IsBadWritePtr((void *)p, 16) && !p[0] && !p[2], 24);
        p[2] = 99;
        step(ready, go);
        require(VirtualQuery((void *)p, &info, sizeof info) && info.State == MEM_FREE && IsBadReadPtr((void *)p, 1), 25);
        CloseHandle(ready); CloseHandle(go);
        ExitProcess(0);
    }
    DWORD *p = VirtualAlloc(NULL, 8192, 0x08001000, PAGE_READWRITE);
    require(p != NULL, 1);
    p[0] = 0x12345678; p[1] = (DWORD)(p + 2); p[2] = 0xabcdef01;
    HANDLE ready = CreateEventA(NULL, FALSE, FALSE, ready_name);
    HANDLE go = CreateEventA(NULL, FALSE, FALSE, go_name);
    require(ready && go, 2);
    char path[MAX_PATH], command[] = "c00000000";
    require(GetModuleFileNameA(NULL, path, sizeof path), 3);
    for (unsigned i = 0; i < 8; ++i) command[1+i] = "0123456789abcdef"[((DWORD)p >> (28 - 4*i)) & 15];
    STARTUPINFOA si = {sizeof si}; PROCESS_INFORMATION pi = {0};
    require(CreateProcessA(path, command, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi), 4);
    HANDLE waits[] = {ready, pi.hProcess};
    DWORD result = WaitForMultipleObjects(2, waits, FALSE, 10000), code = 0, old;
    if (result == WAIT_OBJECT_0 + 1) {
        require(GetExitCodeProcess(pi.hProcess, &code), 5);
        ExitProcess(code ? code : 6);
    }
    require(result == WAIT_OBJECT_0 && p[2] == 73, 7);
    require(VirtualProtect(p, 4096, PAGE_READONLY, &old) && old == PAGE_READWRITE, 8);
    require(SetEvent(go) && WaitForSingleObject(ready, 10000) == WAIT_OBJECT_0, 9);
    require(VirtualFree(p, 4096, MEM_DECOMMIT), 10);
    require(SetEvent(go) && WaitForSingleObject(ready, 10000) == WAIT_OBJECT_0, 11);
    require(VirtualAlloc(p, 4096, MEM_COMMIT, PAGE_READWRITE) == p, 12);
    require(SetEvent(go) && WaitForSingleObject(ready, 10000) == WAIT_OBJECT_0 && p[2] == 99, 13);
    require(VirtualFree(p, 0, MEM_RELEASE), 14);
    require(SetEvent(go) && WaitForSingleObject(pi.hProcess, 10000) == WAIT_OBJECT_0, 15);
    require(GetExitCodeProcess(pi.hProcess, &code) && code == 0, 16);
    CloseHandle(ready); CloseHandle(go); CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    for (unsigned i = 0; i < 128; ++i) {
        void *wanted = i ? NULL : (void *)0x90030000;
        p = VirtualAlloc(wanted, 4096, 0x08003000, PAGE_READWRITE);
        require(p && (!wanted || p == wanted) && !*p, 40);
        *p = i + 1;
        require(VirtualFree(p, 0, MEM_RELEASE), 41);
        require(VirtualQuery(p, &info, sizeof info) && info.State == MEM_FREE, 42);
    }
    p = VirtualAlloc(NULL, 128u << 20, 0x08002000, PAGE_READWRITE);
    require(p && VirtualQuery(p, &info, sizeof info) && info.State == MEM_RESERVE, 43);
    DWORD *last = (void *)((char *)p + (128u << 20) - 4096);
    require(VirtualAlloc(p, 4096, MEM_COMMIT, PAGE_READWRITE) == p &&
            VirtualAlloc(last, 4096, MEM_COMMIT, PAGE_READWRITE) == last, 44);
    *p = 17; *last = 29;
    require(*p == 17 && *last == 29 && VirtualFree(p, 0, MEM_RELEASE), 45);
    ExitProcess(0);
}
