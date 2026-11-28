/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 *
 * A file the host puts into the drive's folder under a name the guest
 * deleted (tools/test_host_recreated_file.py). For each case the program
 * removes or asks for its file, writes C:\HRSTEPn.MRK and waits until
 * C:\HRGOn.MRK exists: in that time the test writes the file into the
 * drive's folder on the host. Then the guest looks at it the way programs
 * do: GetFileAttributes, FindFirstFile by name and by a pattern, CreateFile
 * and ReadFile. C:\HRRESULT.TXT gets one line per case:
 *
 *   n <attributes> <find by name> <find by pattern> <open> <content>
 *
 * and a last line with the milliseconds of 2000 calls of GetFileAttributes
 * on an existing and on a missing file and of 500 FindFirstFile, the cost
 * of looking at the host each time.
 */
#include <windows.h>

static HANDLE out;
static void put(const char *s)
{
    DWORD n;
    WriteFile(out, s, lstrlenA(s), &n, 0);
}
static void marker(const char *name)
{
    HANDLE h = CreateFileA(name, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    if (h != INVALID_HANDLE_VALUE)
        CloseHandle(h);
}
static void wait_for(const char *name)
{
    DWORD start = GetTickCount();
    while (GetFileAttributesA(name) == 0xFFFFFFFF && GetTickCount() - start < 30000)
        Sleep(20);
}
static int found(const char *pattern, const char *leaf)
{
    WIN32_FIND_DATAA data;
    HANDLE h = FindFirstFileA(pattern, &data);
    int seen = 0;
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    do
        seen = seen || !lstrcmpiA(data.cFileName, leaf);
    while (FindNextFileA(h, &data));
    FindClose(h);
    return seen;
}
/* Case n: `prepare` 0 asks for a name that never existed, 1 deletes a file
   the guest wrote first, 2 deletes a file that was there at the start. */
static void check(int n, const char *path, const char *folder, const char *leaf, int prepare)
{
    char step[32], go[32], line[300], pattern[260], content[64] = "-";
    DWORD attributes, read = 0;
    HANDLE h;
    int open = 0;
    if (prepare == 1) {
        h = CreateFileA(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
        if (h != INVALID_HANDLE_VALUE) {
            WriteFile(h, "guest", 5, &read, 0);
            CloseHandle(h);
        }
    }
    if (prepare)
        DeleteFileA(path);
    /* The guest looks once more after the deletion, as Explorer does. */
    GetFileAttributesA(path);
    found(path, leaf);
    wsprintfA(step, "C:\\HRSTEP%d.MRK", n);
    wsprintfA(go, "C:\\HRGO%d.MRK", n);
    marker(step);
    wait_for(go);
    attributes = GetFileAttributesA(path);
    wsprintfA(pattern, "%s\\*", folder);
    h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
    if (h != INVALID_HANDLE_VALUE) {
        open = 1;
        if (ReadFile(h, content, sizeof content - 1, &read, 0))
            content[read] = 0;
        CloseHandle(h);
    }
    wsprintfA(line, "%d %lx %d %d %d %s\r\n", n, attributes, found(path, leaf), found(pattern, leaf), open, content);
    put(line);
}
void mainCRTStartup(void)
{
    char line[200];
    DWORD start, a, b, c;
    int i;
    out = CreateFileA("C:\\HRRESULT.TXT", GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
    CreateDirectoryA("C:\\HRDIR", 0);
    check(1, "C:\\HRNEW1.TXT", "C:", "HRNEW1.TXT", 0);
    check(2, "C:\\HRNEW2.TXT", "C:", "HRNEW2.TXT", 1);
    check(3, "C:\\HRNEW3.TXT", "C:", "HRNEW3.TXT", 2);
    check(4, "C:\\HRDIR\\HRNEW4.TXT", "C:\\HRDIR", "HRNEW4.TXT", 1);
    check(5, "C:\\WINDOWS\\HRNEW5.TXT", "C:\\WINDOWS", "HRNEW5.TXT", 2);
    start = GetTickCount();
    for (i = 0; i < 2000; i++)
        GetFileAttributesA("C:\\WINDOWS\\WIN.INI");
    a = GetTickCount() - start;
    start = GetTickCount();
    for (i = 0; i < 2000; i++)
        GetFileAttributesA("C:\\WINDOWS\\NOSUCH.INI");
    b = GetTickCount() - start;
    start = GetTickCount();
    for (i = 0; i < 500; i++)
        found("C:\\WINDOWS\\WIN.INI", "WIN.INI");
    c = GetTickCount() - start;
    wsprintfA(line, "cost %lu %lu %lu\r\n", a, b, c);
    put(line);
    CloseHandle(out);
    ExitProcess(0);
}
