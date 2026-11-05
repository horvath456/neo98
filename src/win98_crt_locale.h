// SPDX-License-Identifier: GPL-2.0-or-later
// Windows 98 C runtime locale selection and conversion behavior.
#pragma once
#include <stdint.h>
#include <string.h>
namespace Win98Locale
{
struct Parts
{
    char language[64], country[64], page[8];
};
struct Id
{
    uint16_t language, country, page;
};
struct State
{
    const char c_locale[2] = {'C', 0};
    char *names[6] = {};
    unsigned handles[6] = {};
    Id ids[6] = {};
    unsigned code_page = 0, collate_page = 0, set_active = 0, read_active = 0;
    char last_input[132] = {'C', 0}, canonical[132] = {'C', 0};
    Id cached_id = {};
    unsigned cached_page = 0;
    State()
    {
        for (unsigned i = 1; i < 6; ++i)
            names[i] = const_cast<char *>(c_locale);
    }
};
struct Environment
{
    virtual ~Environment() = default;
    virtual char *Allocate(unsigned bytes) = 0;
    virtual void Free(char *pointer) = 0;
    virtual void Lock(unsigned id) = 0;
    virtual void Unlock(unsigned id) = 0;
    virtual void Sleep(unsigned ms) = 0;
    // Locale qualification resolves the language and country,
    // selects a code page, and replaces Parts with canonical system strings.
    virtual bool Qualify(Parts &parts, Id &id) = 0;
    // The original category callback reads the already-updated category state.
    virtual int Initialize(unsigned category, State &state) = 0;
};
inline const char *Category(unsigned n)
{
    const char *names[] = {"LC_ALL", "LC_COLLATE", "LC_CTYPE", "LC_MONETARY", "LC_NUMERIC", "LC_TIME"};
    return n < 6 ? names[n] : nullptr;
}
// The native port rejects Windows 98 stack/cache
// overflow inputs: code-page components over seven bytes and strings >=132.
inline bool Parse(const char *input, Parts &out)
{
    memset(&out, 0, sizeof out);
    if (!*input)
        return true;
    if (input[0] == '.' && input[1])
    {
        if (strlen(input + 1) >= sizeof out.page)
            return false;
        strcpy(out.page, input + 1);
        return true;
    }
    for (unsigned part = 0;; ++part)
    {
        unsigned count = strcspn(input, "_.,");
        if (!count)
            return false;
        char delimiter = input[count];
        char *target;
        if (part == 0)
        {
            if (count >= 64 || delimiter == '.')
                return false;
            target = out.language;
        }
        else if (part == 1)
        {
            if (count >= 64 || delimiter == '_')
                return false;
            target = out.country;
        }
        else if (part == 2)
        {
            if ((delimiter && delimiter != ',') || count >= 8)
                return false;
            target = out.page;
        }
        else
            return false;
        memcpy(target, input, count);
        if (!delimiter || delimiter == ',')
            return true;
        input += count + 1;
    }
}
// Build the qualified locale string with the append helper.
inline bool Format(const Parts &parts, char *out, unsigned capacity)
{
    unsigned n = strlen(parts.language);
    if (parts.country[0])
        n += 1 + strlen(parts.country);
    if (parts.page[0])
        n += 1 + strlen(parts.page);
    if (n >= capacity)
        return false;
    strcpy(out, parts.language);
    if (parts.country[0])
    {
        strcat(out, "_");
        strcat(out, parts.country);
    }
    if (parts.page[0])
    {
        strcat(out, ".");
        strcat(out, parts.page);
    }
    return true;
}
// Construct the locale record and handle initialization failures.
inline bool Expand(Environment &e, State &s, const char *input, char *out, Id *id = nullptr,
                   unsigned *page = nullptr)
{
    if (!input)
        return false;
    if (!strcmp(input, "C"))
    {
        strcpy(out, "C");
        if (id)
            *id = {};
        if (page)
            *page = 0;
        return true;
    }
    if (strlen(input) >= sizeof s.last_input)
        return false;
    if (strcmp(input, s.canonical) && strcmp(input, s.last_input))
    {
        Parts parts;
        if (!Parse(input, parts) || !e.Qualify(parts, s.cached_id))
            return false;
        s.cached_page = s.cached_id.page;
        if (!Format(parts, s.canonical, sizeof s.canonical))
            return false;
        strcpy(s.last_input, *input ? input : s.canonical);
    }
    if (id)
        *id = s.cached_id;
    if (page)
        *page = s.cached_page;
    strcpy(out, s.canonical);
    return true;
}
// Update the locale category and preserve failure behavior.
inline char *SetCategory(Environment &e, State &s, unsigned category, const char *input)
{
    char expanded[132];
    Id id;
    unsigned page;
    if (!Expand(e, s, input, expanded, &id, &page))
        return nullptr;
    char *replacement = e.Allocate(strlen(expanded) + 1);
    if (!replacement)
        return nullptr;
    char *old = s.names[category];
    unsigned old_handle = s.handles[category], old_page = s.code_page;
    strcpy(replacement, expanded);
    s.names[category] = replacement;
    s.handles[category] = id.language;
    s.ids[category] = id;
    if (category == 2)
        s.code_page = page;
    if (category == 1)
        s.collate_page = page;
    if (!e.Initialize(category, s))
    {
        if (old != s.c_locale)
            e.Free(old);
        return s.names[category];
    }
    s.names[category] = old;
    e.Free(replacement);
    s.handles[category] = old_handle;
    s.code_page = old_page;
    // The original copies old ids to a local but never restores them. It also
    // leaves collate_page changed after a failing LC_COLLATE initializer.
    return nullptr;
}
// Build the combined locale description.
inline char *All(Environment &e, State &s)
{
    if (!s.names[0])
        s.names[0] = e.Allocate(0x351);
    // Original dereferences a failed allocation. Native boundary returns null.
    if (!s.names[0])
        return nullptr;
    bool equal = true;
    char *result = s.names[0];
    result[0] = 0;
    for (unsigned i = 1; i <= 5; ++i)
    {
        if (i > 1)
        {
            strcat(result, ";");
            if (strcmp(s.names[i - 1], s.names[i]))
                equal = false;
        }
        strcat(result, Category(i));
        strcat(result, "=");
        strcat(result, s.names[i]);
    }
    if (!equal)
        return result;
    e.Free(result);
    s.names[0] = nullptr;
    return s.names[2];
}
// Public setlocale behavior, including allocation and validation failures.
inline char *Set(Environment &e, State &s, int category, const char *input)
{
    if (category < 0 || category > 5)
        return nullptr;
    e.Lock(19);
    ++s.set_active;
    while (s.read_active)
        e.Sleep(1);
    char *result = nullptr;
    if (category)
        result = input ? SetCategory(e, s, category, input) : s.names[category];
    else if (!input)
        result = All(e, s);
    else if (!strncmp(input, "LC_", 3))
    {
        unsigned successes = 0;
        const char *at = input;
        for (;;)
        {
            const char *end = strpbrk(at, "=;");
            if (!end || end == at || *end == ';')
                break;
            unsigned length = end - at, selected = 6;
            for (unsigned i = 1; i <= 5; ++i)
                if (strlen(Category(i)) == length && !strncmp(Category(i), at, length))
                {
                    selected = i;
                    break;
                }
            const char *value = end + 1;
            unsigned size = strcspn(value, ";");
            if (!size && *value != ';')
                break;
            if (selected < 6)
            {
                char temporary[132];
                if (size >= sizeof temporary)
                    break;
                memcpy(temporary, value, size);
                temporary[size] = 0;
                if (SetCategory(e, s, selected, temporary))
                    ++successes;
            }
            at = value + size;
            if (!*at || !*++at)
            {
                if (successes)
                    result = All(e, s);
                break;
            }
        }
    }
    else
    {
        char expanded[132];
        if (Expand(e, s, input, expanded))
        {
            bool complete = true;
            unsigned successes = 0;
            for (unsigned i = 1; i <= 5; ++i)
            {
                if (!strcmp(s.names[i], expanded) || SetCategory(e, s, i, expanded))
                    ++successes;
                else
                    complete = false;
            }
            if (complete)
            {
                result = All(e, s);
                e.Free(s.names[0]);
                s.names[0] = nullptr;
            }
            else if (successes)
                result = All(e, s);
        }
    }
    e.Unlock(19);
    --s.set_active;
    return result;
}

// Native storage for category data. The original algorithms use dynamically
// allocated pointer tables; fixed buffers make their ownership independent of
// x86 pointer width. Values come from the local original CRT and NLS files.
struct SystemData
{
    uint16_t ctype[257] = {}, wide_type[257] = {};
    unsigned mb_max = 1;
    char numeric[3][128] = {};
    char monetary[7][128] = {};
    uint8_t monetary_flags[8] = {};
    char time[43][128] = {};
    SystemData();
    int Initialize(unsigned category, const State &state);
};
// Configured system profile is English-US, ACP1252. Unsupported language,
// country or code-page requests fail; no fallback claims success for them.
bool QualifySystem(Parts &parts, Id &id);
const char *SystemInfo(unsigned locale, unsigned type);
// Calendar metadata does not require an installed ANSI conversion table.
unsigned LocaleCalendar(unsigned locale, bool alternate);
const char *CalendarYearMaximum(unsigned calendar);
unsigned CalendarYear(unsigned calendar, unsigned year, unsigned month, unsigned day, const char *&era);
void ConvertGrouping(char *text);
// Native MSVCRT SBCS classification and case-conversion decisions.
// Values below -1 in active locales are a native safe boundary for the
// original direct table underflow. They are returned unchanged by Case.
const uint16_t *ClassicType();
unsigned IsCType(const SystemData &data, unsigned character, unsigned mask);
unsigned Case(const State &state, const SystemData &data, unsigned character, bool upper);
} // namespace Win98Locale
