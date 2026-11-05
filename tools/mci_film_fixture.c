/* SPDX-License-Identifier: GPL-2.0-or-later */
/* An x86 program that plays a film through MCI command strings, the way the
   video player of a guest application and Media Player do: open with an alias, time
   in milliseconds, play, and the position asked a few times while it plays.
   Windows serves this with MCIAVI.DRV, AVIFILE and the installed codec.
   Writes what MCI answered into the result file. tools/test_mci_film_host.py.

       MCIFILM.EXE C:\FILM.AVI C:\RESULT.TXT */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API void Sleep(U);
API char *GetCommandLineA(void);
API U GetTickCount(void);
API U CreateFileA(const char *, U, U, U, U, U, U);
API U WriteFile(U, const void *, U, U *, U);
API U CloseHandle(U);
API int lstrlenA(const char *);
API char *lstrcpyA(char *, const char *);
API char *lstrcatA(char *, const char *);
API U mciSendStringA(const char *, char *, U, U);
API int PeekMessageA(U *, U, U, U, U);
API int DispatchMessageA(const U *);
__declspec(dllimport) int __cdecl wsprintfA(char *, const char *, ...);

static char report[2048];
static void Note(const char *name, const char *value)
{
    lstrcatA(report, name);
    lstrcatA(report, "=");
    lstrcatA(report, value);
    lstrcatA(report, "\r\n");
}
static void Number(const char *name, U value)
{
    char text[16];
    wsprintfA(text, "%u", value);
    Note(name, text);
}
static void Pump(U milliseconds)
{
    const U start = GetTickCount();
    U message[7];
    while (GetTickCount() - start < milliseconds)
    {
        while (PeekMessageA(message, 0, 0, 0, 1)) DispatchMessageA(message);
        Sleep(10);
    }
}
static char *Word(char *p, char *out)
{
    while (*p == ' ') ++p;
    while (*p && *p != ' ') *out++ = *p++;
    *out = 0;
    return p;
}
void mainCRTStartup(void)
{
    char film[260], result[260], command[400], text[64];
    char *p = GetCommandLineA();
    U written, file, i;
    p = Word(p, film);
    p = Word(p, film);
    Word(p, result);
    lstrcpyA(command, "open ");
    lstrcatA(command, film);
    lstrcatA(command, " alias film");
    Number("open", mciSendStringA(command, 0, 0, 0));
    Number("format", mciSendStringA("set film time format milliseconds", 0, 0, 0));
    text[0] = 0;
    Number("length_error", mciSendStringA("status film length", text, sizeof text, 0));
    Note("length", text);
    text[0] = 0;
    mciSendStringA("where film source", text, sizeof text, 0);
    Note("source", text);
    Number("play", mciSendStringA("play film", 0, 0, 0));
    for (i = 0; i < 3; ++i)
    {
        char name[16];
        Pump(700);
        text[0] = 0;
        mciSendStringA("status film position", text, sizeof text, 0);
        wsprintfA(name, "position%u", i);
        Note(name, text);
        text[0] = 0;
        mciSendStringA("status film mode", text, sizeof text, 0);
        wsprintfA(name, "mode%u", i);
        Note(name, text);
    }
    /* Left open and playing for a picture of the screen, then closed. */
    file = CreateFileA(result, 0x40000000u, 0, 0, 2, 0x80, 0);
    lstrcatA(report, "done=1\r\n");
    WriteFile(file, report, (U)lstrlenA(report), &written, 0);
    CloseHandle(file);
    Pump(4000);
    mciSendStringA("close film", 0, 0, 0);
    ExitProcess(0);
}
