// SPDX-License-Identifier: GPL-2.0-or-later
// macOS/POSIX backend for the read-only Win98 system tree of src/system_tree.h.
// It lives outside src/ because it opens host files. The tree is the Win98
// folder `python3 neo98.py setup --iso...` creates once (build/win98-c,
// README.md). The folder has the format of a host drive of
// tools/host_volume.h: the files under their long names, and in every
// directory the sidecar.neo98-index with the 8.3 aliases, DOS attribute bytes
// and creation times FAT kept. This backend is exactly that provider, opened
// read-only: it reads a directory's sidecar when a lookup first enters it and
// writes nothing, so a start neither scans nor hashes the tree.
// Include this header in a runner and call SystemTreeHost::Mount before the
// first use of Files::System.
#pragma once
#include "../src/system_tree.h"
#include "host_volume.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace SystemTreeHost
{
// The folder setup writes; NEO98_WIN98_ROOT names another one.
inline const char *DefaultRoot() { return HostVolume::DefaultDriveC(); }
// Setup writes this file last. A folder without it is not ready.
constexpr const char *Marker = ".neo98-setup.json";
inline bool Ready(const char *root)
{
    struct stat info;
    return root && !stat(LegacyData::ReadPath(std::string(root) + "/" + Marker).c_str(), &info);
}
inline void ExplainSetup(const char *root)
{
    fprintf(stderr, "No configured Win98 folder at %s. Run setup first:\n"
                    "    python3 neo98.py setup --iso '/pfad/zu/Windows 98 Second Edition.iso'\n",
            root);
}
using HostVolume::SameDirectory;

class Tree final : public SystemTree::Backend
{
  public:
    bool Load(const char *root_directory)
    {
        root = root_directory;
        struct stat info;
        if (stat(LegacyData::ReadPath(root + "/.neo98-index").c_str(), &info))
            return false;
        description = root + " (read-only)";
        return drive.Open(root_directory, false, "NEO98");
    }
    bool Find(const char *absolute, SystemTree::Entry &out) override
    {
        Volume::Entry entry;
        if (!Local(absolute) || !drive.Find(absolute, entry) || entry.tombstone)
            return false;
        Fill(entry, out);
        return true;
    }
    bool Child(const char *directory, unsigned index, SystemTree::Entry &out) override
    {
        Volume::Entry entry;
        if (!Local(directory) || !drive.Child(directory, index, entry))
            return false;
        Fill(entry, out);
        return true;
    }
    unsigned Read(const char *absolute, unsigned offset, void *buffer, unsigned size) override
    {
        unsigned done = 0;
        if (!Local(absolute) || drive.Read(absolute, offset, buffer, size, done))
            return 0;
        return done;
    }
    const char *Description() const override { return description.c_str(); }
    const char *Root() const { return root.c_str(); }
    HostVolume::Drive *Provider() { return &drive; }

  private:
    HostVolume::Drive drive;
    std::string root, description;
    static bool Local(const char *absolute)
    {
        return absolute && HostVolume::Fold((unsigned char)absolute[0]) == 'C' && absolute[1] == ':';
    }
    static void Fill(const Volume::Entry &entry, SystemTree::Entry &out)
    {
        out = SystemTree::Entry{};
        snprintf(out.name, sizeof out.name, "%s", entry.name);
        snprintf(out.alias, sizeof out.alias, "%s", entry.alias);
        out.attributes = entry.attributes;
        out.size = entry.size;
        for (unsigned i = 0; i < 3; ++i)
            out.times[i] = entry.times[i];
    }
};

// Mount the Win98 folder, or the fixture tree `root` names. Returns false when
// the folder is missing or setup has not finished it; a caller may go on with
// the plain RAM volume. Raises the volume limits, which only takes effect
// before the first use of Files::System.
inline bool Mount(const char *root = nullptr, unsigned nodes = 24576,
                  unsigned bytes = 192u * 1024 * 1024)
{
    static Tree tree;
    const char *base = root ? root : DefaultRoot();
    if (!root && !Ready(base))
    {
        ExplainSetup(base);
        return false;
    }
    if (!tree.Load(base))
        return false;
    Files::Configure(nodes, bytes);
    SystemTree::Mount(&tree);
    HostVolume::TreeRoot() = base;
    HostVolume::TreeDrive() = tree.Provider();
    return true;
}
// The folder of the mounted tree, or null.
inline const char *MountedRoot()
{
    auto *backend = SystemTree::Mounted();
    return backend ? static_cast<Tree *>(backend)->Root() : nullptr;
}

// What a runner needs from the command line before it touches Files::System:
// --system-tree [DIR] mounts the tree, --exe PATH names a program by guest path
// ("C:\\WINDOWS\\SOL.EXE") or by host path and implies the tree.
struct Options
{
    bool tree = false;
    const char *root = nullptr, *exe = nullptr;
};
inline Options Scan(int argc, char **argv)
{
    Options options;
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--system-tree"))
        {
            options.tree = true;
            if (i + 1 < argc && argv[i + 1][0] != '-')
                options.root = argv[++i];
            continue;
        }
        if (!strcmp(argv[i], "--exe") && i + 1 < argc)
        {
            options.exe = argv[++i];
            options.tree = true;
        }
    }
    return options;
}
inline bool GuestPath(const char *path)
{
    return path && path[0] && path[1] == ':' && (path[2] == '\\' || path[2] == '/');
}
// Read one file of the layered volume, so a program can start from the tree
// without a host copy. Win98 loads a program of any size its file system
// holds; the only bound here guards the host copy, and it is far above the
// 18 MB of KIDS50's Quiz.exe that the old 16 MiB bound refused.
inline bool ReadGuest(const char *guest, std::vector<unsigned char> &out)
{
    Files::Session session(Files::System());
    unsigned handle = 0, status = 0, size = 0, done = 0;
    if (session.Open(guest, 1, 1, 3, 0, handle, status) || session.Size(handle, size) || !size ||
        size > 1024u * 1024 * 1024)
    {
        if (handle != Files::Invalid)
            session.Close(handle);
        return false;
    }
    out.resize(size);
    const bool ok = !session.Read(handle, out.data(), size, done) && done == size;
    session.Close(handle);
    if (!ok)
        out.clear();
    return ok;
}
} // namespace SystemTreeHost
