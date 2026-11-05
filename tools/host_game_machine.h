// SPDX-License-Identifier: GPL-2.0-or-later
// The game machine ("Game machine").
// A game machine is a second machine beside the normal one that runs one
// program and nothing else: no shell, no desktop, and it ends when the last
// process of that program has ended. It has a drive C: and a registry of its
// own, so what the game writes stays there. A later saved state of a game is
// the state of this whole machine plus its disk, which is why a game that is
// to get saved states runs in one.
// On disk it is a folder in the folder of game machines of the machine it
// was made from (its saved states lie in it as well, tools/host_savestate.h):
// <machine>/Spielmaschinen/<Name>/ (<machine>.Spielmaschinen/<Name>/ when
// the machine folder is drive C: itself)
// C-Schicht/ the write layer of its drive C:
// D-Schicht/... the write layers of its other hard disks, each
// made by the first write to that drive
// C-Schicht.registry its registry journal
// settings.ini the program, its folder and arguments, view and drives
// C-Schicht.host-lock the lock of the host that runs it (tools/host_single.h)
// <Name>.app what the person double-clicks to start it
// Each of its hard disks is the drive of the normal machine, read where it
// lies, with a write layer above it: HostVolume::Layered of
// tools/host_volume.h, one writable drive to Files::Store and to the DOS
// machine alike. A lookup takes the layer first and the normal drive then;
// the first write to a file of the normal drive copies it into the layer (a
// block clone where the medium can); what the game creates lies in the
// layer; what it deletes or renames leaves a tombstone in the sidecar.neo98-index of the layer's directory. The normal drives are opened
// read-only and never change. The layer folders hold everything the game
// machine changed, tombstones included, so a later snapshot of the disk for
// a saved state is one clone of those folders.
// The registry does not layer: the game machine starts from the registry of
// the normal machine as it was when the game machine was made (the hives
// SYSTEM.DAT and USER.DAT copied into the layer, the journal copied beside
// it) and keeps its own from then on.
//
// Nothing here needs AppKit.
#pragma once
#include "legacy_data.h"
#include "host_settings.h"
#include "host_volume.h"
#include <algorithm>
#include <cstdint>
#include <dirent.h>
#include <fcntl.h>
#include <spawn.h>
#include <string>
#include <sys/file.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <vector>

extern char **environ;

