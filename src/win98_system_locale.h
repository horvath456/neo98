// SPDX-License-Identifier: GPL-2.0-or-later
// Original Win98 KERNEL32 NLS startup policy, locale sources and number parsers.
// Memory, registry/profile IO, locks and NLS table loaders are native providers.
#pragma once
namespace Win98SystemLocale
{
using U = unsigned;
static_assert(sizeof(U) == 4, "Original NLS arithmetic requires DWORD width");
struct State
{
    U heap = 0, tableStorage = 0, codepageBuckets = 0, localeBuckets = 0;
    // Original zeroed globals: 9710,96E8,9728,9720,96D8 in that order.
    U transient[5]{};
    U ansi = 0, oem = 0, system = 0, user = 0, resource = 0;
    U ansiTable = 0, oemTable = 0, systemTable = 0, userTable = 0;
    U overrides = 0, overrideValid = 0, initialized = 0;
};
// Every character first shifts the DWORD; even a quote
// or x/X loses the previous top nibble when the original shifts it back.
inline U ParseHex(const char *text)
{
    U value = 0;
    for (U c; (c = static_cast<unsigned char>(*text++)) != 0;)
    {
        value <<= 4;
        if (c == '"' || c == 'x' || c == 'X')
            value >>= 4;
        else if (c >= '0' && c <= '9')
            value += c - '0';
        else if (c >= 'A' && c <= 'F')
            value += c - 'A' + 10;
        else if (c >= 'a' && c <= 'f')
            value += c - 'a' + 10;
        else
            return 0;
    }
    return value;
}
// ACP/OEMCP use the decimal version, including its
// multiply-before-quote/divide behavior and DWORD overflow.
inline U ParseDecimal(const char *text)
{
    U value = 0;
    for (U c; (c = static_cast<unsigned char>(*text++)) != 0;)
    {
        value *= 10;
        if (c == '"')
            value /= 10;
        else if (c >= '0' && c <= '9')
            value += c - '0';
        else
            return 0;
    }
    return value;
}
inline const char *MachinePath() { return "SYSTEM\\CurrentControlSet\\Control\\Nls\\Locale"; }
inline const char *UserPath() { return "Control Panel\\International"; }
template <class E> void FreeText(E &e, const State &s, char *text, U allocated)
{
    if (allocated && text)
        e.Free(s.heap, 0, text);
}
// Registry parse0 falls through to the INI source.
template <class E> U SystemSource(E &e, const State &s, U fallback)
{
    char buffer[128];
    char *text = buffer;
    U allocated = 0;
    if (!e.ReadRegistry(0x80000002u, MachinePath(), "", text, 128, allocated))
    {
        const U value = ParseHex(text);
        if (value)
        {
            FreeText(e, s, text, allocated);
            return value;
        }
    }
    e.ReadIni("Intl", "SystemLocale", "", text, 128, "system.ini");
    const U value = ParseHex(text);
    FreeText(e, s, text, allocated);
    return value ? value : fallback;
}
// Only a registry failure tries HKLM; a successfully
// read user string that parses to0 goes directly to the supplied fallback.
template <class E> U UserSource(E &e, const State &s, U fallback)
{
    char buffer[128];
    char *text = buffer;
    U allocated = 0, value = 0;
    if (!e.ReadRegistry(0x80000001u, UserPath(), "Locale", text, 128, allocated))
        value = ParseHex(text);
    else if (!e.ReadRegistry(0x80000002u, MachinePath(), "", text, 128, allocated))
        value = ParseHex(text);
    FreeText(e, s, text, allocated);
    return value ? value : fallback;
}

template <class E> U ResourceSource(E &e, const State &s, U fallback)
{
    char buffer[128];
    char *text = buffer;
    U allocated = 0, value = 0;
    if (!e.ReadRegistry(0x80000001u, "Control Panel\\desktop\\ResourceLocale", "", text, 128, allocated))
        value = ParseHex(text);
    FreeText(e, s, text, allocated);
    return value ? value : fallback;
}
// ACP/OEMCP source selection uses the decimal parser.
template <class E> U CodePageSource(E &e, const State &s, const char *name, U fallback)
{
    char buffer[128];
    char *text = buffer;
    U allocated = 0;
    if (!e.ReadRegistry(0x80000002u, "SYSTEM\\CurrentControlSet\\Control\\nls\\codepage", name, text, 128,
                        allocated))
    {
        const U value = ParseDecimal(text);
        if (value)
        {
            FreeText(e, s, text, allocated);
            return value;
        }
    }
    e.ReadIni("Intl", name, "", text, 128, "system.ini");
    const U value = ParseDecimal(text);
    FreeText(e, s, text, allocated);
    return value ? value : fallback;
}
// Native fields replace the root object's +0/+4 slots.
// Partial allocations are intentionally not rolled back by this routine.
template <class E> U InitializeTables(E &e, State &s)
{
    s.tableStorage = e.Allocate(s.heap, 8, 0x22c);
    s.codepageBuckets = s.localeBuckets = 0;
    if (!s.tableStorage)
        return 14;
    s.codepageBuckets = e.Allocate(s.heap, 8, 0x90);
    if (!s.codepageBuckets)
        return 14;
    s.localeBuckets = e.Allocate(s.heap, 8, 0x120);
    return s.localeBuckets ? 0 : 14;
}
// Result0 means success; initialization can leave partial
// state on failure. A loader returns an original status and publishes a
// descriptor through its reference argument only as its provider specifies.
template <class E> U Initialize(E &e, State &s)
{
    s.heap = e.CreateHeap(0x04000000, 0, 0x8000);
    if (!s.heap)
        return 14;
    U error = InitializeTables(e, s);
    if (error)
        return error;
    for (auto &value : s.transient)
        value = 0;
    s.ansi = CodePageSource(e, s, "ACP", 1252);
    s.oem = CodePageSource(e, s, "OEMCP", 437);
    s.system = SystemSource(e, s, 0x409);
    s.user = UserSource(e, s, 0x409);
    s.resource = ResourceSource(e, s, s.system);
    if (e.LoadCodePage(s.ansi, s.ansiTable))
        return 2;
    if (s.ansi == s.oem)
        s.oemTable = s.ansiTable;
    else if (e.LoadCodePage(s.oem, s.oemTable))
        return 2;
    error = e.LoadLocale(s.system, s.systemTable, true);
    if (error)
    {
        if (e.LoadLocale(0x409, s.systemTable, true))
            return error;
        s.system = 0x409;
    }
    if (s.user == s.system)
        s.userTable = s.systemTable;
    else if (e.LoadLocale(s.user, s.userTable, true))
    {
        s.user = s.system;
        s.userTable = s.systemTable;
    }
    error = e.InitializeUnicode();
    if (error)
        return error;
    if (e.BuildLocale(s.system, e.LocaleCodePage(s.systemTable), s.systemTable, false))
        return 2;
    if (s.user != s.system && e.BuildLocale(s.user, e.LocaleCodePage(s.userTable), s.userTable, false))
        return 2;
    s.overrides = e.Allocate(s.heap, 8, 0xb44);
    if (!s.overrides)
    {
        s.overrides = e.GlobalAllocate(0, 0xb44);
        if (!s.overrides)
        {
            e.Error(14);
            return 14;
        }
    }
    s.overrideValid = e.InitializeOverrides(s.overrides, 0) ? 1 : 0;
    return 0;
}
// The original flag is set only on success and is not
// cleared here on failure. Callers decide when this startup wrapper may run.
template <class E> U Start(E &e, State &s)
{
    e.BootstrapLock();
    e.InitializeLock();
    e.BootstrapUnlock();
    e.EnterLock();
    const U error = Initialize(e, s);
    e.LeaveLock();
    if (error)
        return 0;
    s.initialized = 1;
    return 1;
}
inline U GetSystem(const State &s) { return s.system; } //
// Return the low WORD value.
inline U GetSystemLang(const State &s) { return s.system & 0xffff; } //
inline U GetThread(const State &s) { return s.user; } //
// GetLocaleInfoA. Resolve special/default LCIDs before
// the separate descriptor lookup. Neutral language IDs select sublanguage1
// and preserve only the original four sort bits.
template <class UserLocale> U ResolveLocale(U requested, U system, UserLocale user)
{
    if (requested == system)
        return requested;
    if (requested == 0x800)
        return system;
    if (!requested || requested == 0x400)
        return user();
    if ((requested & 0xffff) < 0x400)
        return (requested & 0xf0000) | (requested & 0x3ff) | 0x400;
    return requested;
}
inline U Invalidate(State &s) //
{
    s.user = 0;
    return 1;
}
} // namespace Win98SystemLocale
