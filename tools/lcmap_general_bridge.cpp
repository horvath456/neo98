// SPDX-License-Identifier: GPL-2.0-or-later
// Native side of tools/test_lcmap_general_original.py: the general
// LCMapStringA route of src/win98_lcmap_general.h over the bytes of
// CP_1252.NLS, with an optional synthetic table standing in for one NLS
// header entry, on a flat buffer that mirrors guest 120000h.
#include "../src/win98_lcmap_general.h"
#include "../src/win98_character_case.h"
#include <cstring>
using U = unsigned;
struct Bridge {
    const unsigned char *table;       // CP_1252.NLS
    unsigned char *memory;            // guest 120000h..
    U memory_size;
    U synthetic_index;                // NLS header entry replaced, or 0
    const unsigned char *synthetic;   // its words
    U error = 0, scratch_used = 0;
    U Half(const unsigned char *p, U at) { return U(p[at]) | U(p[at + 1]) << 8; }
    U Entry(U index) { return (Half(table, index * 4) | Half(table, index * 4 + 2) << 16) * 2; }
    // Case route environment (win98_character_case.h).
    U SystemLocale() { return 0x409; }
    U UserLocale() { return 0x409; }
    U SystemTable() { return 1; }
    U LookupLocale(U locale) { return locale == 0x409 ? 1 : 0; }
    U Length(U p) { return strlen(reinterpret_cast<char *>(memory + (p - 0x120000))); }
    U Read8(U p) { return memory[p - 0x120000]; }
    void Write8(U p, U v) { memory[p - 0x120000] = v; }
    bool Multibyte(U) { return false; }
    bool Lead(U, U) { return false; }
    U LocaleNumber(U) { return 0x409; }
    U CaseByte(U, bool lower, U c) { return table[Entry(lower ? 7 : 6) + c]; }
    U CaseWord(U, bool, U) { return 0; }
    void Error(U v) { error = v; }
    // General route environment (win98_lcmap_general.h).
    bool HasTable(U, U index) { return index == synthetic_index ? synthetic != nullptr : Entry(index) != 0; }
    U TableWord(U, U index, U entry) {
        if (index == synthetic_index) return synthetic ? Half(synthetic, entry * 2) : 0;
        return Half(table, Entry(index) + entry * 2);
    }
    U CodePage(U) { return 1252; }
    void Unported(const char *) { error = 0xdead; }
    U Scratch(U bytes) { scratch_used = bytes; return 0x120000 + memory_size - 0x1000; }
    void FreeScratch(U) {}
    void Case(bool lower, U from, U bytes, U to, U room) { Win98CharacterCase::KernelCase(*this, 0x409, lower, from, bytes, to, room); }
};
extern "C" U lcmap_general(const unsigned char *table, unsigned char *memory, U memory_size, U synthetic_index,
                           const unsigned char *synthetic, const U *a, U *error)
{
    Bridge e{table, memory, memory_size, synthetic_index, synthetic, *error};
    const U count = int(a[3]) < 0 ? e.Length(a[2]) + 1 : a[3];
    const U result = Win98LcMapGeneral::Map(e, 1, a[1], a[2], count, a[4], a[5]);
    *error = e.error;
    return result;
}