namespace HostGameMachine
{
// Where the game machines of a machine live. Never inside its drive C:, so a
// clone of the drive cannot hold them.
inline std::string Directory(const std::string &machine)
{
    if (machine.empty())
        return std::string();
    std::string base = machine;
    while (base.size() > 1 && base.back() == '/')
        base.pop_back();
    return HostSettings::DriveC(base) == base ? base + ".Spielmaschinen" : base + "/Spielmaschinen";
}
inline std::string SettingsFile(const std::string &folder) { return folder + "/settings.ini"; }
// The write layer of its drive C:; the journal and the lock lie beside it.
inline std::string Layer(const std::string &folder) { return folder + "/C-Schicht"; }
// The write layer of one of its other hard disks, "D-Schicht" to "Z-Schicht";
// made by the first write to that drive.
inline std::string DriveLayer(const std::string &folder, char letter) { return folder + "/" + std::string(1, letter) + "-Schicht"; }
inline std::string Leaf(const std::string &path)
{
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}
// The bundle that starts it from the Finder, and the icon in it.
inline std::string Launcher(const std::string &folder) { return folder + "/" + Leaf(folder) +
#ifdef __APPLE__
    ".app";
#else
    ".desktop";
#endif
}
inline std::string LauncherIcon(const std::string &folder) { return Launcher(folder) + "/Contents/Resources/icon.icns"; }
// The program of the bundle: a link to the host under the name of the game.
// A host started through it has the bundle as its main bundle, so macOS
// shows it in the Dock, in the menu bar and in the list of programs under the
// name and with the icon of the game, however it was started.
inline std::string LauncherProgram(const std::string &folder)
{
#ifndef __APPLE__
    return folder + "/neo98-host";
#else
    const std::string name = Leaf(folder);
    return Launcher(folder) + "/Contents/MacOS/" + (strcasecmp(name.c_str(), "spielmaschine") ? name : std::string("spiel"));
#endif
}

// A name a folder can have: no path separators, no control characters, no
// leading dot, at most 60 bytes.
inline std::string SafeName(const std::string &wanted)
{
    std::string name;
    for (unsigned char c : wanted)
        name += c < 0x20 || c == '/' || c == ':' || c == '\\' ? ' ' : char(c);
    name = HostSettings::Trim(name);
    while (!name.empty() && name[0] == '.')
        name.erase(0, 1);
    if (name.size() > 60)
    {
        name.resize(60);
        // Not in the middle of a UTF-8 sequence.
        while (!name.empty() && (static_cast<unsigned char>(name.back()) & 0xc0) == 0x80)
            name.pop_back();
        if (!name.empty() && static_cast<unsigned char>(name.back()) >= 0xc0)
            name.pop_back();
        name = HostSettings::Trim(name);
    }
    return name;
}

// Reads the settings of a game machine without touching the settings the
// process itself runs with.
inline bool LoadSettings(const std::string &folder, HostSettings::Settings &settings)
{
    const std::string kept = HostSettings::FileOverride();
    HostSettings::FileOverride() = SettingsFile(folder);
    const bool found = HostSettings::Load(settings);
    HostSettings::FileOverride() = kept;
    return found && !settings.game_program.empty();
}
inline bool SaveSettings(const std::string &folder, const HostSettings::Settings &settings)
{
    const std::string kept = HostSettings::FileOverride();
    HostSettings::FileOverride() = SettingsFile(folder);
    const bool saved = HostSettings::Save(settings);
    HostSettings::FileOverride() = kept;
    return saved;
}

// Whether a host has this folder as its drive C:: somebody holds its lock.
inline bool Locked(const std::string &drive)
{
    const int descriptor = open((drive + ".host-lock").c_str(), O_RDWR | O_CLOEXEC);
    if (descriptor < 0)
        return false;
    const bool busy = flock(descriptor, LOCK_EX | LOCK_NB) && errno == EWOULDBLOCK;
    if (!busy)
        flock(descriptor, LOCK_UN);
    close(descriptor);
    return busy;
}
// Whether the game machine of this folder runs, and whether a normal machine does.
inline bool Running(const std::string &folder) { return Locked(Layer(folder)); }
inline bool MachineRunning(const std::string &machine) { return Locked(HostSettings::DriveC(machine)); }

struct Entry
{
    std::string name, folder, program;
    std::string file; // the program file on this Mac; empty when it is not there
    bool running = false;
};
inline std::string HostFile(const std::string &root, const std::string &guest_rest);
// The program file of a game machine on this Mac: the one its settings name,
// else the file behind its guest path on the drives it has.
inline std::string ProgramFile(const HostSettings::Settings &settings)
{
    if (!settings.game_file.empty())
        return settings.game_file;
    const std::string &program = settings.game_program;
    if (program.size() < 4 || program[1] != ':' || program[2] != '\\')
        return std::string();
    const int letter = (program[0] & ~0x20) - 'A';
    if (letter < 2 || letter > 25)
        return std::string();
    const std::string root = letter == 2 ? HostSettings::DriveC(settings.game_source) : settings.drives[letter];
    return root.empty() ? std::string() : HostFile(root, HostVolume::ToHost(program.substr(3)));
}
// Whether two paths name one file of this Mac.
inline bool SameFile(const std::string &a, const std::string &b)
{
    if (a.empty() || b.empty())
        return false;
    struct stat one, other;
    if (!stat(a.c_str(), &one) && !stat(b.c_str(), &other))
        return one.st_dev == other.st_dev && one.st_ino == other.st_ino;
    return !strcasecmp(a.c_str(), b.c_str());
}
// The game machines of a machine, by name.
inline std::vector<Entry> List(const std::string &machine)
{
    std::vector<Entry> entries;
    const std::string directory = Directory(machine);
    DIR *handle = directory.empty() ? nullptr : opendir(directory.c_str());
    if (!handle)
        return entries;
    while (const dirent *item = readdir(handle))
    {
        if (item->d_name[0] == '.')
            continue;
        Entry entry;
        entry.folder = directory + "/" + item->d_name;
        HostSettings::Settings settings;
        if (!HostSettings::IsDirectory(entry.folder) || !LoadSettings(entry.folder, settings))
            continue;
        entry.name = item->d_name;
        entry.program = settings.game_program;
        entry.file = ProgramFile(settings);
        entry.running = Running(entry.folder);
        entries.push_back(entry);
    }
    closedir(handle);
    std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) { return strcasecmp(a.name.c_str(), b.name.c_str()) < 0; });
    return entries;
}
// The folder a start names: a folder with the settings of a game machine, or
// the name of one of this machine's. Empty when there is none.
inline std::string Resolve(const std::string &given, const std::string &machine)
{
    HostSettings::Settings settings;
    if (given.find('/') != std::string::npos)
    {
        std::string folder = given;
        while (folder.size() > 1 && folder.back() == '/')
            folder.pop_back();
        char real[PATH_MAX];
        if (realpath(folder.c_str(), real))
            folder = real;
        return LoadSettings(folder, settings) ? folder : std::string();
    }
    const std::string folder = Directory(machine) + "/" + given;
    return !machine.empty() && LoadSettings(folder, settings) ? folder : std::string();
}

