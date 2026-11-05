// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Icon extraction as SHELL32 4.72 does it.
namespace Win98ShellIcons {
using U = unsigned;
constexpr U CountResources = 0x67932;
// ExtractIconW and InternalExtractIconListA/W return zero.
constexpr U EmptyExport() { return 0; }
inline bool Negative(U value) { return (value & 0x80000000u) != 0; }

// 7FCCD B2F: section virtual size is preferred; raw size is the zero fallback.
template<class E> U Rva(E &e, U base, U rva) {
    const U pe = base + e.Read32(base + 0x3c);
    const U sections = pe + 24 + e.Read16(pe + 20), count = e.Read16(pe + 6);
    for (U i = 0; i < count && e.Valid(); ++i) {
        const U section = sections + i * 40, start = e.Read32(section + 12);
        U size = e.Read32(section + 8);
        if (!size) size = e.Read32(section + 16);
        if (rva >= start && rva < start + size)
            return base + e.Read32(section + 20) - start + rva;
    }
    return 0;
}
// the original checks the optional-header size, not its magic.
template<class E> U PeRoot(E &e, U base) {
    const U pe = base + e.Read32(base + 0x3c);
    if (e.Read16(base) != 0x5a4d || e.Read32(pe) != 0x4550 || e.Read16(pe + 20) < 0xe0) return 0;
    return Rva(e, base, e.Read32(pe + 0x88));
}
// this path requires a Windows NE resource table, version >= 3.0.
template<class E> U NeRoot(E &e, U base) {
    const U ne = base + e.Read32(base + 0x3c);
    if (e.Read16(base) != 0x5a4d || e.Read16(ne) != 0x454e) return 0;
    const U target = e.Read8(ne + 0x36);
    if ((target != 2 && target != 4) || e.Read16(ne + 0x3e) < 0x300) return 0;
    const U resource = e.Read16(ne + 0x24);
    return resource != e.Read16(ne + 0x26) ? ne + resource : 0;
}
// positive values select by table position, negative values by ID.
// The first language is selected, including named entries in the position count.
template<class E> U PeResource(E &e, U base, U root, U index, U type, U &bytes) {
    const U count = e.Read16(root + 12) + e.Read16(root + 14);
    U entry = root + 16, i = 0;
    for (; i < count && e.Valid(); ++i, entry += 8) if (e.Read32(entry) == type) break;
    if (i == count || !e.Valid()) return 0;
    const U directory = root + (e.Read32(entry + 4) & 0x7fffffffu);
    const U entries = e.Read16(directory + 12) + e.Read16(directory + 14);
    if (index == CountResources) return entries;
    entry = directory + 16;
    if (Negative(index)) {
        for (i = 0; i < entries && e.Valid(); ++i) if (e.Read32(entry + i * 8) == 0u - index) break;
    } else i = index;
    if (i >= entries || !e.Valid()) return 0;
    U offset = e.Read32(entry + i * 8 + 4);
    if (offset & 0x80000000u) { entry = root + (offset & 0x7fffffffu) + 16; i = 0; }
    offset = e.Read32(entry + i * 8 + 4);
    if (offset & 0x80000000u) return 0;
    const U data = root + offset;
    bytes = e.Read32(data + 4);
    return Rva(e, base, e.Read32(data));
}
// table alignment uses x86's masked shift count, including odd inputs.
template<class E> U NeResource(E &e, U base, U root, U index, U type, U &bytes) {
    U group = root + 2;
    while (e.Valid()) {
        const U kind = e.Read16(group);
        if (!kind) { bytes = 0; return 0; }
        if (kind == (type | 0x8000)) break;
        group += 8 + 12 * e.Read16(group + 2);
    }
    if (!e.Valid()) return 0;
    const U count = e.Read16(group + 2);
    if (index == CountResources) return count;
    U i = index;
    if (Negative(index)) {
        const U id = (0u - index) | 0x8000;
        for (i = 0; i < count && e.Valid(); ++i) if (e.Read16(group + 14 + i * 12) == id) break;
    }
    if (i >= count || !e.Valid()) return 0;
    const U item = group + 8 + i * 12, shift = e.Read8(root) & 31;
    bytes = e.Read16(item + 2) << shift;
    return base + (e.Read16(item) << shift);
}

// Extract icons using the PE/NE resource helpers.
// File mapping, USER image selection/creation and output addresses are providers.
// E::Valid reports the native equivalent of the enclosing original SEH scope.
template<class E> U MappedExtract(E &e, U file, U index, U cx, U cy, U count, U flags) {
    U result = 0, root = 0;
    const U size = e.FileSize(file), mapping = e.CreateMapping(file);
    const U base = mapping ? e.Map(mapping) : 0;
    if (base) {
        bool ne = false;
        const U signature0 = e.Read16(base);
        const U offset = signature0 == 0x5a4d ? e.Read32(base + 0x3c) : 0;
        if (offset && !Negative(offset) && offset < size) {
            const U signature = e.Read16(base + offset);
            if (signature == 0x454e) { root = NeRoot(e, base); ne = true; }
            else if (signature == 0x4550) root = PeRoot(e, base);
        }
        U bytes = 0;
        auto resource = [&](U at, U type) { return ne ? NeResource(e, base, root, at, type, bytes)
                                                       : PeResource(e, base, root, at, type, bytes); };
        if (root && e.Valid()) {
            if (!e.HasOutput()) result = resource(CountResources, 14);
            else while (result < count && e.Valid()) {
                const U group = resource(index, 14);
                if (!group || e.Read16(group) || e.Read16(group + 2) != 1) break;
                U width = cx, height = cy;
                while (e.Valid()) {
                    const U id = e.Select(group, width & 65535, height & 65535, flags);
                    const U bits = resource(0u - id, 3);
                    if (!bits) goto cleanup;
                    const U header = e.Read32(bits);
                    if (header != 40 && header != 12) goto cleanup;
                    // Original probes the final byte before publishing IDs.
                    e.Read8(bits + bytes - 1);
                    if (!e.Valid()) goto cleanup;
                    e.StoreId(result, id);
                    const U icon = e.CreateIcon(bits, bytes, width & 65535, height & 65535, flags);
                    e.StoreIcon(result++, icon); // a failed USER creation is still counted
                    width >>= 16;
                    if (!width) break;
                    height >>= 16;
                }
                ++index;
            }
        }
    }
cleanup:
    if (!e.Valid()) result = 0; // original exception handler
    if (base) e.Unmap(base);
    if (mapping != ~0u) e.CloseMapping(mapping); // includes zero on creation failure
    return result;
}

// standalone icon/cursor files use USER's file-image loader.
// The original returns one for a successful packed pair, unlike MappedExtract.
template<class E> U IconFile(E &e, const char *path, U index, U cx, U cy, U *out, U flags) {
    if (!Negative(index) && index >= 1) return 0;
    flags |= 0x10;
    for (;;) {
        const U icon = e.LoadImageFile(path, 1, cx & 65535, cy & 65535, flags);
        if (!icon) return 0;
        if (out) e.FileIcon(out, icon); else e.DestroyIcon(icon);
        cx >>= 16;
        if (!cx) return 1;
        cy >>= 16;
        if (out) ++out;
    }
}

//  with cold blocks. Filename expansion/search,
// file metadata and USER's standalone image loader are external services.
template<class E> U FileExtract(E &e, const char *name, U index, U cx, U cy, U *out, U count, U flags) {
    if (out) *out = 0;
    e.Output(out);
    if (!e.Valid()) return 0;
    const U extension = e.Extension(name);
    if (!e.Valid()) return 0;
    if (extension == 0x6669702e || extension == 0x6b6e6c2e ||
        extension == 0x6d6f632e || extension == 0x7461622e) return 0;
    char expanded[260] = {}, path[260] = {};
    e.Expand(name, expanded, 260); expanded[259] = 0;
    if (!e.Valid()) return 0;
    const bool unc = expanded[0] == '\\' && expanded[1] == '\\';
    if (!e.Resolve(expanded, path, unc)) { e.ResolveError(unc); return out ? ~0u : 0; }
    U file = e.Open(path, 0x80000100u);
    if (file == ~0u) file = e.Open(path, 0x80000000u);
    else e.PreserveAccessTime(file);
    if (file == ~0u) return out ? ~0u : 0;
    U result = 0;
    unsigned char header[12] = {};
    if (e.ReadFile(file, header, 12) == 12) {
        const U magic = header[0] | U(header[1]) << 8;
        if (magic == 0x5a4d) result = MappedExtract(e, file, index, cx, cy, count, flags);
        else if (magic == 0x4d42) result = e.BitmapFile(path, index, cx, cy, out, count, flags);
        else if ((magic == 0x4952 && header[2] == 'F' && header[3] == 'F' &&
                  header[8] == 'A' && header[9] == 'C' && header[10] == 'O' && header[11] == 'N') ||
                 (!magic && (header[2] == 1 || header[2] == 2) && !header[3] &&
                  (U(header[4]) | U(header[5]) << 8) >= 1 && (U(header[4]) | U(header[5]) << 8) <= 10))
            result = IconFile(e, path, index, cx, cy, out, flags);
    }
    e.CloseFile(file);
    return result;
}

// Providers retain
// pointer identity; the temporary two-handle array never aliases guest output.
template<class E> U ExtractIcon(E &e, U file, U index) {
    U icon = 0;
    if (index == ~0u) return e.Extract(file, 0, 0, 0, nullptr, 0);
    const U cy = e.Metric(12), cx = e.Metric(11);
    e.Extract(file, index, cx, cy, &icon, 1);
    return icon;
}
template<class E> U ExtractIconEx(E &e, U file, U index, U *large, U *small, U count) {
    if (index == ~0u || (!large && !small)) return e.Extract(file, 0, 0, 0, nullptr, 0);
    if (large && small && count == 1) {
        U pair[2] = {};
        const U sy = e.Metric(50), ly = e.Metric(12), sx = e.Metric(49), lx = e.Metric(11);
        const U result = e.Extract(file, index, (sx << 16) | (lx & 65535),
                                  (sy << 16) | (ly & 65535), pair, 2);
        *large = pair[0]; *small = pair[1]; return result;
    }
    U result = index;
    if (large) { const U cy = e.Metric(12), cx = e.Metric(11); result = e.Extract(file, index, cx, cy, large, count); }
    if (small) { const U cy = e.Metric(50), cx = e.Metric(49); result = e.Extract(file, index, cx, cy, small, count); }
    return result;
}
} // namespace Win98ShellIcons
