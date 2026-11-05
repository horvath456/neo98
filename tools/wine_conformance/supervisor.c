/* SPDX-License-Identifier: GPL-2.0-or-later
 * Same supervisor and test binaries run on Windows 98 and NEO98. */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <tlhelp32.h>
#include "suite.h"

static HANDLE journal;
#ifdef CONFORMANCE_TOPMOST_FIXTURE
static HWND topmost_fixture;
#endif
static void kill_children(DWORD parent, unsigned depth)
{
    HANDLE snapshot, process;
    PROCESSENTRY32 entry;
    if (depth > 16) return;
    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;
    memset(&entry, 0, sizeof(entry)); entry.dwSize = sizeof(entry);
    if (Process32First(snapshot, &entry)) do
    {
        if (entry.th32ParentProcessID != parent || entry.th32ProcessID == parent) continue;
        kill_children(entry.th32ProcessID, depth + 1);
        process = OpenProcess(PROCESS_TERMINATE, FALSE, entry.th32ProcessID);
        if (process) { TerminateProcess(process, 248); CloseHandle(process); }
    } while (Process32Next(snapshot, &entry));
    CloseHandle(snapshot);
}
static void record(const char *text)
{
    DWORD written;
    WriteFile(journal, text, (DWORD)strlen(text), &written, NULL);
    FlushFileBuffers(journal);
}

#ifdef CONFORMANCE_SHELL_FIXTURE
#define SHELL_READY_TIMEOUT 120000
static PROCESS_INFORMATION owned_shell;
static BOOL shell_visible(void)
{
    HWND tray=FindWindowA("Shell_TrayWnd",NULL);
    return tray && IsWindowVisible(tray);
}
static void shell_ready(void)
{
    HWND tray=FindWindowA("Shell_TrayWnd",NULL);
    HWND desktop,last_foreground=NULL;DWORD pid=0,result,start,stable=0;HANDLE process;char line[256];
    GetWindowThreadProcessId(tray,&pid);
    process=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_INFORMATION,FALSE,pid);
    result=process?WaitForInputIdle(process,SHELL_READY_TIMEOUT):WAIT_FAILED;
    if(process)CloseHandle(process);
    if(result || !shell_visible()){
        sprintf(line,"FIXTURE_ERROR\tEXPLORER\tNOT_IDLE\t%lu\n",result);
        record(line);ExitProcess(250);
    }
    /* Explorer's tray thread can become idle before its main thread creates
     * the desktop and calls SetForegroundWindow. Starting the test then
     * lets that startup activation interrupt its first foreground change. */
    start=GetTickCount();
    for(;;){
        HWND foreground=GetForegroundWindow(),view;DWORD owner=0,reply=0,now=GetTickCount();
        /* Progman identifies Explorer on Win9x without relying on the
         * NT shell-window API. The supervisor's console may own foreground. */
        desktop=FindWindowA("Progman",NULL);
        view=desktop?FindWindowExA(desktop,NULL,"SHELLDLL_DefView",NULL):NULL;
        if(desktop)GetWindowThreadProcessId(desktop,&owner);
        if(foreground && IsWindow(foreground) && desktop && view && FindWindowExA(view,NULL,"SysListView32",NULL) &&
           owner==pid && SendMessageTimeoutA(desktop,WM_NULL,0,0,SMTO_ABORTIFHUNG,500,&reply)){
            if(foreground!=last_foreground || !stable)stable=now;
            if(now-stable>=1000)break;
        }else stable=0;
        last_foreground=foreground;
        if(now-start>=SHELL_READY_TIMEOUT){
            sprintf(line,"FIXTURE_ERROR\tEXPLORER\tDESKTOP_NOT_READY\tdesktop=%p\tview=%p\towner=%lu\texplorer=%lu\n",desktop,view,owner,pid);
            record(line);ExitProcess(250);
        }
        Sleep(50);
    }
    record("FIXTURE\tEXPLORER\tDESKTOP_READY\tstable_ms=1000\n");
    sprintf(line,"FIXTURE\tEXPLORER\tREADY\tstyle=%08lx\texstyle=%08lx\tforeground=%p\n",
        GetWindowLongA(tray,GWL_STYLE),GetWindowLongA(tray,GWL_EXSTYLE),GetForegroundWindow());
    record(line);
}
static void prepare_shell(void)
{
    char windows[MAX_PATH], command[MAX_PATH+4], line[256];
    STARTUPINFOA startup;
    DWORD start, code;
    if (FindWindowA("Shell_TrayWnd",NULL)) {
        start=GetTickCount();
        while(!shell_visible() && GetTickCount()-start<SHELL_READY_TIMEOUT)Sleep(100);
        record("FIXTURE\tEXPLORER\tEXISTING_TRAY\n"); shell_ready();return;
    }
    GetWindowsDirectoryA(windows, sizeof windows);
    sprintf(command, "\"%s\\EXPLORER.EXE\"", windows);
    memset(&startup, 0, sizeof startup); startup.cb = sizeof startup;
    if (!CreateProcessA(NULL, command, NULL, NULL, FALSE, 0, NULL, windows, &startup, &owned_shell)) {
        sprintf(line, "FIXTURE_ERROR\tEXPLORER\tCREATE\t%lu\n", GetLastError());
        record(line); ExitProcess(250);
    }
    start = GetTickCount();
    while (!shell_visible() && GetTickCount()-start < SHELL_READY_TIMEOUT) {
        GetExitCodeProcess(owned_shell.hProcess, &code);
        if (code != STILL_ACTIVE) break;
        Sleep(100);
    }
    if (!shell_visible()) {
        record("FIXTURE_ERROR\tEXPLORER\tNO_TRAY\n");
        kill_children(owned_shell.dwProcessId, 0);
        TerminateProcess(owned_shell.hProcess, 248); ExitProcess(250);
    }
    record("FIXTURE\tEXPLORER\tSTARTED_TRAY\n");
    shell_ready();
}
#endif

