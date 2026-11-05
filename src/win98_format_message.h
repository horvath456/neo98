// SPDX-License-Identifier: GPL-2.0-or-later
// FormatMessageA selects text from a string, module resource, or system
// resource. Validate the output pointer, unwrap supported HRESULTs, and retry
// system lookup when the module lacks a message. Honor IGNORE_INSERTS and
// ARGUMENT_ARRAY. ALLOCATE_BUFFER uses LocalAlloc and clears the caller's
// pointer first; otherwise enforce capacity. Return the length excluding NUL.
// Resource bytes are imported separately; this path supports widths zero and
// 0xff.
#pragma once

namespace Win98FormatMessage
{
enum Flag : unsigned
{
    WidthMask = 0x000000ff,
    AllocateBuffer = 0x00000100,
    IgnoreInserts = 0x00000200,
    FromString = 0x00000400,
    FromModule = 0x00000800,
    FromSystem = 0x00001000,
    ArgumentArray = 0x00002000,
};
enum Status : unsigned
{
    InvalidParameter = 87,    // 0x57
    InsufficientBuffer = 122, // 0x7a
    OutOfMemory = 8,
    NotEnoughResource = 31,   // 0x1f, the general failure of the status map
    MessageNotFound = 317,    // 0x13d
};
enum Source
{
    None,
    String,
    Module,
    System,
};

// the low byte is the maximum line width and the value 0xff means
// no output line breaks at all.
inline unsigned Width(unsigned flags)
{
    const unsigned width = flags & WidthMask;
    return width == WidthMask ? 0xffffffffu : width;
}

// a Win32 error wrapped as an HRESULT is unwrapped, but only when
// FROM_SYSTEM is the single source bit in the 0x1c00 group.
inline unsigned Unwrap(unsigned flags, unsigned identifier)
{
    if ((identifier & 0xffff0000u) == 0x80070000u && (flags & 0x1c00u) == FromSystem)
        return identifier & 0xffffu;
    return identifier;
}

// the source bits are tested in this order and no source at all
// is a parameter error.
inline Source Select(unsigned flags)
{
    if (flags & FromString)
        return String;
    if (flags & FromModule)
        return Module;
    if (flags & FromSystem)
        return System;
    return None;
}

// inside the formatting core: the seventh parameter is read
// differently for the two insert modes. With FORMAT_MESSAGE_ARGUMENT_ARRAY it
// points at the values themselves and the original walks it with its own
// cursor. Without the flag it points at the va_list: the original loads
// *Arguments, advances that by four, writes it back through the pointer and
// takes the value from the slot it just left. The arguments are therefore one
// dereference further away.
inline bool IndirectArguments(unsigned flags)
{
    return !(flags & ArgumentArray);
}

// a missing message with both module and system requested falls
// back to the system table once.
inline bool Retry(unsigned flags)
{
    return (flags & (FromModule | FromSystem)) == (FromModule | FromSystem);
}

// The original message table layout: a block count, then blocks of low, high
// and the offset of the first entry, then variable length entries of a length
// word, a flags word and the text. Flags bit 0 marks a Unicode entry.
struct Entry
{
    const unsigned char *text = nullptr;
    unsigned bytes = 0;
    bool wide = false;
};

inline unsigned Read32(const unsigned char *data, unsigned offset)
{
    return unsigned(data[offset]) | unsigned(data[offset + 1]) << 8 |
           unsigned(data[offset + 2]) << 16 | unsigned(data[offset + 3]) << 24;
}
inline unsigned Read16(const unsigned char *data, unsigned offset)
{
    return unsigned(data[offset]) | unsigned(data[offset + 1]) << 8;
}

inline bool Lookup(const unsigned char *table, unsigned size, unsigned identifier, Entry &entry)
{
    if (size < 4)
        return false;
    const unsigned blocks = Read32(table, 0);
    if (blocks > (size - 4) / 12)
        return false;
    for (unsigned index = 0; index < blocks; ++index)
    {
        const unsigned base = 4 + index * 12;
        const unsigned low = Read32(table, base), high = Read32(table, base + 4);
        unsigned offset = Read32(table, base + 8);
        if (identifier < low || identifier > high)
            continue;
        for (unsigned current = low; current <= high; ++current)
        {
            if (offset + 4 > size)
                return false;
            const unsigned length = Read16(table, offset), flags = Read16(table, offset + 2);
            if (length < 4 || offset + length > size)
                return false;
            if (current == identifier)
            {
                entry.text = table + offset + 4;
                entry.bytes = length - 4;
                entry.wide = (flags & 1) != 0;
                return true;
            }
            offset += length;
        }
        return false;
    }
    return false;
}
} // namespace Win98FormatMessage
