// SPDX-License-Identifier: GPL-2.0-or-later
// Host file behind the guest clock (Application::ClockStorage, src/x86_time.cpp).
//
// A PC keeps the date and time Windows sets in its battery clock. The port
// never sets the host's clock; it keeps the distance between the guest clock
// and the host's local clock, in milliseconds, in a small text file beside
// drive C:, where the registry journal lies: `<drive>.clock`. The guest clock
// of the next start is the host's clock plus that distance, so it also ran
// on while the port was off. Removing the file gives the host's time again.
#pragma once
#include "../src/x86_app.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace ClockFile
{
class File final : public X86::Application::ClockStorage
{
  public:
    bool Open(const char *path)
    {
        name = path ? path : "";
        return !name.empty();
    }
    bool Load(long long &milliseconds) override
    {
        FILE *file = fopen(name.c_str(), "r");
        if (!file)
            return false;
        char line[64] = {};
        const bool read = fgets(line, sizeof line, file) != nullptr;
        fclose(file);
        if (!read)
            return false;
        char *end = nullptr;
        const long long value = strtoll(line, &end, 10);
        if (end == line || (*end && *end != '\n' && *end != '\r' && *end != ' '))
            return false;
        milliseconds = value;
        return true;
    }
    bool Save(long long milliseconds) override
    {
        const std::string temporary = name + ".new";
        FILE *file = fopen(temporary.c_str(), "w");
        if (!file)
            return false;
        const bool written = fprintf(file, "%lld\nguest clock minus host clock, in milliseconds; "
                                           "remove this file for the host's time\n", milliseconds) > 0;
        const bool closed = fclose(file) == 0;
        if (!written || !closed)
            return false;
        return rename(temporary.c_str(), name.c_str()) == 0;
    }
    const char *Path() const { return name.c_str(); }

  private:
    std::string name;
};

// Command line: --clock FILE names the file, --host-clock starts from the
// host's time and removes what was stored. `drive` is the directory of C:,
// used for the default path; a run without a registry file (--no-registry-file)
// keeps no clock either unless --clock names one. Call before
// Application::StartGuestClock.
inline bool Attach(int argc, char **argv, const char *drive)
{
    static File file;
    const char *path = nullptr;
    bool host = false, disabled = false;
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--clock") && i + 1 < argc)
            path = argv[++i];
        else if (!strcmp(argv[i], "--host-clock"))
            host = true;
        else if (!strcmp(argv[i], "--no-registry-file") || !strcmp(argv[i], "--ram-drive"))
            disabled = true;
    }
    static std::string fallback;
    if (!path && drive && !disabled)
    {
        fallback = std::string(drive) + ".clock";
        path = fallback.c_str();
    }
    if (!path || !file.Open(path))
        return false;
    if (host)
        remove(path);
    X86::Application::AttachGuestClock(&file);
    return true;
}
} // namespace ClockFile
