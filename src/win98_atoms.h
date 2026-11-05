// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98Atoms
{
// KRNL386. Storage and allocation are supplied services;
// offsets, hash arithmetic, references and atom IDs retain their WORD width.
struct Environment
{
    unsigned char *bytes;
    const unsigned char *upper;
    Environment(unsigned char *memory, const unsigned char *mapping) : bytes(memory), upper(mapping) {}
    virtual unsigned Allocate(unsigned flags, unsigned size) = 0;
    virtual void Free(unsigned pointer) = 0;
    virtual bool Valid(unsigned pointer) const = 0;
    unsigned Byte(unsigned p) const { return bytes[p & 65535]; }
    unsigned Word(unsigned p) const { return Byte(p) | (Byte(p + 1) << 8); }
    void Byte(unsigned p, unsigned n) { bytes[p & 65535] = n; }
    void Word(unsigned p, unsigned n)
    {
        Byte(p, n);
        Byte(p + 1, n >> 8);
    }
    unsigned Upper(unsigned c) const { return c >= 128 ? upper[c - 128] : c >= 'a' && c <= 'z' ? c - 32 : c; }
};
inline unsigned Integer(unsigned value)
{
    value &= 65535;
    return value && value < 0xc000 ? value : 0;
}
inline unsigned Atom(unsigned pointer) { return pointer ? 0xc000 | (pointer >> 2) : 0; }
inline unsigned Initialize(Environment &e, unsigned buckets)
{
    unsigned table = e.Word(8);
    if (!table)
    {
        buckets &= 65535;
        if (!buckets)
            buckets = 37;
        table = e.Allocate(0x40, ((buckets + 1) * 2) & 65535);
        if (table)
        {
            e.Word(8, table);
            e.Word(table, buckets);
        }
    }
    return table;
}
inline unsigned Search(Environment &e, const unsigned char *name, unsigned operation)
{
    // a decimal name may wrap repeatedly before range validation.
    if (*name == '#')
    {
        unsigned value = 0;
        const auto *p = name + 1;
        for (; *p && *p >= '0' && *p <= '9'; ++p)
            value = (value * 10 + *p - '0') & 65535;
        if (!*p)
            return Integer(value);
    }
    if (!Initialize(e, 0))
        return 0;
    unsigned length = 0, hash = 0;
    for (const auto *p = name; *p; ++p)
    {
        if (++length == 256)
            return 0;
        const unsigned left = ((hash << 1) | (hash >> 15)) & 65535;
        const unsigned right = (hash >> 1) | ((hash & 1) << 15);
        hash = (left + right + hash + e.Upper(*p)) & 65535;
    }
    if (!length)
        return 0;
    const unsigned table = e.Word(8), buckets = e.Word(table);
    // Native boundary: reject corrupt/overflowed tables rather than dividing by zero.
    if (!buckets)
        return 0;
    unsigned link = table + 2 + (hash % buckets) * 2, node = 0;
    for (unsigned steps = 0; steps < 16384; ++steps)
    {
        node = e.Word(link);
        if (!node)
            break;
        bool equal = e.Byte(node + 4) == length;
        for (unsigned i = 0; equal && i < length; ++i)
            equal = e.Upper(e.Byte(node + 5 + i)) == e.Upper(name[i]);
        if (equal)
            break;
        link = node;
        if (steps == 16383)
            return 0;
    }
    if (!operation)
        return Atom(node);
    if (operation == 2)
    {
        if (!node)
            return 0; // Public native Delete validates its allocation first.
        const unsigned references = e.Word(node + 2);
        if (references != 65535)
        {
            e.Word(node + 2, references - 1);
            if (references == 1)
            {
                e.Word(link, e.Word(node));
                e.Word(node, 0);
                e.Free(node);
            }
        }
        return 0;
    }
    if (node)
    {
        const unsigned references = e.Word(node + 2);
        if (references != 65535)
            e.Word(node + 2, references + 1);
    }
    else
    {
        node = e.Allocate(0x40, length + 6);
        if (!node)
            return 0;
        e.Word(link, node);
        e.Word(node + 2, 1);
        e.Byte(node + 4, length);
        for (unsigned i = 0; i <= length; ++i)
            e.Byte(node + 5 + i, name[i]);
    }
    return Atom(node);
}
inline unsigned Delete(Environment &e, unsigned atom)
{
    atom &= 65535;
    if (atom < 0xc000)
        return 0;
    const unsigned node = (atom * 4) & 65535;
    if (!e.Valid(node))
        return atom;
    return Search(e, e.bytes + node + 5, 2) ? atom : 0;
}
inline unsigned Name(Environment &e, unsigned atom, unsigned char *out, unsigned capacity)
{
    atom &= 65535;
    capacity &= 65535;
    if (!capacity)
        return 0;
    *out = 0;
    // Sizes whose signed WORD is negative are outside the safe native contract.
    if (capacity & 32768)
        return 0;
    if (atom < 0xc000)
    {
        if (!atom || capacity < 2)
            return 0;
        out[0] = '#';
        unsigned char digits[5];
        unsigned count = 0;
        for (; atom && count < capacity - 2; atom /= 10)
            digits[count++] = '0' + atom % 10;
        for (unsigned i = 0; i < count; ++i)
            out[i + 1] = digits[count - i - 1];
        out[count + 1] = 0;
        return count + 1;
    }
    const unsigned node = (atom * 4) & 65535;
    if (!e.Valid(node) || !e.Word(node + 2))
        return 0;
    unsigned length = e.Byte(node + 4);
    if (!length)
        return 0;
    if (length >= capacity)
        length = capacity - 1;
    for (unsigned i = 0; i < length; ++i)
        out[i] = e.Byte(node + 5 + i);
    out[length] = 0;
    return length;
}
} // namespace Win98Atoms
