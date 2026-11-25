/* SPDX-License-Identifier: GPL-2.0-or-later */
/* GetProcessDword (KERNEL32 ordinal 18) for the fields of the task database.

   -48 is the task's instance, the image base here; -44 the Windows version
   the program was made for, as GetExeVersion has it: the subsystem version
   of the image, major in the high byte and minor in the low one. The fixture
   is linked as subsystem version 4.10. A 16-bit child, WINVER.EXE, answers
   the expected version of its NE header.

   The rows (number, got, wanted) go to C:\PDWORD.BIN behind the count of
   failed checks. */
#include <windows.h>
typedef DWORD(WINAPI *ProcessDword)(DWORD, int);
static DWORD rows[96], used, failed;
static void check(DWORD got, DWORD want)
{
    rows[used] = used / 3 + 1;
    rows[used + 1] = got; rows[used + 2] = want; used += 3;
    if (got != want) ++failed;
}
void mainCRTStartup(void)
{
    ProcessDword get = (ProcessDword)GetProcAddress(GetModuleHandleA("KERNEL32.DLL"), (LPCSTR)18);
    check(get != 0, 1);
    if (get)
    {
        check(get(0, -48), (DWORD)GetModuleHandleA(0));
        check(get(0, -44), 0x040a);
        check(get(GetCurrentProcessId(), -44), 0x040a);
        STARTUPINFOA startup = {0};
        startup.cb = sizeof startup;
        PROCESS_INFORMATION process = {0};
        char command[] = "C:\\WINDOWS\\WINVER.EXE";
        check(CreateProcessA(0, command, 0, 0, FALSE, 0, 0, 0, &startup, &process), 1);
        WaitForInputIdle(process.hProcess, 10000);
        check(get(process.dwProcessId, -44), 0x0400);
        TerminateProcess(process.hProcess, 0);
    }
    DWORD out[1 + 96], written = 0;
    out[0] = failed;
    for (DWORD i = 0; i < used; ++i) out[1 + i] = rows[i];
    HANDLE file = CreateFileA("C:\\PDWORD.BIN", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    WriteFile(file, out, (1 + used) * 4, &written, 0);
    CloseHandle(file);
    ExitProcess(failed);
}
