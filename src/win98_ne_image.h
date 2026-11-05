// SPDX-License-Identifier: GPL-2.0-or-later
// Reader for the New Executable format, the image format of every 16-bit
// Windows program and 16-bit DLL of the Win98 tree. Field offsets and the
// meaning of the flag bits are the ones KRNL386.EXE reads in its loader
// (segment 2, the LoadModule path and the segment loader it calls);
// the same fields drive tools/gui_inventory.py, which is the Python side of
// this reader.
//
// The reader owns nothing and allocates nothing. It answers questions about a
// byte range and refuses every field that leaves it, so a damaged file ends in
// an error code and never in a read past the buffer.
#pragma once
namespace Win98Ne
{
// The loader error codes of KRNL386. LoadModule and LoadLibrary answer with
// them below 32, and the Win16 loader of NEO98 keeps the same numbers.
enum Error
{
    Ok = 0,
    FileNotFound = 2,      // the file is not there
    OutOfMemory = 8,       // not enough memory to load the module
    BadFormat = 11,        // not an NE image, or a field outside the file
    MultipleInstance = 16, // a second instance of a module with multiple data
    BadLibrary = 20,       // the module is a library where a program is needed,
                           // or a program where a library is needed
};
// Segment table flags, as the segment loader tests them.
enum SegmentFlag
{
    SegmentData = 0x0001,
    SegmentMoveable = 0x0010,
    SegmentPreload = 0x0040,
    SegmentReadOnly = 0x0080,
    SegmentRelocations = 0x0100,
    SegmentDiscardable = 0x1000,
    Segment32Bit = 0x2000,
};
// Program flags (header byte 0x0c) and application flags (byte 0x0d).
enum ProgramFlag
{
    SingleData = 0x01,
    MultipleData = 0x02,
    ProtectedOnly = 0x08,
    Has8086 = 0x10,
};
enum ApplicationFlag
{
    ImageError = 0x20,
    Library = 0x80,
};
// Relocation record kinds and target classes.
enum RelocationKind
{
    RelocationLowByte = 0,
    RelocationSelector = 2,
    RelocationFarAddress = 3,
    RelocationOffset = 5,
};
enum RelocationTarget
{
    TargetInternal = 0,
    TargetImportOrdinal = 1,
    TargetImportName = 2,
    TargetOsFixup = 3,
    TargetAdditive = 4,
};
struct Segment
{
    unsigned number = 0;      // one based, as every reference spells it
    unsigned file_offset = 0; // already shifted by the file alignment
    unsigned file_bytes = 0;
    unsigned allocation = 0; // bytes to reserve, 65536 when the field is zero
    unsigned flags = 0;
    bool Code() const { return !(flags & SegmentData); }
    bool Use32() const { return (flags & Segment32Bit) != 0; }
    bool HasRelocations() const { return (flags & SegmentRelocations) != 0; }
};
struct Relocation
{
    unsigned kind = 0, target_flags = 0, source = 0, target = 0, value = 0;
    unsigned Class() const { return target_flags & 3; }
    bool Additive() const { return (target_flags & TargetAdditive) != 0; }
};
struct Entry
{
    unsigned segment = 0, offset = 0, flags = 0;
    bool moveable = false;
};
class Image
{
  public:
    // Accepts the byte range of one file. `error` carries the KRNL386 code.
    bool Parse(const void *bytes, unsigned length, unsigned &error)
    {
        data = static_cast<const unsigned char *>(bytes);
        size = length;
        base = 0;
        error = BadFormat;
        if (!data || size < 64 || data[0] != 'M' || data[1] != 'Z')
            return false;
        const unsigned at = U32(0x3c);
        if (!at || at + 64 > size)
            return false;
        if (data[at] != 'N' || data[at + 1] != 'E')
            return false;
        base = at;
        // Every table the loader walks has to sit inside the file.
        if (SegmentTable() + Segments() * 8u > size || EntryTable() > size ||
            ModuleTable() + Modules() * 2u > size || ImportedNames() > size)
        {
            base = 0;
            return false;
        }
        error = Ok;
        return true;
    }
    bool Valid() const { return base != 0; }
    unsigned Base() const { return base; }
    unsigned Bytes() const { return size; }
    const unsigned char *Data() const { return data; }
    unsigned ProgramFlags() const { return U8(base + 0x0c); }
    unsigned ApplicationFlags() const { return U8(base + 0x0d); }
    bool IsLibrary() const { return (ApplicationFlags() & Library) != 0; }
    // Self-loading modules bring their own loader in segment 1 and expect to
    // be called back for every segment. KRNL386 tests this bit before it
    // touches a segment; NEO98 refuses those modules instead.
    bool SelfLoading() const { return (U16(base + 0x0c) & 0x0800) != 0; }
    unsigned AutoDataSegment() const { return U16(base + 0x0e); }
    unsigned HeapSize() const { return U16(base + 0x10); }
    unsigned StackSize() const { return U16(base + 0x12); }
    unsigned EntrySegment() const { return U16(base + 0x16); }
    unsigned EntryOffset() const { return U16(base + 0x14); }
    unsigned StackSegment() const { return U16(base + 0x1a); }
    unsigned StackPointer() const { return U16(base + 0x18); }
    unsigned Segments() const { return U16(base + 0x1c); }
    unsigned Modules() const { return U16(base + 0x1e); }
    unsigned Alignment() const
    {
        const unsigned shift = U16(base + 0x32);
        return shift ? shift : 9;
    }
    unsigned ExpectedVersion() const { return U16(base + 0x3e); }
    unsigned Resources() const { return base + U16(base + 0x24); }
    unsigned SegmentTable() const { return base + U16(base + 0x22); }
    unsigned EntryTable() const { return base + U16(base + 0x04); }
    unsigned ResidentNames() const { return base + U16(base + 0x26); }
    unsigned ModuleTable() const { return base + U16(base + 0x28); }
    unsigned ImportedNames() const { return base + U16(base + 0x2a); }
    bool SegmentAt(unsigned number, Segment &out) const
    {
        if (!number || number > Segments())
            return false;
        const unsigned at = SegmentTable() + (number - 1) * 8;
        const unsigned sector = U16(at), length = U16(at + 2);
        out.number = number;
        out.file_offset = sector << Alignment();
        out.file_bytes = sector ? (length ? length : 65536) : 0;
        out.flags = U16(at + 4);
        out.allocation = U16(at + 6);
        if (!out.allocation)
            out.allocation = 65536;
        if (out.file_bytes > out.allocation)
            out.allocation = out.file_bytes;
        return sector == 0 || out.file_offset + out.file_bytes <= size;
    }
    // Relocation records follow the segment bytes: a count word and then
    // eight bytes each.
    unsigned RelocationCount(const Segment &segment) const
    {
        if (!segment.HasRelocations() || !segment.file_bytes)
            return 0;
        const unsigned at = segment.file_offset + segment.file_bytes;
        if (at + 2 > size)
            return 0;
        const unsigned count = U16(at);
        return at + 2 + count * 8u <= size ? count : 0;
    }
    bool RelocationAt(const Segment &segment, unsigned index, Relocation &out) const
    {
        if (index >= RelocationCount(segment))
            return false;
        const unsigned at = segment.file_offset + segment.file_bytes + 2 + index * 8;
        out.kind = U8(at);
        out.target_flags = U8(at + 1);
        out.source = U16(at + 2);
        out.target = U16(at + 4);
        out.value = U16(at + 6);
        return true;
    }
    // The entry table is a chain of bundles. An ordinal is a position in that
    // chain, counted from one, and a bundle with segment zero only skips
    // ordinals. Segment 255 marks a moveable entry, whose record carries the
    // segment number behind the INT 3Fh instruction the linker wrote.
    bool EntryAt(unsigned ordinal, Entry &out) const
    {
        unsigned at = EntryTable(), number = 1;
        while (at + 2 <= size && U8(at))
        {
            const unsigned count = U8(at), segment = U8(at + 1);
            at += 2;
            for (unsigned i = 0; i < count; ++i, ++number)
            {
                if (!segment)
                    continue;
                const unsigned record = segment == 255 ? 6u : 3u;
                if (at + record > size)
                    return false;
                if (number == ordinal)
                {
                    out.flags = U8(at);
                    out.moveable = segment == 255;
                    out.segment = out.moveable ? U8(at + 3) : segment;
                    out.offset = out.moveable ? U16(at + 4) : U16(at + 1);
                    return out.segment != 0;
                }
                at += record;
            }
            if (!segment)
                continue;
        }
        return false;
    }
    // Highest ordinal the entry table names, so a caller can bound a search.
    unsigned EntryCount() const
    {
        unsigned at = EntryTable(), number = 0;
        while (at + 2 <= size && U8(at))
        {
            const unsigned count = U8(at), segment = U8(at + 1);
            at += 2;
            number += count;
            if (segment)
                at += count * (segment == 255 ? 6u : 3u);
        }
        return number;
    }
    // A counted string of one of the name tables, upper case as the file
    // spells it. Returns the number of bytes written without the terminator.
    unsigned CountedName(unsigned at, char *out, unsigned capacity) const
    {
        if (!capacity)
            return 0;
        out[0] = 0;
        if (at >= size)
            return 0;
        unsigned length = U8(at);
        if (at + 1 + length > size)
            return 0;
        if (length > capacity - 1)
            length = capacity - 1;
        for (unsigned i = 0; i < length; ++i)
            out[i] = static_cast<char>(data[at + 1 + i]);
        out[length] = 0;
        return length;
    }
    // Module reference `index`, counted from one, is an offset into the
    // imported names table.
    unsigned ModuleName(unsigned index, char *out, unsigned capacity) const
    {
        if (!index || index > Modules())
        {
            if (capacity)
                out[0] = 0;
            return 0;
        }
        return CountedName(ImportedNames() + U16(ModuleTable() + (index - 1) * 2), out, capacity);
    }
    unsigned ImportedName(unsigned offset, char *out, unsigned capacity) const
    {
        return CountedName(ImportedNames() + offset, out, capacity);
    }
    // The first resident name is the module name itself; the rest are exports
    // with their ordinal behind the string.
    unsigned ModuleOwnName(char *out, unsigned capacity) const
    {
        return CountedName(ResidentNames(), out, capacity);
    }
    // Walks the resident name table for an export name. `index` counts from
    // one, so zero stays the module name.
    bool ResidentExport(unsigned index, char *out, unsigned capacity, unsigned &ordinal) const
    {
        unsigned at = ResidentNames();
        for (unsigned i = 0;; ++i)
        {
            if (at >= size || !U8(at))
                return false;
            const unsigned length = U8(at);
            if (i == index)
            {
                CountedName(at, out, capacity);
                ordinal = U16(at + 1 + length);
                return true;
            }
            at += 1 + length + 2;
        }
    }
    // The non-resident name table lies outside the header at a file offset of
    // its own. KRNL386 searches it after the resident one, so an export that
    // is not in memory all the time is found just the same.
    unsigned NonResidentNames() const { return U32(base + 0x2c); }
    unsigned NonResidentBytes() const { return U16(base + 0x20); }
    bool NonResidentExport(unsigned index, char *out, unsigned capacity, unsigned &ordinal) const
    {
        const unsigned start = NonResidentNames(), end = start + NonResidentBytes();
        if (!start || end > size)
            return false;
        unsigned at = start;
        for (unsigned i = 0;; ++i)
        {
            if (at >= end || !U8(at))
                return false;
            const unsigned length = U8(at);
            if (i == index)
            {
                CountedName(at, out, capacity);
                ordinal = U16(at + 1 + length);
                return true;
            }
            at += 1 + length + 2;
        }
    }
    // ---------------------------------------------------------- resources
    //
    // The resource table of an NE file is a shift count followed by a chain of
    // type blocks. A type block names its type, the number of resources of
    // that type and then one twelve-byte record each: offset and length in
    // units of the shift, the flags, the name and two reserved dwords. A type
    // or name word with bit 15 set is an integer, the rest is an offset from
    // the start of the table to a counted string. KRNL386 walks exactly this
    // chain in FindResource and hands the two shifted words to the
    // segment loader.
    unsigned ResourceShift() const
    {
        const unsigned table = Resources();
        return table > base && table + 2 <= size ? U16(table) : 0u;
    }
    bool HasResources() const { return Resources() > base && Resources() + 2 <= size; }
    // Finds one resource. `type` and `name` are integers with bit 15 set, or
    // zero with `type_label` or `label` carrying the counted-string key. The comparison of a
    // string name ignores case, the way KRNL386 compares it.
    bool ResourceAt(unsigned type, unsigned name, const char *label, unsigned &offset,
                    unsigned &bytes, const char *type_label = nullptr) const
    {
        if (!HasResources())
            return false;
        const unsigned table = Resources(), shift = U16(table);
        if (shift > 16)
            return false;
        auto find = [&](unsigned wanted_type, unsigned wanted_name, const char *wanted_label,
                        unsigned &found_offset, unsigned &found_bytes) {
            unsigned at = table + 2;
            while (at + 8 <= size)
            {
                const unsigned kind = U16(at), count = U16(at + 2);
                if (!kind) return false;
                at += 8;
                if (at + count * 12u > size) return false;
                for (unsigned i = 0; i < count; ++i)
                {
                    const unsigned record = at + i * 12;
                    if (ResourceNameMatches(table, kind, wanted_type, wanted_type ? nullptr : type_label) &&
                        ResourceNameMatches(table, U16(record + 6), wanted_name, wanted_label))
                    {
                        found_offset = U16(record) << shift;
                        found_bytes = U16(record + 2) << shift;
                        if (!found_offset || found_offset >= size)
                            return false;
                        // Resource lengths are rounded in units of 1 <<
                        // shift, so the last entry may extend beyond EOF
                        // without file padding. Clamp its length to available
                        // bytes rather than rejecting the resource, matching
                        // Windows 98.
                        if (found_bytes > size - found_offset)
                            found_bytes = size - found_offset;
                        return true;
                    }
                }
                at += count * 12;
            }
            return false;
        };
        if (find(type, name, label, offset, bytes)) return true;
        // Windows 3.0 RT_NAMETABLE maps names to the ordinal type/name pairs
        // stored in the ordinary resource table. Each entry is a byte count,
        // type ordinal, name ordinal, ASCIZ type name and ASCIZ resource name.
        // Windows 3.1 no longer emits the table, but its loader retains this
        // compatibility path for 3.0-format modules.
        if (!label || !*label) return false;
        unsigned names = 0, name_bytes = 0;
        if (!find(0x800f, 0x8001, nullptr, names, name_bytes)) return false;
        for (unsigned at = names; at + 7 <= names + name_bytes;)
        {
            const unsigned length = U16(at);
            if (!length) break;
            if (length < 8 || at + length > names + name_bytes) return false;
            unsigned resource_name = at + 6;
            while (resource_name < at + length && U8(resource_name)) ++resource_name;
            if (resource_name == at + length) return false;
            ++resource_name;
            unsigned i = 0;
            for (; label[i] && resource_name + i < at + length && U8(resource_name + i); ++i)
            {
                unsigned a = U8(resource_name + i), b = (unsigned char)label[i];
                if (a >= 'a' && a <= 'z') a -= 32;
                if (b >= 'a' && b <= 'z') b -= 32;
                if (a != b) break;
            }
            if (!label[i] && resource_name + i < at + length && !U8(resource_name + i) &&
                (U16(at + 2) & 0x7fffu) == (type & 0x7fffu))
                return find(type, U16(at + 4), nullptr, offset, bytes);
            at += length;
        }
        return false;
    }
    // The name word of one resource record, for a caller that enumerates.
    bool ResourceNameMatches(unsigned table, unsigned word, unsigned name, const char *label) const
    {
        if (word & 0x8000)
            return name == word;
        if (!label || !*label)
            return false;
        char text[256];
        if (!CountedName(table + word, text, sizeof text))
            return false;
        const char *a = text, *b = label;
        while (*a || *b)
        {
            unsigned x = (unsigned char)*a++, y = (unsigned char)*b++;
            if (x >= 'a' && x <= 'z')
                x -= 32;
            if (y >= 'a' && y <= 'z')
                y -= 32;
            if (x != y)
                return false;
        }
        return true;
    }
    unsigned U8(unsigned at) const { return at < size ? data[at] : 0u; }
    unsigned U16(unsigned at) const { return at + 1 < size ? data[at] | data[at + 1] << 8 : 0u; }
    unsigned U32(unsigned at) const
    {
        return at + 3 < size ? data[at] | data[at + 1] << 8 | data[at + 2] << 16 | data[at + 3] << 24 : 0u;
    }

  private:
    const unsigned char *data = nullptr;
    unsigned size = 0, base = 0;
};
// True for a file that begins like an NE image. Used by the process loader to
// decide which of the two loaders owns a file, before anything is allocated.
inline bool IsImage(const void *bytes, unsigned length)
{
    const auto *data = static_cast<const unsigned char *>(bytes);
    if (!data || length < 64 || data[0] != 'M' || data[1] != 'Z')
        return false;
    const unsigned at = data[0x3c] | data[0x3d] << 8 | data[0x3e] << 16 | data[0x3f] << 24;
    return at && at + 2 <= length && data[at] == 'N' && data[at + 1] == 'E';
}
} // namespace Win98Ne
