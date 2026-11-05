// SPDX-License-Identifier: GPL-2.0-or-later
// Provider for the 16-bit INET16.DLL routine behind the Win16 module catalogue
// (Route::Native16 in src/dll_routes.h). INET16 is the 16-bit half of
// ICFG95.DLL, the component check of the Internet Connection Wizard; ICFG95
// connects WIZTHK_THUNKDATA16 in its DLL_PROCESS_ATTACH and calls
// through it when the wizard asks which network pieces are installed.
// Index 8 of the target table is, an internal routine, so its
// catalogue ordinal is 9 (index byte plus one). The
// 32-bit stub in ICFG95 turns twelve dwords into twelve words, passes
// them as one far pointer and widens them back afterwards; retf 4.
// zeroes the twelve words and fills them from the registry
// through SETUPX and KERNEL's registry calls:
// DiGetClassDevs("NetTrans"); +0 = 1 when a device's ID is MSTCP
// (Windows 98 compares with lstrcmpi).
// DiGetClassDevs("Net"); for each adapter Windows 98 reads CompatibleIDs
// of its hardware key: "*PNP8387" is the Dial-Up Adapter (+6), any
// other is a network card (+2). opens Bindings of the same
// key and looks for a value that starts with "MSTCP\" (_fstrnicmp,
// 6 bytes): TCP/IP bound to it, +8 for dial-up, +4 for the card.
// RegCreateKey(HKLM, Setup\OptionalComponents), then Windows 98 reads
// Installed of MAPI (+0Ah), RNA (+0Ch), MSNetwork (+0Eh),
// MSNetwork105 (+10h) and InternetMail (+12h); "1" is installed.
// The answer is zero, or the SETUPX error of a failed DiGetClassDevs. The
// devices SETUPX enumerates are the Enum devnodes of the class, which this
// port keeps in the imported registry (tools/network_setup.py adds the card).
#include "x86_app.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
namespace
{
// ASCII case folding, as lstrcmpi and _fstrnicmp compare the names here.
bool SameText(const char *a, const char *b, unsigned limit = ~0u)
{
    for (unsigned i = 0; i < limit; ++i)
    {
        char x = a[i], y = b[i];
        if (x >= 'a' && x <= 'z') x -= 32;
        if (y >= 'a' && y <= 'z') y -= 32;
        if (x != y) return false;
        if (!x) return true;
    }
    return true;
}
} // namespace
unsigned Application::Inet16Words(unsigned ordinal)
{
    return ordinal == 9 ? 2 : ~0u;
}

unsigned Application::Inet16(unsigned ordinal, unsigned stack)
{
    if (ordinal != 9) return 0;
    // ICFG95 reaches this through FT_Thunk with pointer mask 1: the original
    // FT_Thunk maps the flat pointer to a 16:16 one for the 16-bit side, the
    // native dispatch hands the flat pointer over as it is. A caller that
    // pushes a SEGPTR itself is served too.
    const unsigned raw = Read32(stack);
    unsigned block = virtual_memory.Access(raw, 0x18, true) ? raw : selectors.Linear(raw);
    if (!block || !virtual_memory.Access(block, 0x18, true)) return 0;
    unsigned short words[12] = {};
    auto &store = registry.store;
    unsigned machine = 0;
    if (registry.Key(0x80000002u, machine)) return 0;
    auto equal = [](const char *a, const char *b) { return SameText(a, b); };
    auto value = [&](unsigned key, const char *name) -> const char * {
        const Registry::Value *v = nullptr;
        if (store.Query(key, name, v) || !v->data || !v->size) return nullptr;
        return reinterpret_cast<const char *>(v->data);
    };
    // Every devnode of HKLM\Enum: Enum\<enumerator>\<device>\<instance>.
    auto devices = [&](auto visit) {
        unsigned enum_key = 0;
        bool created = false;
        if (store.Open(machine, "Enum", false, enum_key, created)) return;
        unsigned a = 0, b = 0, c = 0;
        for (unsigned i = 0; !store.EnumKey(enum_key, i, a); ++i)
            for (unsigned j = 0; !store.EnumKey(a, j, b); ++j)
                for (unsigned k = 0; !store.EnumKey(b, k, c); ++k)
                    visit(b, c);
    };
    devices([&](unsigned device, unsigned instance) {
        const char *klass = value(instance, "Class");
        if (!klass) return;
        if (equal(klass, "NetTrans"))
        {
            if (equal(store.nodes[device].name, "MSTCP")) words[0] = 1;
            return;
        }
        if (!equal(klass, "Net")) return;
        const char *compatible = value(instance, "CompatibleIDs");
        const bool dialup = compatible && !strcmp(compatible, "*PNP8387");   // lstrcmp, USER 430
        words[dialup ? 3 : 1] = 1;
        unsigned bindings = 0;
        bool created = false;
        if (store.Open(instance, "Bindings", false, bindings, created)) return;
        const Registry::Value *binding = nullptr;
        for (unsigned i = 0; !store.EnumValue(bindings, i, binding); ++i)
            if (binding->name && SameText(binding->name, "MSTCP\\", 6))
            {
                words[dialup ? 4 : 2] = 1;
                break;
            }
    });
    unsigned components = 0;
    bool created = false;
    if (!store.Open(machine, "Software\\Microsoft\\Windows\\CurrentVersion\\Setup\\OptionalComponents", false,
                    components, created))
    {
        static const struct { const char *name; unsigned word; } optional[] = {
            {"MAPI", 5}, {"InternetMail", 9}, {"RNA", 6}, {"MSNetwork", 7}, {"MSNetwork105", 8}};
        for (const auto &item : optional)
        {
            unsigned key = 0;
            if (store.Open(components, item.name, false, key, created)) continue;
            const char *installed = value(key, "Installed");
            if (installed && equal(installed, "1")) words[item.word] = 1;
        }
    }
    Store(block, words, sizeof words);
    if (log)
    {
        char text[160];
        snprintf(text, sizeof text,
                 "INET16 components tcpip=%u lan=%u lan_tcpip=%u dialup=%u dialup_tcpip=%u mapi=%u rna=%u",
                 words[0], words[1], words[2], words[3], words[4], words[5], words[6]);
        log(text);
    }
    return 0;
}
} // namespace X86
