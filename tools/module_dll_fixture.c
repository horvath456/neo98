// SPDX-License-Identifier: GPL-2.0-or-later
#include "module_fixture.h"
API void W Report(U, U, U);
#if MODULE_ID == 1
API U W BetaAnswer(U);
#elif MODULE_ID == 5
API U W CycleBValue(U);
#elif MODULE_ID == 6
API U W CycleAValue(U);
#endif
U counter = 0, worker = 0;
#if MODULE_ID == 2
static U W EarlyWorker(void *unused)
{
    (void)unused;
    U module = GetModuleHandleA("ALPHA");
    U *value = (U *)GetProcAddress(module, "counter");
    if (!value || *value < 7)
        ExitProcess(920);
    return 98;
}
#endif
U W Value(U x)
{
    ++counter;
#if MODULE_ID == 1
    return BetaAnswer(x) + 1;
#elif MODULE_ID == 5
    return x ? CycleBValue(x - 1) + 5 : 0;
#elif MODULE_ID == 6
    return x ? CycleAValue(x - 1) + 6 : 0;
#else
    return x + MODULE_ID;
#endif
}
U W DllMain(U module, U reason, U reserved)
{
    Report(MODULE_ID, reason, reserved);
    if (reason == 0)
    {
#if MODULE_ID == 3
        if (reserved && !FreeLibrary(module))
            ExitProcess(943);
#endif
        U info = FindResourceA(module, (const char *)33, (const char *)10);
        const unsigned char *data = (const unsigned char *)LockResource(LoadResource(module, info));
        char text[32];
        if (!info || SizeofResource(module, info) != 6 || !data || data[0] != MODULE_ID ||
            LoadStringA(module, 7, text, sizeof text) != 10 || text[7] != '0' + MODULE_ID ||
            (unsigned char)text[9] != 0x80) ExitProcess(930 + MODULE_ID);
    }
    if (reason == 1)
    {
        char name[260];
        if (GetModuleHandleA(MODULE_NAME) != module || !GetModuleFileNameA(module, name, sizeof name))
            ExitProcess(900 + MODULE_ID);
        if (!GetProcAddress(module, "Value"))
            ExitProcess(910 + MODULE_ID);
#if MODULE_ID == 1
        counter = 7;
#endif
#if MODULE_ID == 2
        worker = CreateThread(0, 0, EarlyWorker, 0, 0, 0);
        if (!worker)
            ExitProcess(921);
#endif
        Sleep(0);
#if MODULE_ID == 4
        return 0;
#endif
    }
    return 1;
}
