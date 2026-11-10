// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Andreas Horvath
#pragma once
#include "../src/windows_runtime_data.h"
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>
#include <cstdint>

// This reader supports the Windows 98 SE table layout. Resource payloads are
// never build inputs. Every read comes from the mounted user's installation.
namespace HostWindowsData {
using Bytes = std::vector<unsigned char>;
// FNV-1a identifies the complete ordered import inputs, independent of paths
// on the host and of heap addresses. This is a compatibility check, not a MAC.
inline uint64_t importIdentity = 14695981039346656037ull;
inline void identify(const Bytes &bytes) {
    uint64_t size = bytes.size();
    for (unsigned i = 0; i < 8; ++i) { importIdentity ^= (size >> (8 * i)) & 255; importIdentity *= 1099511628211ull; }
    for (unsigned char byte : bytes) { importIdentity ^= byte; importIdentity *= 1099511628211ull; }
}
struct Reader {
    const Bytes &b;
    bool ok = true;
    unsigned get(size_t p, unsigned n) {
        if (p > b.size() || n > b.size() - p) { ok = false; return 0; }
        unsigned v = 0;
        for (unsigned i = 0; i < n; ++i) v |= unsigned(b[p + i]) << (8 * i);
        return v;
    }
    Bytes slice(size_t p, size_t n) {
        if (p > b.size() || n > b.size() - p) { ok = false; return {}; }
        return Bytes(b.begin() + p, b.begin() + p + n);
    }
    unsigned ne() {
        unsigned at = get(60, 4);
        if (get(0, 2) != 0x5a4d || get(at, 2) != 0x454e) ok = false;
        return at;
    }
    Bytes segment(unsigned number, unsigned offset, unsigned size) {
        unsigned at = ne(), shift = get(at + 50, 2);
        unsigned count = get(at + 28, 2), table = at + get(at + 34, 2);
        if (!number || number > count || shift > 16) { ok = false; return {}; }
        unsigned entry = table + (number - 1) * 8;
        unsigned length = get(entry + 2, 2); if (!length) length = 65536;
        if (offset > length || size > length - offset) { ok = false; return {}; }
        return slice((size_t(get(entry, 2)) << shift) + offset, size);
    }
    unsigned rva(unsigned address, unsigned size) {
        unsigned pe = get(60, 4), count = get(pe + 6, 2);
        unsigned table = pe + 24 + get(pe + 20, 2);
        if (get(pe, 4) != 0x4550 || count > 96) { ok = false; return 0; }
        for (unsigned i = 0; i < count; ++i) {
            unsigned e = table + i * 40, base = get(e + 12, 4), length = get(e + 16, 4);
            if (address >= base && address - base <= length && size <= length - (address - base))
                return get(e + 20, 4) + address - base;
        }
        ok = false; return 0;
    }
    Bytes peData(unsigned address, unsigned size) { return slice(rva(address, size), size); }
    Bytes peResource(unsigned type, unsigned id, unsigned &language) {
        unsigned pe = get(60, 4), base = rva(get(pe + 24 + 112, 4), 16);
        unsigned dir = base, found = language;
        const unsigned keys[] = {type, id};
        for (unsigned depth = 0; depth < 3 && ok; ++depth) {
            unsigned count = get(dir + 12, 2) + get(dir + 14, 2), next = 0;
            if (count > 4096) { ok = false; break; }
            // Of the languages of a resource: the one asked for, else
            // English, else the first, as the resource search of Windows.
            unsigned rank = 0;
            for (unsigned i = 0; i < count; ++i) {
                unsigned key = get(dir + 16 + i * 8, 4), value = get(dir + 20 + i * 8, 4);
                unsigned fits = depth < 2 ? key == keys[depth] : key == language ? 3 : key == 0x409 ? 2 : 1;
                if (fits <= rank) continue;
                if ((depth < 2) != bool(value & 0x80000000)) { ok = false; break; }
                rank = fits; next = base + (value & 0x7fffffff);
                if (depth == 2) found = key;
            }
            if (!next) { ok = false; break; }
            dir = next;
        }
        language = found;
        return ok ? peData(get(dir, 4), get(dir + 4, 4)) : Bytes{};
    }
};
struct Resource { unsigned type, id; std::string name; Bytes bytes; };
inline bool resources(const Bytes &b, std::vector<Resource> &out) {
    Reader r{b}; unsigned ne = r.ne(), table = ne + r.get(ne + 36, 2);
    unsigned shift = r.get(table, 2), at = table + 2;
    if (shift > 16) return false;
    for (unsigned group = 0; group < 256 && r.ok; ++group) {
        unsigned type = r.get(at, 2), count = r.get(at + 2, 2); at += 8;
        if (!type) return r.ok;
        if (count > 4096 || !(type & 0x8000)) return false;
        for (unsigned i = 0; i < count && r.ok; ++i, at += 12) {
            unsigned id = r.get(at + 6, 2);
            Resource item{type & 32767, id & 32767, {}, {}};
            if (!(id & 32768)) {
                auto name = r.slice(table + id + 1, r.get(table + id, 1));
                item.name.assign(name.begin(), name.end()); item.id = 0;
            }
            item.bytes = r.slice(size_t(r.get(at, 2)) << shift, size_t(r.get(at + 2, 2)) << shift);
            out.push_back(std::move(item));
        }
    }
    return false;
}
inline const Bytes &find(const std::vector<Resource> &rs, unsigned type, unsigned id, const char *name = "") {
    for (const auto &r : rs) if (r.type == type && r.id == id && r.name == name) return r.bytes;
    static const Bytes empty; return empty;
}
inline bool copy(void *target, size_t size, const Bytes &data) {
    if (data.size() != size) return false;
    std::memcpy(target, data.data(), size); return true;
}
template<class T> bool copy(T &target, const Bytes &data) { return copy(&target, sizeof target, data); }
template<unsigned Capacity> bool copy(Win98LocalizedBytes<Capacity> &target, const Bytes &data) {
    if (data.empty() || data.size() > Capacity) return false;
    target = {}; std::memcpy(target.bytes, data.data(), data.size()); target.size = unsigned(data.size()); return true;
}
inline void word(Bytes &out, unsigned v) { out.push_back(v); out.push_back(v >> 8); }
inline void dword(Bytes &out, unsigned v) { word(out, v); word(out, v >> 16); }
// CP1252 is read from the installed NLS file, including punctuation above ASCII.
inline unsigned wide(unsigned c) {
    unsigned at = (OriginalCodePage1252[8] | unsigned(OriginalCodePage1252[9]) << 8 | unsigned(OriginalCodePage1252[10]) << 16 | unsigned(OriginalCodePage1252[11]) << 24) * 2 + c * 2;
    return at + 1 < sizeof OriginalCodePage1252 ? OriginalCodePage1252[at] | unsigned(OriginalCodePage1252[at + 1]) << 8 : 0;
}
inline bool menuItems(Reader &r, unsigned &at, Bytes &out, unsigned depth) {
    if (depth > 16) return false;
    for (unsigned i = 0; i < 1024 && r.ok; ++i) {
        unsigned flags = r.get(at, 2); at += 2; word(out, flags);
        if (!(flags & 16)) { word(out, r.get(at, 2)); at += 2; }
        unsigned c;
        do { c = r.get(at++, 1); word(out, wide(c)); } while (c && r.ok);
        if ((flags & 16) && !menuItems(r, at, out, depth + 1)) return false;
        if (flags & 128) return r.ok;
    }
    return false;
}
inline Bytes menu(const Bytes &data) {
    Reader r{data}; unsigned version = r.get(0, 2), at = 4 + r.get(2, 2); Bytes out(4);
    return !version && menuItems(r, at, out, 0) ? out : Bytes{};
}
inline void dialogString(Reader &r, unsigned &at, Bytes &out, bool klass = false) {
    unsigned c = r.get(at, 1);
    if (klass && (c & 128)) { ++at; word(out, 65535); word(out, c); return; }
    if (c == 255) { ++at; word(out, 65535); word(out, r.get(at, 2)); at += 2; return; }
    std::string text;
    while ((c = r.get(at++, 1)) && r.ok) text.push_back(char(c));
    if (klass && text == "msctls_trackbar") text = "msctls_trackbar32";
    for (unsigned char ch : text) word(out, wide(ch));
    word(out, 0);
}
inline Bytes dialog(const Bytes &data) {
    Reader r{data}; unsigned style = r.get(0, 4), count = r.get(4, 1), at = 13; Bytes out;
    dword(out, style); dword(out, 0); word(out, count);
    for (unsigned p = 5; p < 13; p += 2) word(out, r.get(p, 2));
    dialogString(r, at, out); dialogString(r, at, out); dialogString(r, at, out);
    if (style & 64) { word(out, r.get(at, 2)); at += 2; dialogString(r, at, out); }
    for (unsigned i = 0; i < count && r.ok; ++i) {
        while (out.size() % 4) out.push_back(0);
        dword(out, r.get(at + 10, 4)); dword(out, 0);
        for (unsigned p = 0; p < 10; p += 2) word(out, r.get(at + p, 2));
        at += 14; dialogString(r, at, out, true); dialogString(r, at, out);
        unsigned extra = r.get(at++, 1); word(out, extra);
        auto bytes = r.slice(at, extra); at += extra; out.insert(out.end(), bytes.begin(), bytes.end());
    }
    return r.ok ? out : Bytes{};
}
inline bool icon(const Bytes &data, unsigned *out, unsigned width) {
    Reader r{data}; unsigned header = r.get(0, 4), w = r.get(4, 4), h = r.get(8, 4) / 2;
    unsigned bits = r.get(14, 2), colors = r.get(32, 4);
    if (header < 40 || w != width || h != width || (bits != 1 && bits != 4 && bits != 8)) return false;
    if (!colors) colors = 1u << bits;
    if (colors > 256) return false;
    unsigned base = header + colors * 4, stride = ((w * bits + 31) / 32) * 4, mask = base + stride * h;
    for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x) {
        unsigned packed = r.get(base + (h - 1 - y) * stride + x * bits / 8, 1);
        unsigned index = (packed >> (8 - bits - (x * bits % 8))) & ((1u << bits) - 1);
        unsigned transparent = r.get(mask + (h - 1 - y) * ((w + 31) / 32) * 4 + x / 8, 1) >> (7 - x % 8) & 1;
        if (index >= colors) return false;
        out[y * w + x] = transparent ? 0 : 0xff000000 | r.get(header + index * 4, 3);
    }
    return r.ok;
}

} // namespace HostWindowsData
#include "host_windows_locale.h"
namespace HostWindowsData {
// Read must resolve a guest path through the mounted volume, including overlays.
// Fail closed: no guest or renderer may run after an unsuccessful import.
template<class Read> bool Load(Read read, std::string &error) {
    importIdentity = 14695981039346656037ull;
    auto file = [&](const char *name, Bytes &data) {
        const std::string path = std::string("C:\\WINDOWS\\SYSTEM\\") + name;
        if (!read(path.c_str(), data)) { error = "Missing Windows file " + path + ". Run the setup assistant."; return false; }
        identify(data);
        return true;
    };
    auto invalid = [&](const char *name) { error = std::string("Unsupported or damaged Windows 98 SE data in ") + name + ". Run the setup assistant."; return false; };
    Bytes data;
#define NEO98_NLS(name, target) if (!file(name, data)) return false; if (!copy(target, data)) return invalid(name)
    NEO98_NLS("CP_1252.NLS", OriginalCodePage1252);
    NEO98_NLS("CP_437.NLS", OriginalCodePage437);
    NEO98_NLS("CP_850.NLS", OriginalCodePage850);
    NEO98_NLS("CP_866.NLS", OriginalCodePage866);
    NEO98_NLS("UNICODE.NLS", OriginalUnicodeComposition);
#undef NEO98_NLS
    if (!file("KERNEL32.DLL", data)) return false;
    Reader kernel{data}; unsigned language = 0;
    Bytes messages = kernel.peResource(11, 1, language);
    if (messages.empty() || messages.size() > sizeof Win98MessageTable::Data) return invalid("KERNEL32.DLL");
    std::memset(Win98MessageTable::Data, 0, sizeof Win98MessageTable::Data);
    std::memcpy(Win98MessageTable::Data, messages.data(), messages.size());
    Win98MessageTable::Bytes = unsigned(messages.size()); Win98MessageTable::Language = language;
    if (
        !copy(OriginalComputerAnsiHigh, kernel.peData(0x5a480, 128)) ||
        !copy(OriginalComputerOemBytes, kernel.peData(0x5a500, 160)) || !kernel.ok) return invalid("KERNEL32.DLL");
    if (!file("KRNL386.EXE", data)) return false;
    Reader krnl{data};
    if (!copy(OriginalAtomUpper, krnl.segment(4, 0x20e2, 128)) || !krnl.ok) return invalid("KRNL386.EXE");
    if (!file("GDI.EXE", data)) return false;
    Reader gdi{data};
    if (!copy(OriginalGdiAnsiCharset, gdi.segment(11, 0x6ee, 512)) ||
        !copy(Win98GdiPalette::DefaultColors, gdi.segment(11, 0x25da, 80)) ||
        !copy(Win98GdiPalette::HalftoneColors, gdi.segment(2, 0x118a, 1024)) || !gdi.ok) return invalid("GDI.EXE");
    if (!file("GDI32.DLL", data)) return false;
    Reader gdi32{data};
    if (!copy(OriginalGdiObjectTypes, gdi32.peData(0x4d95, 13)) ||
        !copy(OriginalGdiCurrentOffsets, gdi32.peData(0x4790, 8)) ||
        !copy(OriginalGdiCharsetInfo, gdi32.peData(0x1f300, 168)) || !gdi32.ok) return invalid("GDI32.DLL");
    if (!file("USER.EXE", data)) return false;
    std::vector<Resource> user;
    if (!resources(data, user)) return invalid("USER.EXE");
    for (const auto &r : OriginalImageResources) {
        const Bytes &found = find(user, r.type, r.id, r.name);
        if (!r.length) { if (!copy(const_cast<unsigned char *>(r.data), r.size, found)) return invalid("USER.EXE resources"); continue; }
        if (found.empty() || found.size() > r.size) return invalid("USER.EXE resources");
        std::memset(const_cast<unsigned char *>(r.data), 0, r.size);
        std::memcpy(const_cast<unsigned char *>(r.data), found.data(), found.size()); *r.length = unsigned(found.size());
    }
    if (!copy(OriginalSystemMenu1, menu(find(user, 4, 1))) || !copy(OriginalSystemMenu2, menu(find(user, 4, 2)))) return invalid("USER.EXE menus");
    Reader usr{data};
    if (!copy(Win98UserResources::StripCalls, usr.segment(21, 0xb87, 364)) || !usr.ok) return invalid("USER.EXE OEM strip");
    const auto &group = find(user, 14, 105); Reader icons{group};
    unsigned count = icons.get(4, 2);
    if (count > 32) return invalid("USER.EXE icon group");
    unsigned found = 0;
    for (unsigned i = 0; i < count && icons.ok; ++i) {
        unsigned w = icons.get(6 + i * 14, 1), id = icons.get(18 + i * 14, 2); unsigned *target = nullptr;
#define NEO98_ICON(n, bit) case n: target = Win98UserResources::DefaultIcon##n; found |= bit; break
        switch (w) { NEO98_ICON(10, 1); NEO98_ICON(12, 2); NEO98_ICON(14, 4); NEO98_ICON(16, 8); NEO98_ICON(22, 16); NEO98_ICON(32, 32); }
#undef NEO98_ICON
        if (target && !icon(find(user, 3, id), target, w)) return invalid("USER.EXE icon");
    }
    if (!icons.ok || found != 63) return invalid("USER.EXE icon sizes");
    if (!file("SERIALUI.DLL", data)) return false;
    std::vector<Resource> serial;
    if (!resources(data, serial)) return invalid("SERIALUI.DLL");
    using namespace OriginalSerialUiAssets;
    if (!copy(Template119, dialog(find(serial, 5, 119))) || !copy(Template101, dialog(find(serial, 5, 101)))) return invalid("SERIALUI.DLL dialogs");
    for (const auto &resource : serial) {
        if (resource.type != 6) continue;
        Reader strings{resource.bytes}; unsigned at = 0;
        for (unsigned i = 0; i < 16 && strings.ok; ++i) {
            unsigned length = strings.get(at++, 1), id = (resource.id - 1) * 16 + i;
            Bytes value = strings.slice(at, length); at += length; value.push_back(0);
            switch (id) {
            case 462: if (!copy(String462, value)) return invalid("SERIALUI.DLL strings"); break;
            case 342: if (!copy(String342, value)) return invalid("SERIALUI.DLL strings"); break;
            case 208: if (!copy(String208, value)) return invalid("SERIALUI.DLL strings"); break;
            case 209: if (!copy(String209, value)) return invalid("SERIALUI.DLL strings"); break;
            case 210: if (!copy(String210, value)) return invalid("SERIALUI.DLL strings"); break;
            case 211: if (!copy(String211, value)) return invalid("SERIALUI.DLL strings"); break;
            case 213: if (!copy(String213, value)) return invalid("SERIALUI.DLL strings"); break;
            case 214: if (!copy(String214, value)) return invalid("SERIALUI.DLL strings"); break;
            case 215: if (!copy(String215, value)) return invalid("SERIALUI.DLL strings"); break;
            case 216: if (!copy(String216, value)) return invalid("SERIALUI.DLL strings"); break;
            case 217: if (!copy(String217, value)) return invalid("SERIALUI.DLL strings"); break;
            case 218: if (!copy(String218, value)) return invalid("SERIALUI.DLL strings"); break;
            case 219: if (!copy(String219, value)) return invalid("SERIALUI.DLL strings"); break;
            case 220: if (!copy(String220, value)) return invalid("SERIALUI.DLL strings"); break;
            case 221: if (!copy(String221, value)) return invalid("SERIALUI.DLL strings"); break;
            case 222: if (!copy(String222, value)) return invalid("SERIALUI.DLL strings"); break;
            case 200: if (!copy(String200, value)) return invalid("SERIALUI.DLL strings"); break;
            case 201: if (!copy(String201, value)) return invalid("SERIALUI.DLL strings"); break;
            case 202: if (!copy(String202, value)) return invalid("SERIALUI.DLL strings"); break;
            case 203: if (!copy(String203, value)) return invalid("SERIALUI.DLL strings"); break;
            case 204: if (!copy(String204, value)) return invalid("SERIALUI.DLL strings"); break;
            case 205: if (!copy(String205, value)) return invalid("SERIALUI.DLL strings"); break;
            case 206: if (!copy(String206, value)) return invalid("SERIALUI.DLL strings"); break;
            case 207: if (!copy(String207, value)) return invalid("SERIALUI.DLL strings"); break;
            case 240: if (!copy(String240, value)) return invalid("SERIALUI.DLL strings"); break;
            case 241: if (!copy(String241, value)) return invalid("SERIALUI.DLL strings"); break;
            case 242: if (!copy(String242, value)) return invalid("SERIALUI.DLL strings"); break;
            case 243: if (!copy(String243, value)) return invalid("SERIALUI.DLL strings"); break;
            case 244: if (!copy(String244, value)) return invalid("SERIALUI.DLL strings"); break;
            case 250: if (!copy(String250, value)) return invalid("SERIALUI.DLL strings"); break;
            case 251: if (!copy(String251, value)) return invalid("SERIALUI.DLL strings"); break;
            case 252: if (!copy(String252, value)) return invalid("SERIALUI.DLL strings"); break;
            case 253: if (!copy(String253, value)) return invalid("SERIALUI.DLL strings"); break;
            case 254: if (!copy(String254, value)) return invalid("SERIALUI.DLL strings"); break;
            case 255: if (!copy(String255, value)) return invalid("SERIALUI.DLL strings"); break;
            case 256: if (!copy(String256, value)) return invalid("SERIALUI.DLL strings"); break;
            case 257: if (!copy(String257, value)) return invalid("SERIALUI.DLL strings"); break;
            case 260: if (!copy(String260, value)) return invalid("SERIALUI.DLL strings"); break;
            case 261: if (!copy(String261, value)) return invalid("SERIALUI.DLL strings"); break;
            case 262: if (!copy(String262, value)) return invalid("SERIALUI.DLL strings"); break;
            }
        }
        if (!strings.ok) return invalid("SERIALUI.DLL strings");
    }
    if (!OriginalSerialUiAssets::String462.size) return invalid("SERIALUI.DLL strings");
    if (!OriginalSerialUiAssets::String342.size) return invalid("SERIALUI.DLL strings");
    if (!OriginalSerialUiAssets::String208.size) return invalid("SERIALUI.DLL strings");
    if (!OriginalSerialUiAssets::String209.size) return invalid("SERIALUI.DLL strings");
    if (!OriginalSerialUiAssets::String210.size) return invalid("SERIALUI.DLL strings");
    if (!OriginalSerialUiAssets::String211.size) return invalid("SERIALUI.DLL strings");
    if (!OriginalSerialUiAssets::String213.size) return invalid("SERIALUI.DLL strings");
    if (!OriginalSerialUiAssets::String214.size) return invalid("SERIALUI.DLL strings");
    if (!OriginalSerialUiAssets::String215.size) return invalid("SERIALUI.DLL strings");
    if (!OriginalSerialUiAssets::String216.size) return invalid("SERIALUI.DLL strings");
    if (!OriginalSerialUiAssets::String217.size) return invalid("SERIALUI.DLL strings");
    if (!OriginalSerialUiAssets::String218.size) return invalid("SERIALUI.DLL strings");
    if (!OriginalSerialUiAssets::String219.size) return invalid("SERIALUI.DLL strings");
    if (!OriginalSerialUiAssets::String220.size) return invalid("SERIALUI.DLL strings");
    if (!OriginalSerialUiAssets::String221.size) return invalid("SERIALUI.DLL strings");
    if (!OriginalSerialUiAssets::String222.size) return invalid("SERIALUI.DLL strings");
    if (!OriginalSerialUiAssets::String200.size) return invalid("SERIALUI.DLL strings");
    if (!OriginalSerialUiAssets::String201.size) return invalid("SERIALUI.DLL strings");
    if (!OriginalSerialUiAssets::String202.size) return invalid("SERIALUI.DLL strings");
    if (!OriginalSerialUiAssets::String203.size) return invalid("SERIALUI.DLL strings");
    if (!OriginalSerialUiAssets::String204.size) return invalid("SERIALUI.DLL strings");
    if (!OriginalSerialUiAssets::String205.size) return invalid("SERIALUI.DLL strings");
    if (!OriginalSerialUiAssets::String206.size) return invalid("SERIALUI.DLL strings");
    if (!OriginalSerialUiAssets::String207.size) return invalid("SERIALUI.DLL strings");
    if (!OriginalSerialUiAssets::String240.size) return invalid("SERIALUI.DLL strings");
    if (!OriginalSerialUiAssets::String241.size) return invalid("SERIALUI.DLL strings");
    if (!OriginalSerialUiAssets::String242.size) return invalid("SERIALUI.DLL strings");
    if (!OriginalSerialUiAssets::String243.size) return invalid("SERIALUI.DLL strings");
    if (!OriginalSerialUiAssets::String244.size) return invalid("SERIALUI.DLL strings");
    if (!OriginalSerialUiAssets::String250.size) return invalid("SERIALUI.DLL strings");
    if (!OriginalSerialUiAssets::String251.size) return invalid("SERIALUI.DLL strings");
    if (!OriginalSerialUiAssets::String252.size) return invalid("SERIALUI.DLL strings");
    if (!OriginalSerialUiAssets::String253.size) return invalid("SERIALUI.DLL strings");
    if (!OriginalSerialUiAssets::String254.size) return invalid("SERIALUI.DLL strings");
    if (!OriginalSerialUiAssets::String255.size) return invalid("SERIALUI.DLL strings");
    if (!OriginalSerialUiAssets::String256.size) return invalid("SERIALUI.DLL strings");
    if (!OriginalSerialUiAssets::String257.size) return invalid("SERIALUI.DLL strings");
    if (!OriginalSerialUiAssets::String260.size) return invalid("SERIALUI.DLL strings");
    if (!OriginalSerialUiAssets::String261.size) return invalid("SERIALUI.DLL strings");
    if (!OriginalSerialUiAssets::String262.size) return invalid("SERIALUI.DLL strings");
    if (!file("MMSYSTEM.DLL", data)) return false;
    std::vector<Resource> multimedia;
    if (!resources(data, multimedia)) return invalid("MMSYSTEM.DLL");
    for (const auto &resource : multimedia) {
        if (resource.type != 6) continue;
        Reader strings{resource.bytes}; unsigned at = 0;
        for (unsigned i = 0; i < 16 && strings.ok; ++i) {
            unsigned length = strings.get(at++, 1);
            Bytes value = strings.slice(at, length); at += length;
            if (length) OriginalMultimediaMessages.push_back({(resource.id - 1) * 16 + i, keep(std::string(value.begin(), value.end()))});
        }
        if (!strings.ok) return invalid("MMSYSTEM.DLL strings");
    }
    if (OriginalMultimediaMessages.empty()) return invalid("MMSYSTEM.DLL strings");
    if (!file("COMCTL32.DLL", data)) return false;
    Reader comctl{data};
    if (!copy(OriginalPropertySheetAssets::Template1006, comctl.peResource(5, 1006, language)) ||
        !copy(OriginalPropertySheetAssets::Template1020, comctl.peResource(5, 1020, language))) return invalid("COMCTL32.DLL dialogs");
    Bytes caption = comctl.peResource(6, 261, language); Reader cap{caption}; unsigned at = 0;
    for (unsigned i = 0; i < 2; ++i) at += 2 + cap.get(at, 2) * 2;
    unsigned length = cap.get(at, 2); Bytes value = cap.slice(at + 2, length * 2); word(value, 0);
    if (!copy(OriginalPropertySheetAssets::CaptionFormat, value) || !cap.ok || !comctl.ok) return invalid("COMCTL32.DLL caption");
    Bytes locale, crt;
    if (!file("LOCALE.NLS", locale) || !file("MSVCRT.DLL", crt)) return false;
    if (!loadLocale(locale, crt)) return invalid("LOCALE.NLS / MSVCRT.DLL");
    if (!file("COMDLG32.DLL", data)) return false;
    for (const char *name : {"VGAFIX.FON", "SSERIFE.FON", "MARLETT.TTF"}) {
        std::string path = std::string("C:\\WINDOWS\\FONTS\\") + name;
        if (!read(path.c_str(), data) || data.empty()) {
            error = "Missing Windows font " + path + ". Run the setup assistant."; return false;
        }
        identify(data);
    }
    return true;
}
} // namespace HostWindowsData