// The game machines of a machine that run this program: by its file on this
// Mac, or by its guest path where a file is not known. One is the normal
// case; with several the caller lets the person choose.
inline std::vector<Entry> FindFor(const std::string &machine, const std::string &file, const std::string &program = std::string())
{
    std::vector<Entry> found;
    for (const auto &entry : List(machine))
        if (SameFile(entry.file, file) || (!program.empty() && !strcasecmp(entry.program.c_str(), program.c_str())))
            found.push_back(entry);
    return found;
}

// --- Programs opened from the Mac (docs/BUILDING.md, "Double-click") ----------
// Whether a file opened in the Finder is a program a game machine can be made
// for: a Windows or DOS program file. Not a shortcut, a batch file, a PIF, a
// screen saver or a document.
inline bool ProgramName(const std::string &path)
{
    const std::string name = Leaf(path);
    const auto dot = name.rfind('.');
    return dot != std::string::npos && dot && (!strcasecmp(name.c_str() + dot, ".exe") || !strcasecmp(name.c_str() + dot, ".com"));
}
// Whether the name says the program installs or removes something: SETUP,
// INSTALL, INSTALAR, UNINSTALL, UNINST, UNWISE, AUTORUN and names that
// contain them (SETUP32, _INSTALL, ISUNINST). Such a program is not asked
// about; it belongs into the normal machine.
inline bool InstallerName(const std::string &path)
{
    std::string name = Leaf(path);
    if (const auto dot = name.rfind('.'); dot != std::string::npos)
        name.resize(dot);
    for (auto &c : name)
        if (c >= 'a' && c <= 'z')
            c = char(c & ~0x20);
    for (const char *part : {"SETUP", "INSTAL", "UNINST", "UNWISE", "AUTORUN"})
        if (name.find(part) != std::string::npos)
            return true;
    return false;
}

