/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Checks MCI_BREAK validation and Escape during MCIAVI wait playback.
   MCIBREAK.EXE C:\FILM.AVI C:\RESULT.TXT; tools/test_mci_break_host.py. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API char *GetCommandLineA(void);
API U GetTickCount(void);
API U CreateFileA(const char *, U, U, U, U, U, U);
API U WriteFile(U, const void *, U, U *, U);
API U CloseHandle(U);
API int lstrlenA(const char *);
API char *lstrcpyA(char *, const char *);
API char *lstrcatA(char *, const char *);
API U mciSendStringA(const char *, char *, U, U);
API U mciSendCommandA(U, U, U, const void *);
API U mciGetDeviceIDA(const char *);
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
    /* Unselected MCI_OPEN members are deliberately invalid pointers. */
    {
        U block[5] = {0, 0, (U)"waveaudio", 1, 1};
        Number("open_type_only", mciSendCommandA(0, 0x803, 0x2000, block));
        Number("close_type_only", mciSendCommandA(block[1], 0x804, 0, 0));
        block[1] = 0; block[2] = 1; block[3] = (U)film;
        Number("open_element_only", mciSendCommandA(0, 0x803, 0x200, block));
        Number("close_element_only", mciSendCommandA(block[1], 0x804, 0, 0));
    }
    lstrcpyA(command, "open ");
    lstrcatA(command, film);
    lstrcatA(command, " alias film");
    Number("open", mciSendStringA(command, 0, 0, 0));
    Number("break_missing", mciSendStringA("break film", 0, 0, 0));
    Number("break_off", mciSendStringA("break film off", 0, 0, 0));
    Number("break_conflict", mciSendStringA("break film on 27 off", 0, 0, 0));
    {
        U block[3] = {0, 27, 0};
        U id = mciGetDeviceIDA("film");
        Number("command_key", mciSendCommandA(id, 0x811, 0x100, block));
        Number("command_off", mciSendCommandA(id, 0x811, 0x400, 0));
        Number("command_missing", mciSendCommandA(id, 0x811, 0, block));
        Number("command_conflict", mciSendCommandA(id, 0x811, 0x500, block));
        Number("command_invalid_device", mciSendCommandA(0x1234, 0x811, 0x100, block));
    }
    Number("break_key", mciSendStringA("break film on 27 wait", 0, 0, 0));
    i = GetTickCount();
    Number("play_wait", mciSendStringA("play film wait", 0, 0, 0));
    Number("elapsed", GetTickCount() - i);
    text[0] = 0;
    mciSendStringA("status film position", text, sizeof text, 0);
    Note("position", text);
    Number("off_again", mciSendStringA("break film off", 0, 0, 0));
    Number("rewind", mciSendStringA("seek film to start wait", 0, 0, 0));
    i = GetTickCount();
    Number("play_off", mciSendStringA("play film wait", 0, 0, 0));
    Number("elapsed_off", GetTickCount() - i);
    file = CreateFileA(result, 0x40000000u, 0, 0, 2, 0x80, 0);
    lstrcatA(report, "done=1\r\n");
    WriteFile(file, report, (U)lstrlenA(report), &written, 0);
    CloseHandle(file);

    mciSendStringA("close film", 0, 0, 0);
    ExitProcess(0);
}
