/* SPDX-License-Identifier: GPL-2.0-or-later
 * The file of a running program and of a loaded DLL is open in KERNEL32:
 * others read it, nobody writes, deletes or renames it. InstallShield's
 * deleter (_ISDEL.EXE) relies on it: it opens Setup's program for writing
 * with OF_SHARE_EXCLUSIVE every three seconds and deletes Setup's temporary
 * files as soon as that succeeds.
 *
 * Started without an argument from any drive, the program copies itself to
 * C:\IMGHOLD1.EXE and runs the copy with "probe"; its exit code is the
 * copy's: 0, or the number of the first check that failed. The copy writes
 * what it saw to C:\IMGHOLD.TXT. */
#include <windows.h>

static HANDLE report;

static void say(const char *what, DWORD value, DWORD error)
{
    char text[160];
    DWORD written;
    wsprintfA(text, "%s=%lu error=%lu\r\n", what, value, error);
    WriteFile(report, text, lstrlenA(text), &written, NULL);
}

static int has(const char *line, const char *word)
{
    for (; *line; ++line)
    {
        const char *a = line, *b = word;
        while (*b && *a == *b) { ++a; ++b; }
        if (!*b) return 1;
    }
    return 0;
}

/* Whether the file can be opened for writing, the way _ISDEL asks. */
static int writable(const char *path, DWORD *error)
{
    OFSTRUCT of;
    HFILE file = OpenFile(path, &of, OF_WRITE | OF_SHARE_EXCLUSIVE);
    *error = file == HFILE_ERROR ? GetLastError() : 0;
    if (file != HFILE_ERROR) _lclose(file);
    return file != HFILE_ERROR;
}

static int readable(const char *path)
{
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    CloseHandle(file);
    return 1;
}

static HANDLE run(const char *command)
{
    STARTUPINFOA start = {sizeof start};
    PROCESS_INFORMATION process;
    char line[300];
    lstrcpyA(line, command);
    if (!CreateProcessA(NULL, line, NULL, NULL, FALSE, 0, NULL, NULL, &start, &process)) return NULL;
    CloseHandle(process.hThread);
    return process.hProcess;
}

static DWORD probe(void)
{
    char self[260];
    DWORD error, code = 0;
    HANDLE child, file;
    HMODULE library;
    report = CreateFileA("C:\\IMGHOLD.TXT", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    GetModuleFileNameA(NULL, self, sizeof self);

    /* 1 to 4: the running program itself. */
    if (!readable(self)) return 1;
    if (writable(self, &error) || error != ERROR_SHARING_VIOLATION) { say("self write", 1, error); return 2; }
    say("self write refused", 1, error);
    file = CreateFileA(self, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    error = GetLastError();
    if (file != INVALID_HANDLE_VALUE || error != ERROR_SHARING_VIOLATION) { say("self CreateFile write", 1, error); return 2; }
    if (DeleteFileA(self) || GetLastError() != ERROR_ACCESS_DENIED) { say("self delete", 1, GetLastError()); return 3; }
    say("self delete refused", 1, GetLastError());
    if (MoveFileA(self, "C:\\IMGHOLDX.EXE") || GetLastError() != ERROR_ACCESS_DENIED) { say("self rename", 1, GetLastError()); return 4; }
    say("self rename refused", 1, GetLastError());

    /* 5 to 9: a DLL, before, while and after it is loaded. */
    if (!CopyFileA("C:\\WINDOWS\\SYSTEM\\VERSION.DLL", "C:\\IMGHOLD.DLL", FALSE)) { say("copy dll", 0, GetLastError()); return 5; }
    SetFileAttributesA("C:\\IMGHOLD.DLL", FILE_ATTRIBUTE_NORMAL);
    if (!writable("C:\\IMGHOLD.DLL", &error)) { say("dll write before load", 0, error); return 6; }
    library = LoadLibraryA("C:\\IMGHOLD.DLL");
    if (!library) { say("load dll", 0, GetLastError()); return 7; }
    if (!readable("C:\\IMGHOLD.DLL") || writable("C:\\IMGHOLD.DLL", &error) || error != ERROR_SHARING_VIOLATION ||
        DeleteFileA("C:\\IMGHOLD.DLL"))
    { say("loaded dll", 1, error); return 8; }
    say("loaded dll refused", 1, error);
    FreeLibrary(library);
    if (!writable("C:\\IMGHOLD.DLL", &error) || !DeleteFileA("C:\\IMGHOLD.DLL")) { say("freed dll", 0, GetLastError()); return 9; }
    say("freed dll deleted", 1, 0);

    /* 10 to 13: another program, while it runs and after it has ended. */
    if (!CopyFileA(self, "C:\\IMGHOLD2.EXE", FALSE)) { say("copy program", 0, GetLastError()); return 10; }
    child = run("C:\\IMGHOLD2.EXE sleep");
    if (!child) { say("start program", 0, GetLastError()); return 11; }
    Sleep(500);
    if (writable("C:\\IMGHOLD2.EXE", &error) || error != ERROR_SHARING_VIOLATION || DeleteFileA("C:\\IMGHOLD2.EXE"))
    { say("running program", 1, error); code = 12; }
    else say("running program refused", 1, error);
    WaitForSingleObject(child, 20000);
    CloseHandle(child);
    if (!code && (!writable("C:\\IMGHOLD2.EXE", &error) || !DeleteFileA("C:\\IMGHOLD2.EXE")))
    { say("ended program", 0, GetLastError()); code = 13; }
    else if (!code) say("ended program deleted", 1, 0);
    CloseHandle(report);
    return code;
}

void mainCRTStartup(void)
{
    const char *line = GetCommandLineA();
    char self[260];
    HANDLE child;
    DWORD code = 99;
    if (has(line, " sleep")) { Sleep(3000); ExitProcess(0); }
    if (has(line, " probe")) ExitProcess(probe());
    GetModuleFileNameA(NULL, self, sizeof self);
    if (!CopyFileA(self, "C:\\IMGHOLD1.EXE", FALSE)) ExitProcess(90);
    SetFileAttributesA("C:\\IMGHOLD1.EXE", FILE_ATTRIBUTE_NORMAL);
    child = run("C:\\IMGHOLD1.EXE probe");
    if (!child) ExitProcess(91);
    WaitForSingleObject(child, 60000);
    GetExitCodeProcess(child, &code);
    ExitProcess(code);
}
