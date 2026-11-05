// SPDX-License-Identifier: GPL-2.0-or-later
// Keep 600 real registry handles across a machine save and load.
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U GetModuleHandleA(const char *);
API U RegisterClassA(const U *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
API U DefWindowProcA(U, U, U, U);
API U GetMessageA(void *, U, U, U);
API U DispatchMessageA(void *);
API U SetWindowTextA(U, const char *);
API U RegCreateKeyExA(U, const char *, U, char *, U, U, void *, U *, U *);
API U RegSetValueExA(U, const char *, U, U, const void *, U);
API U RegQueryValueExA(U, const char *, U *, U *, void *, U *);
API U RegCloseKey(U);
static U keys[600];
static const char path[] = "Software\\NEO98StateHandles";
static U __stdcall procedure(U window, U message, U wp, U lp)
{
    if (message == 0x100 && wp == 0x56)
    {
        U okay = 1, fresh = 0, disposition;
        for (U i = 0; i < 600; ++i)
        {
            U value = 0, bytes = 4, type = 0;
            if (RegQueryValueExA(keys[i], "Value", 0, &type, &value, &bytes) ||
                type != 4 || bytes != 4 || value != 731) okay = 0;
        }
        if (RegCreateKeyExA(0x80000001, path, 0, 0, 0, 0xf003f, 0, &fresh, &disposition)) okay = 0;
        for (U i = 0; i < 600; ++i) if (fresh == keys[i]) okay = 0;
        if (fresh) RegCloseKey(fresh);
        SetWindowTextA(window, okay ? "REGISTRY STATE PASS" : "REGISTRY STATE FAIL");
    }
    return DefWindowProcA(window, message, wp, lp);
}
void mainCRTStartup(void)
{
    U disposition, value = 731;
    for (U i = 0; i < 600; ++i)
        RegCreateKeyExA(0x80000001, path, 0, 0, 0, 0xf003f, 0, &keys[i], &disposition);
    RegSetValueExA(keys[0], "Value", 0, 4, &value, 4);
    U instance = GetModuleHandleA(0);
    U klass[10] = {0, (U)procedure, 0, 0, instance, 0, 0, 16, 0, (U)"RegistryState98"};
    RegisterClassA(klass);
    CreateWindowExA(0, "RegistryState98", "Registry state fixture", 0x90cf0000,
                    64, 55, 300, 180, 0, 0, instance, 0);
    U message[7];
    while (GetMessageA(message, 0, 0, 0)) DispatchMessageA(message);
}
