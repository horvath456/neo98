/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The host's own size as the stored display mode.

   The host gives the desktop its size (neo98-kms --desktop 1024x700, or
   seamless mode). The child process stores 800x600 with
   ChangeDisplaySettings(CDS_UPDATEREGISTRY) and then the host's size again,
   the way the Settings page of DESK.CPL applies one mode and then another,
   and ends. The parent waits for it and reads the screen and the mode of the
   registry: the desktop has the host's size again, not 800x600 with the
   host's size as a mode of the child that ended with it.

   The rows (number, got, wanted) go to C:\HOSTSIZE.BIN behind the count of
   failed checks. */
#include <windows.h>
static DWORD rows[96], used, failed;
static void check(DWORD got, DWORD want)
{
    rows[used] = used / 3 + 1;
    rows[used + 1] = got; rows[used + 2] = want; used += 3;
    if (got != want) ++failed;
}
static void finish(const char *path)
{
    DWORD out[1 + 96], written = 0;
    out[0] = failed;
    for (DWORD i = 0; i < used; ++i) out[1 + i] = rows[i];
    HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    WriteFile(file, out, (1 + used) * 4, &written, 0);
    CloseHandle(file);
    ExitProcess(failed);
}
static LONG store(int width, int height)
{
    DEVMODEA mode = {0};
    mode.dmSize = sizeof mode;
    mode.dmPelsWidth = width; mode.dmPelsHeight = height; mode.dmBitsPerPel = 32;
    mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
    return ChangeDisplaySettingsA(&mode, CDS_UPDATEREGISTRY);
}
void mainCRTStartup(void)
{
    const char *line = GetCommandLineA();
    while (*line && *line != ' ') ++line;
    if (line[0] == ' ' && line[1] == 'c')
    {
        /* The child: 800x600, then the host's size. */
        check(store(800, 600), DISP_CHANGE_SUCCESSFUL);
        check(GetSystemMetrics(SM_CXSCREEN), 800);
        check(store(1024, 700), DISP_CHANGE_SUCCESSFUL);
        check(GetSystemMetrics(SM_CXSCREEN), 1024);
        check(GetSystemMetrics(SM_CYSCREEN), 700);
        finish("C:\\HOSTSIZ2.BIN");
    }
    check(GetSystemMetrics(SM_CXSCREEN), 1024);
    char path[MAX_PATH], command[MAX_PATH + 8];
    GetModuleFileNameA(0, path, sizeof path);
    lstrcpyA(command, path);
    lstrcatA(command, " c");
    STARTUPINFOA startup = {0};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION process = {0};
    check(CreateProcessA(path, command, 0, 0, FALSE, 0, 0, 0, &startup, &process), 1);
    check(WaitForSingleObject(process.hProcess, 20000), WAIT_OBJECT_0);
    DWORD code = 99;
    GetExitCodeProcess(process.hProcess, &code);
    check(code, 0);
    check(GetSystemMetrics(SM_CXSCREEN), 1024);
    check(GetSystemMetrics(SM_CYSCREEN), 700);
    DEVMODEA mode = {0};
    mode.dmSize = sizeof mode;
    check(EnumDisplaySettingsA(0, ENUM_REGISTRY_SETTINGS, &mode), 1);
    check(mode.dmPelsWidth, 1024);
    check(mode.dmPelsHeight, 700);
    finish("C:\\HOSTSIZE.BIN");
}
