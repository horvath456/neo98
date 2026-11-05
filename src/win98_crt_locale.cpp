// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_crt_locale.h"
#ifdef NEO98_RUNTIME_CONTENT
#include "windows_runtime_locale.h"
#else
#include "../build/crt-locale/crt_locale_data.h"
#endif
namespace Win98Locale
{
namespace
{
bool Equal(const char *a, const char *b)
{
    while (*a && *b)
    {
        unsigned x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z')
            x += 32;
        if (y >= 'A' && y <= 'Z')
            y += 32;
        if (x != y)
            return false;
    }
    return *a == *b;
}
bool Info(unsigned type, char *out)
{
    const char *value = SystemInfo(0x409, type);
    if (!value || strlen(value) >= 128)
        return false;
    strcpy(out, value);
    return true;
}
} // namespace
const char *SystemInfo(unsigned locale, unsigned type)
{
    if (!locale || locale == 9 || locale == 0x400 || locale == 0x800) locale = 0x409;
    type &= 0x1fffffffu;
    for (const auto &entry : OriginalLocaleData)
        if (entry.locale == locale)
            for (unsigned i = 0; i < sizeof OriginalLocaleTypes / sizeof OriginalLocaleTypes[0]; ++i)
                if (OriginalLocaleTypes[i] == type) return entry.values[i];
    return nullptr;
}
unsigned LocaleCalendar(unsigned locale, bool alternate)
{
    for (const auto &entry : OriginalLocaleCalendars)
        if (entry.locale == locale)
            return alternate && entry.alternate ? entry.alternate : entry.standard;
    return 0;
}
const char *CalendarYearMaximum(unsigned calendar)
{
    for (const auto &entry : OriginalCalendars)
        if (entry.id == calendar) return entry.maximum;
    return nullptr;
}
unsigned CalendarYear(unsigned calendar, unsigned year, unsigned month, unsigned day, const char *&era)
{
    // KERNEL32: calendars 3/4 choose the most recent era that
    // has started; 5/7 add the single era's year offset. Before the first
    // era the Gregorian year remains unchanged and no era is emitted.
    era = nullptr;
    for (const auto &entry : OriginalCalendarEras)
        if (entry.calendar == calendar)
        {
            if (calendar == 5 || calendar == 7)
            { era = entry.name; return (year + entry.year) & 0xffff; }
            if ((calendar == 3 || calendar == 4) &&
                (year > entry.year || (year == entry.year &&
                 (month > entry.month || (month == entry.month && day >= entry.day)))))
            { era = entry.name; return (year - entry.year + 1) & 0xffff; }
        }
    return year;
}
bool QualifySystem(Parts &parts, Id &id)
{
    if (parts.language[0] && !Equal(parts.language, "English") && !Equal(parts.language, "ENU"))
        return false;
    if (parts.country[0] && !Equal(parts.country, "United States") && !Equal(parts.country, "USA"))
        return false;
    if (parts.page[0] && strcmp(parts.page, "ACP") && strcmp(parts.page, "1252"))
        return false;
    strcpy(parts.language, SystemInfo(0x409, 0x1001));
    strcpy(parts.country, SystemInfo(0x409, 0x1002));
    strcpy(parts.page, SystemInfo(0x409, 0x1004));
    id = {0x409, 0x409, 1252};
    return true;
}
// Identical grouping-string conversions at780051D3 and7800534E. The CRT
// turns ASCII digits into byte group sizes and removes semicolons in place.
void ConvertGrouping(char *text)
{
    if (!text)
        return;
    while (*text)
    {
        unsigned char c = *text;
        if (c >= '0' && c <= '9')
        {
            *text = c - '0';
            ++text;
        }
        else if (c == ';')
        {
            char *p = text;
            do
            {
                *p = p[1];
                ++p;
            } while (*p);
        }
        else
            ++text;
    }
}
SystemData::SystemData()
{
    memcpy(ctype, OriginalCType, sizeof ctype);
    memcpy(wide_type, OriginalCType, sizeof wide_type);
    strcpy(numeric[0], ".");
    memset(monetary_flags, 127, sizeof monetary_flags);
    for (unsigned i = 0; i < 43; ++i)
        strcpy(time[i], OriginalCTime[i]);
}
int SystemData::Initialize(unsigned category, const State &state)
{
    if (category < 1 || category > 5)
        return 1;
    const bool classic = !state.handles[category];
    if (!classic && (state.handles[category] != 0x409 || state.ids[category].language != 0x409 ||
                     state.ids[category].country != 0x409 || state.ids[category].page != 1252))
        return 1;
    if (category == 1)
        return 0; // Original78004DB3 has no collation-table initializer.
    if (category == 2)
    {
        // Original78002F9C builds EOF+256 classification entries. Win98's W
        // API is absent, so its wide helper falls back through the ACP table.
        if (!classic && state.code_page != 1252)
            return 1;
        memcpy(ctype, classic ? OriginalCType : OriginalSystemCType, sizeof ctype);
        memcpy(wide_type, classic ? OriginalCType : OriginalSystemWideType, sizeof wide_type);
        mb_max = 1;
        return 0;
    }
    if (category == 3)
    {
        // Original78004E2F field order, with780051D3 grouping conversion.
        if (classic)
        {
            memset(monetary, 0, sizeof monetary);
            memset(monetary_flags, 127, sizeof monetary_flags);
            return 0;
        }
        const unsigned strings[] = {0x15, 0x14, 0x16, 0x17, 0x18, 0x50, 0x51};
        const unsigned numbers[] = {0x1a, 0x19, 0x54, 0x55, 0x56, 0x57, 0x52, 0x53};
        for (unsigned i = 0; i < 7; ++i)
            if (!Info(strings[i], monetary[i]))
                return 1;
        ConvertGrouping(monetary[4]);
        for (unsigned i = 0; i < 8; ++i)
        {
            const char *value = SystemInfo(0x409, numbers[i]);
            if (!value)
                return 1;
            unsigned n = 0;
            for (unsigned k = 0; k < 4 && value[k] >= '0' && value[k] <= '9'; ++k)
                n = n * 10 + value[k] - '0';
            monetary_flags[i] = n;
        }
        return 0;
    }
    if (category == 4)
    {
        // Original78005296 shares these three fields with localeconv's
        // monetary record; native ownership keeps the numeric strings here.
        if (classic)
        {
            memset(numeric, 0, sizeof numeric);
            strcpy(numeric[0], ".");
            return 0;
        }
        for (unsigned i = 0; i < 3; ++i)
            if (!Info(0xe + i, numeric[i]))
                return 1;
        ConvertGrouping(numeric[2]);
        return 0;
    }
    // Original780053E4 uses Sunday-first arrays although Windows numbers
    // Monday first. It takes names from the language and formats from country.
    if (classic)
    {
        for (unsigned i = 0; i < 43; ++i)
            strcpy(time[i], OriginalCTime[i]);
        return 0;
    }
    for (unsigned i = 0; i < 7; ++i)
    {
        if (!Info(i ? 0x30 + i : 0x37, time[i]) || !Info(i ? 0x29 + i : 0x30, time[i + 7]))
            return 1;
    }
    for (unsigned i = 0; i < 12; ++i)
        if (!Info(0x44 + i, time[14 + i]) || !Info(0x38 + i, time[26 + i]))
            return 1;
    const unsigned formats[] = {0x28, 0x29, 0x1f, 0x20, 0x1003};
    for (unsigned i = 0; i < 5; ++i)
        if (!Info(formats[i], time[38 + i]))
            return 1;
    return 0;
}
const uint16_t *ClassicType() { return OriginalCType; }
// Original7800C348. Outside EOF..255, the SBCS path classifies the low byte
// through the system GetStringType provider, even when the CRT locale is C.
unsigned IsCType(const SystemData &data, unsigned character, unsigned mask)
{
    const unsigned value =
        character + 1 <= 256 ? data.ctype[character + 1] : OriginalSystemCType[(character & 255) + 1];
    return value & mask;
}
// toupper and tolower use the current locale case mappings.
unsigned Case(const State &state, const SystemData &data, unsigned character, bool upper)
{
    const int value = (int)character;
    if (!state.handles[2])
    {
        const int first = upper ? 'a' : 'A', last = upper ? 'z' : 'Z';
        return value >= first && value <= last ? character + (upper ? -32 : 32) : character;
    }
    if (value < -1)
        return character;
    if (value < 256 && !(data.ctype[character + 1] & (upper ? 2 : 1)))
        return character;
    return (upper ? OriginalUpper : OriginalLower)[character & 255];
}
} // namespace Win98Locale
