/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Reads imported hive keys back through the ported Reg* exports and compares them
   with the bytes the Python hive parser produced for the same keys. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U RegOpenKeyExA(U, const char *, U, U, U *);
API U RegQueryValueExA(U, const char *, U *, U *, void *, U *);
API U RegEnumKeyA(U, U, char *, U);
API U RegCloseKey(U);
#include "registry_import_expected.h"
static void check(int yes, U code)
{
    if (!yes)
        ExitProcess(code);
}
void run_registry_import_test(void)
{
    U keys[EXPECTED_KEYS], i, j;
    for (i = 0; i < EXPECTED_KEYS; ++i)
        check(!RegOpenKeyExA(ExpectedKeys[i].root, ExpectedKeys[i].path, 0, 0x20019, &keys[i]), 26000 + i);
    for (i = 0; i < EXPECTED_VALUES; ++i)
    {
        unsigned char buffer[2048];
        U type = 0, size = sizeof buffer;
        check(!RegQueryValueExA(keys[ExpectedValues[i].key], ExpectedValues[i].name, 0, &type, buffer, &size),
              27000 + i);
        check(type == ExpectedValues[i].type && size == ExpectedValues[i].size, 28000 + i);
        for (j = 0; j < size; ++j)
            check(buffer[j] == ExpectedBytes[ExpectedValues[i].offset + j], 29000 + i);
    }
    for (i = 0; i < EXPECTED_CHILDREN; ++i)
    {
        char name[256];
        const char *expected = ExpectedChildren[i].name;
        check(!RegEnumKeyA(keys[ExpectedChildren[i].key], ExpectedChildren[i].index, name, sizeof name),
              30000 + i);
        for (j = 0; expected[j] || name[j]; ++j)
            check(name[j] == expected[j], 31000 + i);
    }
    for (i = 0; i < EXPECTED_COUNTS; ++i)
    {
        char name[256];
        U count = 0;
        while (!RegEnumKeyA(keys[ExpectedCounts[i].key], count, name, sizeof name))
            ++count;
        check(count == ExpectedCounts[i].count, 32000 + i);
    }
    /* A name the installation never stored must still be missing. */
    {
        U type = 0, size = 4;
        unsigned char buffer[4];
        check(RegQueryValueExA(keys[0], "NEO98NeverImported", 0, &type, buffer, &size) == 2, 33000);
    }
    for (i = 0; i < EXPECTED_KEYS; ++i)
        check(!RegCloseKey(keys[i]), 34000 + i);
}
#ifdef REGISTRY_IMPORT_STANDALONE
void mainCRTStartup(void)
{
    run_registry_import_test();
    ExitProcess(0);
}
#endif
