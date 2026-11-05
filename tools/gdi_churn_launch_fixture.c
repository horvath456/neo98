/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Starts a program the way it starts late in a long session.
 *
 * CHURN.EXE D:\DIR\PROGRAM.EXE
 *
 * Before the start every GDI slot is taken and freed forty times, so the free
 * slots carry generations that no longer fit the word a 16-bit program keeps
 * of a handle. Explorer reaches the same state after a while on its own. The
 * program runs with its own directory as the current directory, as it does
 * from an Explorer folder window; the launcher then waits for messages. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U CreateProcessA(const char *, char *, U, U, U, U, U, const char *, U *, U *);
API char *GetCommandLineA(void);
API U CreateSolidBrush(U);
API U DeleteObject(U);
API U GetMessageA(U *, U, U, U);
API U DispatchMessageA(const U *);
static U brushes[2100];
static char command[260], directory[260];
void mainCRTStartup(void)
{
    const char *line = GetCommandLineA();
    if (*line == '"') { ++line; while (*line && *line != '"') ++line; if (*line) ++line; }
    else while (*line && *line != ' ') ++line;
    while (*line == ' ') ++line;
    U n = 0, slash = 0;
    for (; line[n] && n < sizeof command - 1; ++n) { command[n] = directory[n] = line[n]; if (line[n] == '\\') slash = n; }
    if (!n || !slash) ExitProcess(1000);
    directory[slash] = 0;
    for (U round = 0; round < 40; ++round)
    {
        U count = 0;
        while (count < 2100 && (brushes[count] = CreateSolidBrush(round))) ++count;
        for (U i = 0; i < count; ++i) DeleteObject(brushes[i]);
    }
    U startup[17] = {68}, process[4] = {0};
    if (!CreateProcessA(0, command, 0, 0, 0, 0, 0, directory, startup, process)) ExitProcess(1001);
    U message[7];
    while (GetMessageA(message, 0, 0, 0)) DispatchMessageA(message);
    ExitProcess(0);
}
