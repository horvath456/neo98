// SPDX-License-Identifier: GPL-2.0-or-later
// The same thing opened and closed many times in one process, and what is
// left of it each time (tools/test_reopen_host.py).
//
//   REOPEN.EXE avi   C:\FILM.AVI  N   mciSendString: open, play to the end, close
//   REOPEN.EXE wave  C:\SOUND.WAV N   the same with the wave device
//   REOPEN.EXE sound C:\SOUND.WAV N   sndPlaySound, synchronous
//   REOPEN.EXE exec  C:\WINDOWS\WINVER.EXE N
//                                     WinExec, WM_CLOSE to the window of the
//                                     new process, wait until it has ended
//   REOPEN.EXE thread - N             a thread started and waited for
//
// After a round it counts the descriptors of the local descriptor table
// with LAR, the way a program finds out whether a selector is valid, and
// appends "round=descriptors" to C:\REOPEN.TXT. After the rounds 1, 2 and N it
// writes C:\REOPEN.Mn and waits for C:\REOPEN.Gn, so that the test can read the
// memory of the host at that moment.
#include <windows.h>
#include <mmsystem.h>

static char text[8192];
static int used;

static void line(const char *name, long value)
{
    if (used < (int)sizeof text - 64)
        used += wsprintfA(text + used, "%s=%ld\r\n", name, value);
}
static void save(void)
{
    DWORD written = 0;
    HANDLE file = CreateFileA("C:\\REOPEN.TXT", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    WriteFile(file, text, used, &written, 0);
    CloseHandle(file);
}
// The selectors of the local descriptor table that name a descriptor.
static int descriptors(void)
{
    int count = 0;
    unsigned index;
    for (index = 1; index < 8192; ++index)
    {
        unsigned selector = index * 8 + 7, rights = 0;
        unsigned char valid = 0;
        __asm__ volatile("larl %2, %0\n\tsetz %1" : "+r"(rights), "=q"(valid) : "r"(selector) : "cc");
        count += valid;
    }
    return count;
}
static void pump(void)
{
    MSG message;
    while (PeekMessageA(&message, 0, 0, 0, PM_REMOVE))
        DispatchMessageA(&message);
}
static void mark(int round)
{
    char name[32];
    HANDLE file;
    int waited;
    wsprintfA(name, "C:\\REOPEN.M%d", round);
    file = CreateFileA(name, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    CloseHandle(file);
    wsprintfA(name, "C:\\REOPEN.G%d", round);
    for (waited = 0; waited < 60000 && GetFileAttributesA(name) == 0xffffffffu; waited += 20)
        Sleep(20);
}
static DWORD wanted;
static HWND found;
static BOOL CALLBACK window_of(HWND window, LPARAM unused)
{
    DWORD process = 0;
    (void)unused;
    GetWindowThreadProcessId(window, &process);
    if (process == wanted && IsWindowVisible(window))
    {
        found = window;
        return FALSE;
    }
    return TRUE;
}
static DWORD WINAPI thread_body(void *given)
{
    return (DWORD)given;
}
// One round; zero when everything answered as it should.
static int once(const char *mode, const char *file)
{
    char command[400];
    if (!lstrcmpA(mode, "avi") || !lstrcmpA(mode, "wave"))
    {
        int error;
        wsprintfA(command, "open %s %salias thing", file, mode[0] == 'w' ? "type waveaudio " : "");
        error = mciSendStringA(command, 0, 0, 0);
        if (error)
            return error;
        error = mciSendStringA("play thing wait", 0, 0, 0);
        pump();
        if (error)
        {
            mciSendStringA("close thing", 0, 0, 0);
            return error;
        }
        return mciSendStringA("close thing", 0, 0, 0);
    }
    if (!lstrcmpA(mode, "sound"))
        return sndPlaySoundA(file, SND_SYNC | SND_NODEFAULT) ? 0 : 1;
    if (!lstrcmpA(mode, "thread"))
    {
        DWORD identity = 0, code = 0;
        HANDLE thread = CreateThread(0, 0, thread_body, (void *)0x1234, 0, &identity);
        if (!thread)
            return GetLastError() ? GetLastError() : 1;
        if (WaitForSingleObject(thread, 30000) != WAIT_OBJECT_0 || !GetExitCodeThread(thread, &code))
            code = 0;
        CloseHandle(thread);
        return code == 0x1234 && identity ? 0 : 2;
    }
    {
        STARTUPINFOA start = {sizeof start};
        PROCESS_INFORMATION child;
        int waited;
        lstrcpyA(command, file);
        if (!CreateProcessA(0, command, 0, 0, FALSE, 0, 0, 0, &start, &child))
            return GetLastError();
        wanted = child.dwProcessId;
        found = 0;
        for (waited = 0; waited < 30000 && !found; waited += 20)
        {
            EnumWindows(window_of, 0);
            if (!found)
                Sleep(20);
        }
        if (found)
            PostMessageA(found, WM_CLOSE, 0, 0);
        waited = WaitForSingleObject(child.hProcess, 30000);
        CloseHandle(child.hThread);
        CloseHandle(child.hProcess);
        return !found ? 2 : waited != WAIT_OBJECT_0 ? 3 : 0;
    }
}
static const char *word(const char *p, char *out)
{
    while (*p == ' ')
        ++p;
    while (*p && *p != ' ')
        *out++ = *p++;
    *out = 0;
    return p;
}
void mainCRTStartup(void)
{
    char mode[32], file[MAX_PATH], number[32];
    const char *p = GetCommandLineA();
    int rounds = 0, round, i, errors = 0, first_error = 0;
    p = word(p, mode);
    p = word(p, mode);
    p = word(p, file);
    word(p, number);
    for (i = 0; number[i] >= '0' && number[i] <= '9'; ++i)
        rounds = rounds * 10 + (number[i] - '0');
    line("before", descriptors());
    for (round = 1; round <= rounds; ++round)
    {
        char name[16];
        const int error = once(mode, file);
        if (error && !errors++)
            first_error = error;
        // Every round of a few, the first and the last of many.
        if (rounds <= 100 || round <= 3 || round == rounds)
        {
            wsprintfA(name, "%d", round);
            line(name, descriptors());
        }
        if (round == 1 || round == 2 || round == rounds)
        {
            save();
            mark(round);
        }
    }
    line("errors", errors);
    line("first error", first_error);
    line("done", 1);
    save();
    ExitProcess(0);
}
