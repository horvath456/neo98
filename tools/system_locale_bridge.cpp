// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_system_locale.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using U = unsigned;
using namespace Win98SystemLocale;
constexpr U StateWords = 21, OutputWords = 2048;
static_assert(sizeof(State) == StateWords * sizeof(U));
namespace
{
struct Environment
{
    const U *config;
    const unsigned char *strings;
    U *out, count = 0, error = 0x9898, allocations = 0, cpCalls = 0, localeCalls = 0, buildCalls = 0;
    U nextBlock = 0x140000;
    char dynamic[128]{}, *initial = nullptr;
    U descriptor[16]{}, codepage[16]{}, descriptors = 0;
    const char *Text(U slot) const { return reinterpret_cast<const char *>(strings + slot * 128); }
    void Event(U kind, U a = 0, U b = 0, U c = 0, U d = 0, U e = 0, U f = 0, U g = 0)
    {
        assert(24 + count + 8 <= OutputWords);
        U values[] = {kind, a, b, c, d, e, f, g};
        memcpy(out + 24 + count, values, sizeof values);
        count += 8;
    }
    U Pointer(const char *p) const { return !p ? 0 : p == dynamic ? 2 : 1; }
    U Key(U root, const char *path, const char *name)
    {
        if (root == 0x80000002u && !strcmp(path, MachinePath()) && !*name)
            return 0;
        if (root == 0x80000001u && !strcmp(path, UserPath()) && !strcmp(name, "Locale"))
            return 1;
        if (root == 0x80000001u && !strcmp(path, "Control Panel\\desktop\\ResourceLocale") && !*name)
            return 2;
        assert(root == 0x80000002u && !strcmp(path, "SYSTEM\\CurrentControlSet\\Control\\nls\\codepage"));
        assert(!strcmp(name, "ACP") || !strcmp(name, "OEMCP"));
        return !strcmp(name, "ACP") ? 3 : 4;
    }
    U ReadRegistry(U root, const char *path, const char *name, char *&text, U capacity, U &allocated)
    {
        const U key = Key(root, path, name), before = allocated, pointer = Pointer(text), result = config[16 + key];
        if (!result || (config[34] & (1u << key)))
        {
            allocated = config[24 + key];
            if (allocated)
                text = dynamic;
            memcpy(text, Text(key), 128);
        }
        Event(1, key, capacity, before, pointer, result, allocated, Pointer(text));
        return result;
    }
    U ReadIni(const char *section, const char *key, const char *fallback, char *text, U capacity,
              const char *file)
    {
        assert(!strcmp(section, "Intl") && !*fallback && !strcmp(file, "system.ini"));
        const U slot = !strcmp(key, "SystemLocale") ? 5 : !strcmp(key, "ACP") ? 6 : 7;
        assert(slot != 7 || !strcmp(key, "OEMCP"));
        Event(2, slot - 5, capacity, Pointer(text));
        memcpy(text, Text(slot), 128);
        return strlen(text);
    }
    void Free(U heap, U flags, char *text) { Event(3, heap, flags, Pointer(text)); }
    U CreateHeap(U flags, U initialBytes, U maximum)
    {
        const U result = config[2] ? 0 : 0x444;
        Event(4, flags, initialBytes, maximum, result);
        return result;
    }
    U Allocate(U heap, U flags, U bytes)
    {
        const U result = ++allocations == config[3] ? 0 : nextBlock;
        if (result)
            nextBlock += 0x1000;
        Event(5, heap, flags, bytes, result);
        return result;
    }
    U GlobalAllocate(U flags, U bytes)
    {
        const U result = config[4] ? 0 : nextBlock;
        Event(6, flags, bytes, result);
        return result;
    }
    U LoadCodePage(U page, U &out)
    {
        const U before = out, result = ++cpCalls == config[5] ? config[32] : 0;
        if (!result || config[15])
            out = 0x130800 + cpCalls * 0x40;
        Event(7, page, before, result, out);
        return result;
    }
    U LoadLocale(U locale, U &out, bool create)
    {
        assert(localeCalls < 3);
        const U before = out, result = config[6 + localeCalls++];
        if (!result || config[15])
        {
            out = 0x130000 + localeCalls * 0x40;
            descriptor[descriptors] = out;
            codepage[descriptors++] = locale ^ 0x55aa;
        }
        Event(8, locale, create, before, result, out);
        return result;
    }
    U LocaleCodePage(U pointer)
    {
        for (U i = 0; i < descriptors; ++i)
            if (descriptor[i] == pointer)
                return codepage[i];
        assert(false);
        return 0;
    }
    U BuildLocale(U locale, U page, U &out, bool create)
    {
        const U before = out, result = ++buildCalls == config[10] ? config[33] : 0;
        if (config[14] && (!result || config[15]))
        {
            out = 0x131000 + buildCalls * 0x40;
            descriptor[descriptors] = out;
            codepage[descriptors++] = page;
        }
        Event(9, locale, page, before, create, result, out);
        return result;
    }
    U InitializeUnicode()
    {
        Event(10, config[9]);
        return config[9];
    }
    U InitializeOverrides(U pointer, U mode)
    {
        Event(11, pointer, mode, config[11]);
        return config[11];
    }
    void Error(U value)
    {
        error = value;
        Event(12, value);
    }
    void BootstrapLock() { Event(13, 0); }
    void InitializeLock() { Event(13, 1); }
    void BootstrapUnlock() { Event(13, 2); }
    void EnterLock() { Event(13, 3); }
    void LeaveLock() { Event(13, 4); }
};
} // namespace
extern "C" void system_locale_call(const U *config, const unsigned char *text, const U *initial, U *out)
{
    memset(out, 0, OutputWords * sizeof(U));
    State state;
    memcpy(&state, initial, sizeof state);
    Environment e{config, text, out};
    U result = 0;
    switch (config[0])
    {
    case 0: result = ParseHex(e.Text(8)); break;
    case 1: result = ParseDecimal(e.Text(8)); break;
    case 2: result = SystemSource(e, state, config[1]); break;
    case 3: result = UserSource(e, state, config[1]); break;
    case 4: result = ResourceSource(e, state, config[1]); break;
    case 5: result = CodePageSource(e, state, "ACP", config[1]); break;
    case 6: result = CodePageSource(e, state, "OEMCP", config[1]); break;
    case 7: result = InitializeTables(e, state); break;
    case 8: result = Initialize(e, state); break;
    case 9: result = Start(e, state); break;
    case 10: result = GetSystem(state); break;
    case 11: result = GetSystemLang(state); break;
    case 12: result = GetThread(state); break;
    case 13: result = Invalidate(state); break;
    case 14:
        result = ResolveLocale(config[35], state.system, [&] {
            if (!state.user)
            {
                e.Event(14, config[1]);
                state.user = config[1];
            }
            return state.user;
        });
        break;
    default: assert(false);
    }
    out[0] = result;
    out[1] = e.error;
    out[2] = e.count;
    memcpy(out + 3, &state, sizeof state);
}
#ifdef SYSTEM_LOCALE_MAIN
int main(int argc, char **argv)
{
    constexpr U ConfigWords = 40, TextBytes = 9 * 128;
    assert(argc == 2);
    FILE *file = fopen(argv[1], "rb");
    assert(file);
    U config[ConfigWords], initial[StateWords], expected[OutputWords], actual[OutputWords], count = 0;
    unsigned char text[TextBytes];
    while (fread(config, sizeof config, 1, file) == 1)
    {
        assert(fread(text, sizeof text, 1, file) == 1);
        assert(fread(initial, sizeof initial, 1, file) == 1);
        assert(fread(expected, sizeof expected, 1, file) == 1);
        system_locale_call(config, text, initial, actual);
        assert(!memcmp(expected, actual, sizeof actual));
        ++count;
    }
    assert(feof(file));
    fclose(file);
    printf("SYSTEM LOCALE ASan/UBSan PASS %u cases\n", count);
}
#endif
