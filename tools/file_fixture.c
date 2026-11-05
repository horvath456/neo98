/* SPDX-License-Identifier: GPL-2.0-or-later
 * Own x86 file and implemented path contract, also run at ARM boot. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U GetLastError(void);
API U CreateFileA(const char *, U, U, U, U, U, U);
API U ReadFile(U, void *, U, U *, U);
API U WriteFile(U, const void *, U, U *, U);
API U CloseHandle(U);
API U GetFileSize(U, U *);
API U GetFileType(U);
API U SetFilePointer(U, int, int *, U);
API U SetEndOfFile(U);
API U FlushFileBuffers(U);
API U SetCurrentDirectoryA(const char *);
API U GetCurrentDirectoryA(U, char *);
API U GetFullPathNameA(const char *, U, char *, char **);
API U GetFileAttributesA(const char *);
API U SetFileAttributesA(const char *, U);
API U CreateDirectoryA(const char *, U);
API U RemoveDirectoryA(const char *);
API U MoveFileA(const char *, const char *);
API U DeleteFileA(const char *);
API U FindFirstFileA(const char *, void *);
API U FindNextFileA(U, void *);
API U FindClose(U);
API U _lopen(const char *, U);
API U _lcreat(const char *, U);
API U _lwrite(U, const void *, U);
API U _lread(U, void *, U);
API U _llseek(U, int, U);
API U _lclose(U);
API char *PathCombineA(char *, const char *, const char *);
API U PathCanonicalizeA(char *, const char *);
API char *PathFindFileNameA(const char *);
API char *PathFindExtensionA(const char *);
API U PathRemoveFileSpecA(char *);
static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}
static int equal(const char *a, const char *b)
{
    while (*a && *a == *b)
        ++a, ++b;
    return *a == *b;
}
void run_file_test(void)
{
    char path[260], bytes[32];
    check(PathCombineA(path, "C:\\WINDOWS", "..\\TEMP\\file.txt") == path &&
              equal(path, "C:\\TEMP\\file.txt"),
          501);
    check(equal(PathFindFileNameA(path), "file.txt") && equal(PathFindExtensionA(path), ".txt"), 502);
    check(PathRemoveFileSpecA(path) && equal(path, "C:\\TEMP"), 503);
    check(PathCanonicalizeA(path, "C:\\a\\.\\b\\..\\c") && equal(path, "C:\\a\\c"), 504);
    /* Scratch below the TEMP folder Win98 Setup creates. */
    check(CreateDirectoryA("C:\\WINDOWS\\TEMP\\FileContract", 0), 505);
    check(SetCurrentDirectoryA("C:/WINDOWS/TEMP/FileContract"), 506);
    /* Win98 probes but otherwise ignores directory SECURITY_ATTRIBUTES.
       InstallShield passes a real structure, including on Windows 98. */
    U security[3] = {12, 0, 1};
    check(CreateDirectoryA("Security", (U)security), 534);
    check(!CreateDirectoryA("Security", (U)security) && GetLastError() == 183, 535);
    check(RemoveDirectoryA("Security"), 536);
    security[0] = 0;
    security[1] = 0xdeadbeef;
    check(CreateDirectoryA("IgnoredSecurity", (U)security), 537);
    check(RemoveDirectoryA("IgnoredSecurity"), 538);
    check(!CreateDirectoryA("InvalidSecurity", 0xdeadbeef) && GetLastError() == 87, 539);
    check(GetFileAttributesA("InvalidSecurity") == ~0u, 540);
    check(GetCurrentDirectoryA(260, path) == 28 && equal(path, "C:\\WINDOWS\\TEMP\\FileContract"), 507);
    char *part = 0;
    check(GetFullPathNameA("..\\FileContract\\contract.txt", 260, path, &part) == 41 &&
              equal(part, "contract.txt"),
          508);
    U h = CreateFileA("contract.txt", 0xc0000000, 1, 0, 1, 128, 0), done = 99;
    check(h != ~0u && GetFileType(h) == 1, 509);
    check(WriteFile(h, "hello", 5, &done, 0) && done == 5 && GetFileSize(h, 0) == 5, 510);
    check(CreateFileA("contract.txt", 0x40000000, 3, 0, 3, 0, 0) == ~0u && GetLastError() == 32, 511);
    check(SetFilePointer(h, -1, 0, 0) == ~0u && GetLastError() == 131, 512);
    check(SetFilePointer(h, 8, 0, 0) == 8 && WriteFile(h, "!", 1, &done, 0), 513);
    check(SetFilePointer(h, 0, 0, 0) == 0 && ReadFile(h, bytes, 32, &done, 0) && done == 9, 514);
    check(bytes[0] == 'h' && bytes[4] == 'o' && !bytes[5] && !bytes[6] && !bytes[7] && bytes[8] == '!', 515);
    check(ReadFile(h, bytes, 32, &done, 0) && done == 0, 516);
    check(SetFilePointer(h, 2, 0, 0) == 2 && SetEndOfFile(h) && GetFileSize(h, 0) == 2, 517);
    check(FlushFileBuffers(h) && CloseHandle(h) && !CloseHandle(h) && GetLastError() == 6, 518);
    U data[80];
    h = FindFirstFileA("*.txt", data);
    check(h != ~0u && data[8] == 2 && equal((char *)data + 44, "contract.txt"), 519);
    check(!FindNextFileA(h, data) && GetLastError() == 18 && FindClose(h), 520);
    check(!FindClose(h) && GetLastError() == 6, 521);
    check(SetFileAttributesA("contract.txt", 1) && (GetFileAttributesA("contract.txt") & 1), 522);
    check(!DeleteFileA("contract.txt") && GetLastError() == 5, 523);
    check(SetFileAttributesA("contract.txt", 128), 524);
    h = _lopen("contract.txt", 0x42);
    check(h >= 5 && h < 256 && _llseek(h, 1, 0) == 1 && _lwrite(h, "", 0) == 0 && GetFileSize(h, 0) == 1, 525);
    check(_llseek(h, 0, 0) == 0 && _lread(h, bytes, 32) == 1 && bytes[0] == 'h' && !_lclose(h), 526);
    h = _lcreat("legacy", 0);
    check(h >= 5 && h < 256 && _lwrite(h, "ARM", 3) == 3 && !_lclose(h), 527);
    check(DeleteFileA("legacy") && MoveFileA("contract.txt", "renamed") && DeleteFileA("renamed"), 528);
    check(SetCurrentDirectoryA("C:\\") && RemoveDirectoryA("C:\\WINDOWS\\TEMP\\FileContract"), 529);
}
