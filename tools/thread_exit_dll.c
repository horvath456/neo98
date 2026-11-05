/* SPDX-License-Identifier: GPL-2.0-or-later
 * Own x86 test DLL for tools/thread_exit_fixture.c: it counts the
 * notifications its entry point receives, by reason. */
typedef unsigned U;
static U counts[4];
__declspec(dllexport) U __cdecl ThreadNotices(U reason) { return counts[reason & 3]; }
int __stdcall DllEntry(void *instance, U reason, void *reserved)
{
    (void)instance;
    (void)reserved;
    if (reason < 4)
        ++counts[reason];
    return 1;
}
