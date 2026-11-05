// SPDX-License-Identifier: GPL-2.0-or-later
// A window whose thread waits in KERNEL, like dxdiag's OLE worker window.
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U GetModuleHandleA(const char *);
API char *GetCommandLineA(void);
API U CreateEventA(void *, U, U, const char *);
API U WaitForSingleObject(U, U);
API void ExitProcess(U);
API U RegisterClassA(const U *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
API U DefWindowProcA(U, U, U, U);
API U GetMessageA(void *, U, U, U);
API U DispatchMessageA(void *);
static U veto;
static U __stdcall procedure(U window, U message, U wp, U lp)
{
    if (message == 0x11 && veto) return 0;
    return DefWindowProcA(window, message, wp, lp);
}
void mainCRTStartup(void)
{
    const char *command = GetCommandLineA();
    for (U i = 0; command[i]; ++i) if (command[i] == '-') { veto = 1; break; }
    U instance = GetModuleHandleA(0);
    U klass[10] = {0, (U)procedure, 0, 0, instance, 0, 0, 0, 0, (U)"ShutdownWait98"};
    if (!RegisterClassA(klass) || !CreateWindowExA(0, "ShutdownWait98", "waiting worker", 0x80000000,
                                                0, 0, 0, 0, 0, 0, instance, 0)) ExitProcess(1);
    if (veto) { U message[7]; while (GetMessageA(message, 0, 0, 0)) DispatchMessageA(message); }
    else WaitForSingleObject(CreateEventA(0, 1, 0, 0), ~0u);
    ExitProcess(2);
}