void mainCRTStartup(void)
{
    OSVERSIONINFOA version;
    char root[MAX_PATH], path[MAX_PATH], cwd[MAX_PATH], command[1024], line[1024];
    const char *selection = GetCommandLineA();
    unsigned i;
    DWORD code, wait;
    STARTUPINFOA startup;
    PROCESS_INFORMATION process;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    if (*selection == '"') { ++selection; while (*selection && *selection != '"') ++selection; if (*selection) ++selection; }
    else while (*selection && *selection != ' ') ++selection;
    while (*selection == ' ') ++selection;
    GetModuleFileNameA(NULL, root, sizeof(root));
    *strrchr(root, '\\') = 0;
    sprintf(path, "%s\\RUN.TSV", root);
    journal = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    if (journal == INVALID_HANDLE_VALUE) ExitProcess(250);
    memset(&version, 0, sizeof(version));
    version.dwOSVersionInfoSize = sizeof(version);
    GetVersionExA(&version);
    sprintf(line, "INFO\tos=%lu.%lu.%lu\tplatform=%lu\tcsd=%s\tacp=%u\toemcp=%u\tlcid=%lu\n",
            version.dwMajorVersion, version.dwMinorVersion, version.dwBuildNumber & 0xffff,
            version.dwPlatformId, version.szCSDVersion, GetACP(), GetOEMCP(), GetUserDefaultLCID());
    record(line);
    SetEnvironmentVariableA("WINETEST_PLATFORM", "windows");
    SetEnvironmentVariableA("WINETEST_REPORT_SUCCESS", "1");
    SetEnvironmentVariableA("WINETEST_DEBUG", "1");
    SetEnvironmentVariableA("WINETEST_INTERACTIVE", "0");
#ifdef CONFORMANCE_SHELL_FIXTURE
    prepare_shell();
#endif
#ifdef CONFORMANCE_TOPMOST_FIXTURE
    topmost_fixture=CreateWindowExA(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,"STATIC","Conformance order fixture",
        WS_POPUP|WS_DISABLED,-20,-20,1,1,NULL,NULL,NULL,NULL);
    if(!topmost_fixture){record("FIXTURE_ERROR\tTOPMOST\tCREATE\n");ExitProcess(250);}
    ShowWindow(topmost_fixture,SW_SHOWNOACTIVATE);
    record("FIXTURE\tTOPMOST\tVISIBLE_DISABLED\n");
#endif
    for (i = 0; suite[i].name; ++i)
    {
        if (*selection && strcmp(selection, suite[i].name)) continue;
        sprintf(cwd, "%s\\%s", root, suite[i].name);
        CreateDirectoryA(cwd, NULL);
#ifdef CAMPAIGN_INPUT_FILES
        {
            unsigned j;
            char from[MAX_PATH], to[MAX_PATH];
            for (j = 0; input_files[j]; ++j)
            {
                sprintf(from, "%s\\%s", root, input_files[j]);
                sprintf(to, "%s\\%s", cwd, input_files[j]);
                if (!CopyFileA(from, to, FALSE))
                {
                    sprintf(line, "INPUT_ERROR\t%s\t%s\t%lu\n", suite[i].name, input_files[j], GetLastError());
                    record(line);
                    ExitProcess(250);
                }
            }
        }
#endif
#ifdef CONFORMANCE_WININIT_FIXTURE
        /* Paired fixture for MSI's empty-file underread. The unmodified
         * package EXE and MSI.DLL still run; both systems get the same INI. */
        {
            static const char contents[] = "[rename]\r\n";
            char ini[MAX_PATH]; DWORD written = 0; HANDLE file;
            GetWindowsDirectoryA(ini, sizeof(ini));
            strcat(ini, "\\WININIT.INI");
            file = CreateFileA(ini, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
            if (file == INVALID_HANDLE_VALUE ||
                !WriteFile(file, contents, sizeof(contents)-1, &written, NULL) ||
                written != sizeof(contents)-1) ExitProcess(250);
            CloseHandle(file);
            record("FIXTURE\tWININIT.INI\t[rename] CRLF\n");
        }
#endif
#ifdef CONFORMANCE_LOGON_FIXTURE
        /* Win98 GetUserNameA reads the logon value populated during boot.
         * Give both isolated systems the same explicit user for this batch. */
        {
            HKEY key;LONG result;
            static const char user[]="Conformance";
            result=RegOpenKeyExA(HKEY_LOCAL_MACHINE,"System\\CurrentControlSet\\Control",0,KEY_SET_VALUE,&key);
            if(!result){
                result=RegSetValueExA(key,"Current User",0,REG_SZ,(const BYTE *)user,sizeof user);
                RegCloseKey(key);
            }
            if(result){sprintf(line,"FIXTURE_ERROR\tLOGON\t%ld\n",result);record(line);ExitProcess(250);}
            record("FIXTURE\tLOGON\tCurrent User=Conformance\n");
        }
#endif
#ifdef CONFORMANCE_BROWSER_FIXTURE
        /* Active Setup normally supplies these per-user IE settings. Use
         * identical explicit inputs in the two private installations. */
        {
            HKEY key; LONG result; unsigned j;
            static const char *names[]={"Start Page","Search Page"};
            static const char *values[]={"http://example.invalid/home","http://example.invalid/search"};
            result=RegCreateKeyExA(HKEY_CURRENT_USER,"Software\\Microsoft\\Internet Explorer\\Main",
                0,NULL,0,KEY_SET_VALUE,NULL,&key,NULL);
            if(!result){
                for(j=0;j<2 && !result;++j)
                    result=RegSetValueExA(key,names[j],0,REG_SZ,(const BYTE *)values[j],strlen(values[j])+1);
                RegCloseKey(key);
            }
            if(result){sprintf(line,"FIXTURE_ERROR\tBROWSER\t%ld\n",result);record(line);ExitProcess(250);}
            record("FIXTURE\tBROWSER\tEXPLICIT_HOME_AND_SEARCH\n");
        }
#endif
        SetEnvironmentVariableA("TEMP", cwd);
        SetEnvironmentVariableA("TMP", cwd);
        sprintf(command, "\"%s\\%s\"", root, suite[i].exe);
#ifdef CAMPAIGN_ARGUMENTS
        if (*suite[i].arguments) { strcat(command, " "); strcat(command, suite[i].arguments); }
#endif
        memset(&startup, 0, sizeof(startup));
        memset(&process, 0, sizeof(process));
        startup.cb = sizeof(startup);
        sprintf(line, "BEGIN\t%s\n", suite[i].name); record(line);
        if (!CreateProcessA(NULL, command, NULL, NULL, FALSE, 0, NULL, cwd, &startup, &process))
        {
            sprintf(line, "END\t%s\tLOAD_ERROR\t%lu\n", suite[i].name, GetLastError());
            record(line); continue;
        }
        sprintf(line, "PID\t%s\t%lu\n", suite[i].name, process.dwProcessId); record(line);
        wait = WaitForSingleObject(process.hProcess, suite[i].timeout_ms);
        if (wait == WAIT_TIMEOUT)
        {
            TerminateProcess(process.hProcess, 248);
            WaitForSingleObject(process.hProcess, 5000);
            sprintf(line, "END\t%s\tTIMEOUT\t248\n", suite[i].name);
        }
        else if (wait != WAIT_OBJECT_0)
        {
            sprintf(line, "END\t%s\tWAIT_ERROR\t%lu\n", suite[i].name, GetLastError());
            TerminateProcess(process.hProcess, 247);
        }
        else
        {
            GetExitCodeProcess(process.hProcess, &code);
            sprintf(line, "END\t%s\tEXIT\t%lu\n", suite[i].name, code);
        }
        record(line);
        kill_children(process.dwProcessId, 0);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
#ifdef CONFORMANCE_SHELL_FIXTURE
    if (owned_shell.hProcess) {
        kill_children(owned_shell.dwProcessId, 0);
        TerminateProcess(owned_shell.hProcess, 0);
        WaitForSingleObject(owned_shell.hProcess, 5000);
        CloseHandle(owned_shell.hThread); CloseHandle(owned_shell.hProcess);
    }
#endif
#ifdef CONFORMANCE_TOPMOST_FIXTURE
    DestroyWindow(topmost_fixture);
#endif
    record("DONE\n");
    CloseHandle(journal);
    ExitProcess(0);
}
