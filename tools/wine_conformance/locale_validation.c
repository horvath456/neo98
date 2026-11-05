/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <windows.h>
#include "wine/test.h"

START_TEST(locale_validation)
{
    const DWORD ids[] = {0, 9, 0x400, 0x800, 0x409, 0x407, 0xc07, 0x419,
        0x411, 0x412, 0x404, 0x804, 0x40d, 0x401, 0x10407, 0x100000, 0xffffffff};
    const DWORD flags[] = {0, 1, 2, 3, 4, 0xffffffff};
    const char *keys[] = {"SYSTEM\\CurrentControlSet\\Control\\Nls\\Locale",
                         "SYSTEM\\CurrentControlSet\\Control\\Nls\\AddLocales"};
    unsigned i, j;
    for (i = 0; i < sizeof(ids)/sizeof(ids[0]); ++i)
        for (j = 0; j < sizeof(flags)/sizeof(flags[0]); ++j) {
            BOOL result;
            DWORD error;
            SetLastError(0x12345678);
            result = IsValidLocale(ids[i], flags[j]);
            error = GetLastError();
            trace("LOCALE_VALID id=%08lx flags=%08lx result=%u error=%08lx\n",
                  ids[i], flags[j], result, error);
            /* Successful calls may change LastError while loading locale data.
             * Compare the API result separately; invalid inputs assert the error. */
            trace("LOCALE_RESULT id=%08lx flags=%08lx result=%u\n",
                  ids[i], flags[j], result);
            if ((flags[j] != 1 && flags[j] != 2) || (ids[i] & 0xfff00000)) {
                ok(!result, "invalid locale/flags accepted\n");
                ok(error == ERROR_INVALID_FLAGS, "invalid locale/flags error %lu\n", error);
            }
        }
    ok(IsValidLocale(0x409, LCID_INSTALLED), "English locale installed\n");
    ok(IsValidLocale(0x419, LCID_INSTALLED), "Russian locale installed\n");
    for (j = 0; j < 2; ++j) {
        HKEY key;
        LONG opened = RegOpenKeyExA(HKEY_LOCAL_MACHINE, keys[j], 0, KEY_READ, &key);
        trace("LOCALE_KEY index=%u status=%ld\n", j, opened);
        if (opened) continue;
        for (i = 0; i < sizeof(ids)/sizeof(ids[0]); ++i) {
            char name[16], value[256] = {0};
            DWORD size = sizeof(value)-1, type = 0;
            LONG result;
            wsprintfA(name, "%08lx", ids[i]);
            result = RegQueryValueExA(key, name, NULL, &type, (BYTE *)value, &size);
            value[sizeof(value)-1] = 0;
            trace("LOCALE_REG key=%u id=%s status=%ld type=%lu value=%s\n",
                  j, name, result, type, result ? "" : value);
        }
        RegCloseKey(key);
    }
}