// --- The name a program suggests -------------------------------------------
// ProductName or FileDescription of the version resource of a 32-bit program
// file, as UTF-8; empty for a file without one (a 16-bit or DOS program).
inline std::string VersionName(const std::string &file)
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
        return std::string();
    const size_t pe = dword(0x3c);
    if (dword(pe) != 0x4550 || word(pe + 24) != 0x10b)
        return std::string();
    const unsigned sections = word(pe + 6), optional = word(pe + 20);
    const unsigned resources = dword(pe + 24 + 96 + 2 * 8);
    if (!resources)
        return std::string();
    // The file offset of a virtual address.
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
    const size_t root = offset(resources);
    if (!root)
        return std::string();
    // Type RT_VERSION, then the first name and the first language below it.
    size_t directory = root;
    for (int level = 0; level < 3; ++level)
    {
        const unsigned count = word(directory + 12) + word(directory + 14);
        unsigned next = 0;
        for (unsigned i = 0; i < count && !next; ++i)
        {
            const size_t entry = directory + 16 + i * 8;
            if (level == 0 && dword(entry) != 16)
                continue;
            next = dword(entry + 4);
        }
        if (!next)
            return std::string();
        if (level < 2 && !(next & 0x80000000u))
            return std::string();
        directory = root + (next & 0x7fffffffu);
    }
    const size_t blob = offset(dword(directory)), length = dword(directory + 4);
    if (!blob || blob + length > data.size())
        return std::string();
    // A String of the block: wLength, wValueLength, wType, the key in UTF-16
    // with its zero, padding to 32 bits, the value in UTF-16.
    const auto value = [&](const char *key) {
        std::string text;
        const size_t key_length = strlen(key);
        for (size_t at = blob + 6; at + 2 * (key_length + 1) <= blob + length; at += 2)
        {
            bool match = true;
            for (size_t i = 0; i <= key_length && match; ++i)
                match = word(at + 2 * i) == static_cast<unsigned char>(key[i]);
            if (!match)
                continue;
            size_t from = at + 2 * (key_length + 1);
            from = blob + (((from - blob) + 3) & ~size_t(3));
            for (unsigned units = word(at - 4); units && from + 2 <= blob + length; --units, from += 2)
            {
                const unsigned c = word(from);
                if (!c)
                    break;
                if (c < 0x80)
                    text += char(c);
                else if (c < 0x800)
                {
                    text += char(0xc0 | (c >> 6));
                    text += char(0x80 | (c & 0x3f));
                }
                else
                {
                    text += char(0xe0 | (c >> 12));
                    text += char(0x80 | ((c >> 6) & 0x3f));
                    text += char(0x80 | (c & 0x3f));
                }
            }
            break;
        }
        return HostSettings::Trim(text);
    };
    // A program of Windows itself names the system as its product; what it is
    // stands in its description.
    const std::string product = value("ProductName"), description = value("FileDescription");
    const bool system = product.find("Microsoft") != std::string::npos && product.find("Windows") != std::string::npos;
    if (!product.empty() && !system)
        return product;
    return description.empty() ? product : description;
}
// "C:\SPIELE\KROKO.EXE" -> "Kroko", for a program without a version resource.
inline std::string FileName(const std::string &program)
{
    std::string name = Leaf(program);
    if (const auto dot = name.rfind('.'); dot != std::string::npos && dot)
        name.resize(dot);
    // A name all in capitals is an 8.3 name; one with small letters is
    // spelled as its author wanted it ("BudRedhead").
    bool small = false;
    for (char c : name)
        small = small || (c >= 'a' && c <= 'z');
    for (size_t i = 1; i < name.size() && !small; ++i)
        if (name[i] >= 'A' && name[i] <= 'Z')
            name[i] = char(name[i] | 0x20);
    return name;
}
// The file of this Mac behind a guest path below `root`, each component
// matched without regard to case. Empty when a component is not there (a
// name the guest knows only by its 8.3 alias).
inline std::string HostFile(const std::string &root, const std::string &guest_rest)
{
    std::string path = root;
    for (size_t at = 0; at < guest_rest.size();)
    {
        size_t end = guest_rest.find('\\', at);
        if (end == std::string::npos)
            end = guest_rest.size();
        const std::string part = guest_rest.substr(at, end - at);
        at = end + 1;
        if (part.empty())
            continue;
        std::string found;
        if (DIR *handle = opendir(path.c_str()))
        {
            while (const dirent *item = readdir(handle))
                if (!strcasecmp(item->d_name, part.c_str()))
                {
                    found = item->d_name;
                    break;
                }
            closedir(handle);
        }
        if (found.empty())
            return std::string();
        path += "/" + found;
    }
    return path;
}

