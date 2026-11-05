// SPDX-License-Identifier: GPL-2.0-or-later
// CreateAcceleratorTableA, CopyAcceleratorTableA and DestroyAcceleratorTable.
// The rules are in win98_accelerators.h; this file holds the table store the
// three exports share with TranslateAcceleratorA.
#include "x86_app.h"
#include "win98_accelerators.h"
namespace X86 {
using Win98Accelerators::Entry;
namespace {
inline unsigned short Word(const unsigned char *p)
{
    return (unsigned short)(unsigned(p[0]) | unsigned(p[1]) << 8);
}
// asks GlobalAlloc, so a created table lives in the global heap of the
// system and outlives neither more nor less than the process that frees it.
// NEO98 keeps the same one store for the whole runtime.
constexpr unsigned Slots = 64;
// A created handle has to stay apart from the handles LoadAcceleratorsA
// returns, which use the module's resource handle. Windows 98
// has one global handle space for both; NEO98 marks the created ones.
constexpr unsigned CreatedTag = 0xacc00000;
struct Slot
{
    Entry *entries = nullptr;
    unsigned count = 0;
};
Slot *Tables()
{
    static Slot slots[Slots];
    return slots;
}
} // namespace

unsigned Application::AcceleratorResource(unsigned handle, unsigned &size)
{
    for (const auto &resource : resource_handles)
        if (resource.id == handle && resource.accelerator) {
            size = resource.size;
            return resource.address;
        }
    return 0;
}

bool Application::DestroyCreatedAcceleratorTable(unsigned handle)
{
    if ((handle & 0xfff00000u) != CreatedTag) return false;
    const unsigned index = handle & 0xfffffu;
    if (!index || index > Slots || !Tables()[index - 1].entries) return false;
    delete[] Tables()[index - 1].entries;
    Tables()[index - 1] = {};
    return true;
}

// The entries of a created table, for TranslateAcceleratorA.
bool Application::AcceleratorTableEntries(unsigned handle, const Entry *&entries, unsigned &count)
{
    if ((handle & 0xfff00000u) != CreatedTag) return false;
    const unsigned index = handle & 0xfffffu;
    if (!index || index > Slots) return false;
    Slot &slot = Tables()[index - 1];
    if (!slot.entries) return false;
    entries = slot.entries;
    count = slot.count;
    return true;
}

unsigned Application::AcceleratorApi(ApiName name, const unsigned *a)
{
    switch (name.id)
    {
    // Convert the caller's six-byte ACCEL records into the five-byte
    // form, allocate count*5+2 bytes, then set bit 80h in the last fVirt.
    // Windows faults on a null pointer during the write probe.
    // A count of zero reaches an allocation of two bytes and a
    // five byte underflow write; both answer with no table here.
    API_CASE("CreateAcceleratorTableA")
    {
        const int requested = int(a[1]);
        if (!a[0] || requested <= 0 || unsigned(requested) > Win98Accelerators::MaxEntries)
            return 0;
        const unsigned count = unsigned(requested);
        unsigned index = 0;
        while (index < Slots && Tables()[index].entries) ++index;
        if (index == Slots) return 0;
        Entry *entries = new Entry[count];
        if (!entries) return 0;
        for (unsigned i = 0; i < count; ++i)
        {
            unsigned short record[3] = {};
            if (!Read(a[0] + i * 6, record, sizeof record))
            {
                delete[] entries;
                return 0;
            }
            entries[i] = {(unsigned char)(record[0] & 255), record[1], record[2]};
        }
        // marks the last entry; the entries before it keep what the
        // caller wrote, bit 80h included.
        entries[count - 1].virt |= (unsigned char)Win98Accelerators::FlagLast;
        Tables()[index] = {entries, count};
        return CreatedTag | (index + 1);
    }
    // locks the table, counts its entries, and with a null buffer or a
    // count that is not positive answers with that number alone. Otherwise it
    // copies the smaller of the two counts and, when the whole table fits,
    // clears the terminator bit in the copy.
    API_CASE("CopyAcceleratorTableA")
    {
        const Entry *entries = nullptr;
        unsigned available = 0;
        unsigned resource_length = 0;
        const unsigned char *resource = nullptr;
        const unsigned address = AcceleratorResource(a[0], resource_length);
        if (!AcceleratorTableEntries(a[0], entries, available))
        {
            if (!address) resource = image.Resource(9, a[0], &resource_length);
            if (!address && !resource) return 0;
            available = resource_length / 8;
        }
        const int requested = int(a[2]);
        if (!a[1] || requested <= 0) return available;
        const unsigned count =
            unsigned(requested) < available ? unsigned(requested) : available;
        for (unsigned i = 0; i < count; ++i)
        {
            unsigned short record[3] = {};
            if (entries)
                record[0] = entries[i].virt, record[1] = entries[i].key,
                record[2] = entries[i].command;
            else if (address) {
                unsigned short wire[4]{};
                if (!Read(address + i * 8, wire, sizeof wire)) return 0;
                record[0] = wire[0], record[1] = wire[1], record[2] = wire[2];
            } else
                record[0] = Word(resource + i * 8), record[1] = Word(resource + i * 8 + 2),
                record[2] = Word(resource + i * 8 + 4);
            // The public ACCEL never exposes the private terminator. Keep
            // all explicitly created records even when callers supplied bit
            // 80h early: native Win98 truncates there and its Win32 thunk can
            // then copy uninitialized temporary records. We retain the safe
            // full-table behavior and normalize each public flag byte.
            record[0] &= (unsigned short)~unsigned(Win98Accelerators::FlagLast);
            if (!Store(a[1] + i * 6, record, sizeof record)) return 0;
        }
        return count;
    }
    // is FreeResource on the handle. A created table is an ordinary
    // block, so the first call frees it and answers true and a second one
    // answers false. A table that came from LoadAcceleratorsA keeps its
    // resource use count, and NEO98 never lets that count reach zero.
    API_CASE("DestroyAcceleratorTable")
    {
        unsigned bytes=0;
        if(AcceleratorResource(a[0],bytes))return true;
        return DestroyCreatedAcceleratorTable(a[0]);
    }
    default:
        break;
    }
    Fail(name);
    return 0;
}
} // namespace X86
