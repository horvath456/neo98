// SPDX-License-Identifier: GPL-2.0-or-later
// Original KERNEL32 environment block, search, mutation and expansion bodies.
// Native memory/locks replace address-space and x86 exception providers.
#pragma once
namespace Win98Environment
{
using U = unsigned;
// Read8/Write8 and Read32/Write32 use flat native-provider tokens.
template<class E> U Length(E &e, U text)
{
    U count = 0; while (e.Read8(text + count)) ++count; return count;
}
// and helpers retain forward DWORD copying for strings.
template<class E> void CopyBytes(E &e, U to, U from, U bytes)
{
    if (to > from && from + bytes >= to)
    {
        U offset = bytes;
        while (offset >= 4) { offset -= 4; e.Write32(to + offset, e.Read32(from + offset)); }
        while (offset) { --offset; e.Write8(to + offset, e.Read8(from + offset)); }
    }
    else
    {
        U offset = 0;
        while (bytes - offset >= 4) { e.Write32(to + offset, e.Read32(from + offset)); offset += 4; }
        while (offset < bytes) { e.Write8(to + offset, e.Read8(from + offset)); ++offset; }
    }
}
template<class E> void CopyString(E &e, U to, U from)
{
    const U bytes = Length(e, from) + 1; U offset = 0;
    while (bytes - offset >= 4) { e.Write32(to + offset, e.Read32(from + offset)); offset += 4; }
    while (offset < bytes) { e.Write8(to + offset, e.Read8(from + offset)); ++offset; }
}
template<class E> void AppendString(E &e, U to, U from) { CopyString(e, to + Length(e, to), from); }
// Names use ASCII folding, independent of the system code page.
template<class E> U Upper(E &e, U text)
{
    U n = 0, c;
    while ((c = e.Read8(text + n)))
    { if (c >= 'a' && c <= 'z') e.Write8(text + n, c & 0xdf); ++n; }
    return n;
}
// Includes the second terminating NUL, even for an empty block.
template<class E> U BlockLength(E &e, U block, U maximum)
{
    U p = block, bytes;
    do
    {
        while (e.Read8(p)) ++p;
        bytes = ++p - block;
    } while (e.Read8(p) && maximum > bytes);
    return maximum < bytes ? 0 : bytes + 1;
}
// PCHE allocation header validation.
template<class E> U ValidBlock(E &e, U block)
{
    const U p = block - 0x20;
    if ((p & 0xfff) || e.Read32(p) != 0x45484350) return 0;
    const U owner = e.Read32(p + 0xc);
    if (owner && owner != e.CurrentProcess()) return 0;
    const U reserved = e.Read32(p + 4), committed = e.Read32(p + 8);
    if (!reserved || reserved > 0x3fffffff || (reserved & 0xfff) ||
        !committed || reserved < committed || (committed & 0xfff) ||
        e.Read8(p + 0x10) != 0x4d ||
        (e.Read8(p + 0x11) != 0xff && e.Read8(p + 0x12) != 0xff)) return 0;
    return ((e.Read8(p + 0x14) * 256 + e.Read8(p + 0x13) + 2) << 4) == committed;
}
// A foreign block has the original unbounded sentinel capacity.
template<class E> U Capacity(E &e, U block)
{
    return ValidBlock(e, block) ? e.Read32(block - 0x18) - 0x20 : ~0u;
}
// Grow committed pages in the existing reservation, never move.
template<class E> U Grow(E &e, U block, U bytes)
{
    if (!ValidBlock(e, block)) return 0;
    const U p = block - 0x20, requested = bytes + 0x20;
    if (!requested || e.Read32(p + 4) < requested) return 0;
    const U committed = (requested | 0xfff) + 1;
    if (e.Read32(p + 8) >= committed) return 1;
    if (e.Read32(p + 4) < committed || !e.VirtualAllocate(p, committed, 0x1000, 4)) return 0;
    e.Write32(p + 8, committed);
    const U usable = committed - 0x20;
    e.Write8(p + 0x13, usable >> 4); e.Write8(p + 0x14, usable >> 12);
    return 1;
}
// Initial PCHE allocation and its complete commit-failure rollback.
template<class E> U CreateBlock(E &e, U source, U shared)
{
    const U bytes = BlockLength(e, source, ~0u);
    if (!bytes || bytes > 0x3fffffff) return 0;
    const U committed = ((bytes + 0x20) | 0xfff) + 1;
    const U reserved = committed > 0x4000 ? committed : 0x4000;
    const U base = e.VirtualAllocate(0, reserved, shared ? 0x08002000 : 0x2000, 4);
    if (!base) return 0;
    if (!e.VirtualAllocate(base, committed, 0x1000, 4))
    { e.VirtualRelease(base, 0, 0x8000); return 0; }
    e.Write32(base, 0x45484350); e.Write32(base + 4, reserved); e.Write32(base + 8, committed);
    e.Write32(base + 0xc, shared ? 0 : e.CurrentProcess());
    e.Write8(base + 0x10, 0x4d); e.Write8(base + 0x11, 0xff); e.Write8(base + 0x12, 0xff);
    const U usable = committed - 0x20;
    e.Write8(base + 0x13, usable >> 4); e.Write8(base + 0x14, usable >> 12);
    CopyBytes(e, base + 0x20, source, bytes);
    return base + 0x20;
}
// A leading '=' is allowed for the per-drive directory variables.
template<class E> U Match(E &e, U entry, U name)
{
    if (e.Read8(entry) == '=')
    { if (e.Read8(name) != '=') return 0; ++entry; ++name; }
    for (;;)
    {
        U a = e.Read8(entry++), b = e.Read8(name++);
        if (a == '=') return b ? 0 : entry;
        if (!b) return 0;
        if (a >= 'a' && a <= 'z') a &= 0xdf;
        if (b >= 'a' && b <= 'z') b &= 0xdf;
        if (a != b) return 0;
    }
}
// Success updates name to the matched full entry.
template<class E> U Find(E &e, U &name, U &value, U block)
{
    const U capacity = Capacity(e, block); U p = block;
    for (;;)
    {
        value = Match(e, p, name);
        if (value) { name = p; return 1; }
        while (e.Read8(p)) ++p;
        ++p;
        if (!e.Read8(p) || block + capacity <= p) return 0;
    }
}
// Get borrows the same mutable block.
template<class E> U GetStrings(E &e) { return e.ProcessBlock(); }
template<class E> U FreeStrings(E &e, U block)
{
    if (block != e.ProcessBlock()) { e.Error(87); return 0; } return 1;
}
// Short output is untouched; missing name sets 203.
template<class E> U Get(E &e, U name, U output, U capacity)
{
    e.Lock(); U value = 0, result = 0;
    if (!Find(e, name, value, e.ProcessBlock())) e.Error(203);
    else
    {
        result = Length(e, value);
        if (result >= capacity) ++result;
        else CopyString(e, output, value);
    }
    e.Unlock(); return result;
}
// Empty or null values delete; replacements append at the end.
template<class E> U SetBlock(E &e, U name, U value, U block)
{
    const U capacity = Capacity(e, block); U used = BlockLength(e, block, capacity), needed = 0;
    if (value && e.Read8(value)) needed = Length(e, value) + Length(e, name) + 2;
    U entry = name, old_value = 0;
    if (Find(e, entry, old_value, block))
    {
        const U old_bytes = Length(e, entry) + 1;
        if (capacity < needed - old_bytes + used && !Grow(e, block, needed - old_bytes + used))
        { e.Error(8); return 0; }
        CopyBytes(e, entry, entry + old_bytes, used - (entry + old_bytes) + block);
        used -= old_bytes;
    }
    if (needed)
    {
        if (capacity < used + needed && !Grow(e, block, used + needed)) { e.Error(8); return 0; }
        const U p = block + used - 1;
        CopyString(e, p, name); Upper(e, p);
        const U end = p + Length(e, p); e.Write8(end, '='); e.Write8(end + 1, 0);
        AppendString(e, p, value); e.Write8(p + needed, 0);
    }
    return 1;
}
// Block lookup deliberately precedes lock acquisition.
template<class E> U Set(E &e, U name, U value)
{
    const U block = e.ProcessBlock(); e.Lock(); const U result = SetBlock(e, name, value, block); e.Unlock(); return result;
}
// 100 bytes of local name storage, then explicit heap fallback.
template<class E> U ExpandName(E &e, U source, U output, U capacity)
{
    U bytes = 0; while (e.Read8(source + bytes) != '%') ++bytes;
    const bool heap = bytes >= 100;
    const U name = heap ? e.AllocateTemporary(bytes + 1) : e.LocalName();
    if (!name) { e.Error(8); return ~0u; }
    CopyBytes(e, name, source, bytes); e.Write8(name + bytes, 0);
    const U result = Get(e, name, output, capacity);
    if (heap) e.FreeTemporary(name);
    return result;
}
// Preserve unknown names, unmatched '%' and original short-buffer counts.
template<class E> U Expand(E &e, U source, U output, U capacity)
{
    U p = source, next = output, total = 0;
    while (e.Read8(p))
    {
        U fragment = p, bytes;
        if (e.Read8(p) != '%')
        { do { ++p; } while (e.Read8(p) && e.Read8(p) != '%'); bytes = p - fragment; }
        else
        {
            do { ++p; } while (e.Read8(p) && e.Read8(p) != '%');
            if (e.Read8(p) != '%') bytes = p - fragment;
            else
            {
                ++p;
                const U remaining = capacity + output - next;
                bytes = ExpandName(e, fragment + 1, next, remaining);
                if (bytes == ~0u) return 0;
                if (!bytes) bytes = p - fragment;
                else { if (bytes > remaining) --bytes; fragment = next; }
            }
        }
        total += bytes;
        if (capacity > total)
        { if (fragment != next) CopyBytes(e, next, fragment, bytes); next += bytes; }
    }
    ++total;
    if (capacity >= total) e.Write8(next, 0);
    return total;
}
// Internal replacement keeps the process block address stable.
template<class E> U ReplaceBlock(E &e, U source)
{
    const U bytes = BlockLength(e, source, ~0u);
    if (!bytes) return 0;
    const U target = e.ProcessBlock();
    if (!Grow(e, target, bytes)) return 0;
    CopyBytes(e, target, source, bytes); return 1;
}
} // namespace Win98Environment
