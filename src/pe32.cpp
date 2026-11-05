// SPDX-License-Identifier: GPL-2.0-or-later
#include <stdlib.h>
#include "pe32.h"
#include "win98_exports.h"
namespace Pe32 {
static unsigned U16(const unsigned char *p) { return p[0] | unsigned(p[1]) << 8; }
static U32 U32At(const unsigned char *p) { return U16(p) | U32(U16(p + 2)) << 16; }
static bool Fits(Size offset, Size size, Size limit) { return offset <= limit && size <= limit - offset; }
static U32 RoundPage(U32 size) { return (size + 4095) & ~4095u; }
// KERNEL32 compares a resource name without regard to case: the search helper
// folds every
// character below 0x587 through the upcase table of the NLS data before it
// subtracts. The port folds the Latin-1 range, like Upper in
// win98_version_info.h, because no wider table is implemented yet.
static unsigned FoldName(unsigned c)
{
    if (c >= 'a' && c <= 'z') return c - 32;
    if ((c >= 0xe0 && c <= 0xf6) || (c >= 0xf8 && c <= 0xfe)) return c - 32;
    return c;
}

bool Image::Parse(const void *file, Size size)
{
    bytes = static_cast<const unsigned char *>(file); length = size; count = 0;
    error = "Invalid DOS header";
    if (!bytes || size < 64 || U16(bytes) != 0x5a4d) return false;
    const U32 nt = U32At(bytes + 60);
    error = "Invalid PE header";
    if (!Fits(nt, 24, size) || U32At(bytes + nt) != 0x4550) return false;
    // Two kinds of image: PE32 for the i386 (the Win98 programs) and PE32+
    // for ARM64 (docs/ARM64-APPS.md). PE32+ has an image base of 64 bits and
    // its data directories 16 bytes further down; sections, exports and
    // resources are the same format in both.
    machine = U16(bytes + nt + 4);
    if (machine != 0x14c && machine != 0xaa64) return false;
    wide = machine == 0xaa64;
    const unsigned n = U16(bytes + nt + 6), optional_size = U16(bytes + nt + 20);
    const Size optional = Size(nt) + 24, table = optional + optional_size;
    const unsigned fixed = wide ? 112 : 96;
    if ((wide && !n) || n > 96 || optional_size < fixed || !Fits(optional, optional_size, size) ||
        !Fits(table, n * 40, size) || U16(bytes + optional) != (wide ? 0x20bu : 0x10bu)) return false;
    characteristics = U16(bytes + nt + 22);
    subsystem = U16(bytes + optional + 68);
    subsystem_version = (U32(U16(bytes + optional + 48)) << 16) | U16(bytes + optional + 50);
    // SizeOfStackReserve and SizeOfStackCommit; PE32+ holds them as 64-bit
    // fields, of which an address space of 4 GiB uses the low half.
    stack_reserve = U32At(bytes + optional + 72);
    stack_commit = U32At(bytes + optional + (wide ? 80 : 76));
    preferred_high = wide ? U32At(bytes + optional + 28) : 0;
    preferred = base = U32At(bytes + optional + (wide ? 24 : 28)); image_size = U32At(bytes + optional + 56);
    header_size = U32At(bytes + optional + 60); entry = U32At(bytes + optional + 16);
    // Win98 rounds SizeOfImage to pages, including DLLs with no sections.
    // The original header bytes keep the declared size (loader_layout probe).
    if (!wide && image_size <= 0xfffff000u) image_size = RoundPage(image_size);
    error = "Unsupported PE address layout";
    // An image that prefers a base above 4 GiB is never loaded there.
    if (preferred_high) base = 0;
    // KERNEL32 accepts SectionAlignment >= 4096 for i386.
    // RVAs already carry the larger alignment; mapping and protection still
    // operate on 4-KiB guest pages, not SectionAlignment-sized ranges.
    if ((base & 65535) || !image_size ||
        (image_size & 4095) || (unsigned long long)base + image_size > 0x100000000ull ||
        (wide ? U32At(bytes + optional + 32) != 4096 : U32At(bytes + optional + 32) < 4096) || !header_size ||
        header_size > image_size || (wide && header_size > length) || entry >= image_size ||
        table + n * 40 > RoundPage(header_size) || table + n * 40 > length)
        return false;
    exports = export_size = 0;
    imports = import_size = resources = resource_size = 0;
    const Size directory = optional + fixed;
    const U32 directories = U32At(bytes + directory - 4);
    if (directories) {
        if (optional_size < fixed + 8) return false;
        exports = U32At(bytes + directory); export_size = U32At(bytes + directory + 4);
        if (!Fits(exports, export_size, image_size)) return false;
    }
    if (directories > (optional_size - fixed) / 8) return false;
    if (directories > 1) {
        imports = U32At(bytes + directory + 8); import_size = U32At(bytes + directory + 12);
        if (!Fits(imports, import_size, image_size)) return false;
    }
    if (directories > 2) {
        resources = U32At(bytes + directory + 16); resource_size = U32At(bytes + directory + 20);
        if (!Fits(resources, resource_size, image_size)) return false;
    }
    relocations = relocation_size = tls = tls_size = 0;
    if (directories > 5) {
        relocations = U32At(bytes + directory + 40); relocation_size = U32At(bytes + directory + 44);
        if (!Fits(relocations, relocation_size, image_size)) return false;
    }
    if (directories > 9) {
        tls = U32At(bytes + directory + 72);
        tls_size = U32At(bytes + directory + 76);
        if (!Fits(tls, tls_size, image_size)) return false;
    }
    error = "Invalid PE section";
    for (unsigned i = 0; i < n; ++i) {
        const auto *s = bytes + table + i * 40;
        Section section = {U32At(s + 12), U32At(s + 8), U32At(s + 20), U32At(s + 16), U32At(s + 36), {}};
        for (unsigned c = 0; c < 8; ++c) section.name[c] = s[c] ? (char)s[c] : 0;
        // PointerToRawData 0 means the section has no bytes in the file: it
        // is uninitialised data, zero in memory for SizeOfRawData bytes. The
        // Watcom linker writes.bss that way, with a VirtualSize of 0; the
        // bytes at file offset 0 are the MZ header and must not end up there
        // (Need for Speed II SE).
        if (!section.raw && section.raw_size) {
            if (section.virtual_size < section.raw_size) section.virtual_size = section.raw_size;
            section.raw_size = 0;
        }
        const U32 extent = section.virtual_size > section.raw_size ? section.virtual_size : section.raw_size;
        if ((section.rva & 4095) || section.rva < RoundPage(header_size) ||
            !Fits(section.rva, extent, image_size) || !Fits(section.raw, wide ? section.raw_size : 0, length)) return false;
        for (unsigned j = 0; j < i; ++j) {
            const auto &previous = sections[j];
            U32 end = previous.rva + RoundPage(previous.virtual_size > previous.raw_size ? previous.virtual_size : previous.raw_size);
            if (section.rva < end && previous.rva < section.rva + RoundPage(extent)) return false;
        }
        sections[i] = section;
    }
    count = n; error = nullptr; return true;
}

// An ARM64 image prefers a base above 4 GiB (140000000h from the linker) and
// is always moved; it goes where Win98 puts a program.
bool Image::Load(AddressSpace &space) { return Load(space, preferred_high ? 0x00400000u : preferred); }
bool Image::Load(AddressSpace &space, U32 address)
{
    live = &space;
    if ((address & 65535) || (unsigned long long)address + image_size > 0x100000000ull ||
        ((address != preferred || preferred_high) && !CanRelocate())) return false;
    base = address;
    // Win98 maps the file's header pages, not exactly SizeOfHeaders bytes. A
    // linker may leave the section table above SizeOfHeaders inside the first
    // page, and the guest reads it there.
    const U32 headers = RoundPage(header_size) < length ? RoundPage(header_size) : (U32)length;
    if (error || !space.Map(base, image_size, Read | Write) || !space.Store(base, bytes, headers)) return false;
    for (unsigned i = 0; i < count; ++i) {
        const auto &s = sections[i];
        // A short final file section is zero-filled past EOF on Win98.
        // Parse checked its offset; never read the missing file bytes.
        const Size stored = s.raw_size < length - s.raw ? s.raw_size : length - s.raw;
        if (stored && !space.Store(base + s.rva, bytes + s.raw, stored)) return false;
    }
    if ((base != preferred || preferred_high) && !Relocate(space, base)) return false;
    // An ARM64 image gets the rights its sections name, read, write and
    // execute apart, as Windows on ARM gives them. The header is read-only.
    if (wide) {
        if (!space.Protect(base, image_size, 0) || !space.Protect(base, RoundPage(header_size), Read)) return false;
        for (unsigned i = 0; i < count; ++i) {
            const auto &s = sections[i];
            const U32 extent = RoundPage(s.virtual_size > s.raw_size ? s.virtual_size : s.raw_size);
            const unsigned rights = (s.flags & 0x40000000u ? (unsigned)Read : 0u) | (s.flags & 0x80000000u ? (unsigned)Write : 0u) |
                                    (s.flags & 0x20000000u ? (unsigned)Execute : 0u);
            if (extent && !space.Protect(base + s.rva, extent, rights)) return false;
        }
        return true;
    }
    // Native loader_layout and image_protection establish read-only image
    // headers and section permissions from IMAGE_SCN_MEM_WRITE, for DLLs and
    // the main executable. IsBadWritePtr confirms actual write protection;
    // VirtualProtect can change a private image's header and section pages.
    // Uncovered pages remain inaccessible; x86 readable pages are executable.
    if (!space.ReserveImage(base, image_size) || !space.Protect(base, RoundPage(header_size), Read))
        return false;
    // Execute is a hint for the backend: a section marked as code holds code from the
    // first instruction on, and the backend may treat every other page as data
    // until the guest proves otherwise. The two
    // flags are IMAGE_SCN_CNT_CODE and IMAGE_SCN_MEM_EXECUTE.
    for (unsigned i = 0; i < count; ++i) {
        const auto &s = sections[i];
        const U32 extent = RoundPage(s.virtual_size > s.raw_size ? s.virtual_size : s.raw_size);
        const unsigned code = s.flags & (0x00000020u | 0x20000000u) ? (unsigned)Execute : 0u;
        const unsigned writable=s.flags&0x80000000u?unsigned(Write):0u;
        if (extent && !space.Protect(base + s.rva, extent, Read | writable | code)) return false;
    }
    // Only complete host pages consisting of private data sections of this
    // image qualify. Headers, holes, code, shared sections and the system
    // address ranges retain their normal protection. Relocations are done.
    if (space.ImageDataPageSize() == 16384 && base < 0x80000000u)
        for (U32 offset = 0; offset <= image_size && image_size - offset >= 16384; offset += 16384)
        {
            if (static_cast<unsigned long long>(base) + offset + 16384 > 0x80000000u) break;
            unsigned covered = 0, read_only = 0;
            for (unsigned page = 0; page < 4; ++page)
                for (unsigned i = 0; i < count; ++i)
                {
                    const auto &s = sections[i];
                    const U32 extent = RoundPage(s.virtual_size > s.raw_size ? s.virtual_size : s.raw_size);
                    const U32 at = offset + page * 4096;
                    if (at < s.rva || at - s.rva >= extent) continue;
                    if (!(s.flags & 0x000000c0u) || !(s.flags & 0x40000000u) ||
                        (s.flags & (0x00000020u | 0x20000000u | 0x10000000u))) break;
                    covered |= 1u << page;
                    if (!(s.flags & 0x80000000u)) read_only |= 1u << page;
                    break;
                }
            if (covered == 15 && read_only && read_only != 15 &&
                !space.RelaxImageData(base + offset, read_only)) return false;
        }
    return true;
}

// A section with IMAGE_SCN_MEM_SHARED belongs to the session, not to the
// process. The loader hands these ranges to Application::ShareImageSections.
unsigned Image::SharedSections(SharedSection *out, unsigned capacity) const
{
    unsigned found = 0;
    if (error) return 0;
    for (unsigned i = 0; i < count && found < capacity; ++i) {
        const auto &s = sections[i];
        if (!(s.flags & 0x10000000)) continue;
        const U32 extent = s.virtual_size > s.raw_size ? s.virtual_size : s.raw_size;
        if (!extent) continue;
        out[found].rva = s.rva;
        out[found].size = extent;
        for (unsigned c = 0; c < 9; ++c) out[found].name[c] = c < 8 ? s.name[c] : 0;
        ++found;
    }
    return found;
}

U32 Image::FileOffset(U32 rva) const
{
    if (error) return ~0u;
    // an address below SizeOfHeaders keeps its value, because the
    // headers sit at the same offset in the file as in the image.
    if (rva < header_size) return rva;
    for (unsigned i = 0; i < count; ++i)
    {
        const auto &s = sections[i];
        // the comparison uses the raw size, so a page that only the
        // virtual size covers has no file bytes and no offset.
        if (rva >= s.rva && rva - s.rva < s.raw_size) return s.raw + rva - s.rva;
    }
    return ~0u;
}
const unsigned char *Image::Rva(U32 address, Size size) const
{
    if (Fits(address, size, header_size)) return bytes + address;
    for (unsigned i = 0; i < count; ++i) {
        const auto &s = sections[i];
        if (address >= s.rva && Fits(address - s.rva, size, s.raw_size)) return bytes + s.raw + address - s.rva;
    }
    return LiveRva(address, size);
}
const unsigned char *Image::LiveRva(U32 address, Size size) const
{
    if (!live || !base || !size) return nullptr;
    bool inside = false;
    for (unsigned i = 0; i < count && !inside; ++i)
        inside = address >= sections[i].rva && Fits(address - sections[i].rva, size, sections[i].virtual_size);
    if (!inside) return nullptr;
    LiveBlock *block = live_blocks;
    while (block && (block->rva != address || block->size != size)) block = block->next;
    if (!block)
    {
        block = static_cast<LiveBlock *>(malloc(sizeof(LiveBlock) + size));
        if (!block) return nullptr;
        block->next = live_blocks;
        block->rva = address;
        block->size = static_cast<U32>(size);
        live_blocks = block;
    }
    return live->Fetch(base + address, block->bytes, static_cast<U32>(size)) ? block->bytes : nullptr;
}
Image::~Image()
{
    while (live_blocks)
    {
        LiveBlock *next = live_blocks->next;
        free(live_blocks);
        live_blocks = next;
    }
}

struct Image::ExportMemory final : Win98Exports::Memory
{
    const Image &image;
    explicit ExportMemory(const Image &p) : image(p) {}
    bool Read(U32 address, void *data, U32 size) override
    {
        if (address < image.base) return false;
        const auto *p = image.Rva(address - image.base, size);
        if (!p) return false;
        auto *out = static_cast<unsigned char *>(data);
        for (U32 i = 0; i < size; ++i) out[i] = p[i];
        return true;
    }
    void Error(U32) override {}
};
bool Image::FindExport(const char *name, U32 ordinal, Symbol &symbol, U32 hint) const
{
    symbol = {};
    if (error || !exports) return false;
    ExportMemory memory(*this);
    U32 address = Win98Exports::Find(memory, base, exports, export_size, name, ordinal, hint);
    if (!address || address < base || address - base >= image_size) return false;
    if (Win98Exports::Forwarded(base, exports, export_size, address)) {
        if (!String(address - base, symbol.forwarder, sizeof symbol.forwarder)) return false;
    } else symbol.address = address;
    return true;
}
U32 Image::Export(const char *name) const
{
    Symbol symbol;
    return name && FindExport(name, 0, symbol) ? symbol.address : 0;
}
bool Image::NearestExport(U32 address, char *out, unsigned capacity, U32 &offset) const
{
    offset = 0;
    if (out && capacity) out[0] = 0;
    if (error || !exports || !out || !capacity) return false;
    if (address < base || address - base >= image_size) return false;
    const auto *directory = Rva(exports, 40);
    if (!directory) return false;
    const U32 functions = U32At(directory + 28), names = U32At(directory + 32);
    const U32 ordinals = U32At(directory + 36), ordinal_base = U32At(directory + 16);
    const U32 function_count = U32At(directory + 20), name_count = U32At(directory + 24);
    if (!function_count || function_count > 65536 || name_count > 65536) return false;
    const auto *table = Rva(functions, function_count * 4);
    if (!table) return false;
    const U32 target = address - base;
    U32 best = 0, best_index = 0;
    bool found = false;
    for (U32 i = 0; i < function_count; ++i) {
        const U32 rva = U32At(table + i * 4);
        // A forwarder's slot points into the export directory, not at code.
        if (!rva || rva > target || (rva >= exports && rva - exports < export_size)) continue;
        if (found && rva <= best) continue;
        best = rva; best_index = i; found = true;
    }
    if (!found) return false;
    offset = target - best;
    // Name the entry if it has one, otherwise report its ordinal.
    const auto *name_table = name_count ? Rva(names, name_count * 4) : nullptr;
    const auto *ordinal_table = name_count ? Rva(ordinals, name_count * 2) : nullptr;
    if (name_table && ordinal_table)
        for (U32 i = 0; i < name_count; ++i)
            if (U16(ordinal_table + i * 2) == best_index && String(U32At(name_table + i * 4), out, capacity))
                return true;
    char digits[12];
    unsigned count = 0;
    U32 ordinal = ordinal_base + best_index;
    do { digits[count++] = char('0' + ordinal % 10); ordinal /= 10; } while (ordinal && count < sizeof digits);
    unsigned n = 0;
    for (const char *prefix = "ordinal_"; *prefix && n + 1 < capacity; ++prefix) out[n++] = *prefix;
    while (count && n + 1 < capacity) out[n++] = digits[--count];
    out[n] = 0;
    return n != 0;
}

bool Image::Relocate(AddressSpace &space, U32 address) const
{
    const U32 delta = address - preferred;
    // The 64-bit difference, for DIR64 records of a PE32+ image.
    const unsigned long long delta64 = (unsigned long long)address - ((unsigned long long)preferred_high << 32 | preferred);
    // PE/i386 relocation records. This is new NEO98 loader infrastructure;
    // the table search above matches Win98 KERNEL32.
    for (U32 offset = 0; offset < relocation_size;) {
        if (!Fits(offset, 8, relocation_size)) return false;
        const auto *block = Rva(relocations + offset, 8);
        if (!block) return false;
        const U32 page = U32At(block), size = U32At(block + 4);
        if (size < 8 || (size & 1) || !Fits(offset, size, relocation_size) || (page & 4095)) return false;
        const auto *entries = Rva(relocations + offset + 8, size - 8);
        if (!entries) return false;
        for (U32 i = 0; i < (size - 8) / 2; ++i) {
            const unsigned record = U16(entries + i * 2), type = record >> 12;
            if (!type) continue;
            const U32 target = page + (record & 4095);
            if (type == 10) {
                // IMAGE_REL_BASED_DIR64, the only kind lld-link writes for ARM64.
                unsigned char wide_data[8] = {};
                if (!wide || target < page || !Fits(target, 8, image_size) || !space.Fetch(address + target, wide_data, 8)) return false;
                unsigned long long value64 = 0;
                for (U32 j = 0; j < 8; ++j) value64 |= (unsigned long long)wide_data[j] << (j * 8);
                value64 += delta64;
                for (U32 j = 0; j < 8; ++j) wide_data[j] = (unsigned char)(value64 >> (j * 8));
                if (!space.Store(address + target, wide_data, 8)) return false;
                continue;
            }
            if (wide && type != 3) return false;
            if (target < page || type > 4 || !Fits(target, type == 3 ? 4 : 2, image_size)) return false;
            unsigned char data[4] = {};
            const U32 width = type == 3 ? 4 : 2;
            if (!space.Fetch(address + target, data, width)) return false;
            U32 value = U32At(data);
            if (type == 1) value += delta >> 16;
            else if (type == 2) value += delta & 65535;
            else if (type == 3) value += delta;
            else {
                if (++i >= (size - 8) / 2) return false;
                U32 low = U16(entries + i * 2);
                if (low & 0x8000) low |= 0xffff0000u;
                value = ((value << 16) + low + delta + 0x8000u) >> 16;
            }
            for (U32 j = 0; j < width; ++j) data[j] = value >> (j * 8);
            if (!space.Store(address + target, data, width)) return false;
        }
        offset += size;
    }
    return true;
}

bool Image::String(U32 address, char *output, unsigned capacity) const
{
    for (unsigned i = 0; i < capacity && i <= ~address; ++i) {
        const auto *p = Rva(address + i, 1); if (!p) return false;
        output[i] = *p; if (!*p) return true;
    }
    return false;
}

bool Image::BindImports(AddressSpace &space, ImportResolver resolve, void *context) const
{
    if (error || !resolve) return false;
    if (!imports && !import_size) return true;
    if (!imports || import_size < 20) return false;
    unsigned total = 0;
    for (U32 offset = 0; offset <= import_size - 20; offset += 20) {
        const auto *descriptor = Rva(imports + offset, 20); if (!descriptor) return false;
        U32 lookup = U32At(descriptor), name = U32At(descriptor + 12), slots = U32At(descriptor + 16);
        if (!lookup && !name && !slots && !U32At(descriptor + 4) && !U32At(descriptor + 8)) return true;
        char dll[256]; if (!name || !slots || !String(name, dll, sizeof dll)) return false;
        if (!lookup) lookup = slots;
        // A PE32+ import table has entries of 8 bytes, the ordinal flag in bit 63.
        const unsigned width = wide ? 8 : 4;
        for (unsigned index = 0;; ++index) {
            if (index >= 4096 || !Fits(lookup, Size(index) * width + width, image_size) ||
                !Fits(slots, Size(index) * width + width, image_size)) return false;
            const auto *thunk = Rva(lookup + index * width, width); if (!thunk) return false;
            U32 value = U32At(thunk); const U32 high = wide ? U32At(thunk + 4) : 0;
            if (!value && !high) break;
            if (++total > 4096) return false;
            char symbol[256] = {}; unsigned ordinal = 0; bool by_ordinal = wide ? (high & 0x80000000) != 0 : (value & 0x80000000) != 0;
            if (by_ordinal) ordinal = value & 65535;
            else if (high || value > ~0u - 2 || !String(value + 2, symbol, sizeof symbol)) return false;
            U32 target[2] = {0, 0};
            if (!resolve(context, dll, by_ordinal ? nullptr : symbol, ordinal, &target[0]) || !target[0] ||
                !space.Store(base + slots + index * width, target, width)) return false;
        }
    }
    return false; // The descriptor terminator must fit in its directory.
}

bool Image::FindResource(const ResourceKey &type, const ResourceKey &name, U32 language,
                         ResourceEntry &result, U32 *failure) const
{
    result = {};
    if (failure) *failure = 1812;
    if (error || !resources || resource_size < 16) return false;
    U32 offset = 0, selected_language = 0;
    for (unsigned level = 0; level < 3; ++level)
    {
        if (!Fits(offset, 16, resource_size)) return false;
        const auto *directory = Rva(resources + offset, 16);
        if (!directory) return false;
        const unsigned named = U16(directory + 12), numbered = U16(directory + 14), entries = named + numbered;
        if (!Fits(offset + 16, Size(entries) * 8, resource_size)) return false;
        const auto *table = Rva(resources + offset + 16, Size(entries) * 8);
        if (!table) return false;
        const ResourceKey &key = level == 0 ? type : name;
        bool found = false;
        for (unsigned i = 0; i < entries; ++i)
        {
            const U32 label = U32At(table + i * 8);
            if ((i < named) != bool(label & 0x80000000)) return false;
            if (level == 2)
            {
                if (i < named || (language != ~0u && label != language)) continue;
            }
            else if (key.name)
            {
                if (i >= named) continue;
                const U32 string_offset = label & 0x7fffffff;
                if (!Fits(string_offset, 2, resource_size)) return false;
                const auto *header = Rva(resources + string_offset, 2);
                if (!header) return false;
                const unsigned n = U16(header);
                if (!Fits(string_offset + 2, Size(n) * 2, resource_size)) return false;
                const auto *text = Rva(resources + string_offset + 2, Size(n) * 2);
                if (!text) return false;
                if (n != key.length) continue;
                unsigned j = 0;
                while (j < n && FoldName(U16(text + j * 2)) == FoldName(key.name[j])) ++j;
                if (j != n) continue;
            }
            else if (i < named || label != key.id) continue;
            const U32 next = U32At(table + i * 8 + 4);
            if (bool(next & 0x80000000) != (level < 2)) return false;
            offset = next & 0x7fffffff;
            if (level == 2) selected_language = label;
            found = true;
            break;
        }
        if (!found) { if (failure) *failure = 1813 + level; return false; }
    }
    if (!Fits(offset, 16, resource_size)) return false;
    const auto *data = Rva(resources + offset, 16);
    if (!data) return false;
    // The data address of a directory entry is an RVA of the loaded image, and
    // the original reads it out of the mapped image. It is checked against the
    // image and not against the file, because a runtime unpacker restores
    // resource bytes that the file does not carry: JRE.EXE of games/KIDS200 is
    // packed with Petite 2.x, keeps its directory tree inside the raw bytes of
    // the section and has every data address of it past the raw end, so a file
    // check refused all twelve dialogs of the installer. Whoever wants
    // the bytes out of the file asks Rva or FileOffset for them and gets the
    // refusal there.
    if (!Fits(U32At(data), U32At(data + 4), image_size)) return false;
    result = {resources + offset, U32At(data), U32At(data + 4), U32At(data + 8), selected_language};
    if (failure) *failure = 0;
    return true;
}
bool Image::ResourceLabelAt(const ResourceKey *type, unsigned index,
                            ResourceLabel &label, U32 *failure) const
{
    label = {};
    if (failure) *failure = 1812;
    if (error || !resources || resource_size < 16) return false;
    U32 offset = 0;
    for (unsigned level = 0; level <= (type ? 1u : 0u); ++level) {
        if (!Fits(offset, 16, resource_size)) return false;
        const auto *directory = Rva(resources + offset, 16);
        if (!directory) return false;
        unsigned named = U16(directory + 12), count = named + U16(directory + 14);
        if (!Fits(offset + 16, Size(count) * 8, resource_size)) return false;
        const auto *table = Rva(resources + offset + 16, Size(count) * 8);
        if (!table && count) return false;
        const bool selecting = type && !level;
        if (!selecting && index >= count) {
            if (failure) *failure = 0; // End of this directory, including an empty one.
            return false;
        }
        bool found = false;
        for (unsigned i = selecting ? 0 : index; i < count; ++i) {
            U32 value = U32At(table + i * 8), next = U32At(table + i * 8 + 4);
            ResourceLabel candidate;
            if ((i < named) != bool(value & 0x80000000)) return false;
            if (i < named) {
                U32 string = value & 0x7fffffff;
                if (!Fits(string, 2, resource_size)) return false;
                const auto *header = Rva(resources + string, 2);
                if (!header) return false;
                candidate.length = U16(header);
                if (!Fits(string + 2, Size(candidate.length) * 2, resource_size)) return false;
                candidate.text = Rva(resources + string + 2, Size(candidate.length) * 2);
                if (!candidate.text) return false;
            } else candidate.id = value & 65535;
            if (!selecting) {
                label = candidate;
                if (failure) *failure = 0;
                return true;
            }
            if (type->name) {
                if (!candidate.text || candidate.length != type->length) continue;
                unsigned j = 0;
                while (j < candidate.length && U16(candidate.text + j * 2) == type->name[j]) ++j;
                if (j != candidate.length) continue;
            } else if (candidate.text || candidate.id != type->id) continue;
            if (!(next & 0x80000000)) return false;
            offset = next & 0x7fffffff;
            found = true;
            break;
        }
        if (!found) { if (failure) *failure = 1813; return false; }
    }
    return false;
}
unsigned Image::ResourceLanguages(const ResourceKey &type, const ResourceKey &name, U32 *out,
                                  unsigned capacity, U32 *failure) const
{
    if (failure) *failure = 1812;
    if (error || !resources || resource_size < 16) return 0;
    U32 offset = 0;
    for (unsigned level = 0; level < 3; ++level)
    {
        if (!Fits(offset, 16, resource_size)) return 0;
        const auto *directory = Rva(resources + offset, 16);
        if (!directory) return 0;
        const unsigned named = U16(directory + 12), numbered = U16(directory + 14), entries = named + numbered;
        if (!Fits(offset + 16, Size(entries) * 8, resource_size)) return 0;
        const auto *table = Rva(resources + offset + 16, Size(entries) * 8);
        if (!table) return 0;
        if (level == 2)
        {
            unsigned count = 0;
            for (unsigned i = named; i < entries; ++i)
                if (count < capacity) out[count++] = U32At(table + i * 8);
            if (failure) *failure = count ? 0 : 1815;
            return count;
        }
        const ResourceKey &key = level == 0 ? type : name;
        bool found = false;
        for (unsigned i = 0; i < entries; ++i)
        {
            const U32 label = U32At(table + i * 8);
            if (key.name)
            {
                if (i >= named) continue;
                const U32 string_offset = label & 0x7fffffff;
                if (!Fits(string_offset, 2, resource_size)) return 0;
                const auto *header = Rva(resources + string_offset, 2);
                if (!header) return 0;
                const unsigned n = U16(header);
                if (!Fits(string_offset + 2, Size(n) * 2, resource_size)) return 0;
                const auto *text = Rva(resources + string_offset + 2, Size(n) * 2);
                if (!text || n != key.length) continue;
                unsigned j = 0;
                while (j < n && FoldName(U16(text + j * 2)) == FoldName(key.name[j])) ++j;
                if (j != n) continue;
            }
            else if (i < named || label != key.id) continue;
            const U32 next = U32At(table + i * 8 + 4);
            if (!(next & 0x80000000)) return 0;
            offset = next & 0x7fffffff;
            found = true;
            break;
        }
        if (!found) { if (failure) *failure = 1813 + level; return 0; }
    }
    return 0;
}
const unsigned char *Image::Resource(unsigned type, unsigned id, U32 *size) const
{
    if (!size) return nullptr;
    *size = 0;
    ResourceEntry entry;
    // Same order as the FindResourceA path in x86_resources.cpp: LANG_NEUTRAL,
    // then English, then the first language in the directory. JOY.CPL keeps its
    // applet name as string 1076 in seventeen languages and lists Chinese first,
    // so taking the first entry named the Control Panel icon in a script the
    // ANSI code page turns into question marks.
    U32 error = 0;
    if (!FindResource({type}, {id}, 0, entry, &error) &&
        !(error == 1815 && (FindResource({type}, {id}, 0x409, entry, &error) ||
                            FindResource({type}, {id}, ~0u, entry, &error))))
        return nullptr;
    *size = entry.size;
    return Rva(entry.data, entry.size);
}
}
