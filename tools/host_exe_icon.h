// SPDX-License-Identifier: GPL-2.0-or-later
// The icon in the resources of a 32-bit program file, read on this Mac
// without the guest.
// A game machine carries the icon of its game from the moment it is made: in
// the Finder on its launcher bundle and in the Dock when it starts. The icon
// is the one Explorer shows for the file, the first RT_GROUP_ICON of its
// resources, in its largest picture and there in its deepest colours. A
// 16-bit or DOS program has none here; its game machine takes the icon of the
// first window as before (serve_game_icon in tools/neo98_host.cpp).
//
// Nothing here needs AppKit.
#pragma once
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace HostExeIcon
{
// The picture as rows of ARGB, alpha 255 where the icon covers (0 to 255 for
// an icon with an alpha channel). False for a file without a usable icon.
inline bool Load(const std::string &file, int &width, int &height, std::vector<unsigned> &argb)
{
    std::vector<unsigned char> data;
    if (FILE *handle = fopen(file.c_str(), "rb"))
    {
        unsigned char block[65536];
        size_t got;
        while ((got = fread(block, 1, sizeof block, handle)) > 0 && data.size() < (64u << 20))
            data.insert(data.end(), block, block + got);
        fclose(handle);
    }
    const auto word = [&](size_t at) { return at + 2 <= data.size() ? unsigned(data[at] | data[at + 1] << 8) : 0u; };
    const auto dword = [&](size_t at) { return at + 4 <= data.size() ? word(at) | word(at + 2) << 16 : 0u; };
    if (data.size() < 0x40 || word(0) != 0x5a4d)
        return false;
    const size_t pe = dword(0x3c);
    if (dword(pe) != 0x4550 || word(pe + 24) != 0x10b)
        return false;
    const unsigned sections = word(pe + 6), optional = word(pe + 20);
    const unsigned resources = dword(pe + 24 + 96 + 2 * 8);
    // The file offset of a virtual address, 0 for none.
    const auto offset = [&](unsigned rva) -> size_t {
        for (unsigned i = 0; i < sections; ++i)
        {
            const size_t at = pe + 24 + optional + i * 40;
            const unsigned size = std::max(dword(at + 8), dword(at + 16)), start = dword(at + 12);
            if (rva >= start && rva < start + size)
                return dword(at + 20) + (rva - start);
        }
        return 0;
    };
    const size_t root = resources ? offset(resources) : 0;
    if (!root)
        return false;
    // The data of a resource: below `type` the entry with identifier `id`
    // (the first entry for id -1), and its first language.
    const auto resource = [&](unsigned type, int id, size_t &at, size_t &length) {
        size_t directory = root;
        for (int level = 0; level < 3; ++level)
        {
            const unsigned count = word(directory + 12) + word(directory + 14);
            unsigned next = 0;
            for (unsigned i = 0; i < count && !next; ++i)
            {
                const size_t entry = directory + 16 + i * 8;
                if (level == 0 && dword(entry) != type)
                    continue;
                if (level == 1 && id >= 0 && dword(entry) != unsigned(id))
                    continue;
                next = dword(entry + 4);
            }
            if (!next || (level < 2 && !(next & 0x80000000u)))
                return false;
            directory = root + (next & 0x7fffffffu);
        }
        at = offset(dword(directory));
        length = dword(directory + 4);
        return at && at + length <= data.size();
    };
    size_t group = 0, group_length = 0;
    if (!resource(14, -1, group, group_length) || group_length < 6 || word(group + 2) != 1)
        return false;
    // GRPICONDIRENTRY: width, height, colours, reserved, planes, bits, bytes, id.
    size_t picture = 0, picture_length = 0;
    unsigned best = 0;
    for (unsigned i = 0; i < word(group + 4) && group + 6 + (i + 1) * 14 <= group + group_length; ++i)
    {
        const size_t entry = group + 6 + i * 14;
        size_t at = 0, length = 0;
        if (!resource(3, int(word(entry + 12)), at, length) || length < 40 || dword(at) != 40)
            continue; // not there, or a PNG picture of a later Windows
        const unsigned side = unsigned(std::abs(int(dword(at + 4)))), bits = word(at + 14);
        if (!side || side > 256 || (bits != 1 && bits != 4 && bits != 8 && bits != 24 && bits != 32))
            continue;
        if (const unsigned score = side * 64 + bits; score > best)
        {
            best = score;
            picture = at;
            picture_length = length;
        }
    }
    if (!best)
        return false;
    // A DIB of twice the height: the colours, then the mask of one bit.
    width = int(dword(picture + 4));
    height = int(dword(picture + 8)) / 2;
    const unsigned bits = word(picture + 14);
    if (width <= 0 || height <= 0 || dword(picture + 16) != 0)
        return false;
    unsigned colours = bits <= 8 ? dword(picture + 32) : 0;
    if (bits <= 8 && (!colours || colours > (1u << bits)))
        colours = 1u << bits;
    const size_t palette = picture + 40, colour_rows = palette + colours * 4;
    const size_t colour_stride = ((size_t(width) * bits + 31) / 32) * 4, mask_stride = ((size_t(width) + 31) / 32) * 4;
    const size_t mask_rows = colour_rows + colour_stride * size_t(height);
    if (mask_rows > picture + picture_length)
        return false;
    const bool has_mask = mask_rows + mask_stride * size_t(height) <= picture + picture_length;
    argb.assign(size_t(width) * size_t(height), 0);
    // A picture of 32 bits with an alpha channel keeps its alpha; any other
    // is opaque where the mask covers.
    bool alpha = false;
    for (size_t at = colour_rows; bits == 32 && !alpha && at < mask_rows; at += 4)
        alpha = data[at + 3] != 0;
    for (int y = 0; y < height; ++y)
    {
        // The rows of a DIB run from the bottom up.
        const size_t row = colour_rows + colour_stride * size_t(height - 1 - y), mask = mask_rows + mask_stride * size_t(height - 1 - y);
        for (int x = 0; x < width; ++x)
        {
            unsigned colour = 0;
            if (bits == 32)
                colour = dword(row + size_t(x) * 4);
            else if (bits == 24)
                colour = data[row + size_t(x) * 3] | data[row + size_t(x) * 3 + 1] << 8 | data[row + size_t(x) * 3 + 2] << 16;
            else
            {
                const unsigned byte = data[row + size_t(x) * bits / 8];
                const unsigned index = bits == 8 ? byte : bits == 4 ? (x & 1 ? byte & 15 : byte >> 4) : (byte >> (7 - (x & 7))) & 1;
                colour = index < colours ? dword(palette + index * 4) : 0;
            }
            const bool covered = !has_mask || !((data[mask + size_t(x) / 8] >> (7 - (x & 7))) & 1);
            argb[size_t(y) * size_t(width) + size_t(x)] = alpha ? colour : covered ? (colour | 0xff000000u) : 0;
        }
    }
    return true;
}
} // namespace HostExeIcon
