/* SPDX-License-Identifier: GPL-2.0-or-later */
/* RUN.EXE of tools/test_wininit_native.py: starts the program its own command
   line names, in that program's folder, as Explorer does, waits for it and
   exits with its exit code. "RUN.EXE D:\DXSETUP.EXE /silent" runs DXSETUP
   from the unpacked package. A failed start exits with 0x10000 plus the
   error of CreateProcessA. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U GetLastError(void);
API char *GetCommandLineA(void);
API U CreateProcessA(const char *, char *, U, U, U, U, U, const char *, U *, U *);
API U WaitForSingleObject(U, U);
API U GetExitCodeProcess(U, U *);

static char folder[260];

void mainCRTStartup(void)
{
    char *p = GetCommandLineA();
    /* Our own name, quoted or not. */
    if (*p == '"') {
        for (++p; *p && *p != '"'; ++p) {}
        if (*p) ++p;
    } else {
        while (*p && *p != ' ' && *p != '\t') ++p;
    }
    while (*p == ' ' || *p == '\t') ++p;
    /* The program's folder is the current directory of the new process. */
    U n = 0, last = 0;
    for (const char *q = p; *q && *q != ' ' && *q != '\t' && n + 1 < sizeof folder; ++q, ++n) {
        folder[n] = *q;
        if (*q == '\\') last = n;
    }
    folder[last > 2 ? last : 3] = 0;
    U startup[17] = {68};
    U information[4] = {0};
    if (!CreateProcessA(0, p, 0, 0, 0, 0, 0, folder, startup, information))
        ExitProcess(0x10000 + GetLastError());
    U code = 0;
    WaitForSingleObject(information[0], 0xffffffffu);
    GetExitCodeProcess(information[0], &code);
    ExitProcess(code);
}
