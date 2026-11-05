// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Read-only UDF 1.02 on an optical disc, ECMA-167 parts 3/4 and ECMA TR/112-7.
// Physical partition map, file entries, short/long allocation descriptors and
// embedded data. No VAT, sparable or metadata partitions (later UDF versions).
#include <algorithm>
#include <cstring>
#include <functional>
#include <string>
#include <strings.h>
#include <utility>
#include <vector>
namespace HostUdf
{
using U = unsigned;
struct Extent { U sector, bytes; bool recorded; };
struct Node
{
    bool directory = false;
    U size = 0;
    std::string name;
    unsigned char date[7] = {};
    std::vector<Extent> extents;
    std::vector<unsigned char> embedded;
};
class Volume
{
    using Reader = std::function<bool(U, unsigned char *, U, U)>;
    Reader read;
    U partition = 0, blocks = 0;
    Node root;
    static U Word(const unsigned char *p) { return p[0] | U(p[1]) << 8; }
    static U Dword(const unsigned char *p) { return Word(p) | Word(p + 2) << 16; }
    static bool Tag(const unsigned char *p, U id, U size = 2048)
    {
        U sum = 0;
        for (U i = 0; i < 16; ++i) if (i != 4) sum += p[i];
        const U length = Word(p + 10);
        if (Word(p) != id || (sum & 255) != p[4] || length > size - 16) return false;
        U crc = 0;
        for (U i = 16; i < 16 + length; ++i) {
            crc ^= U(p[i]) << 8;
            for (U bit = 0; bit < 8; ++bit) crc = ((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0)) & 0xffff;
        }
        return crc == Word(p + 8);
    }
    static std::string Name(const unsigned char *p, U size)
    {
        std::string text;
        if (!size || (p[0] != 8 && p[0] != 16) || (p[0] == 16 && !(size & 1))) return text;
        const U step = p[0] == 16 ? 2 : 1;
        for (U i = 1; i < size; i += step) {
            const U c = step == 2 ? U(p[i]) << 8 | p[i + 1] : p[i];
            // Same single-byte guest spelling as HostVolume.
            if (!c || c == '/' || c == '\\') return {};
            text += c <= 255 ? char(c) : '_';
        }
        return text;
    }
    bool Block(U relative, unsigned char *p) const
    {
        return relative < blocks && read(partition + relative, p, 0, 2048);
    }
    bool File(U relative, Node &node) const
    {
        unsigned char p[2048];
        if (!Block(relative, p)) return false;
        const bool extended = Tag(p, 266);
        if (!extended && !Tag(p, 261)) return false;
        if (Word(p + 20) != 4 || (p[27] != 4 && p[27] != 5)) return false;
        const U info = extended ? 64 : 56, attrs = extended ? 208 : 168, start = extended ? 216 : 176;
        if (Dword(p + info + 4)) return false; // Volume::Backend uses 32-bit file sizes.
        node = Node{};
        node.directory = p[27] == 4;
        node.size = Dword(p + info);
        const U date = extended ? 92 : 84;
        const U year = Word(p + date + 2);
        node.date[0] = year >= 1900 && year <= 2155 ? year - 1900 : 80;
        memcpy(node.date + 1, p + date + 4, 5);
        const U ea = Dword(p + attrs), ad = Dword(p + attrs + 4);
        if (ea > 2048 - start || ad > 2048 - start - ea) return false;
        const U kind = Word(p + 34) & 7;
        const unsigned char *data = p + start + ea;
        if (kind == 3) {
            if (node.size > ad) return false;
            node.embedded.assign(data, data + node.size);
            return true;
        }
        if (kind > 1) return false;
        const U step = kind ? 16 : 8;
        if (ad % step) return false;
        unsigned long long total = 0;
        for (U at = 0; at < ad; at += step) {
            const U length = Dword(data + at), bytes = length & 0x3fffffff, type = length >> 30;
            const U sector = Dword(data + at + 4);
            if (type == 3 || (kind && Word(data + at + 8))) return false;
            if (!bytes) continue;
            if (type < 2 && (sector >= blocks || (bytes + 2047ull) / 2048 > blocks - sector)) return false;
            node.extents.push_back({partition + sector, bytes, type == 0});
            total += bytes;
        }
        return total >= node.size;
    }
  public:
    bool Open(Reader reader)
    {
        read = std::move(reader);
        unsigned char p[2048];
        if (!read(256, p, 0, 2048) || !Tag(p, 2)) return false;
        const U bytes = Dword(p + 16), first = Dword(p + 20);
        if (!bytes || bytes > 1024 * 2048 || first > ~0u - bytes / 2048) return false;
        U part_id = ~0u, map_id = ~0u, fsd = ~0u;
        for (U at = 0; at < bytes / 2048; ++at) {
            if (!read(first + at, p, 0, 2048)) return false;
            if (Tag(p, 8)) break;
            if (Tag(p, 5)) {
                part_id = Word(p + 22);
                partition = Dword(p + 188); blocks = Dword(p + 192);
            } else if (Tag(p, 6)) {
                if (Dword(p + 212) != 2048 || Dword(p + 264) != 6 || Dword(p + 268) != 1 ||
                    p[440] != 1 || p[441] != 6 || Word(p + 256)) return false;
                map_id = Word(p + 444); fsd = Dword(p + 252);
            }
        }
        if (part_id == ~0u || part_id != map_id || !blocks || partition > ~0u - blocks ||
            !Block(fsd, p) || !Tag(p, 256) || Word(p + 408)) return false;
        return File(Dword(p + 404), root) && root.directory;
    }
    bool Read(const Node &node, U offset, unsigned char *out, U bytes) const
    {
        if (offset > node.size || bytes > node.size - offset) return false;
        if (!node.embedded.empty()) {
            memcpy(out, node.embedded.data() + offset, bytes); return true;
        }
        U done = 0;
        for (const auto &extent : node.extents) {
            if (offset >= extent.bytes) { offset -= extent.bytes; continue; }
            const U count = std::min(bytes - done, extent.bytes - offset);
            if (!extent.recorded) memset(out + done, 0, count);
            else for (U n = 0; n < count;) {
                const U inside = (offset + n) % 2048;
                const U take = std::min(count - n, 2048 - inside);
                if (!read(extent.sector + (offset + n) / 2048, out + done + n, inside, take)) return false;
                n += take;
            }
            done += count; offset = 0;
            if (done == bytes) return true;
        }
        return done == bytes;
    }
    bool List(const Node &node, std::vector<Node> &children) const
    {
        // A damaged image must not allocate an unbounded directory buffer.
        if (!node.directory || node.size > 16 * 1024 * 1024) return false;
        std::vector<unsigned char> data(node.size);
        if (!Read(node, 0, data.data(), node.size)) return false;
        for (U at = 0; at < node.size;) {
            if (node.size - at < 38) return false;
            const auto *p = data.data() + at;
            const U impl = Word(p + 36), name_size = p[19], length = (38 + impl + name_size + 3) & ~3u;
            if (length > node.size - at || !Tag(p, 257, length) || Word(p + 28)) return false;
            if (!(p[18] & (4 | 8))) { // deleted or parent entry
                Node child;
                if (!File(Dword(p + 24), child)) return false;
                child.name = Name(p + 38 + impl, name_size);
                if (child.name.empty()) return false;
                children.push_back(std::move(child));
            }
            at += length;
        }
        return true;
    }
    bool Walk(const char *absolute, Node &node) const
    {
        if (!absolute || !absolute[0] || absolute[1] != ':') return false;
        node = root;
        const char *p = absolute + 2;
        while (*p) {
            while (*p == '\\' || *p == '/') ++p;
            if (!*p) break;
            std::string part;
            while (*p && *p != '\\' && *p != '/') part += *p++;
            std::vector<Node> children;
            if (!List(node, children)) return false;
            bool found = false;
            for (const auto &child : children) if (!strcasecmp(part.c_str(), child.name.c_str())) {
                node = child; found = true; break;
            }
            if (!found) return false;
        }
        return true;
    }
};
} // namespace HostUdf
