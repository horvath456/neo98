// SPDX-License-Identifier: GPL-2.0-or-later
// The Win98 registry hive file, "CREG" version 1.0, as VMM reads and writes it.
// Read from VMM32.VXD of the Win98 SE CD (the W3 container of WIN98_21.CAB,
// VMM as an uncompressed LE module; flat addresses of the LE image with object
// N at N*100000h):
// 0xD038F2 opens a hive for RegLoadKey and RegReplaceKey: CREG, version
// 10000h or 20000h, RGKN at 20h, every RGDB block header checked
// (signature, size below 10000h), records tiling each block.
// A wrong signature or version is ERROR_BADDB (1009), a read
// failure ERROR_REGISTRY_IO_FAILED (1016).
// 0xD042E8 the recursive copy of RegSaveKey: values first, then subkeys;
// REG_SZ is written without its terminator; a value whose query
// fails (data over 4000h bytes) is left out; the walk stops
// silently below depth 20h.
// 0x603694 the key hash: the sum of the name bytes, a to z folded to A to
// Z, bytes from 80h up skipped, 16 bits kept.
// 0x6024C0 the RGKN slot rule: contiguous 1Ch-byte slots up to 1FD8h, then
//             146 slots per page of 4 KiB from the page base.
// A hive written here is the same file tools/import_registry.py reads. Only
// version 10000h is understood; a file of version 20000h, which VMM also
// accepts, is refused with ERROR_BADDB.
#pragma once
#include <string.h>
namespace Win98Creg
{
using U = unsigned;
constexpr U BadDatabase = 1009, IoFailed = 1016;
inline U Word(const unsigned char *p) { return p[0] | p[1] << 8; }
inline U Long(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (U)p[3] << 24; }
inline void PutWord(unsigned char *p, U v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); }
inline void PutLong(unsigned char *p, U v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}
inline U Hash(const char *name, U length)
{
    U sum = 0;
    for (U i = 0; i < length; ++i)
    {
        const U c = (unsigned char)name[i];
        if (c >= 0x80) continue;
        sum += c >= 'a' && c <= 'z' ? c - 0x20 : c;
    }
    return sum & 0xffff;
}
// Where a hive's keys and values go while it is read.
struct Sink
{
    virtual ~Sink() {}
    // A key below `parent` (the index a previous call answered, or ~0u for
    // the root of the hive). Answers the index of the new key, or ~0u to
    // stop with an error.
    virtual U Key(U parent, const char *name, U length) = 0;
    virtual bool Value(U key, const char *name, U name_length, U type, const unsigned char *data,
                       U size) = 0;
};
// Reads a whole hive. Zero, BadDatabase or the error a sink call reported.
inline U Read(const unsigned char *data, U size, Sink &sink, U &sink_error)
{
    sink_error = 0;
    if (size < 0x40 || memcmp(data, "CREG", 4) || Long(data + 4) != 0x10000 || memcmp(data + 0x20, "RGKN", 4))
        return BadDatabase;
    const U first = Long(data + 8), blocks = Word(data + 0x10);
    const U rgkn = 0x20, rgkn_size = Long(data + 0x24);
    if (first > size || rgkn + rgkn_size > size) return BadDatabase;
    // The start of every block, in file order; its index is its number.
    U block_start[256] = {}, block_size[256] = {}, count = 0;
    for (U at = first; at + 0x20 <= size && count < blocks; ++count)
    {
        if (count >= 256 || memcmp(data + at, "RGDB", 4)) return BadDatabase;
        const U length = Long(data + at + 4);
        if (length < 0x20 || length >= 0x10000 || at + length > size) return BadDatabase;
        // The records tile the block exactly.
        U record = at + 0x20;
        while (record < at + length)
        {
            if (record + 12 > at + length) return BadDatabase;
            const U total = Long(data + record);
            if (total < 12 || record + total > at + length) return BadDatabase;
            record += total;
        }
        block_start[count] = at;
        block_size[count] = length;
        at += length;
    }
    if (count != blocks) return BadDatabase;
    const auto record_of = [&](U identifier, U &at) -> bool {
        const U block = identifier >> 16, id = identifier & 0xffff;
        if (block >= count) return false;
        const U end = block_start[block] + block_size[block];
        for (U r = block_start[block] + 0x20; r + 20 <= end;)
        {
            const U total = Long(data + r);
            if (Long(data + r + 4) == ((block << 16) | id) && Long(data + r + 4) != ~0u)
            {
                at = r;
                return true;
            }
            r += total;
        }
        return false;
    };
    // The key tree, walked iteratively over its child and sibling links.
    struct Frame { U offset, key; };
    Frame stack[64];
    U depth = 0, visited = 0;
    const U root = Long(data + rgkn + 8);
    // The root: its values, if it has a record.
    {
        if (rgkn + root + 0x1c > size) return BadDatabase;
        const U identifier = Long(data + rgkn + root + 0x18);
        U at = 0;
        if ((identifier & 0xffff) != 0xffff && record_of(identifier, at))
        {
            const U name_length = Word(data + at + 0xc), values = Word(data + at + 0xe);
            U v = at + 0x14 + name_length;
            for (U i = 0; i < values; ++i)
            {
                if (v + 12 > at + Long(data + at + 8)) return BadDatabase;
                const U type = Long(data + v), nl = Word(data + v + 8), dl = Word(data + v + 0xa);
                if (!sink.Value(~0u, (const char *)data + v + 12, nl, type, data + v + 12 + nl, dl))
                    return sink_error = 8;
                v += 12 + nl + dl;
            }
        }
        const U child = Long(data + rgkn + root + 0x10);
        if (child != ~0u) stack[depth++] = {child, ~0u};
    }
    while (depth)
    {
        Frame frame = stack[--depth];
        if (++visited > 1u << 20 || rgkn + frame.offset + 0x1c > size) return BadDatabase;
        const unsigned char *k = data + rgkn + frame.offset;
        const U next = Long(k + 0x14), child = Long(k + 0x10), identifier = Long(k + 0x18);
        if (next != ~0u)
        {
            if (depth == 64) return BadDatabase;
            stack[depth++] = {next, frame.key};
        }
        U at = 0;
        if (!record_of(identifier, at)) return BadDatabase;
        const U name_length = Word(data + at + 0xc), values = Word(data + at + 0xe);
        const U key = sink.Key(frame.key, (const char *)data + at + 0x14, name_length);
        if (key == ~0u) return sink_error = 8;
        U v = at + 0x14 + name_length;
        for (U i = 0; i < values; ++i)
        {
            if (v + 12 > at + Long(data + at + 8)) return BadDatabase;
            const U type = Long(data + v), nl = Word(data + v + 8), dl = Word(data + v + 0xa);
            if (!sink.Value(key, (const char *)data + v + 12, nl, type, data + v + 12 + nl, dl))
                return sink_error = 8;
            v += 12 + nl + dl;
        }
        if (child != ~0u)
        {
            if (depth == 64) return BadDatabase;
            stack[depth++] = {child, key};
        }
    }
    return 0;
}
// A hive under construction: keys in depth-first order, each with its name
// and its value records, the way 0xD042E8 walks the source.
class Writer
{
  public:
    ~Writer()
    {
        for (U i = 0; i < count; ++i) delete[] keys[i].body;
        delete[] keys;
    }
    // A key below `parent` (~0u for the hive root, which must come first).
    // Answers its index.
    U Key(U parent, const char *name, U length)
    {
        if (!Grow()) return ~0u;
        Entry &e = keys[count];
        e = {};
        e.parent = parent;
        e.name_length = length > 255 ? 255 : length;
        memcpy(e.name, name, e.name_length);
        if (parent != ~0u)
        {
            // Link as the last child of the parent.
            if (keys[parent].first_child == ~0u) keys[parent].first_child = count;
            else keys[keys[parent].last_child].next = count;
            keys[parent].last_child = count;
        }
        return count++;
    }
    bool Value(U key, const char *name, U name_length, U type, const void *data, U size)
    {
        Entry &e = keys[key];
        if (name_length > 255 || size > 0xffff) return false;
        const U grow = 12 + name_length + size;
        auto *body = new unsigned char[e.body_size + grow];
        if (e.body_size) memcpy(body, e.body, e.body_size);
        unsigned char *v = body + e.body_size;
        PutLong(v, type);
        PutLong(v + 4, 0);
        PutWord(v + 8, name_length);
        PutWord(v + 10, size);
        memcpy(v + 12, name, name_length);
        if (size) memcpy(v + 12 + name_length, data, size);
        delete[] e.body;
        e.body = body;
        e.body_size += grow;
        ++e.values;
        return true;
    }
    // The whole file. `word16` is the header word at +16h: 1 for a system
    // hive, 80h for one RegSaveKey writes. The caller deletes the bytes.
    unsigned char *Finish(U word16, U &size)
    {
        size = 0;
        if (!count) return nullptr;
        // RGKN slots.
        U offset = 0x20;
        for (U i = 0; i < count; ++i)
        {
            if (offset >= 0x1000 && (offset >> 12) != ((offset + 0x1c - 1) >> 12))
                offset = (offset + 0xfff) & ~0xfffu;
            keys[i].slot = offset;
            offset += 0x1c;
        }
        const U end = offset, rgkn_size = (end + 0xfff) & ~0xfffu;
        // RGDB records into blocks of at most 255 records and F000h bytes.
        U block = 0, in_block = 0, used = 0x20;
        U *record_block = new U[count], *record_id = new U[count];
        for (U i = 0; i < count; ++i)
        {
            record_block[i] = record_id[i] = ~0u;
            if (i == 0 && !keys[0].values) continue;
            const U length = RecordBytes(i);
            if (in_block == 255 || used + length + 0x0c > 0xf000)
            {
                ++block;
                in_block = 0;
                used = 0x20;
            }
            record_block[i] = block;
            record_id[i] = in_block++;
            used += length;
        }
        const U blocks = in_block ? block + 1 : block;
        U *block_bytes = new U[blocks ? blocks : 1], *block_used = new U[blocks ? blocks : 1],
          *block_records = new U[blocks ? blocks : 1];
        for (U b = 0; b < blocks; ++b) block_used[b] = 0x20, block_records[b] = 0;
        for (U i = 0; i < count; ++i)
            if (record_block[i] != ~0u)
            {
                block_used[record_block[i]] += RecordBytes(i);
                ++block_records[record_block[i]];
            }
        U total = 0x20 + rgkn_size;
        for (U b = 0; b < blocks; ++b)
        {
            block_bytes[b] = (block_used[b] + 0x0c + 0xfff) & ~0xfffu;
            total += block_bytes[b];
        }
        auto *out = new unsigned char[total];
        memset(out, 0, total);
        memcpy(out, "CREG", 4);
        PutLong(out + 4, 0x10000);
        PutLong(out + 8, 0x20 + rgkn_size);
        PutWord(out + 0x10, blocks);
        PutWord(out + 0x12, 0x21);
        PutWord(out + 0x16, word16);
        unsigned char *k = out + 0x20;
        memcpy(k, "RGKN", 4);
        PutLong(k + 4, rgkn_size);
        PutLong(k + 8, keys[0].slot);
        PutLong(k + 0xc, rgkn_size - end >= 0x1c ? end : ~0u);
        PutLong(k + 0x10, 8);
        if (rgkn_size - end >= 0x1c)
        {
            PutLong(k + end, 0x80000000u);
            PutLong(k + end + 4, rgkn_size - end);
            PutLong(k + end + 8, ~0u);
        }
        for (U i = 0; i < count; ++i)
        {
            const Entry &e = keys[i];
            unsigned char *r = k + e.slot;
            PutLong(r, 0);
            PutLong(r + 4, e.parent == ~0u ? 0 : Hash(e.name, e.name_length));
            PutLong(r + 8, ~0u);
            PutLong(r + 0xc, e.parent == ~0u ? ~0u : keys[e.parent].slot);
            PutLong(r + 0x10, e.first_child == ~0u ? ~0u : keys[e.first_child].slot);
            PutLong(r + 0x14, e.next == ~0u ? ~0u : keys[e.next].slot);
            PutLong(r + 0x18, record_block[i] == ~0u ? ~0u : record_block[i] << 16 | record_id[i]);
        }
        U at = 0x20 + rgkn_size;
        for (U b = 0; b < blocks; ++b)
        {
            unsigned char *d = out + at;
            U cursor = 0x20;
            for (U i = 0; i < count; ++i)
            {
                if (record_block[i] != b) continue;
                const Entry &e = keys[i];
                const U length = RecordBytes(i);
                const U name_length = i == 0 ? 1 : e.name_length;
                unsigned char *r = d + cursor;
                PutLong(r, length);
                PutLong(r + 4, b << 16 | record_id[i]);
                PutLong(r + 8, length);
                PutWord(r + 0xc, name_length);
                PutWord(r + 0xe, e.values);
                PutLong(r + 0x10, 0);
                if (i == 0) r[0x14] = 0;
                else memcpy(r + 0x14, e.name, name_length);
                if (e.body_size) memcpy(r + 0x14 + name_length, e.body, e.body_size);
                cursor += length;
            }
            const U free_bytes = block_bytes[b] - cursor;
            PutLong(d + cursor, free_bytes);
            PutLong(d + cursor + 4, ~0u);
            PutLong(d + cursor + 8, ~0u);
            memcpy(d, "RGDB", 4);
            PutLong(d + 4, block_bytes[b]);
            PutLong(d + 8, free_bytes);
            PutWord(d + 0xc, 8);
            PutWord(d + 0xe, b);
            PutLong(d + 0x10, cursor);
            PutWord(d + 0x14, block_records[b] ? block_records[b] - 1 : 0);
            PutWord(d + 0x16, block_records[b] < 255 ? block_records[b] : 0xff);
            at += block_bytes[b];
        }
        delete[] record_block;
        delete[] record_id;
        delete[] block_bytes;
        delete[] block_used;
        delete[] block_records;
        size = total;
        return out;
    }

  private:
    struct Entry
    {
        U parent = ~0u, first_child = ~0u, last_child = ~0u, next = ~0u, slot = 0;
        U name_length = 0, values = 0, body_size = 0;
        char name[256] = {};
        unsigned char *body = nullptr;
    };
    Entry *keys = nullptr;
    U count = 0, capacity = 0;
    bool Grow()
    {
        if (count < capacity) return true;
        const U next = capacity ? capacity * 2 : 64;
        auto *grown = new Entry[next];
        for (U i = 0; i < count; ++i) grown[i] = keys[i];
        delete[] keys;
        keys = grown;
        capacity = next;
        return true;
    }
    U RecordBytes(U i) const { return 0x14 + (i == 0 ? 1 : keys[i].name_length) + keys[i].body_size; }
};
} // namespace Win98Creg
