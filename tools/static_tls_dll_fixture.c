/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A DLL with its own static TLS directory. TLS_DLL_ID separates the statically
   imported copy from the one the test brings in with LoadLibrary. */
#include "static_tls_support.h"
#define API __declspec(dllimport) __stdcall
API U GetCurrentThreadId(void);
API U TlsGetValue(U);

__declspec(thread) U dll_value = 0xd0000000u + TLS_DLL_ID;
__declspec(thread) U dll_zero;
__declspec(thread) unsigned char dll_room[64] = {1, 2, 3};

/* Process wide on purpose: the log has to outlive the thread it describes.
   The scheduler is cooperative, so a plain counter is enough. */
static U log_reasons[64], log_threads[64], log_template_seen[64], log_count;
static void note(U reason)
{
    if (log_count < 64)
    {
        log_template_seen[log_count] = dll_value;
        log_threads[log_count] = GetCurrentThreadId();
        log_reasons[log_count++] = reason;
    }
}
/* Win98 never reads AddressOfCallBacks. The directory still points at a real
   list, so a port that did call it would show up here as a reason below 100h. */
static void __stdcall callback(void *module, U reason, void *reserved)
{
    (void)module;
    (void)reserved;
    note(reason);
}
__attribute__((section(".CRT$XLB"))) TlsCallback dll_callback = callback;

/* One entry point for every query keeps the import table small. */
__declspec(dllexport) U __stdcall TlsProbe(U what, U value)
{
    switch (what)
    {
    case 0: return _tls_index;
    case 1: return (U)&dll_value;
    case 2: return dll_value;
    case 3: dll_value = value; return 0;
    case 4: return dll_zero;
    case 5: dll_zero = value; return 0;
    case 6: return dll_room[value & 63];
    case 7: return log_count;
    case 8: return value < 64 ? log_reasons[value] : 0xffffffffu;
    case 9: return value < 64 ? log_threads[value] : 0xffffffffu;
    case 10: return value < 64 ? log_template_seen[value] : 0xffffffffu;
    case 11: return TLS_DLL_ID;
    case 12: return TlsGetValue(_tls_index);
    case 13: return (U)&dll_zero;
    default: return 0xffffffffu;
    }
}
U __stdcall DllMain(U module, U reason, U reserved)
{
    (void)module;
    (void)reserved;
    note(0x100 + reason);
    return 1;
}
