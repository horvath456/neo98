/* SPDX-License-Identifier: GPL-2.0-or-later */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U GetVersionExA(void *);
API U GetCurrentProcess(void);
API U GetLastError(void);
API void SetLastError(U);
API void ExitProcess(U);
API U RegCreateKeyExA(U, const char *, U, const char *, U, U, void *, U *, U *);
API U RegSetValueExA(U, const char *, U, U, const void *, U);
API U RegDeleteValueA(U, const char *);
API U RegCloseKey(U);
static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}
void run_version_test(void)
{
    U data[40];
    for (U i = 0; i < 40; ++i)
        data[i] = 0xa5a5a5a5;
    data[0] = 148;
    SetLastError(98);
    check(GetCurrentProcess() == 0x7fffffff && GetVersionExA(data) && GetLastError() == 98, 3120);
    check(data[1] == 4 && data[2] == 10 && data[3] == 0x040a08ae && data[4] == 1, 3121);
    check(data[37] == 0xa5a5a5a5 && data[38] == 0xa5a5a5a5 && data[39] == 0xa5a5a5a5, 3122);
    data[0] = 147;
    check(!GetVersionExA(data) && GetLastError() == 98 && data[1] == 4, 3123);
    U key, disposition, pack = 0xab123456;
    check(!RegCreateKeyExA(0x80000002, "Software\\Microsoft\\Windows\\CurrentVersion", 0, 0, 0, 0xf003f, 0,
                           &key, &disposition),
          3124);
    check(!RegSetValueExA(key, "ServicePackNumber", 0, 4, &pack, 4), 3125);
    check(!RegSetValueExA(key, "SubVersionNumber", 0, 1, " ARM", 5), 3126);
    for (U i = 0; i < 40; ++i)
        data[i] = 0xa5a5a5a5;
    data[0] = 156;
    check(GetVersionExA(data), 3127);
    const unsigned char *bytes = (const void *)data;
    check(bytes[20] == ' ' && bytes[21] == 'A' && bytes[22] == 'R' && bytes[23] == 'M' && !bytes[24] &&
              bytes[25] == 0xa5,
          3128);
    check(data[37] == 0x00560034 && data[38] == 0x12000000 && data[39] == 0xa5a5a5a5, 3129);
    unsigned char long_suffix[129];
    for (U i = 0; i < sizeof long_suffix; ++i)
        long_suffix[i] = 'X';
    check(!RegSetValueExA(key, "SubVersionNumber", 0, 1, long_suffix, sizeof long_suffix), 3130);
    check(GetVersionExA(data), 3131);
    for (U i = 20; i < 148; ++i)
        check(!bytes[i], 3132);
    check(!RegDeleteValueA(key, "ServicePackNumber") && !RegDeleteValueA(key, "SubVersionNumber"), 3133);
    check(GetVersionExA(data) && !data[37] && !data[38] && RegCloseKey(key) == 0, 3134);
}
