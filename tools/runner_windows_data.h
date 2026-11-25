// SPDX-License-Identifier: GPL-2.0-or-later
// The Windows data of a test runner that links the sources of src/ the way
// the hosts build them (NEO98_RUNTIME_CONTENT): the tables come out of the
// files of an installed Windows 98 when the runner starts
// (tools/host_windows_data.h), not out of headers that tools of the private
// repository generate, and the fonts of C:\WINDOWS\FONTS out of the drive,
// which the runner mounts as the hosts do. NEO98_RUNNER_DRIVE names the folder
// of that drive C:; the Python side of a test sets it to setup's drive
// (tools/runner_windows.py).
#pragma once
#include "host_windows_data.h"
#include "system_tree_host.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace RunnerWindowsData
{
// The file of a guest path C:\... under the folder of the drive, each part
// of the path found without regard to case, as Windows finds it.
inline bool Read(const std::filesystem::path &drive, const char *guest, HostWindowsData::Bytes &bytes)
{
    if (!guest || std::toupper(static_cast<unsigned char>(guest[0])) != 'C' || guest[1] != ':' || guest[2] != '\\')
        return false;
    std::filesystem::path at = drive;
    std::string rest = guest + 3;
    for (size_t from = 0; from <= rest.size();)
    {
        size_t end = rest.find('\\', from);
        if (end == std::string::npos) end = rest.size();
        const std::string part = rest.substr(from, end - from);
        from = end + 1;
        if (part.empty()) continue;
        bool found = false;
        std::error_code error;
        for (const auto &entry : std::filesystem::directory_iterator(at, error))
        {
            const std::string name = entry.path().filename().string();
            if (name.size() != part.size()) continue;
            bool same = true;
            for (size_t i = 0; i < name.size() && same; ++i)
                same = std::toupper(static_cast<unsigned char>(name[i])) == std::toupper(static_cast<unsigned char>(part[i]));
            if (same) { at = entry.path(); found = true; break; }
        }
        if (!found) return false;
    }
    std::ifstream stream(at, std::ios::binary);
    if (!stream) return false;
    bytes.assign(std::istreambuf_iterator<char>(stream), {});
    return true;
}

// Loads the tables from the drive NEO98_RUNNER_DRIVE names and mounts it as
// drive C:; false, with the reason on stderr, without one.
inline bool Load()
{
    const char *drive = getenv("NEO98_RUNNER_DRIVE");
    if (!drive || !*drive)
    {
        fprintf(stderr, "NEO98_RUNNER_DRIVE names no drive C: with Windows 98\n");
        return false;
    }
    std::string error;
    if (!HostWindowsData::Load([&](const char *path, HostWindowsData::Bytes &bytes) { return Read(drive, path, bytes); },
                               error))
    {
        fprintf(stderr, "%s\n", error.c_str());
        return false;
    }
    if (!SystemTreeHost::Mount(drive))
    {
        fprintf(stderr, "Cannot mount %s as drive C:\n", drive);
        return false;
    }
    return true;
}
}
