/* SPDX-License-Identifier: GPL-2.0-or-later
   The PIF properties through SHELL32's nameless ordinals 9, 10, 11 and 13,
   the way the shell and WINOLDAP read and write them.
   Three handles: a PIF of the collection, a program without a PIF, and the
   PIF again with a changed title that Close writes back. The groups go to
   files on D: for the test to read; the exit code names the first failure. */
typedef unsigned U;
__declspec(dllimport) U __stdcall PifMgr_OpenProperties(const char *app, const char *pif, U inf, U flags);
__declspec(dllimport) U __stdcall PifMgr_GetProperties(U props, const char *group, void *buffer, U cb, U flags);
__declspec(dllimport) U __stdcall PifMgr_SetProperties(U props, const char *group, const void *buffer, U cb, U flags);
__declspec(dllimport) U __stdcall PifMgr_CloseProperties(U props, U flags);
__declspec(dllimport) U __stdcall CreateFileA(const char *, U, U, void *, U, U, U);
__declspec(dllimport) U __stdcall WriteFile(U, const void *, U, U *, void *);
__declspec(dllimport) U __stdcall CloseHandle(U);
__declspec(dllimport) void __stdcall ExitProcess(U);
static void save(const char *path, const void *data, U size)
{
    U h = CreateFileA(path, 0x40000000u, 0, 0, 2, 0x80, 0), done = 0;
    if (h == 0xffffffffu) ExitProcess(90);
    WriteFile(h, data, size, &done, 0);
    CloseHandle(h);
}
static unsigned char prg[0x292], prg2[0x292], mem[16], tsk[14];
void __stdcall mainCRTStartup(void)
{
    U h = PifMgr_OpenProperties("D:\\OMF.PIF", 0, 0, 0);
    if (!h) ExitProcess(1);
    if (PifMgr_GetProperties(h, (const char *)1, prg, sizeof prg, 0) != sizeof prg) ExitProcess(2);
    if (PifMgr_GetProperties(h, (const char *)4, mem, sizeof mem, 0) != sizeof mem) ExitProcess(3);
    if (PifMgr_GetProperties(h, (const char *)2, tsk, sizeof tsk, 0) != sizeof tsk) ExitProcess(4);
    save("D:\\PRG1.BIN", prg, sizeof prg);
    save("D:\\MEM1.BIN", mem, sizeof mem);
    save("D:\\TSK1.BIN", tsk, sizeof tsk);
    /* A program without a PIF: the defaults, NOPIF set. */
    U h2 = PifMgr_OpenProperties("D:\\HELLO.COM", 0, 0, 0);
    if (!h2) ExitProcess(5);
    if (PifMgr_GetProperties(h2, (const char *)1, prg2, sizeof prg2, 0) != sizeof prg2) ExitProcess(6);
    save("D:\\PRG2.BIN", prg2, sizeof prg2);
    if (PifMgr_CloseProperties(h2, 0) != 0) ExitProcess(7);
    /* A changed title, written back by Set and Close. */
    const char *title = "Changed Title";
    U i;
    for (i = 0; i < 30; ++i) prg[4 + i] = 0;
    for (i = 0; title[i]; ++i) prg[4 + i] = (unsigned char)title[i];
    prg[0] |= 1; /* close on exit */
    if (PifMgr_SetProperties(h, (const char *)1, prg, sizeof prg, 2) != sizeof prg) ExitProcess(8);
    if (PifMgr_CloseProperties(h, 0) != 0) ExitProcess(9);
    /* The same handle is gone now. */
    if (PifMgr_GetProperties(h, (const char *)1, prg, sizeof prg, 0) != 0) ExitProcess(10);
    ExitProcess(0);
}
