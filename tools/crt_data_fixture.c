/* SPDX-License-Identifier: GPL-2.0-or-later
 * Exercise the same MSVCRT data through named and original ordinal exports. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U GetModuleHandleA(const char *);
API U GetProcAddress(U, const char *);
API void ExitProcess(U);

void run_crt_data_test(void)
{
    /* Ordinals are from the local MSVCRT 6.0.8797.0 export table, whose hash
     * is also recorded by the API inventory. No original bytes are included. */
    static const struct { const char *name; U ordinal; } exports[] = {
        {"__argc", 80}, {"__argv", 81}, {"__initenv", 90},
        {"__lc_codepage", 94}, {"__lc_collate_cp", 95}, {"__lc_handle", 96},
        {"__mb_cur_max", 98}, {"_acmdln", 144}, {"_adjust_fdiv", 158},
        {"_ctype", 190}, {"_environ", 199}, {"_pctype", 399},
        {"_pgmptr", 400}, {"_pwctype", 408}
    };
    const U module = GetModuleHandleA("MSVCRT.DLL");
    if (!module) ExitProcess(5400);
    for (U i = 0; i < sizeof exports / sizeof *exports; ++i)
    {
        const U named = GetProcAddress(module, exports[i].name);
        const U numbered = GetProcAddress(module, (const char *)exports[i].ordinal);
        if (!named || named != numbered) ExitProcess(5401 + i);
    }
    /* Confirm that typed data and the independent CRT getter agree. */
    typedef int *(__cdecl *ArgcGetter)(void);
    const ArgcGetter get_argc = (ArgcGetter)GetProcAddress(module, "__p___argc");
    int *argc = (int *)GetProcAddress(module, (const char *)80);
    /* argc remains zero until __getmainargs has parsed the command line. */
    if (!get_argc || get_argc() != argc || *argc < 0 || *argc > 256) ExitProcess(5415);
    U *named = (U *)GetProcAddress(module, "_adjust_fdiv");
    U *numbered = (U *)GetProcAddress(module, (const char *)158);
    const U saved = *named;
    *named = 0x98a64001;
    if (*numbered != 0x98a64001) ExitProcess(5416);
    *numbered = saved;
    if (*named != saved) ExitProcess(5417);
}
