// SPDX-License-Identifier: GPL-2.0-or-later
// Original Win98 USER property arrays.
#pragma once
namespace Win98Properties
{
using U = unsigned;
struct State
{
    U enumerated_list = 0, enumerated_item = 0; // USER DGROUP 0382h/0384h.
    U system_atoms[3]{};                      // USER DGROUP 0386h/0388h/038Ah.
};
// Providers: PropertyList(window), SetPropertyList(window, list);
// Read16/Read32/Write16/Write32; Allocate(flags,bytes), Reallocate(list,bytes,flags),
// Free(list), Move(destination,source,bytes); FindAtom(name), AddAtom(name),
// DeleteAtom(atom), SystemCleanup(kind,value). Allocator addresses may be native
// tokens; the original array stores a WORD count, then 8-byte atom/flags/data rows.
// Window validation, pointer probes, synchronization and capacity limits belong
// to the native provider. Valid original USER arrays fit in its 64-KiB local heap.

// USER. A missing list avoids the atom lookup entirely.
template <class E> U Get(E &e, State &, U window, U name, bool string)
{
    const U list = e.PropertyList(window);
    if (!list) return 0;
    const U atom = string ? e.FindAtom(name) : name & 0xffffu;
    if (!atom) return 0;
    const U count = e.Read16(list);
    for (U i = 0, row = list + 2; i < count; ++i, row += 8)
        if (e.Read16(row) == atom) return e.Read32(row + 4);
    return 0;
}

// USER. Public USER32 SetPropA supplies flags=2; bit 0 marks a
// USER-internal property, and bit 1 rejects a failed/zero name atom.
template <class E> U Set(E &e, State &, U window, U name, bool string, U value, U flags = 2)
{
    U atom = string ? e.FindAtom(name) : name & 0xffffu;
    const U old_list = e.PropertyList(window);
    if (old_list && atom)
        for (U i = 0, count = e.Read16(old_list), row = old_list + 2; i < count; ++i, row += 8)
            if (e.Read16(row) == atom)
            {
                e.Write32(row + 4, value);
                return 1;
            }
    if (string) atom = e.AddAtom(name);
    if (!atom && (flags & 2)) return 0;
    // The original does not undo AddAtom if allocation/reallocation fails.
    const U list = old_list ? e.Reallocate(old_list, (e.Read16(old_list) * 8 + 10) & 0xffffu, 2)
                            : e.Allocate(0x40, 10);
    if (!list) return 0;
    e.SetPropertyList(window, list);
    const U count = (e.Read16(list) + 1) & 0xffffu;
    e.Write16(list, count);
    const U row = list + count * 8 - 6;
    e.Write16(row, atom);
    e.Write16(row + 2, (e.Read16(row + 2) & ~3u) | (flags & 1) | (string ? 2u : 0u));
    e.Write32(row + 4, value);
    return 1;
}

// USER. Compact the array and adjust an active EnumProps cursor.
// Shrinking Reallocate must retain the address, as in the original local heap.
template <class E> U Remove(E &e, State &s, U window, U name, bool string)
{
    const U list = e.PropertyList(window);
    if (!list) return 0;
    const U atom = string ? e.FindAtom(name) : name & 0xffffu;
    if (!atom) return 0;
    const bool enumerating = s.enumerated_list == list;
    const U count = e.Read16(list);
    for (U i = 0, row = list + 2; i < count; ++i, row += 8)
        if (e.Read16(row) == atom)
        {
            const U value = e.Read32(row + 4);
            if (enumerating && s.enumerated_item >= row) s.enumerated_item -= 8;
            e.Write16(list, count - 1);
            if (count == 1)
            {
                e.Free(list);
                e.SetPropertyList(window, 0);
            }
            else
            {
                e.Move(row, row + 8, (count - i - 1) * 8);
                if (!enumerating) e.Reallocate(list, (count - 1) * 8 + 2, 2);
            }
            // This decision follows the caller's name form, not the row flags.
            if (string) e.DeleteAtom(atom);
            return value;
        }
    return 0;
}

// USER. Window destruction releases stored string-atom references
// and dispatches the three private USER property kinds before freeing the array.
template <class E> void Destroy(E &e, State &s, U window)
{
    const U list = e.PropertyList(window);
    if (!list) return;
    const U count = e.Read16(list);
    for (U i = 0, row = list + 2; i < count; ++i, row += 8)
    {
        const U atom = e.Read16(row), flags = e.Read16(row + 2);
        if (flags & 1)
        {
            const U low = e.Read16(row + 4);
            if (atom == s.system_atoms[0]) e.SystemCleanup(0, low);       // KERNEL.7 LocalFree.
            else if (atom == s.system_atoms[1]) e.SystemCleanup(1, low);  // USER.
            else if (atom == s.system_atoms[2])
            {
                if (e.Read16(row + 6)) e.SystemCleanup(2, low);          // USER(low,8).
                else if (low) e.SystemCleanup(3, low);                   // USER(low).
            }
        }
        else if (atom >= 0xc000 && (flags & 2)) e.DeleteAtom(atom);
    }
    e.Free(list);
    e.SetPropertyList(window, 0);
}
} // namespace Win98Properties
