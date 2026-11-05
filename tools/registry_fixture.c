/* SPDX-License-Identifier: GPL-2.0-or-later
 * Own x86 registry ABI contract, also embedded in the bare ARM boot test. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U RegCreateKeyExA(U, const char *, U, const char *, U, U, U, U *, U *);
API U RegOpenKeyA(U, const char *, U *);
API U RegCloseKey(U);
API U RegSetValueExA(U, const char *, U, U, const void *, U);
API U RegQueryValueExA(U, const char *, U, U *, void *, U *);
API U RegEnumKeyA(U, U, char *, U);
API U RegEnumKeyExA(U, U, char *, U *, U, char *, U *, void *);
API U RegEnumValueA(U, U, char *, U *, U, U *, void *, U *);
API U RegQueryInfoKeyA(U, char *, U *, U, U *, U *, U *, U *, U *, U *, U *, void *);
API U RegDeleteValueA(U, const char *);
API U RegDeleteKeyA(U, const char *);
API U RegSetValueA(U, const char *, U, const char *, U);
API U RegQueryValueA(U, const char *, char *, U *);
API U ShowCursor(U);
static void check(int ok, U code)
{
    if (!ok)
        ExitProcess(code);
}
static int equal(const char *a, const char *b)
{
    while (*a && *a == *b)
    {
        ++a;
        ++b;
    }
    return *a == *b;
}
void run_registry_test(void)
{
    U root = 0x80000001, key = 0, created = 0, other = 0;
    check(
        !RegCreateKeyExA(root, "Software\\NEO98Abi\\Nested", 0, "Contract", 0, 0xf003f, 0, &key, &created) &&
            created == 1,
        301);
    U number = 0xdeadbeef;
    check(!RegSetValueExA(key, "Number", 0, 4, &number, 4), 302);
    check(!RegOpenKeyA(0x80000003, ".Default\\software\\NEO98ABI\\nested", &other), 303);
    U size = 0, type = 0;
    check(!RegQueryValueExA(other, "NUMBER", 0, &type, 0, &size) && type == 4 && size == 4, 304);
    U output = 0x12345678;
    size = 3;
    check(RegQueryValueExA(other, "Number", 0, &type, &output, &size) == 234 && size == 4 &&
              output == 0x12345678,
          305);
    size = 4;
    check(!RegQueryValueExA(other, "Number", 0, &type, &output, &size) && output == number, 306);
    U reserved = 0;
    size = sizeof output;
    check(!RegQueryValueExA(other, "Number", (U)&reserved, &type, &output, &size) &&
          output == number && reserved == 0, 322);
    char name[64];
    U name_length = 64;
    size = 4;
    check(!RegEnumValueA(key, 0, name, &name_length, 0, &type, &output, &size) && equal(name, "Number") &&
              name_length == 6 && output == number,
          307);
    check(RegEnumValueA(key, 1, name, &name_length, 0, &type, 0, &size) == 259, 308);
    U parent = 0;
    check(!RegOpenKeyA(root, "Software\\NEO98Abi", &parent), 309);
    name[0] = '!';
    check(RegEnumKeyA(parent, 0, name, 3) == 234 && name[0] == '!', 310);
    check(!RegEnumKeyA(parent, 0, name, 64) && equal(name, "Nested") &&
              RegEnumKeyA(parent, 1, name, 64) == 259,
          311);
    char klass[64];
    name_length = 64;
    U klass_length = 64;
    check(!RegEnumKeyExA(parent, 0, name, &name_length, 0, klass, &klass_length, 0) && name_length == 6 &&
              equal(klass, "") && klass_length == 0,
          312);
    U children = 99, max_name = 99, max_class = 99, values = 99, max_value_name = 99, max_data = 99;
    check(!RegQueryInfoKeyA(parent, 0, 0, 0, &children, &max_name, &max_class, &values, &max_value_name,
                            &max_data, 0, 0) &&
              children == 1 && max_name == 6 && max_class == 0 && values == 0,
          313);
    check(!RegQueryInfoKeyA(key, 0, 0, 0, &children, &max_name, &max_class, &values, &max_value_name,
                            &max_data, 0, 0) &&
              children == 0 && values == 1 && max_value_name == 6 && max_data == 4,
          314);
    check(!RegSetValueA(key, "", 1, "default", 1), 315);
    size = 64;
    check(!RegQueryValueA(key, "", name, &size) && size == 8 && equal(name, "default"), 316);
    check(!RegDeleteValueA(key, "number") && RegQueryValueExA(key, "Number", 0, 0, 0, &size) == 2, 317);
    check(!RegDeleteKeyA(parent, "Nested") && RegQueryValueExA(other, "Number", 0, 0, 0, &size) == 1018, 318);
    check(!RegCloseKey(key) && !RegCloseKey(other) && RegCloseKey(other) == 6, 319);
    check(!RegCloseKey(parent) && !RegDeleteKeyA(root, "Software\\NEO98Abi"), 320);
    check(ShowCursor(0) == ~0u && ShowCursor(0) == ~1u && ShowCursor(1) == ~0u && ShowCursor(1) == 0, 321);
}
