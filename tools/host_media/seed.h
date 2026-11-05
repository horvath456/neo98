// SPDX-License-Identifier: GPL-2.0-or-later
// Media Center 98 goes to C:\WINDOWS, from the folder of the host program
// or its package (tools/build_host_media.py). Its shortcut goes to Programs
// > Accessories > Entertainment once, when the program is new on this C:; a
// shortcut the user deleted stays deleted. NETFLIX.EXE and KODI.EXE of
// earlier builds and their shortcuts go away.
#pragma once
#include "../host_settings.h"
#include "../../src/files.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>
namespace HostMediaSeed
{
inline bool Read(const std::filesystem::path &path, std::vector<unsigned char> &bytes)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return !bytes.empty();
}
inline bool Seed()
{
    struct Item { const char *program, *target, *shortcut, *link; };
    static const Item items[] = {
        {"MEDIACTR.EXE", "C:\\WINDOWS\\MEDIACTR.EXE", "MEDIACTR.LNK", "C:\\WINDOWS\\Start Menu\\Programs\\Accessories\\Entertainment\\Media Center 98.lnk"}};
    for (const char *old : {"C:\\WINDOWS\\NETFLIX.EXE", "C:\\WINDOWS\\KODI.EXE",
                            "C:\\WINDOWS\\Start Menu\\Programs\\Accessories\\Entertainment\\Netflix.lnk",
                            "C:\\WINDOWS\\Start Menu\\Programs\\Accessories\\Entertainment\\Kodi.lnk"})
        if (unsigned node = 0; !Files::System().Lookup(old, node)) Files::System().Remove(old, false);
    const auto exe = std::filesystem::path(HostSettings::Executable()).parent_path();
    for (const auto &folder : {exe / "../share/neo98", exe / "../Resources", exe / "../host-media"})
    {
        if (!std::filesystem::exists(folder / items[0].program)) continue;
        for (const auto &item : items)
        {
            std::vector<unsigned char> bytes;
            if (!Read(folder / item.program, bytes)) return false;
            unsigned node = 0;
            const bool known = !Files::System().Lookup(item.target, node);
            const auto error = Files::System().Seed(item.target, bytes.data(), unsigned(bytes.size()));
            if (error && error != 32) return false;
            if (!known && Read(folder / item.shortcut, bytes)) Files::System().Seed(item.link, bytes.data(), unsigned(bytes.size()));
        }
        return true;
    }
    return false;
}
}
