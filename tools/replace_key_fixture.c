/* SPDX-License-Identifier: GPL-2.0-or-later */
/* RegReplaceKeyA(HKLM, "NEO98REPLACE", new, old) takes effect when the
 * session ends. The guest saves the key with V="new" into C:\NEW.DAT, sets
 * V="old", asks for the replacement and checks that nothing changes yet.
 * tools/test_replace_key_native.cpp ends the session and looks again.
 * Exit code: 0 on success, 5701.. for the first failed check. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U RegCreateKeyA(U, const char *, U *);
API U RegSetValueExA(U, const char *, U, U, const void *, U);
API U RegQueryValueExA(U, const char *, U *, U *, void *, U *);
API U RegSaveKeyA(U, const char *, void *);
API U RegReplaceKeyA(U, const char *, const char *, const char *);
API U RegCloseKey(U);
API void ExitProcess(U);

static U value_is(U key, const char *text)
{
    char buffer[16] = {0};
    U type = 0, size = sizeof buffer;
    if (RegQueryValueExA(key, "V", 0, &type, buffer, &size) || type != 1)
        return 0;
    for (U i = 0; ; ++i)
    {
        if (buffer[i] != text[i]) return 0;
        if (!text[i]) return 1;
    }
}

void mainCRTStartup(void)
{
    U key = 0;
    if (RegCreateKeyA(0x80000002u, "NEO98REPLACE", &key))
        ExitProcess(5701);
    if (RegSetValueExA(key, "V", 0, 1, "new", 4) || RegSaveKeyA(key, "C:\\NEW.DAT", 0))
        ExitProcess(5702);
    if (RegSetValueExA(key, "V", 0, 1, "old", 4))
        ExitProcess(5703);
    RegCloseKey(key);
    const U r = RegReplaceKeyA(0x80000002u, "NEO98REPLACE", "C:\\NEW.DAT", "C:\\OLD.DAT");
    if (r)
        ExitProcess(5710 + r % 80);
    if (RegCreateKeyA(0x80000002u, "NEO98REPLACE", &key) || !value_is(key, "old"))
        ExitProcess(5704);
    RegCloseKey(key);
    ExitProcess(0);
}
