// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef NEO98_RUNTIME_CONTENT
#include "windows_runtime_data.h"
#else
#include "../build/generated/atom_upper.h"
#endif
#include "win98_atoms.h"
#include "win98_global_atoms.h"
#include "win98_local_heap.h"
#include "x86_app.h"
#include <string.h>
namespace X86
{
// The original KRNL386 atom table and local heap run together as native code.
// Their WORD offsets address private storage, never x86 guest addresses.
struct Application::AtomTable final : Win98Atoms::Environment
{
    unsigned char data[65536] = {};
    bool failed = false;
    struct Heap final : Win98LocalHeap::Environment
    {
        AtomTable &table;
        explicit Heap(AtomTable &owner) : Environment(owner.data), table(owner) {}
        // Atom nodes are fixed, non-discardable allocations. This arena has
        // no compaction/growth provider; exhaustion is reported to the caller.
        unsigned Compact(unsigned, unsigned &block) override
        {
            block = 0;
            return 0;
        }
        unsigned Notify(unsigned, unsigned, unsigned) override { return 0; }
        void Error(unsigned) override { table.failed = true; }
    } heap;
    AtomTable() : Environment(data, OriginalAtomUpper), heap(*this)
    {
        failed = !Win98LocalHeap::Initialize(heap, 0x1000, 0xfff0);
    }
    unsigned Allocate(unsigned flags, unsigned size) override
    {
        if (flags != 0x40 || !size || size > 65535)
        {
            failed = true;
            return 0;
        }
        return Win98LocalHeap::Allocate(heap, flags, size);
    }
    bool Valid(unsigned p) const override
    {
        if (p < 0x1004 || p >= 0xfff0 || (p & 3) || p == Word(6) || p == Word(8))
            return false;
        // Validate membership in the actual physical block chain. An interior
        // word that resembles a heap header must not become an atom handle.
        const unsigned control = Word(6), tail = Word(control + 10);
        unsigned block = Word(Word(control + 6) + 2);
        for (unsigned steps = 0; block < tail && steps < 16384; ++steps)
        {
            const unsigned next = Word(block + 2);
            if (next <= block || next > tail)
                return false;
            if (block + 4 == p)
                return (Word(block) & 3) == 1 && next - block >= 12 && Byte(p + 4) &&
                       Byte(p + 4) + 10 <= next - block;
            if (block + 4 > p)
                return false;
            block = next;
        }
        return false;
    }
    void Free(unsigned p) override
    {
        if (Valid(p))
            Win98LocalHeap::Free(heap, p);
    }
};
Application::AtomTable &Application::SystemAtoms(bool registration)
{
    // Original USER selects distinct segment heaps for Global* and registration.
    // Native tables retain their references across process teardown. Each uses
    // a fixed 64 KiB provider arena; original movable-segment growth is unported.
    static AtomTable *global = nullptr, *registered = nullptr;
    auto *&table = registration ? registered : global;
    if (!table)
    {
        table = new AtomTable;
        if (!table->failed)
            Win98Atoms::Initialize(*table, 37);
    }
    return *table;
}
unsigned Application::GlobalAtomSearch(unsigned source, bool add)
{
    if (source > 0xffff && !virtual_memory.Access(source, 1, false))
    {
        last_error = 87;
        return 0;
    }
    auto &table = SystemAtoms(false);
    table.failed = false;
    const auto *text = source <= 0xffff ? nullptr : (const unsigned char *)ReadString(source);
    const unsigned result = Win98GlobalAtoms::Search(&table, source, text, add);
    if (table.failed)
        last_error = 8;
    return result;
}
// Text-side helpers for native callers of the global table. DDEML's string
// handles are these atoms (win98_dde.h); only the guest-pointer entries above
// carry the original's pointer validation.
unsigned Application::GlobalAtomText(const char *text)
{
    auto &table = SystemAtoms(false);
    table.failed = false;
    const unsigned result = Win98GlobalAtoms::Search(&table, 0x10000, (const unsigned char *)text, true);
    if (table.failed)
        last_error = 8;
    return result;
}
unsigned Application::GlobalAtomName(unsigned atom, char *out, unsigned capacity)
{
    unsigned char buffer[256] = {};
    const unsigned length = Win98GlobalAtoms::Name(&SystemAtoms(false), atom, buffer, sizeof buffer - 1);
    unsigned copied = 0;
    while (copied + 1 < capacity && copied < length)
    {
        out[copied] = char(buffer[copied]);
        ++copied;
    }
    if (capacity)
        out[copied] = 0;
    return length;
}
unsigned Application::GlobalAtomDelete(unsigned atom)
{
    return Win98GlobalAtoms::Delete(&SystemAtoms(false), atom);
}
unsigned Application::GlobalAtomApi(ApiName name, const unsigned *a)
{
    if (name[strlen(name) - 1] == 'W')
    {
        struct Errors
        {
            Application &app;
            void Error(unsigned code) { app.last_error = code; }
        } e{*this};
        return Win98GlobalAtoms::UnsupportedWide(e);
    }
    if (API_IS(name,"GlobalAddAtomA") || API_IS(name,"GlobalFindAtomA"))
        return GlobalAtomSearch(a[0], API_IS(name,"GlobalAddAtomA"));
    if (API_IS(name,"GlobalDeleteAtom"))
        return GlobalAtomDelete(a[0]);
    // K32's public output probe precedes the WORD-level USER/KERNEL dispatch.
    if (a[2] > 0xffff || !virtual_memory.Access(a[1], a[2] ? a[2] : 1, true))
    {
        last_error = 87;
        return 0;
    }
    if (!a[2])
        return 0;
    unsigned char output[256]{};
    const unsigned result = Win98GlobalAtoms::Name(&SystemAtoms(false), a[0], output, a[2]);
    Store(a[1], output, result + 1);
    return result;
}
unsigned Application::RegisterAtom(unsigned source)
{
    auto &table = SystemAtoms(true);
    table.failed = false;
    const auto *text = source <= 0xffff ? nullptr : (const unsigned char *)ReadString(source);
    const unsigned result = Win98GlobalAtoms::Register(&table, source, text);
    if (table.failed)
        last_error = 8;
    return result;
}
unsigned Application::RegisterMessage(const char *text)
{
    auto &table = SystemAtoms(true);
    table.failed = false;
    const unsigned result = Win98GlobalAtoms::Register(&table, 0x10000, (const unsigned char *)text);
    if (table.failed)
        last_error = 8;
    return result;
}
unsigned Application::ClipboardAtom(unsigned format, unsigned operation, unsigned char *out, unsigned capacity)
{
    auto &table = SystemAtoms(true);
    if (operation == 2) return Win98Atoms::Delete(table, format);
    if (operation == 1) {
        const unsigned node = (format * 4) & 65535;
        if (format >= 0xc000 && table.Valid(node))
            Win98Atoms::Search(table, table.bytes + node + 5, 1);
        return 0;
    }
    if (format < 0xbfff) { if (capacity) *out = 0; return 0; }
    return Win98Atoms::Name(table, format, out, capacity);
}
void Application::FreeAtoms()
{
    delete atoms;
    atoms = nullptr;
}
unsigned Application::AtomApi(ApiName name, const unsigned *a)
{
    const bool add = API_IS(name,"AddAtomA"), find = API_IS(name,"FindAtomA");
    if ((add || find) && a[0] < 65536)
        return Win98Atoms::Integer(a[0]);
    if (!atoms)
        atoms = new AtomTable;
    auto &table = *atoms;
    table.failed = false;
    unsigned result = 0;
    if (API_IS(name,"InitAtomTable"))
    {
        const unsigned buckets = a[0] & 65535;
        // Reject wrapped allocation sizes when creating a new native table.
        if (!table.Word(8) && buckets > 32766)
        {
            last_error = 8;
            return 0;
        }
        result = Win98Atoms::Initialize(table, buckets);
        result = result & 32768 ? result | 0xffff0000 : result;
    }
    else if (add || find)
        result = Win98Atoms::Search(table, (const unsigned char *)ReadString(a[0]), add ? 1 : 0);
    else if (API_IS(name,"DeleteAtom"))
        result = Win98Atoms::Delete(table, a[0]);
    else
    {
        unsigned capacity = a[2] & 65535;
        if (!capacity)
            return 0;
        unsigned char output[256] = {};
        result = Win98Atoms::Name(table, a[0], output, capacity);
        Store(a[1], output, result + 1);
    }
    if (table.failed)
        last_error = 8;
    return result;
}
} // namespace X86
