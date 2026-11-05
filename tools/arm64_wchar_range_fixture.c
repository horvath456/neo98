/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
/* Windows wchar_t must not silently truncate supplementary UTF-8. */
#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
static int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; printf("FAIL %d\n", __LINE__); } } while (0)
int main(void)
{
    wchar_t out[4];
    mbstate_t state = {0};
    CHECK(setlocale(LC_ALL, "C.UTF-8") != NULL);
    const char *bad[] = {"\xf0\x90\x80\x80", "\xf0\x9f\x98\x80", "\xf4\x8f\xbf\xbf"};
    for (int i = 0; i < 3; ++i)
    {
        out[0] = 0x1234; errno = 0;
        CHECK(mbrtowc(out, bad[i], 4, &state) == (size_t)-1 && errno == EILSEQ && out[0] == 0x1234);
        CHECK(mbsinit(&state));
        errno = 0;
        CHECK(mbtowc(out, bad[i], 4) == -1 && errno == EILSEQ);
        const char *p = bad[i]; errno = 0;
        CHECK(mbsrtowcs(out, &p, 4, &state) == (size_t)-1 && errno == EILSEQ && p == bad[i]);
        p = bad[i]; errno = 0;
        CHECK(mbsrtowcs(NULL, &p, 0, &state) == (size_t)-1 && errno == EILSEQ);
    }
    CHECK(mbrtowc(out, "\xe2", 1, &state) == (size_t)-2);
    CHECK(mbrtowc(out, "\x82\xac", 2, &state) == 2 && out[0] == 0x20ac);
    CHECK(mbrtowc(out, "\xef\xbf\xbf", 3, &state) == 3 && out[0] == 0xffff);
    CHECK(setlocale(LC_ALL, "C") != NULL);
    CHECK(mbrtowc(out, "\xf0", 1, &state) == 1 && out[0] == 0xdff0);
    printf("WCHAR_RANGE checks=%d failures=%d\n", checks, failures);
    return failures;
}
