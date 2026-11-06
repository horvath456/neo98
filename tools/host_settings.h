// SPDX-License-Identifier: GPL-2.0-or-later
// The settings of the Mac app and the machine folder; docs/BUILDING.md.
// Started without the two leading arguments EXE and FONT, neo98-host is the
// app: it reads ~/Library/Application Support/Neo98/settings.ini, finds its
// font and the DOS machine beside itself and starts Explorer from drive C: of
// the machine folder the user chose. Everything tools/host_run.py passes on
// the command line comes from here then. Started with EXE and FONT, as
// `neo98.py run` and every test do, nothing here is read or written.
// settings.ini is plain text, one `name = value` per line, `#` starts a
// comment:
// machine = /path/to/Neo98 the machine folder
// scale = 1 View > Scale
// fullscreen = 0 View > Fullscreen
// online = 1 Network > Internet connected
// net.listen = 127.0.0.1 Network > Allow LAN connections; any opens all interfaces
// net.wifi = 0 Network > Wi-Fi in Windows
// audio.microphone = 1 Machine > Microphone in Windows
// pause.unfocused = 0 Machine > Pause when Neo98 loses focus
// cdrom = /path/to.iso the disc in the CD-ROM drive
// drive.D = /path/to/folder further fixed drives, D: to Z:
// com.2 = tcp:127.0.0.1:7898 where COM1 to COM4 lead
// open.ask = 1 a program opened from the Finder is asked about once:
// normally or as a game machine (docs/BUILDING.md, "Double-click")
// open.normal = /pfad/zu/SPIEL.EXE one line per program that was answered "normally"
// A game machine (tools/host_game_machine.h) is a
// machine folder with a settings.ini of its own in it, which the host reads
// in place of the one above (FileOverride). It has these lines as well:
// game.name = Kroko-Mania the name of the game machine
// game.program = C:\SPIELE\KROKO.EXE the one program it runs
// game.arguments = -window what follows the program on its command line
// game.directory = C:\SPIELE the folder it starts in; without it the program's
// game.file = /path/to/Neo98/C/SPIELE/KROKO.EXE the program file on this Mac
// game.source = /path/to/Neo98 the machine folder it was made from
// drives.readonly = 1 D: to Z: are mounted read-only
// The machine folder holds drive C: as the ordinary folder `C`, the registry
// journal `C.registry` beside it and the print jobs under `Print Output`. A
// folder that is itself a Win98 folder of setup (it has.neo98-setup.json,
// such as build/win98-c of a checkout) is taken as drive C: directly, with
// its journal beside it.
// NEO98_SETTINGS_DIR and NEO98_LOG_DIR name other places than
// ~/Library/Application Support/Neo98 and ~/Library/Logs/Neo98 (tests).
#pragma once
#include "legacy_data.h"
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace HostSettings
{
struct Settings
{
    std::string machine;
    int scale = 1;
    bool fullscreen = false, online = true;
    std::string net_listen = "127.0.0.1";
    bool wifi = false;
    // Windows sees the microphone of the host as its recording device.
    bool microphone = true;
    // The machine pauses while another application is in front.
    bool pause_unfocused = false;
    // View > Seamless, Show taskbar, One Mac window per guest window
    // The app starts with the Windows desktop in one window; View > Seamless
    // turns the seamless view on and the choice is kept.
    bool seamless = false,
        seamless_taskbar = false, seamless_windows = true, seamless_icons = false, seamless_dock = true;
    // The wheel for the window with the focus, as under Win98, instead of
    // the window under the pointer.
    bool wheel_focus = false;
    // A line a scroll gesture in the session log (tools/host_wheel.h); 0 is off.
    bool wheel_log = true;
    // A desktop size of its own, "WIDTHxHEIGHT";
    // empty for the size the registry of the machine names.
    std::string desktop;
    std::string cdrom;
    std::string drives[26]; // index letter - 'A'; C: is the machine's
    // Machine > Serial ports: the endpoint of COM1 to COM4, empty
    // for a port that leads nowhere outside.
    std::string com[4];
    // A game machine: the one program it runs. Empty
    // game_program for a normal machine.
    std::string game_name, game_program, game_arguments, game_directory, game_source;
    // The program file of the game on this Mac, by which a file opened in the
    // Finder finds its game machines.
    std::string game_file;
    // A program opened from the Mac (a double click in the Finder) is asked
    // about once: normally or as a game machine. open_normal lists the
    // program files that were answered "normally" (docs/BUILDING.md).
    bool open_ask = true;
    std::vector<std::string> open_normal;
    // The drives D: to Z: are mounted read-only (a game machine uses those of
    // the normal machine and must not change them).
    bool drives_readonly = false;
    bool found = false;     // settings.ini existed
};

inline std::string Home()
{
    const char *home = getenv("HOME");
    return home && *home ? home : "/";
}
inline std::string DefaultMachine();
inline std::string Directory()
{
    const char *other = getenv("NEO98_SETTINGS_DIR");
    if (other && *other) return other;
    (void)DefaultMachine();
#ifdef __APPLE__
    return LegacyData::MigrateDirectory(Home() + "/Library/Application Support/" + LegacyData::Brand(),
                                        Home() + "/Library/Application Support/Neo98");
#else
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const std::string base = xdg && *xdg ? std::string(xdg) : Home() + "/.config";
    return LegacyData::MigrateDirectory(base + "/" + LegacyData::Brand(true), base + "/neo98");
#endif
}
inline std::string LogDirectory()
{
    const char *other = getenv("NEO98_LOG_DIR");
    if (other && *other) return other;
#ifdef __APPLE__
    return LegacyData::MigrateDirectory(Home() + "/Library/Logs/" + LegacyData::Brand(),
                                        Home() + "/Library/Logs/Neo98");
#else
    const char *xdg = getenv("XDG_STATE_HOME");
    const std::string base = xdg && *xdg ? std::string(xdg) : Home() + "/.local/state";
    return LegacyData::MigrateDirectory(base + "/" + LegacyData::Brand(true), base + "/neo98");
#endif
}
// The settings.ini of a game machine, read and written in place of the app's.
inline std::string &FileOverride()
{
    static std::string file;
    return file;
}
inline std::string File() { return FileOverride().empty() ? Directory() + "/settings.ini" : FileOverride(); }
inline std::string DefaultMachine()
{
    const std::string old = Home() + "/" + LegacyData::Brand();
    std::error_code ec;
    if (std::filesystem::exists(LegacyData::ReadPath(old + "/C/.neo98-setup.json"), ec))
        return LegacyData::MigrateDirectory(old, Home() + "/Neo98");
    return Home() + "/Neo98";
}

inline bool Exists(const std::string &path)
{
    struct stat info;
    return !path.empty() && !stat(path.c_str(), &info);
}
inline bool IsDirectory(const std::string &path)
{
    struct stat info;
    return !path.empty() && !stat(path.c_str(), &info) && S_ISDIR(info.st_mode);
}
// mkdir -p.
inline bool MakeDirectories(const std::string &path)
{
    for (size_t at = 1; at <= path.size(); ++at)
        if (at == path.size() || path[at] == '/')
        {
            const std::string part = path.substr(0, at);
            if (mkdir(part.c_str(), 0755) && errno != EEXIST)
                return false;
        }
    return true;
}

// Drive C: of a machine folder: the folder itself when setup made it, else
// its subfolder C.
inline std::string DriveC(const std::string &machine)
{
    if (machine.empty())
        return std::string();
    if (Exists(LegacyData::ReadPath(machine + "/.neo98-setup.json")))
        return machine;
    return machine + "/C";
}
// Whether setup has finished drive C: of this machine folder.
inline bool Ready(const std::string &machine)
{
    return !machine.empty() && Exists(LegacyData::ReadPath(DriveC(machine) + "/.neo98-setup.json"));
}
// Keep existing print jobs in the legacy folder when upgrading a machine.
inline std::string MachinePrintDirectory(const std::string &machine)
{
    const auto legacy = machine + "/Druckausgabe";
    return IsDirectory(legacy) ? legacy : machine + "/Print Output";
}

// Where the print jobs of LPT1 go.
inline std::string PrintDirectory(const std::string &machine)
{
    if (DriveC(machine) == machine)
        return machine + ".print-output";
    return MachinePrintDirectory(machine);
}

inline std::string Trim(const std::string &text)
{
    size_t from = 0, to = text.size();
    while (from < to && (text[from] == ' ' || text[from] == '\t'))
        ++from;
    while (to > from && (text[to - 1] == ' ' || text[to - 1] == '\t' || text[to - 1] == '\r' || text[to - 1] == '\n'))
        --to;
    return text.substr(from, to - from);
}

inline bool Load(Settings &settings)
{
    settings = Settings();
    FILE *file = fopen(File().c_str(), "r");
    if (!file)
        return false;
    settings.found = true;
    char line[4096];
    while (fgets(line, sizeof line, file))
    {
        const std::string text = Trim(line);
        if (text.empty() || text[0] == '#')
            continue;
        const auto equals = text.find('=');
        if (equals == std::string::npos)
            continue;
        const std::string name = Trim(text.substr(0, equals)), value = Trim(text.substr(equals + 1));
        if (name == "machine")
            settings.machine = value;
        else if (name == "scale")
            settings.scale = atoi(value.c_str()) == 2 ? 2 : 1;
        else if (name == "desktop")
            settings.desktop = value;
        else if (name == "fullscreen")
            settings.fullscreen = atoi(value.c_str()) != 0;
        else if (name == "seamless")
            settings.seamless = atoi(value.c_str()) != 0;
        else if (name == "seamless.taskbar")
            settings.seamless_taskbar = atoi(value.c_str()) != 0;
        else if (name == "seamless.windows")
            settings.seamless_windows = atoi(value.c_str()) != 0;
        else if (name == "wheel.focus")
            settings.wheel_focus = atoi(value.c_str()) != 0;
        else if (name == "wheel.log")
            settings.wheel_log = atoi(value.c_str()) != 0;
        else if (name == "seamless.icons")
            settings.seamless_icons = atoi(value.c_str()) != 0;
        else if (name == "seamless.dock")
            settings.seamless_dock = atoi(value.c_str()) != 0;
        else if (name == "net.listen")
            settings.net_listen = value;
        else if (name == "net.wifi")
            settings.wifi = atoi(value.c_str()) != 0;
        else if (name == "audio.microphone")
            settings.microphone = atoi(value.c_str()) != 0;
        else if (name == "online")
            settings.online = atoi(value.c_str()) != 0;
        else if (name == "pause.unfocused")
            settings.pause_unfocused = atoi(value.c_str()) != 0;
        else if (name == "cdrom")
            settings.cdrom = value;
        else if (name == "game.name")
            settings.game_name = value;
        else if (name == "game.program")
            settings.game_program = value;
        else if (name == "game.arguments")
            settings.game_arguments = value;
        else if (name == "game.directory")
            settings.game_directory = value;
        else if (name == "game.source")
            settings.game_source = value;
        else if (name == "game.file")
            settings.game_file = value;
        else if (name == "open.ask")
            settings.open_ask = atoi(value.c_str()) != 0;
        else if (name == "open.normal")
        {
            if (!value.empty())
                settings.open_normal.push_back(value);
        }
        else if (name == "drives.readonly")
            settings.drives_readonly = atoi(value.c_str()) != 0;
        else if (name.size() == 7 && !name.compare(0, 6, "drive."))
        {
            const char letter = char(name[6] & ~0x20);
            if (letter >= 'D' && letter <= 'Z')
                settings.drives[letter - 'A'] = value;
        }
        else if (name.size() == 5 && !name.compare(0, 4, "com.") && name[4] >= '1' && name[4] <= '4')
            settings.com[name[4] - '1'] = value;
    }
    fclose(file);
    return true;
}

inline std::string Parent(const std::string &path);
// Written to a file beside it and renamed, so a crash leaves the old one.
inline bool Save(const Settings &settings)
{
    if (!MakeDirectories(Parent(File())))
        return false;
    const std::string path = File(), temporary = path + ".neu";
    FILE *file = fopen(temporary.c_str(), "w");
    if (!file)
        return false;
    fprintf(file, "# Neo98 settings\n");
    fprintf(file, "machine = %s\n", settings.machine.c_str());
    fprintf(file, "scale = %d\n", settings.scale);
    if (!settings.desktop.empty()) fprintf(file, "desktop = %s\n", settings.desktop.c_str());
    fprintf(file, "fullscreen = %d\n", settings.fullscreen ? 1 : 0);
    fprintf(file, "seamless = %d\n", settings.seamless ? 1 : 0);
    fprintf(file, "seamless.taskbar = %d\n", settings.seamless_taskbar ? 1 : 0);
    fprintf(file, "seamless.windows = %d\n", settings.seamless_windows ? 1 : 0);
    fprintf(file, "seamless.icons = %d\n", settings.seamless_icons ? 1 : 0);
    if (settings.wheel_focus) fprintf(file, "wheel.focus = 1\n");
    if (!settings.wheel_log) fprintf(file, "wheel.log = 0\n");
    fprintf(file, "seamless.dock = %d\n", settings.seamless_dock ? 1 : 0);
    fprintf(file, "online = %d\n", settings.online ? 1 : 0);
    fprintf(file, "net.listen = %s\n", settings.net_listen.c_str());
    fprintf(file, "net.wifi = %d\n", settings.wifi ? 1 : 0);
    fprintf(file, "audio.microphone = %d\n", settings.microphone ? 1 : 0);
    fprintf(file, "pause.unfocused = %d\n", settings.pause_unfocused ? 1 : 0);
    fprintf(file, "cdrom = %s\n", settings.cdrom.c_str());
    for (char letter = 'D'; letter <= 'Z'; ++letter)
        if (!settings.drives[letter - 'A'].empty())
            fprintf(file, "drive.%c = %s\n", letter, settings.drives[letter - 'A'].c_str());
    for (int port = 0; port < 4; ++port)
        if (!settings.com[port].empty())
            fprintf(file, "com.%d = %s\n", port + 1, settings.com[port].c_str());
    if (!settings.game_program.empty())
    {
        fprintf(file, "# Game machine settings\n");
        fprintf(file, "game.name = %s\n", settings.game_name.c_str());
        fprintf(file, "game.program = %s\n", settings.game_program.c_str());
        if (!settings.game_arguments.empty()) fprintf(file, "game.arguments = %s\n", settings.game_arguments.c_str());
        if (!settings.game_directory.empty()) fprintf(file, "game.directory = %s\n", settings.game_directory.c_str());
        if (!settings.game_file.empty()) fprintf(file, "game.file = %s\n", settings.game_file.c_str());
        fprintf(file, "game.source = %s\n", settings.game_source.c_str());
    }
    else
    {
        // The question of the Finder belongs to the normal machine.
        fprintf(file, "open.ask = %d\n", settings.open_ask ? 1 : 0);
        for (const auto &program : settings.open_normal)
            fprintf(file, "open.normal = %s\n", program.c_str());
    }
    if (settings.drives_readonly) fprintf(file, "drives.readonly = 1\n");
    const bool okay = fclose(file) == 0;
    return okay && !rename(temporary.c_str(), path.c_str());
}

// The path of this program, links resolved.
inline std::string Executable()
{
    char raw[PATH_MAX];
#ifdef __APPLE__
    uint32_t size = sizeof raw;
    if (_NSGetExecutablePath(raw, &size))
        return std::string();
#else
    const ssize_t size = readlink("/proc/self/exe", raw, sizeof raw - 1);
    if (size < 0) return {};
    raw[size] = 0;
#endif
    char real[PATH_MAX];
    return realpath(raw, real) ? real : raw;
}
inline std::string Parent(const std::string &path)
{
    const auto slash = path.rfind('/');
    return slash == std::string::npos ? std::string(".") : slash == 0 ? std::string("/") : path.substr(0, slash);
}
// Contents of the app bundle this program runs from, or empty outside one.
inline std::string BundleContents()
{
    const std::string directory = Parent(Executable());
    if (directory.size() > 6 && !directory.compare(directory.size() - 6, 6, "/MacOS"))
        return Parent(directory);
    return std::string();
}
// A file the host reads at run time: in the bundle under Contents/Resources,
// in a checkout under build/generated (the program is build/native-app/neo98-host).
inline std::string Resource(const char *name)
{
    const std::string contents = BundleContents();
    if (!contents.empty())
        return contents + "/Resources/" + name;
    return Parent(Parent(Executable())) + "/generated/" + name;
}
// The DOS machine in the bundle; outside one the path compiled into
// tools/host_dos_vm.cpp holds.
inline std::string BundledDosCore()
{
    const std::string contents = BundleContents();
    if (contents.empty())
        return std::string();
    const std::string path = contents + "/Frameworks/libdosbox-pure.dylib";
    return Exists(path) ? path : std::string();
}

// Whether the command line is one of the app (no EXE and FONT in front).
inline bool AppMode(int argc, char **argv) { return argc < 2 || (argv[1][0] == '-' && argv[1][1]); }

// Started from the Finder, standard output is /dev/null. The session log then
// goes to <logs>/host-session.log; the one of the run before moves to
// host-session.prev.log, as tools/host_run.py keeps them. A restart (execv)
// inherits the open file and appends.
inline bool RedirectLog()
{
    struct stat info;
    const bool nowhere = !fstat(1, &info) && S_ISCHR(info.st_mode) && !isatty(1);
    const char *forced = getenv("NEO98_LOG_TO_FILE");
    if (!nowhere && !(forced && *forced == '1'))
        return false;
    const std::string directory = LogDirectory();
    if (!MakeDirectories(directory))
        return false;
    const std::string log = directory + "/host-session.log";
    rename(log.c_str(), (directory + "/host-session.prev.log").c_str());
    if (!freopen(log.c_str(), "a", stdout))
        return false;
    dup2(fileno(stdout), 2);
    setvbuf(stdout, nullptr, _IOLBF, 0);
    return true;
}
} // namespace HostSettings
