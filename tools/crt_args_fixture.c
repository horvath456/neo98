/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
#define CRT __declspec(dllimport) __cdecl
API void ExitProcess(U);
API U GetModuleHandleA(const char *);
API U GetProcAddress(U, const char *);
API U GetModuleFileNameA(U, char *, U);
API char *GetCommandLineA(void);
CRT int *__p___argc(void);
CRT char ***__p___argv(void);
CRT char ***__p__environ(void);
CRT char ***__p___initenv(void);
CRT char **__p__acmdln(void);
CRT char **__p__pgmptr(void);
CRT U __getmainargs(int *, char ***, char ***, int, const U *);
static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}
static int equal(const char *a, const char *b)
{
    if (!a || !b)
        return 0;
    while (*a && *a == *b)
    {
        ++a;
        ++b;
    }
    return *a == *b;
}
static int contains(char *const *environment, const char *entry)
{
    for (U i = 0; environment && environment[i] && i < 256; ++i)
        if (equal(environment[i], entry))
            return 1;
    return 0;
}
static void arguments(const char *command, int wanted, const char *const *expected, U code)
{
    int count = -1;
    char **values = 0, **environment = 0;
    const U new_mode = 1;
    *__p__acmdln() = (char *)command;
    check(__getmainargs(&count, &values, &environment, 0, &new_mode) == (U)&environment, code);
    check(count == wanted && *__p___argc() == count && values == *__p___argv() && values && !values[count],
          code + 1);
    check(environment == *__p__environ(), code + 2);
    for (int i = 0; i < count; ++i)
        check(equal(values[i], expected[i]), code + 3);
}
void run_crt_args_test(void)
{
    U dll = GetModuleHandleA("MSVCRT.DLL");
    int *argc_slot = __p___argc();
    char ***argv_slot = __p___argv(), ***environment_slot = __p__environ(), ***initial_slot = __p___initenv();
    char **command_slot = __p__acmdln(), **program_slot = __p__pgmptr();
    check(argc_slot && argv_slot && environment_slot && initial_slot && command_slot && program_slot, 3650);
    check(!*argc_slot && !*argv_slot && !*program_slot, 3651); /* Getters must not parse argv. */
    check(GetProcAddress(dll, "__argc") == (U)argc_slot && GetProcAddress(dll, "__argv") == (U)argv_slot,
          3652);
    check(GetProcAddress(dll, "_environ") == (U)environment_slot &&
              GetProcAddress(dll, "__initenv") == (U)initial_slot,
          3653);
    check(GetProcAddress(dll, "_acmdln") == (U)command_slot && GetProcAddress(dll, "_pgmptr") == (U)program_slot,
          3654);
    check(contains(*environment_slot, "PATH=C:\\WINDOWS;C:\\WINDOWS\\SYSTEM") &&
              contains(*environment_slot, "windir=C:\\WINDOWS"),
          3655); /* _environ exists before the first __getmainargs. */
    char *saved_command = *command_slot;
    char **saved_environment = *environment_slot, **saved_initial = *initial_slot;
    U *adjust = (U *)GetProcAddress(dll, "_adjust_fdiv");
    check(adjust && saved_command == GetCommandLineA(), 3656);
    U saved_adjust = *adjust;
    *adjust = 0x98abcdef;
    static char *private_environment[] = {"PRIVATE=98", 0};
    *initial_slot = private_environment;
    int actual_count = -1;
    char **actual_values = 0, **actual_environment = 0;
    U new_mode = 0;
    check(__getmainargs(&actual_count, &actual_values, &actual_environment, 0, &new_mode) ==
              (U)&actual_environment,
          3657);
    check(actual_count > 0 && actual_values && !actual_values[actual_count] &&
              actual_environment == saved_environment && *initial_slot == private_environment,
          3658);
    char module[260];
    check(GetModuleFileNameA(0, module, sizeof module) && equal(*program_slot, module), 3659);
    static const char first_command[] = "\"C:\\Program Files\\editor.exe\" /p \"hello world.txt\" \"\"";
    static const char *const first[] = {"C:\\Program Files\\editor.exe", "/p", "hello world.txt", ""};
    arguments(first_command, 4, first, 3660);
    char **old_argv = *argv_slot;
    check(equal(*program_slot, module) && *initial_slot == private_environment, 3664);
    static const char *const quotes[] = {"p", "a\"b", "c"};
    arguments("p \"a\"\"b c\"", 3, quotes, 3665);
    static const char slash_command[] = {'p', ' ', 'a', '\\', '\\', '\\', '"', 'b', ' ',
                                         'c', '\\', '\\', '"', 'd', ' ', 'e', '"', 0};
    static const char *const slashes[] = {"p", "a\\\"b", "c\\d e"};
    arguments(slash_command, 3, slashes, 3669);
    static const char *const adjacent[] = {"p", "suffix", "x y"};
    arguments("\"p\"suffix \"x y\"", 3, adjacent, 3673);
    static const char *const leading[] = {"", "p", "x"};
    arguments(" \tp x", 3, leading, 3677);
    check(*argv_slot != old_argv && !old_argv[4], 3681);
    for (U i = 0; i < 4; ++i)
        check(equal(old_argv[i], first[i]), 3682);
    check(*initial_slot == private_environment && contains(*environment_slot, "windir=C:\\WINDOWS") &&
              *adjust == 0x98abcdef,
          3683);
    /* __getmainargs returns the current writable _environ slot, while the
       independently writable __initenv slot keeps the startup program's value. */
    *environment_slot = private_environment;
    arguments("p", 1, adjacent, 3684);
    check(*environment_slot == private_environment && *initial_slot == private_environment, 3688);
    *environment_slot = saved_environment;
    *initial_slot = saved_initial;
    *command_slot = saved_command;
    *adjust = saved_adjust;
    check(__getmainargs(&actual_count, &actual_values, &actual_environment, 0, &new_mode) ==
              (U)&actual_environment && actual_environment == saved_environment &&
              *initial_slot == saved_initial && *command_slot == GetCommandLineA(),
          3689);
}
