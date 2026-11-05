/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A hook DLL for the whole session, x86, with its preferred base at
   711D0000h like MSIDLE.DLL of IE 5.01, and a section.shrdata that the
   linker marks IMAGE_SCN_MEM_SHARED. DLL_PROCESS_ATTACH counts the processes
   in it; the WH_GETMESSAGE procedure counts the messages of every process
   and calls on with the hook handle the setting process left there, as
   MSIDLE does with its keyboard and mouse hooks.
   tools/test_session_hook_shared_host.py. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API int CallNextHookEx(U, int, U, int);

/* attaches, magic, messages, hook */
__attribute__((section(".shrdata"))) volatile U shared[4] = {0, 0, 0, 0};

int __stdcall DllEntry(U instance, U reason, U reserved)
{
    (void)instance;
    (void)reserved;
    if (reason == 1) ++shared[0];
    return 1;
}
__declspec(dllexport) int __stdcall SharedMessages(int code, U wp, int lp)
{
    if (code >= 0) ++shared[2];
    return CallNextHookEx(shared[3], code, wp, lp);
}
__declspec(dllexport) void __stdcall SharedSet(U magic, U hook)
{
    shared[1] = magic;
    shared[3] = hook;
}
__declspec(dllexport) void __stdcall SharedValues(U *out)
{
    for (int i = 0; i < 4; ++i) out[i] = shared[i];
}