// --- Creating and deleting --------------------------------------------------
inline std::string ShellQuoted(const std::string &text)
{
    std::string out = "'";
    for (char c : text)
        out += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return out + "'";
}
inline std::string XmlEscaped(const std::string &text)
{
    std::string out;
    for (char c : text)
        out += c == '&' ? "&amp;" : c == '<' ? "&lt;" : c == '>' ? "&gt;" : std::string(1, c);
    return out;
}
inline bool WriteText(const std::string &path, const std::string &text, mode_t mode)
{
    FILE *file = fopen(path.c_str(), "w");
    if (!file)
        return false;
    const bool written = fwrite(text.data(), 1, text.size(), file) == text.size();
    const bool closed = fclose(file) == 0;
    return written && closed && !chmod(path.c_str(), mode);
}
// <Name>.app in the folder. Its program is a line of shell that starts the
// link LauncherProgram in the same bundle with this game machine; the link
// leads to `executable` (the host, in the NEO98 bundle or in a checkout). The
// host that runs is thereby a program of this bundle, with its name and its
// icon. A copy of the bundle elsewhere starts the same link, so it goes on
// working when NEO98 moves and this one is written again. The icon is the
// one of NEO98 until the game's own is known (LauncherIcon).
inline std::string LauncherScript(const std::string &folder)
{
    return "#!/bin/sh\n# Neo98 game machine\nexec " + ShellQuoted(LauncherProgram(folder)) +
           " --game-machine " + ShellQuoted(folder) + "\n";
}
inline bool WriteLauncher(const std::string &folder, const std::string &executable)
{
#ifndef __APPLE__
    const auto quote = [](const std::string &text) {
        std::string value = "\"";
        for (char c : text) { if (c == '\\' || c == '"' || c == '`' || c == '$') value += '\\'; value += c; if (c == '%') value += '%'; }
        value += '"';
        std::string escaped;
        for (char c : value)
        {
            if (c == '\\') escaped += "\\\\";
            else if (c == '\n') escaped += "\\n";
            else if (c == '\r') escaped += "\\r";
            else if (c == '\t') escaped += "\\t";
            else escaped += c;
        }
        return escaped;
    };
    const auto link = LauncherProgram(folder);
    unlink(link.c_str());
    if (symlink(executable.c_str(), link.c_str())) return false;
    return WriteText(Launcher(folder), "[Desktop Entry]\nType=Application\nName=" + Leaf(folder) +
        "\nExec=/usr/bin/env -- " + quote(link) + " --game-machine " + quote(folder) +
        "\nIcon=applications-games\nTerminal=false\nCategories=Game;\n", 0755);
#else
    const std::string bundle = Launcher(folder), name = Leaf(folder), link = LauncherProgram(folder);
    if (!HostSettings::MakeDirectories(bundle + "/Contents/MacOS") || !HostSettings::MakeDirectories(bundle + "/Contents/Resources"))
        return false;
    unlink(link.c_str());
    if (symlink(executable.c_str(), link.c_str()))
        return false;
    unsigned hash = 2166136261u;
    for (unsigned char c : folder)
        hash = (hash ^ c) * 16777619u;
    char identifier[96];
    snprintf(identifier, sizeof identifier, "at.andreas-horvath.neo98.spielmaschine.%08x", hash);
    const std::string plist =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\">\n<dict>\n"
        "\t<key>CFBundleExecutable</key>\n\t<string>spielmaschine</string>\n"
        "\t<key>CFBundleIdentifier</key>\n\t<string>" + std::string(identifier) + "</string>\n"
        "\t<key>CFBundleName</key>\n\t<string>" + XmlEscaped(name) + "</string>\n"
        "\t<key>CFBundleDisplayName</key>\n\t<string>" + XmlEscaped(name) + "</string>\n"
        "\t<key>CFBundleIconFile</key>\n\t<string>icon</string>\n"
        "\t<key>CFBundlePackageType</key>\n\t<string>APPL</string>\n"
        "\t<key>CFBundleInfoDictionaryVersion</key>\n\t<string>6.0</string>\n"
        "\t<key>CFBundleShortVersionString</key>\n\t<string>1.0</string>\n"
        "\t<key>LSApplicationCategoryType</key>\n\t<string>public.app-category.games</string>\n"
        "\t<key>NSPrincipalClass</key>\n\t<string>NSApplication</string>\n"
        "\t<key>NSHighResolutionCapable</key>\n\t<true/>\n"
        "</dict>\n</plist>\n";
    if (!WriteText(bundle + "/Contents/MacOS/spielmaschine", LauncherScript(folder), 0755) ||
        !WriteText(bundle + "/Contents/Info.plist", plist, 0644))
        return false;
    // The icon of NEO98 until the game's own is known.
    const std::string icon = LauncherIcon(folder), fallback = HostSettings::Resource("Neo98.icns");
    if (!HostSettings::Exists(icon) && HostSettings::Exists(fallback))
        HostVolume::CopyFile(fallback, icon);
    return true;
#endif
}
// Whether the launcher of this folder starts `executable` with this folder.
inline bool LauncherCurrent(const std::string &folder, const std::string &executable)
{
    std::string text;
    if (FILE *file = fopen((Launcher(folder) + "/Contents/MacOS/spielmaschine").c_str(), "r"))
    {
        char block[4096];
        size_t got;
        while ((got = fread(block, 1, sizeof block, file)) > 0)
            text.append(block, got);
        fclose(file);
    }
    char target[PATH_MAX] = {};
    const ssize_t length = readlink(LauncherProgram(folder).c_str(), target, sizeof target - 1);
    return text == LauncherScript(folder) && length > 0 && executable == std::string(target, size_t(length));
}
// The command line that starts the game machine of a folder: through the
// link of its launcher, so the host is a program of that bundle; with the
// host itself where the launcher is not there.
inline std::vector<std::string> StartLine(const std::string &folder, const std::string &executable)
{
    const std::string link = LauncherProgram(folder);
    return {LauncherCurrent(folder, executable) ? link : executable, "--game-machine", folder};
}

