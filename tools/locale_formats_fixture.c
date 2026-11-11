// SPDX-License-Identifier: GPL-2.0-or-later
// EnumTimeFormats, EnumDateFormats and EnumCalendarInfo answer from the
// record of the locale they are asked about, not from English (United
// States) alone. The expected lists are those of LOCALE.NLS of Windows 98 SE
// for German (Germany) and English (United States). The exit code names the
// first step that failed.
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U EnumTimeFormatsA(void *, U, U);
API U EnumDateFormatsA(void *, U, U);
API U EnumCalendarInfoA(void *, U, U, U);
API U GetSystemDefaultLCID(void);
API U GetLastError(void);
API void ExitProcess(U);

static const char *const *expected;
static U seen, wrong;

static int same(const char *a, const char *b)
{
    while (*a && *a == *b) ++a, ++b;
    return *a == *b;
}
static U __stdcall each(char *text)
{
    if (!expected[seen] || !same(text, expected[seen])) wrong = 1;
    else ++seen;
    return !wrong;
}
// The whole list, in order, and nothing after it.
static int lists(const char *const *want, U ok)
{
    const int all = ok && !wrong && !want[seen];
    return all;
}
#define TIMES(locale, want) (expected = want, seen = wrong = 0, lists(want, EnumTimeFormatsA(each, locale, 0)))
#define DATES(locale, flags, want) (expected = want, seen = wrong = 0, lists(want, EnumDateFormatsA(each, locale, flags)))
#define CALENDAR(locale, type, want) (expected = want, seen = wrong = 0, lists(want, EnumCalendarInfoA(each, locale, 1, type)))

static const char *const german_times[] = {"HH:mm:ss", "H:mm:ss", "H.mm", "H.mm' Uhr '", 0};
static const char *const german_short[] = {"dd.MM.yy", "d.MM.yy", "d.M.yy", "d.M.yyyy", "yyyy-MM-dd", 0};
static const char *const german_long[] = {"dddd, d. MMMM yyyy", "d. MMMM yyyy", "d. MMM yyyy", 0};
static const char *const german_month[] = {"MMMM yyyy", 0};
static const char *const german_monday[] = {"Montag", 0};
static const char *const german_march[] = {"M\xe4rz", 0};
static const char *const english_times[] = {"h:mm:ss tt", "hh:mm:ss tt", "H:mm:ss", "HH:mm:ss", 0};
static const char *const english_short[] = {"M/d/yy", "M/d/yyyy", "MM/dd/yy", "MM/dd/yyyy", "yy/MM/dd", "yyyy-MM-dd", "dd-MMM-yy", 0};
static const char *const english_long[] = {"dddd, MMMM dd, yyyy", "MMMM dd, yyyy", "dddd, dd MMMM, yyyy", "dd MMMM, yyyy", 0};
static const char *const english_month[] = {"MMMM, yyyy", 0};
static const char *const english_monday[] = {"Monday", 0};

void mainCRTStartup(void)
{
    if (!TIMES(0x407, german_times)) ExitProcess(2);
    if (!DATES(0x407, 1, german_short)) ExitProcess(3);
    if (!DATES(0x407, 2, german_long)) ExitProcess(4);
    if (!DATES(0x407, 8, german_month)) ExitProcess(5);
    if (!CALENDAR(0x407, 5, german_short)) ExitProcess(6);
    if (!CALENDAR(0x407, 6, german_long)) ExitProcess(7);
    if (!CALENDAR(0x407, 7, german_monday)) ExitProcess(8);
    if (!CALENDAR(0x407, 23, german_march)) ExitProcess(9);
    if (!CALENDAR(0x407, 47, german_month)) ExitProcess(10);
    if (!TIMES(0x409, english_times)) ExitProcess(11);
    if (!DATES(0x409, 1, english_short)) ExitProcess(12);
    if (!DATES(0x409, 2, english_long)) ExitProcess(13);
    if (!DATES(0x409, 8, english_month)) ExitProcess(14);
    if (!CALENDAR(0x409, 7, english_monday)) ExitProcess(15);
    // LOCALE_SYSTEM_DEFAULT is the locale of the installation.
    const int german = GetSystemDefaultLCID() == 0x407;
    if (!TIMES(0x800, german ? german_times : english_times)) ExitProcess(16);
    if (!DATES(0x800, 2, german ? german_long : english_long)) ExitProcess(17);
    // A locale without a record, and a missing callback.
    if (EnumTimeFormatsA(each, 0x7777, 0) || GetLastError() != 87) ExitProcess(18);
    if (EnumDateFormatsA(0, 0x407, 1) || GetLastError() != 87) ExitProcess(19);
    ExitProcess(0);
}
