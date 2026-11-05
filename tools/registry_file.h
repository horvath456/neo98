// SPDX-License-Identifier: GPL-2.0-or-later
// Host file behind the registry change journal of src/registry_persist.cpp.
//
// It is a plain file, deliberately separate from the file volume: a run can
// have persistent files without a persistent registry and the other way round,
// which is what the tests need. The default path lies next to drive C:, so a
// removed drive directory takes the registry with it.
#pragma once
#include "../src/registry.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>

namespace RegistryFile
{
class File final : public Registry::Storage
{
  public:
    bool Open(const char *path)
    {
        name = path ? path : "";
        return !name.empty();
    }
    unsigned Size() override
    {
        struct stat info;
        return name.empty() || stat(name.c_str(), &info) || !S_ISREG(info.st_mode)
                   ? 0u
                   : unsigned(info.st_size);
    }
    bool Load(unsigned char *buffer, unsigned size) override
    {
        FILE *file = fopen(name.c_str(), "rb");
        if (!file)
            return false;
        const bool okay = fread(buffer, 1, size, file) == size;
        fclose(file);
        return okay;
    }
    bool Save(const unsigned char *buffer, unsigned size) override
    {
        const std::string temporary = name + ".new";
        FILE *file = fopen(temporary.c_str(), "wb");
        if (!file)
            return false;
        const bool written = fwrite(buffer, 1, size, file) == size;
        const bool closed = fclose(file) == 0;
        if (!written || !closed)
            return false;
        return rename(temporary.c_str(), name.c_str()) == 0;
    }
    const char *Path() const { return name.c_str(); }

  private:
    std::string name;
};

// Command line: --registry FILE turns persistence on, --no-registry-file off.
// `drive` is the directory of C:, used for the default path.
inline bool Attach(int argc, char **argv, const char *drive)
{
    static File file;
    const char *path = nullptr;
    bool disabled = false;
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--registry") && i + 1 < argc)
            path = argv[++i];
        else if (!strcmp(argv[i], "--no-registry-file"))
            disabled = true;
    }
    static std::string fallback;
    if (!path && !disabled && drive)
    {
        // Beside the drive directory, not inside it: the guest must not see
        // its own registry as one of its files.
        fallback = std::string(drive) + ".registry";
        path = fallback.c_str();
    }
    if (disabled || !path || !file.Open(path))
        return false;
    Registry::Attach(&file);
    // The journal is written at RegFlushKey and once when the run ends. It was
    // constructed by the call above, so it outlives this handler.
    atexit([] { Registry::Flush(); });
    return true;
}
} // namespace RegistryFile