// Runs a program of this Mac and waits for it; its exit code, -1 when it did
// not start.
inline int Run(const std::vector<std::string> &line)
{
    std::vector<char *> arguments;
    for (const auto &word : line)
        arguments.push_back(const_cast<char *>(word.c_str()));
    arguments.push_back(nullptr);
    pid_t child = 0;
    if (posix_spawn(&child, arguments[0], nullptr, nullptr, arguments.data(), environ))
        return -1;
    int status = 0;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR)
    {
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

struct Request
{
    std::string machine;   // the machine folder it is made from
    std::string program;   // the game, by its guest path
    std::string arguments; // what follows the program on its command line
    std::string directory; // the folder it starts in; empty for its own
    std::string name;      // empty: from the program
    std::string program_file; // the program file on this Mac, for the name; may be empty
    // The view, the network cable and the drives of the machine as they are
    // now: the settings of the app, or what the running host has mounted.
    HostSettings::Settings source;
};
struct Result
{
    std::string folder, name, error;
};
// A name that is free in the folder of game machines: "Name", "Name 2",...
inline std::string FreeName(const std::string &directory, const std::string &wanted)
{
    for (int number = 1; number < 1000; ++number)
    {
        const std::string name = number == 1 ? wanted : wanted + " " + std::to_string(number);
        if (!HostSettings::Exists(directory + "/" + name))
            return name;
    }
    return std::string();
}
// The name a program gets when the person gives none.
inline std::string SuggestedName(const std::string &program, const std::string &program_file)
{
    std::string name = program_file.empty() ? std::string() : SafeName(VersionName(program_file));
    if (name.empty())
        name = SafeName(FileName(program));
    return name.empty() ? std::string("Game") : name;
}
// A copy of one file (HostVolume::CopyFile: a block clone where the medium
// can, the bytes else).
inline bool Copy(const std::string &from, const std::string &to) { return HostVolume::CopyFile(from, to); }
// The line of a directory's sidecar.neo98-index for one name (tools/host_volume.h),
// with its newline; empty when the sidecar has none.
inline std::string SidecarRecord(const std::string &directory, const std::string &name)
{
    std::string found;
    if (FILE *file = fopen(LegacyData::ReadPath(directory + "/.neo98-index").c_str(), "r"))
    {
        char line[2048];
        while (found.empty() && fgets(line, sizeof line, file))
        {
            std::string text = line;
            while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
                text.pop_back();
            const auto tab = text.rfind('\t');
            if (line[0] != '#' && tab != std::string::npos && !strcasecmp(text.c_str() + tab + 1, name.c_str()))
                found = text + "\n";
        }
        fclose(file);
    }
    return found;
}
// Makes the game machine. The caller has flushed the registry of a machine
// that runs.
inline bool Create(const Request &request, const std::string &executable, Result &result)
{
    result = Result();
    if (!HostSettings::Ready(request.machine))
    {
        result.error = "No Windows 98 installation found in the machine folder.";
        return false;
    }
    if (request.program.size() < 4 || request.program[1] != ':' || request.program[2] != '\\')
    {
        result.error = "The program needs a guest path, such as C:\\GAMES\\GAME.EXE.";
        return false;
    }
    // A shortcut is not followed; the game is its program file.
    if (const auto dot = request.program.rfind('.'); dot != std::string::npos && !strcasecmp(request.program.c_str() + dot, ".lnk"))
    {
        result.error = "This is a shortcut. Choose the game's executable file.";
        return false;
    }
    const std::string directory = Directory(request.machine), drive_c = HostSettings::DriveC(request.machine);
    char real_c[PATH_MAX], real_directory[PATH_MAX];
    if (!HostSettings::MakeDirectories(directory) || !realpath(drive_c.c_str(), real_c) || !realpath(directory.c_str(), real_directory))
    {
        result.error = "The folder " + directory + " could not be created.";
        return false;
    }
    // The clone must not hold the game machines themselves.
    if (const std::string inside = std::string(real_c) + "/"; !std::string(real_directory).compare(0, inside.size(), inside))
    {
        result.error = "The game machines folder is inside drive C:.";
        return false;
    }
    const std::string wanted = SafeName(request.name.empty() ? SuggestedName(request.program, request.program_file) : request.name);
    if (wanted.empty())
    {
        result.error = "The name is empty.";
        return false;
    }
    if (!request.name.empty() && HostSettings::Exists(directory + "/" + wanted))
    {
        result.error = "A game machine named \"" + wanted + "\" already exists.";
        return false;
    }
    result.name = request.name.empty() ? FreeName(directory, wanted) : wanted;
    // Built under a hidden name and renamed when it is whole, so a start
    // never finds half a machine.
    const std::string folder = directory + "/" + result.name, partial = directory + "/." + result.name + ".neu";
    Run({"/bin/rm", "-rf", partial});
    const std::string layer = Layer(partial);
    if (mkdir(partial.c_str(), 0755) || mkdir(layer.c_str(), 0755))
    {
        result.error = "The folder " + partial + " could not be created.";
        return false;
    }
    const auto fail = [&](const std::string &error) {
        Run({"/bin/rm", "-rf", partial});
        result.error = error;
        return false;
    };
    // The layer is the same volume to the guest: serial number and label.
    if (HostSettings::Exists(std::string(real_c) + "/.neo98-volume"))
        Copy(std::string(real_c) + "/.neo98-volume", layer + "/.neo98-volume");
    // The registry as it is now. The hives the machine starts from go into
    // the layer with their records of the sidecar (attributes, creation
    // time), the journal beside it; from here on the game machine keeps its
    // own, whatever the normal machine does to its registry.
    const std::string windows = HostFile(real_c, "WINDOWS");
    if (windows.empty())
        return fail("Drive C: has no WINDOWS folder.");
    const std::string layer_windows = layer + "/" + Leaf(windows);
    std::string records = "# NEO98 volume index 1: flags\tattributes\tcdate,ctime,chundredths\talias\tname\n";
    if (mkdir(layer_windows.c_str(), 0755))
        return fail("The folder " + layer_windows + " could not be created.");
    for (const char *hive : {"SYSTEM.DAT", "USER.DAT"})
    {
        const std::string file = HostFile(windows, hive);
        if (file.empty())
            continue;
        if (!Copy(file, layer_windows + "/" + Leaf(file)))
            return fail(std::string("The registry file ") + hive + " could not be copied.");
        records += SidecarRecord(windows, Leaf(file));
    }
    if (!WriteText(layer_windows + "/.neo98-index", records, 0644))
        return fail("Could not write the layer.");
    if (const std::string journal = drive_c + ".registry"; HostSettings::Exists(journal) && !Copy(journal, layer + ".registry"))
        return fail("Could not copy the registry journal.");
    HostSettings::Settings settings = request.source;
    settings.found = true;
    settings.machine = folder;
    settings.game_name = result.name;
    settings.game_program = request.program;
    settings.game_arguments = request.arguments;
    settings.game_directory = request.directory;
    settings.game_source = request.machine;
    settings.game_file = request.program_file;
    // One program, one icon in the Dock: the one of the machine. No taskbar
    // and no desktop icons, there is no shell that would draw them.
    settings.seamless_dock = false;
    settings.seamless_taskbar = false;
    settings.seamless_icons = false;
    // The other drives are those of the normal machine and stay its own.
    settings.drives_readonly = true;
    if (!SaveSettings(partial, settings))
        return fail("Could not write the game machine settings.");
    if (rename(partial.c_str(), folder.c_str()))
        return fail("The folder " + folder + " could not be created.");
    WriteLauncher(folder, executable);
    result.folder = folder;
    return true;
}
// Deletes a game machine with everything in it. Refuses one that runs and
// anything that is not a game machine.
inline bool Delete(const std::string &folder, std::string &error)
{
    HostSettings::Settings settings;
    if (folder.empty() || !LoadSettings(folder, settings) || Leaf(HostSettings::Parent(folder)).find("Spielmaschinen") == std::string::npos)
    {
        error = "Not a game machine: " + folder;
        return false;
    }
    if (Running(folder))
    {
        error = "The game machine is still running. Close it first.";
        return false;
    }
    // Out of the list first, then removed.
    const std::string doomed = HostSettings::Parent(folder) + "/." + Leaf(folder) + ".weg";
    Run({"/bin/rm", "-rf", doomed});
    if (rename(folder.c_str(), doomed.c_str()))
    {
        error = "The folder " + folder + " could not be deleted.";
        return false;
    }
    Run({"/bin/rm", "-rf", doomed});
    // The folder of the game machines goes with the last one.
    rmdir(HostSettings::Parent(folder).c_str());
    return true;
}

// Starts a host as a process of its own, detached from this one: the host of
// a game machine (`line` is the program, "--game-machine" and the folder),
// or from a game machine the normal one. The scripts and the lock of this
// host stay here; the log of the new one goes to its own file.
inline std::vector<std::string> SpawnEnvironment(const std::vector<std::string> &extra);
inline bool Spawn(std::vector<std::string> line, const std::vector<std::string> &extra)
{
    std::vector<std::string> kept = SpawnEnvironment(extra);
    std::vector<char *> environment, arguments;
    for (auto &variable : kept)
        environment.push_back(variable.data());
    environment.push_back(nullptr);
    for (auto &word : line)
        arguments.push_back(word.data());
    arguments.push_back(nullptr);
    // Twice removed, so this host never has to wait for it.
    const pid_t child = fork();
    if (child < 0)
        return false;
    if (!child)
    {
        setsid();
        const pid_t machine = fork();
        if (machine)
            _exit(machine < 0 ? 1 : 0);
        const int null = open("/dev/null", O_RDWR);
        if (null >= 0)
        {
            dup2(null, 0);
            dup2(null, 1);
            dup2(null, 2);
        }
        execve(arguments[0], arguments.data(), environment.data());
        _exit(127);
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR)
    {
    }
    return WIFEXITED(status) && !WEXITSTATUS(status);
}
// This process becomes that host: a start that was asked for a game machine
// while no machine runs is the game machine from here on, and no normal
// machine comes up for it. `line` and the environment are those of Spawn.
// Returns only when the program cannot be started.
inline void Become(std::vector<std::string> line, const std::vector<std::string> &extra)
{
    std::vector<std::string> kept = SpawnEnvironment(extra);
    std::vector<char *> environment, arguments;
    for (auto &variable : kept)
        environment.push_back(variable.data());
    environment.push_back(nullptr);
    for (auto &word : line)
        arguments.push_back(word.data());
    arguments.push_back(nullptr);
    fflush(stdout);
    fflush(stderr);
    execve(arguments[0], arguments.data(), environment.data());
}
// The environment of a host this one starts: without the scripts and the lock
// of this one, and with a log file of its own.
inline std::vector<std::string> SpawnEnvironment(const std::vector<std::string> &extra)
{
    std::vector<std::string> kept;
    static const char *const dropped[] = {"NEO98_HOST_INPUT=", "NEO98_HOST_INPUT_ONCE=", "NEO98_HOST_MENU=", "NEO98_HOST_WIZARD=",
                                          "NEO98_HOST_LOCK_KEPT=", "NEO98_HOST_OPEN_AFTER_SETUP=", "NEO98_LOG_TO_FILE=",
                                          "NEO98_GAME_MACHINE=", "NEO98_GAME_MACHINE_LOG_BASE=", "NEO98_LOG_DIR="};
    // A game machine logs below the log folder of the app; the host it
    // starts gets the folder this one was started with back.
    const char *game = getenv("NEO98_GAME_MACHINE");
    const char *base_log = getenv(game && *game ? "NEO98_GAME_MACHINE_LOG_BASE" : "NEO98_LOG_DIR");
    for (char **variable = environ; *variable; ++variable)
    {
        bool drop = false;
        for (const char *name : dropped)
            drop = drop || !strncmp(*variable, name, strlen(name));
        if (!drop)
            kept.push_back(*variable);
    }
    kept.push_back("NEO98_LOG_TO_FILE=1");
    // No launch by macOS: the host has nothing to read from one (OpenWithoutMachine).
    kept.push_back("NEO98_LAUNCH_READ=1");
    if (base_log && *base_log)
        kept.push_back(std::string("NEO98_LOG_DIR=") + base_log);
    for (const auto &variable : extra)
        kept.push_back(variable);
    return kept;
}
} // namespace HostGameMachine
