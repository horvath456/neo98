// SPDX-License-Identifier: GPL-2.0-or-later
// The primary network logon as the registry names it and as the Network
// applet shows it (tools/test_network_identification_host.py).
//
//   NETLOGON.EXE read   writes C:\NETLOGON.TXT:
//     primary=   HKLM\Network\Logon "PrimaryProvider"
//     lmlogon=   its "LMLogon", the bytes in hexadecimal
//     expected=  what "Primary Network Logon" should show: the DriverDesc of
//                the client under Services\Class\NetClient whose
//                Ndi "NetworkProvider" is PrimaryProvider, "Windows Logon"
//                for an empty PrimaryProvider
//     marked=    the clients whose class key has Ndi\Default
//     logon=     the text of the box (control 195h of the page
//                Configuration), read from the applet, which is then
//                cancelled
#include <windows.h>

// The zeroed buffers need memset, which -nostdlib leaves out.
void *memset(void *p, int v, unsigned n)
{
    unsigned char *q = p;
    while (n--)
        *q++ = (unsigned char)v;
    return p;
}

static char text[2048];
static int used;
static HWND box;

static void line(const char *name, const char *value)
{
    used += wsprintfA(text + used, "%s=%s\r\n", name, value);
}

static BOOL CALLBACK find_box(HWND hwnd, LPARAM unused)
{
    (void)unused;
    if (GetDlgCtrlID(hwnd) == 0x195)
    {
        box = hwnd;
        return FALSE;
    }
    return TRUE;
}

static void registry(void)
{
    char primary[128] = "", expected[128] = "", marked[512] = "", value[128];
    BYTE lm[16];
    DWORD size = sizeof primary, lm_size = sizeof lm, type = 0, i;
    HKEY key, clients;
    char hex[40] = "";
    if (!RegOpenKeyA(HKEY_LOCAL_MACHINE, "Network\\Logon", &key))
    {
        if (RegQueryValueExA(key, "PrimaryProvider", 0, &type, (BYTE *)primary, &size))
            lstrcpyA(primary, "(none)");
        if (!RegQueryValueExA(key, "LMLogon", 0, &type, lm, &lm_size))
            for (i = 0; i < lm_size && i < 16; ++i)
                wsprintfA(hex + 2 * i, "%02x", lm[i]);
        else
            lstrcpyA(hex, "(none)");
        RegCloseKey(key);
    }
    line("primary", primary);
    line("lmlogon", hex);
    lstrcpyA(expected, primary[0] && lstrcmpA(primary, "(none)") ? "(no client)" : "Windows Logon");
    if (!RegOpenKeyA(HKEY_LOCAL_MACHINE, "System\\CurrentControlSet\\Services\\Class\\NetClient", &clients))
    {
        char name[32];
        for (i = 0; !RegEnumKeyA(clients, i, name, sizeof name); ++i)
        {
            char path[96];
            HKEY ndi;
            wsprintfA(path, "%s\\Ndi", name);
            if (RegOpenKeyA(clients, path, &ndi))
                continue;
            size = sizeof value;
            if (!RegQueryValueExA(ndi, "NetworkProvider", 0, &type, (BYTE *)value, &size) && primary[0] &&
                !lstrcmpiA(value, primary))
            {
                HKEY client;
                if (!RegOpenKeyA(clients, name, &client))
                {
                    size = sizeof expected;
                    RegQueryValueExA(client, "DriverDesc", 0, &type, (BYTE *)expected, &size);
                    RegCloseKey(client);
                }
            }
            HKEY mark;
            if (!RegOpenKeyA(ndi, "Default", &mark))
            {
                if (marked[0])
                    lstrcatA(marked, ",");
                lstrcatA(marked, name);
                RegCloseKey(mark);
            }
            RegCloseKey(ndi);
        }
        RegCloseKey(clients);
    }
    line("expected", expected);
    line("marked", marked);
}

static void applet(void)
{
    STARTUPINFOA start = {sizeof start};
    PROCESS_INFORMATION child;
    char command[] = "RUNDLL32.EXE SHELL32.DLL,Control_RunDLL NETCPL.CPL", shown[128] = "";
    HWND sheet = 0;
    int i;
    if (!CreateProcessA(0, command, 0, 0, 0, 0, 0, 0, &start, &child))
    {
        line("logon", "(no applet)");
        return;
    }
    for (i = 0; i < 300 && !sheet; ++i)
    {
        sheet = FindWindowA("#32770", "Network");
        if (!sheet)
            Sleep(200);
    }
    for (i = 0; sheet && i < 50 && !shown[0]; ++i)
    {
        box = 0;
        EnumChildWindows(sheet, find_box, 0);
        if (box)
            SendMessageA(box, WM_GETTEXT, sizeof shown, (LPARAM)shown);
        if (!shown[0])
            Sleep(200);
    }
    line("logon", sheet ? (shown[0] ? shown : "(empty)") : "(no sheet)");
    if (sheet)
        PostMessageA(sheet, WM_COMMAND, IDCANCEL, 0);
    WaitForSingleObject(child.hProcess, 30000);
    CloseHandle(child.hProcess);
    CloseHandle(child.hThread);
}

void mainCRTStartup(void)
{
    DWORD written = 0;
    HANDLE file;
    registry();
    applet();
    file = CreateFileA("C:\\NETLOGON.TXT", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    WriteFile(file, text, used, &written, 0);
    CloseHandle(file);
    ExitProcess(0);
}
