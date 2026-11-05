/* SPDX-License-Identifier: GPL-2.0-or-later */
/* An x86 program that plays a film the original way of Windows 98: MCI with
   the AVI driver (MCIAVI.DRV) and the installed codec (ICCVID.DLL for
   Cinepak), in a window of its own, once to the end. It writes what MCI
   reports into the file named after the film. The counterpart of
   sdk/samples/player for tools/measure_arm64_player.py.

       MCIPLAY.EXE C:\FILM.AVI C:\RESULT.TXT */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API char *GetCommandLineA(void);
API U GetTickCount(void);
API U CreateFileA(const char *, U, U, U, U, U, U);
API U WriteFile(U, const void *, U, U *, U);
API U CloseHandle(U);
API int lstrlenA(const char *);
API U mciSendCommandA(U, U, U, U);
__declspec(dllimport) int __cdecl wsprintfA(char *, const char *, ...);

static char *Word(char *p, char *out)
{
    while (*p == ' ') ++p;
    while (*p && *p != ' ') *out++ = *p++;
    *out = 0;
    return p;
}
void mainCRTStartup(void)
{
    char film[260], result[260], line[512];
    char *p = GetCommandLineA();
    /* MCI_OPEN_PARMS, MCI_PLAY_PARMS, MCI_STATUS_PARMS, MCI_GENERIC_PARMS. */
    U open[5] = {0}, play[3] = {0}, status[4] = {0}, generic[1] = {0};
    U errors[5], start, elapsed, written, file, length, skipped, position, device;
    p = Word(p, film); /* the program itself */
    p = Word(p, film);
    Word(p, result);
    open[2] = (U) "avivideo";
    open[3] = (U)film;
    /* MCI_OPEN with MCI_OPEN_TYPE and MCI_OPEN_ELEMENT. */
    errors[0] = mciSendCommandA(0, 0x803, 0x2000 | 0x200, (U)open);
    device = open[1];
    /* MCI_STATUS, MCI_STATUS_ITEM: MCI_STATUS_LENGTH, in frames. */
    status[2] = 1;
    errors[1] = mciSendCommandA(device, 0x814, 0x100, (U)status);
    length = status[1];
    start = GetTickCount();
    /* MCI_PLAY with MCI_WAIT. */
    errors[2] = mciSendCommandA(device, 0x806, 2, (U)play);
    elapsed = GetTickCount() - start;
    /* MCI_AVI_STATUS_FRAMES_SKIPPED, and MCI_STATUS_POSITION. */
    status[1] = 0;
    status[2] = 0x8001;
    errors[3] = mciSendCommandA(device, 0x814, 0x100, (U)status);
    skipped = status[1];
    status[1] = 0;
    status[2] = 2;
    mciSendCommandA(device, 0x814, 0x100, (U)status);
    position = status[1];
    errors[4] = mciSendCommandA(device, 0x804, 0, (U)generic);
    wsprintfA(line, "open=%u\r\nlength=%u\r\nlength_error=%u\r\nplay=%u\r\nwall_ms=%u\r\nskipped=%u\r\nskipped_error=%u\r\nposition=%u\r\nclose=%u\r\ndone=1\r\n",
              errors[0], length, errors[1], errors[2], elapsed, skipped, errors[3], position, errors[4]);
    file = CreateFileA(result, 0x40000000u, 0, 0, 2, 0x80, 0);
    WriteFile(file, line, (U)lstrlenA(line), &written, 0);
    CloseHandle(file);
    ExitProcess(0);
}
