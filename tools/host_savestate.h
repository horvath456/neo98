// SPDX-License-Identifier: GPL-2.0-or-later
// Saved game-machine states: machine data, write layers and registry journal.
// Keep the existing on-disk names for compatibility. Each state identifies
// its parent; loading replaces the live layers/journal and updates the
// current-state marker.
// Write into a hidden folder and rename only when complete. Loading stages
// copies, writes a recovery marker and swaps folders. Recover finishes marked
// swaps and discards unmarked staging copies. HostVolume::CopyFile uses block
// clones where supported and ordinary copies otherwise.
#pragma once
#include "host_game_machine.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>
#include <zlib.h>

namespace HostSaveState
{
// The layout of a state folder and of zustand.ini.
inline constexpr unsigned Format = 1;
// How many automatic states stay ("before loading"); older ones are removed.
inline constexpr unsigned KeepAutomatic = 5;

inline std::string Directory(const std::string &folder) { return folder + "/Zustände"; }
inline std::string StateFolder(const std::string &folder, const std::string &id) { return Directory(folder) + "/" + id; }
inline std::string MachineFile(const std::string &state) { return state + "/Maschine.bin"; }
inline std::string PictureFile(const std::string &state) { return state + "/Bild.png"; }
inline std::string LayerName(char letter) { return std::string(1, letter) + "-Schicht"; }
inline constexpr const char *RegistryName = "C-Schicht.registry";

// A test stops the process in the middle of a write: NEO98_SAVESTATE_STOP
// names a step ("save-copy:3" is the third file copied) and the process ends
// there as if the power had gone.
inline std::map<std::string, unsigned> &StopCounts()
{
    static std::map<std::string, unsigned> seen;
    return seen;
}
inline void Stop(const char *step)
{
    const char *wanted = getenv("NEO98_SAVESTATE_STOP");
    if (!wanted || !*wanted)
        return;
    const unsigned count = ++StopCounts()[step];
    const char *colon = strchr(wanted, ':');
    const std::string name = colon ? std::string(wanted, size_t(colon - wanted)) : std::string(wanted);
    if (name == step && count == (colon ? unsigned(strtoul(colon + 1, nullptr, 10)) : 1u))
        _exit(86);
}

// --- Files and folders ------------------------------------------------------
inline bool IsDirectory(const std::string &path)
{
    struct stat info;
    return !lstat(path.c_str(), &info) && S_ISDIR(info.st_mode);
}
inline bool IsFile(const std::string &path)
{
    struct stat info;
    return !lstat(path.c_str(), &info) && S_ISREG(info.st_mode);
}
inline bool Present(const std::string &path)
{
    struct stat info;
    return !lstat(path.c_str(), &info);
}
inline void ModifiedTime(const struct stat &info, long long &seconds, long &nanoseconds)
{
#ifdef __APPLE__
    seconds = info.st_mtimespec.tv_sec;
    nanoseconds = info.st_mtimespec.tv_nsec;
#else
    seconds = info.st_mtim.tv_sec;
    nanoseconds = info.st_mtim.tv_nsec;
#endif
}
// The times of `from` on `to`: a copy of the bytes has the time of the copy
// otherwise, and the write time of a file of the layer is the one the game
// sees.
inline void CopyTimes(const struct stat &from, const std::string &to)
{
    struct timespec times[2];
#ifdef __APPLE__
    times[0] = from.st_atimespec;
    times[1] = from.st_mtimespec;
#else
    times[0] = from.st_atim;
    times[1] = from.st_mtim;
#endif
    utimensat(AT_FDCWD, to.c_str(), times, AT_SYMLINK_NOFOLLOW);
}
inline bool RemoveTree(const std::string &path)
{
    struct stat info;
    if (lstat(path.c_str(), &info))
        return errno == ENOENT;
    if (!S_ISDIR(info.st_mode))
        return !unlink(path.c_str());
    // A folder somebody made read-only still gives up its entries.
    chmod(path.c_str(), 0755);
    std::vector<std::string> names;
    if (DIR *handle = opendir(path.c_str()))
    {
        while (const dirent *item = readdir(handle))
            if (strcmp(item->d_name, ".") && strcmp(item->d_name, ".."))
                names.push_back(item->d_name);
        closedir(handle);
    }
    bool okay = true;
    for (const auto &name : names)
        okay = RemoveTree(path + "/" + name) && okay;
    return !rmdir(path.c_str()) && okay;
}
// A copy of a folder with everything in it, file for file through
// HostVolume::CopyFile, with the times and modes of the original. `to` must
// not exist. `step` names the copy for Stop.
inline bool CopyTree(const std::string &from, const std::string &to, const char *step)
{
    struct stat info;
    if (lstat(from.c_str(), &info) || !S_ISDIR(info.st_mode) || mkdir(to.c_str(), 0755))
        return false;
    std::vector<std::string> names;
    DIR *handle = opendir(from.c_str());
    if (!handle)
        return false;
    while (const dirent *item = readdir(handle))
        if (strcmp(item->d_name, ".") && strcmp(item->d_name, ".."))
            names.push_back(item->d_name);
    closedir(handle);
    for (const auto &name : names)
    {
        const std::string source = from + "/" + name, target = to + "/" + name;
        struct stat entry;
        if (lstat(source.c_str(), &entry))
            return false;
        if (S_ISDIR(entry.st_mode))
        {
            if (!CopyTree(source, target, step))
                return false;
        }
        else if (S_ISLNK(entry.st_mode))
        {
            char link[PATH_MAX] = {};
            const ssize_t length = readlink(source.c_str(), link, sizeof link - 1);
            if (length < 0 || symlink(std::string(link, size_t(length)).c_str(), target.c_str()))
                return false;
        }
        else if (S_ISREG(entry.st_mode))
        {
            if (!HostVolume::CopyFile(source, target))
                return false;
            chmod(target.c_str(), entry.st_mode & 0777);
            CopyTimes(entry, target);
            Stop(step);
        }
    }
    CopyTimes(info, to);
    return true;
}
inline bool ReadFile(const std::string &path, std::string &text)
{
    text.clear();
    FILE *file = fopen(path.c_str(), "rb");
    if (!file)
        return false;
    char block[65536];
    size_t got;
    while ((got = fread(block, 1, sizeof block, file)) > 0)
        text.append(block, got);
    const bool okay = !ferror(file);
    fclose(file);
    return okay;
}
// Written beside its place and renamed: the file is the old one or the new.
inline bool WriteFile(const std::string &path, const void *data, size_t size)
{
    const std::string partial = path + ".neu";
    FILE *file = fopen(partial.c_str(), "wb");
    if (!file)
        return false;
    const bool written = fwrite(data, 1, size, file) == size;
    const bool flushed = !fflush(file) && !fsync(fileno(file));
    const bool closed = !fclose(file);
    if (!written || !flushed || !closed || rename(partial.c_str(), path.c_str()))
    {
        unlink(partial.c_str());
        return false;
    }
    return true;
}
inline bool WriteFile(const std::string &path, const std::string &text) { return WriteFile(path, text.data(), text.size()); }

// --- The picture ------------------------------------------------------------
// Bild.png: 8 bit RGB without an alpha channel. `pixels` are rows of
// 0x00RRGGBB without padding.
inline bool WritePicture(const std::string &path, const unsigned *pixels, unsigned width, unsigned height)
{
    if (!pixels || !width || !height || width > 8192 || height > 8192)
        return false;
    std::vector<unsigned char> raw((size_t(width) * 3 + 1) * height);
    for (unsigned y = 0; y < height; ++y)
    {
        unsigned char *row = raw.data() + (size_t(width) * 3 + 1) * y;
        *row++ = 0;
        for (unsigned x = 0; x < width; ++x)
        {
            const unsigned pixel = pixels[size_t(y) * width + x];
            *row++ = (unsigned char)(pixel >> 16);
            *row++ = (unsigned char)(pixel >> 8);
            *row++ = (unsigned char)pixel;
        }
    }
    uLongf packed_size = compressBound(uLong(raw.size()));
    std::vector<unsigned char> packed(packed_size);
    if (compress2(packed.data(), &packed_size, raw.data(), uLong(raw.size()), 6) != Z_OK)
        return false;
    std::string out("\x89PNG\r\n\x1a\n", 8);
    const auto chunk = [&](const char *type, const unsigned char *data, size_t size) {
        unsigned char head[8] = {(unsigned char)(size >> 24), (unsigned char)(size >> 16), (unsigned char)(size >> 8), (unsigned char)size,
                                 (unsigned char)type[0], (unsigned char)type[1], (unsigned char)type[2], (unsigned char)type[3]};
        uLong sum = crc32(0, head + 4, 4);
        if (size)
            sum = crc32(sum, data, uInt(size));
        const unsigned char tail[4] = {(unsigned char)(sum >> 24), (unsigned char)(sum >> 16), (unsigned char)(sum >> 8), (unsigned char)sum};
        out.append(reinterpret_cast<const char *>(head), 8);
        out.append(reinterpret_cast<const char *>(data), size);
        out.append(reinterpret_cast<const char *>(tail), 4);
    };
    const unsigned char header[13] = {(unsigned char)(width >> 24), (unsigned char)(width >> 16), (unsigned char)(width >> 8), (unsigned char)width,
                                      (unsigned char)(height >> 24), (unsigned char)(height >> 16), (unsigned char)(height >> 8), (unsigned char)height,
                                      8, 2, 0, 0, 0};
    chunk("IHDR", header, sizeof header);
    chunk("IDAT", packed.data(), packed_size);
    chunk("IEND", header, 0);
    return WriteFile(path, out);
}
// Reads a picture WritePicture wrote (8 bit RGB, not interlaced; every row
// filter of PNG).
inline bool ReadPicture(const std::string &path, std::vector<unsigned> &pixels, unsigned &width, unsigned &height)
{
    std::string file;
    if (!ReadFile(path, file) || file.size() < 8 + 25 || file.compare(0, 8, std::string("\x89PNG\r\n\x1a\n", 8)))
        return false;
    const auto be = [&](size_t at) {
        return (unsigned)(unsigned char)file[at] << 24 | (unsigned)(unsigned char)file[at + 1] << 16 |
               (unsigned)(unsigned char)file[at + 2] << 8 | (unsigned)(unsigned char)file[at + 3];
    };
    std::string packed;
    width = height = 0;
    for (size_t at = 8; at + 12 <= file.size();)
    {
        const size_t size = be(at);
        if (at + 12 + size > file.size())
            return false;
        if (!file.compare(at + 4, 4, "IHDR") && size >= 13)
        {
            width = be(at + 8);
            height = be(at + 12);
            if (file[at + 16] != 8 || file[at + 17] != 2 || file[at + 20])
                return false;
        }
        else if (!file.compare(at + 4, 4, "IDAT"))
            packed.append(file, at + 8, size);
        at += 12 + size;
    }
    if (!width || !height || width > 8192 || height > 8192)
        return false;
    const size_t stride = size_t(width) * 3;
    std::vector<unsigned char> raw((stride + 1) * height);
    uLongf raw_size = uLongf(raw.size());
    if (uncompress(raw.data(), &raw_size, reinterpret_cast<const unsigned char *>(packed.data()), uLong(packed.size())) != Z_OK ||
        raw_size != raw.size())
        return false;
    pixels.assign(size_t(width) * height, 0);
    std::vector<unsigned char> before(stride, 0), row(stride);
    for (unsigned y = 0; y < height; ++y)
    {
        const unsigned char *in = raw.data() + (stride + 1) * y;
        const unsigned filter = *in++;
        for (size_t i = 0; i < stride; ++i)
        {
            const int a = i >= 3 ? row[i - 3] : 0, b = before[i], c = i >= 3 ? before[i - 3] : 0;
            int predicted = 0;
            if (filter == 1)
                predicted = a;
            else if (filter == 2)
                predicted = b;
            else if (filter == 3)
                predicted = (a + b) / 2;
            else if (filter == 4)
            {
                const int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
                predicted = pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
            }
            else if (filter)
                return false;
            row[i] = (unsigned char)(in[i] + predicted);
        }
        for (unsigned x = 0; x < width; ++x)
            pixels[size_t(y) * width + x] = (unsigned)row[x * 3] << 16 | (unsigned)row[x * 3 + 1] << 8 | row[x * 3 + 2];
        before = row;
    }
    return true;
}

// --- zustand.ini ------------------------------------------------------------
// A file of a normal drive the machine has read: the state of the machine
// holds what was in it, so the state fits only while the file is the same.
struct BaseFile
{
    char drive = 'C';
    std::string path; // below the folder of the drive, as it is spelled on this Mac
    unsigned long long size = 0;
    long long seconds = 0; // its time of modification
    long nanoseconds = 0;
};
struct Info
{
    unsigned format = Format;
    std::string id, name;
    long long time = 0;   // seconds since 1970
    std::string kind;     // "manual", "quick", "auto"
    std::string parent;   // the state it came from; empty for a first one
    // What ran in the machine and supplied Maschine.bin: "dos", "windows";
    // "off" for a state of the files alone (the game starts anew from it).
    std::string game;
    std::string program;  // game.program of the game machine
    std::string revision; // the NEO98 that wrote it; told, never a reason to refuse
    // What Maschine.bin is laid out by, from its supplier ("dos-1-…"): a
    // state fits only a host whose supplier says the same.
    std::string engine;
    std::string layers;   // the letters of the drives whose layer is in it, "CD"
    bool registry = false;
    unsigned long long machine_size = 0;
    unsigned machine_sum = 0; // CRC-32 of Maschine.bin
    // What the supplier says about the machine the state needs (memory, kind
    // of machine); the machine that loads it must say the same.
    std::vector<std::pair<std::string, std::string>> machine;
    // How the game was shown, for the host to bring back after a load
    // ("dos-vollbild": the DOS window had the full screen). Never a reason
    // to refuse a state.
    std::vector<std::pair<std::string, std::string>> view;
    std::vector<BaseFile> files;
    bool complete = false; // the last line of the file was read
};
// One line of text: no control characters, at most 80 bytes of UTF-8.
inline std::string CleanName(const std::string &wanted)
{
    std::string name;
    for (unsigned char c : wanted)
        name += c < 0x20 || c == 0x7f ? ' ' : char(c);
    name = HostSettings::Trim(name);
    if (name.size() > 80)
    {
        name.resize(80);
        while (!name.empty() && (static_cast<unsigned char>(name.back()) & 0xc0) == 0x80)
            name.pop_back();
        if (!name.empty() && static_cast<unsigned char>(name.back()) >= 0xc0)
            name.pop_back();
        name = HostSettings::Trim(name);
    }
    return name;
}
inline std::string Serialize(const Info &info)
{
    std::string text = "# Neo98 game machine state\n";
    const auto line = [&](const std::string &key, const std::string &value) { text += key + " = " + value + "\n"; };
    line("format", std::to_string(info.format));
    line("id", info.id);
    line("name", CleanName(info.name));
    line("time", std::to_string(info.time));
    line("kind", info.kind);
    line("parent", info.parent);
    line("game", info.game);
    line("program", info.program);
    line("revision", info.revision);
    line("engine", info.engine);
    line("layers", info.layers);
    line("registry", info.registry ? "1" : "0");
    line("machine.size", std::to_string(info.machine_size));
    char sum[16];
    snprintf(sum, sizeof sum, "%08x", info.machine_sum);
    line("machine.sum", sum);
    for (const auto &pair : info.machine)
        line("machine." + pair.first, pair.second);
    for (const auto &pair : info.view)
        line("view." + pair.first, pair.second);
    for (const auto &file : info.files)
    {
        char numbers[96];
        snprintf(numbers, sizeof numbers, "%c\t%llu\t%lld.%09ld\t", file.drive, file.size, file.seconds, file.nanoseconds);
        line("file", numbers + file.path);
    }
    line("end", "1");
    return text;
}
inline bool Parse(const std::string &text, Info &info)
{
    info = Info();
    info.format = 0;
    bool any = false;
    for (size_t at = 0; at < text.size();)
    {
        size_t end = text.find('\n', at);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const auto equals = line.find(" = ");
        if (line.empty() || line[0] == '#')
            continue;
        // A value may be empty; the line then ends with "= ".
        std::string key, value;
        if (equals != std::string::npos)
        {
            key = line.substr(0, equals);
            value = line.substr(equals + 3);
        }
        else if (line.size() > 2 && !line.compare(line.size() - 2, 2, " ="))
            key = line.substr(0, line.size() - 2);
        else
            continue;
        any = true;
        if (key == "format")
            info.format = unsigned(strtoul(value.c_str(), nullptr, 10));
        else if (key == "id")
            info.id = value;
        else if (key == "name")
            info.name = value;
        else if (key == "time")
            info.time = strtoll(value.c_str(), nullptr, 10);
        else if (key == "kind")
            info.kind = value;
        else if (key == "parent")
            info.parent = value;
        else if (key == "game")
            info.game = value;
        else if (key == "program")
            info.program = value;
        else if (key == "revision")
            info.revision = value;
        else if (key == "engine")
            info.engine = value;
        else if (key == "layers")
            info.layers = value;
        else if (key == "registry")
            info.registry = value == "1";
        else if (key == "machine.size")
            info.machine_size = strtoull(value.c_str(), nullptr, 10);
        else if (key == "machine.sum")
            info.machine_sum = unsigned(strtoul(value.c_str(), nullptr, 16));
        else if (!key.compare(0, 8, "machine."))
            info.machine.emplace_back(key.substr(8), value);
        else if (!key.compare(0, 5, "view."))
            info.view.emplace_back(key.substr(5), value);
        else if (key == "file")
        {
            BaseFile file;
            const auto one = value.find('\t'), two = one == std::string::npos ? one : value.find('\t', one + 1),
                       three = two == std::string::npos ? two : value.find('\t', two + 1);
            if (three == std::string::npos || one != 1)
                continue;
            file.drive = value[0];
            file.size = strtoull(value.c_str() + one + 1, nullptr, 10);
            char *rest = nullptr;
            file.seconds = strtoll(value.c_str() + two + 1, &rest, 10);
            file.nanoseconds = rest && *rest == '.' ? strtol(rest + 1, nullptr, 10) : 0;
            file.path = value.substr(three + 1);
            info.files.push_back(file);
        }
        else if (key == "end")
            info.complete = value == "1";
    }
    return any;
}
inline unsigned FileSum(const std::string &path, unsigned long long &size)
{
    size = 0;
    uLong sum = crc32(0, nullptr, 0);
    if (FILE *file = fopen(path.c_str(), "rb"))
    {
        static unsigned char block[1 << 16];
        size_t got;
        while ((got = fread(block, 1, sizeof block, file)) > 0)
        {
            sum = crc32(sum, block, uInt(got));
            size += got;
        }
        fclose(file);
    }
    return unsigned(sum);
}

// --- What a state has to fit to ---------------------------------------------
// The game machine as it is now.
struct Context
{
    std::string folder;
    std::string revision;  // of this NEO98
    // The engine of each kind of supplier this host has (kind, engine).
    std::vector<std::pair<std::string, std::string>> engines;
    std::string program;   // game.program
    std::string roots[26]; // the folders of the normal drives below the layers
    // What runs in the machine, when one runs: the kind of its supplier and
    // what it says about its machine. Empty when nothing is known; then a
    // state is checked against these when it is put into the machine.
    std::string game;
    std::vector<std::pair<std::string, std::string>> machine;
};
// From the settings of the game machine. False when the folder is none.
inline bool ContextOf(const std::string &folder, const std::string &revision, Context &context)
{
    HostSettings::Settings settings;
    if (!HostGameMachine::LoadSettings(folder, settings))
        return false;
    const auto engines = context.engines;
    context = Context();
    context.engines = engines;
    context.folder = folder;
    context.revision = revision;
    context.program = settings.game_program;
    context.roots[2] = HostSettings::DriveC(settings.game_source);
    for (int letter = 3; letter < 26; ++letter)
        context.roots[letter] = settings.drives[letter];
    return true;
}
// A file of a normal drive as it is now, for the list of a state.
inline bool Describe(const std::string &root, char drive, const std::string &path, BaseFile &out)
{
    struct stat info;
    if (stat((root + "/" + path).c_str(), &info) || !S_ISREG(info.st_mode))
        return false;
    out.drive = drive;
    out.path = path;
    out.size = (unsigned long long)info.st_size;
    ModifiedTime(info, out.seconds, out.nanoseconds);
    return true;
}
inline std::string GuestName(const BaseFile &file)
{
    std::string name = std::string(1, file.drive) + ":\\" + file.path;
    for (auto &c : name)
        if (c == '/')
            c = '\\';
    return name;
}

struct Entry : Info
{
    std::string folder;
    bool usable = false;  // it can be loaded
    std::string reason;   // why not, for the person
    bool damaged = false; // the folder is not a whole state
    bool current = false; // the machine stands on it
    unsigned depth = 0;   // how many states lie between it and a first one
    unsigned children = 0;
    bool has_picture = false;
};
// Whether the state fits the machine as it is; the reason when not.
inline void Check(Entry &entry, const Context &context)
{
    entry.usable = false;
    entry.reason.clear();
    entry.damaged = true;
    if (!entry.complete || entry.id.empty())
    {
        entry.reason = "The state is damaged: its description (zustand.ini) is missing or incomplete.";
        return;
    }
    if (entry.format != Format)
    {
        entry.damaged = false;
        entry.reason = "The state uses a different format (" + std::to_string(entry.format) + "); this Neo98 build reads format " +
                       std::to_string(Format) + ".";
        return;
    }
    for (char letter : entry.layers)
        if (!IsDirectory(entry.folder + "/" + LayerName(letter)))
        {
            entry.reason = "The state is damaged: the copy of drive " + std::string(1, letter) + ": is missing.";
            return;
        }
    if (entry.registry && !IsFile(entry.folder + "/" + RegistryName))
    {
        entry.reason = "The state is damaged: its registry copy is missing.";
        return;
    }
    const bool files_only = entry.game == "off";
    if (!files_only)
    {
        struct stat info;
        if (stat(MachineFile(entry.folder).c_str(), &info) || !S_ISREG(info.st_mode) ||
            (unsigned long long)info.st_size != entry.machine_size || !entry.machine_size)
        {
            entry.reason = "The state is damaged: its machine data (Maschine.bin) is missing or incomplete.";
            return;
        }
    }
    entry.damaged = false;
    // The files alone fit every NEO98 and every state of the normal drives:
    // that is what a write layer is.
    if (files_only)
    {
        entry.usable = true;
        return;
    }
    if (!context.program.empty() && strcasecmp(context.program.c_str(), entry.program.c_str()))
    {
        entry.reason = "The state belongs to another program (" + entry.program + ").";
        return;
    }
    // The bytes of the machine state are laid out by its supplier; a host
    // whose supplier lays them out otherwise cannot read them. The revision
    // of NEO98 as a whole does not matter.
    {
        const std::string *engine = nullptr;
        for (const auto &pair : context.engines)
            if (pair.first == entry.game)
                engine = &pair.second;
        if (!engine)
        {
            entry.reason = "This Neo98 build does not recognize the state's program type (" + entry.game + ").";
            return;
        }
        // DOS providers 2 and 3 preserve the legacy core archive. Only
        // these explicit older heads migrate, with an identical core key.
        const bool dos_v1 = entry.game == "dos" && engine->compare(0, 6, "dos-2-") == 0 &&
                            entry.engine.compare(0, 6, "dos-1-") == 0 && engine->substr(6) == entry.engine.substr(6);
        const bool dos_v3 = entry.game == "dos" && engine->compare(0, 6, "dos-3-") == 0 &&
                            (entry.engine.compare(0, 6, "dos-1-") == 0 || entry.engine.compare(0, 6, "dos-2-") == 0) &&
                            engine->substr(6) == entry.engine.substr(6);
        if (*engine != entry.engine && !dos_v1 && !dos_v3)
        {
            entry.reason = std::string("The state was created with a different version of ") +
                           (entry.game == "dos" ? "the DOS machine" : "the machine") + " in Neo98 (" +
                           (entry.engine.empty() ? std::string("unknown") : entry.engine) + "; current " + *engine +
                           "). Its state layout is incompatible with this version.";
            return;
        }
    }
    // The files of the normal drives the machine had read.
    std::string first;
    unsigned changed = 0;
    for (const auto &file : entry.files)
    {
        const int index = (file.drive & ~0x20) - 'A';
        BaseFile now;
        const bool there = index >= 2 && index < 26 && !context.roots[index].empty() &&
                           Describe(context.roots[index], file.drive, file.path, now);
        if (there && now.size == file.size && now.seconds == file.seconds && now.nanoseconds == file.nanoseconds)
            continue;
        if (!changed++)
            first = GuestName(file) + (there ? "" : " (missing)");
    }
    if (changed)
    {
        entry.reason = "Game files on the main drive have changed since this state was saved: " + first +
                       (changed > 1 ? " and " + std::to_string(changed - 1) + " more" : std::string()) +
                       ". The state is no longer compatible.";
        return;
    }
    if (!context.game.empty() && context.game != entry.game)
    {
        entry.reason = "The state belongs to a different program type (" + entry.game + ").";
        return;
    }
    if (!context.game.empty())
        for (const auto &needed : entry.machine)
        {
            const std::string *have = nullptr;
            for (const auto &pair : context.machine)
                if (pair.first == needed.first)
                    have = &pair.second;
            if (have && *have != needed.second)
            {
                const std::string label = needed.first == "speicher" ? "memory" : needed.first == "grafik" ? "graphics" : needed.first;
                entry.reason = "The machine settings have changed since this state was saved (" + label + ": " + *have +
                               " instead of " + needed.second + "). Restore the program's previous settings.";
                return;
            }
        }
    entry.usable = true;
}

// --- The tree ---------------------------------------------------------------
// The state the machine stands on: the one last saved or loaded. The next
// state saved comes from it.
inline std::string Current(const std::string &folder)
{
    std::string text;
    ReadFile(Directory(folder) + "/aktuell", text);
    text = HostSettings::Trim(text);
    return text.find('/') == std::string::npos && IsDirectory(StateFolder(folder, text)) ? text : std::string();
}
inline bool SetCurrent(const std::string &folder, const std::string &id)
{
    return WriteFile(Directory(folder) + "/aktuell", id + "\n");
}
inline bool ReadInfo(const std::string &state, Info &info)
{
    std::string text;
    return ReadFile(state + "/zustand.ini", text) && Parse(text, info);
}
// Every state of the game machine, in the order of the tree: a state, then
// what came from it, the older branch first; first states by their time.
// Folders that are no whole state are listed as damaged, so they can be seen
// and removed; what is being written or removed is hidden and not listed.
inline std::vector<Entry> List(const Context &context)
{
    std::vector<Entry> found;
    const std::string directory = Directory(context.folder), current = Current(context.folder);
    DIR *handle = opendir(directory.c_str());
    if (!handle)
        return found;
    while (const dirent *item = readdir(handle))
    {
        if (item->d_name[0] == '.')
            continue;
        Entry entry;
        entry.folder = directory + "/" + item->d_name;
        if (!IsDirectory(entry.folder))
            continue;
        Info info;
        if (ReadInfo(entry.folder, info))
            static_cast<Info &>(entry) = info;
        // The folder is the id, whatever the file says: a copy somebody
        // made of a state folder is a state of its own.
        entry.id = item->d_name;
        if (entry.name.empty())
            entry.name = entry.id;
        if (!entry.time)
        {
            struct stat info_of;
            if (!stat(entry.folder.c_str(), &info_of))
                entry.time = info_of.st_mtime;
        }
        entry.current = entry.id == current;
        entry.has_picture = IsFile(PictureFile(entry.folder));
        Check(entry, context);
        found.push_back(entry);
    }
    closedir(handle);
    std::sort(found.begin(), found.end(), [](const Entry &a, const Entry &b) { return a.time != b.time ? a.time < b.time : a.id < b.id; });
    // A parent that is gone makes a first state, and so does a ring of
    // states that name each other: it is cut at the first of them.
    std::map<std::string, size_t> index;
    for (size_t i = 0; i < found.size(); ++i)
        index[found[i].id] = i;
    std::vector<int> parent(found.size(), -1);
    for (size_t i = 0; i < found.size(); ++i)
        if (const auto it = index.find(found[i].parent); !found[i].parent.empty() && it != index.end() && it->second != i)
            parent[i] = int(it->second);
    for (size_t i = 0; i < found.size(); ++i)
    {
        int at = parent[i];
        for (size_t steps = 0; at >= 0 && steps <= found.size(); ++steps)
        {
            if (size_t(at) == i)
            {
                parent[i] = -1;
                break;
            }
            at = parent[size_t(at)];
        }
    }
    std::vector<Entry> ordered;
    std::vector<std::pair<int, unsigned>> stack;
    for (size_t i = found.size(); i-- > 0;)
        if (parent[i] < 0)
            stack.emplace_back(int(i), 0u);
    while (!stack.empty())
    {
        const auto [at, depth] = stack.back();
        stack.pop_back();
        Entry entry = found[size_t(at)];
        entry.depth = depth;
        if (parent[size_t(at)] < 0)
            entry.parent.clear();
        for (size_t i = found.size(); i-- > 0;)
            if (parent[i] == at)
            {
                stack.emplace_back(int(i), depth + 1);
                ++entry.children;
            }
        ordered.push_back(entry);
    }
    return ordered;
}
// How many states a game machine has, for its line in a menu and for the
// question before it is deleted.
inline unsigned Count(const std::string &folder)
{
    unsigned count = 0;
    if (DIR *handle = opendir(Directory(folder).c_str()))
    {
        while (const dirent *item = readdir(handle))
            if (item->d_name[0] != '.' && IsDirectory(Directory(folder) + "/" + item->d_name))
                ++count;
        closedir(handle);
    }
    return count;
}

// --- Saving -----------------------------------------------------------------
struct Request
{
    std::string name;   // empty: one from the kind and the time
    std::string kind;   // "manual", "quick", "auto"
    std::string game;   // "dos", "windows", "off"
    std::string revision;
    std::string engine; // of the supplier of `machine`
    // The state of the machine and its last picture; both may be missing for
    // a state of the files alone.
    const std::vector<unsigned char> *machine = nullptr;
    const unsigned *picture = nullptr;
    unsigned picture_width = 0, picture_height = 0;
    std::vector<std::pair<std::string, std::string>> machine_info;
    std::vector<std::pair<std::string, std::string>> view;
    std::vector<BaseFile> files;
};
inline std::string TimeText(long long when, const char *format)
{
    const time_t seconds = time_t(when);
    struct tm parts = {};
    localtime_r(&seconds, &parts);
    char text[64];
    strftime(text, sizeof text, format, &parts);
    return text;
}
inline std::string DefaultName(const std::string &kind, long long when)
{
    // The list shows the time beside every name.
    if (kind == "auto")
        return "Before loading";
    if (kind == "quick")
        return "Quick save";
    return "State from " + TimeText(when, "%d.%m.%Y %H:%M");
}
inline bool Delete(const std::string &folder, const std::string &id, std::string &error);
// Removes the automatic states beyond the newest `keep`. The state the
// machine stands on and `spared` (the one about to be loaded) stay.
inline void Prune(const std::string &folder, const std::string &spared = std::string(), unsigned keep = KeepAutomatic)
{
    std::vector<std::pair<long long, std::string>> automatic;
    if (DIR *handle = opendir(Directory(folder).c_str()))
    {
        while (const dirent *item = readdir(handle))
        {
            Info info;
            if (item->d_name[0] != '.' && ReadInfo(Directory(folder) + "/" + item->d_name, info) && info.complete && info.kind == "auto")
                automatic.emplace_back(info.time, item->d_name);
        }
        closedir(handle);
    }
    std::sort(automatic.begin(), automatic.end());
    const std::string current = Current(folder);
    std::string error;
    for (size_t i = 0; i + keep < automatic.size(); ++i)
        if (automatic[i].second != spared && automatic[i].second != current)
            Delete(folder, automatic[i].second, error);
}
// Writes a state of the game machine as it lies on the disk now. The caller
// holds the machine still and has written out what it kept in memory (the
// sidecars of the layers, the registry journal). The state comes from the
// one the machine stands on, and the machine stands on the new one after.
inline bool Save(const std::string &folder, const Request &request, std::string &id, std::string &error)
{
    id.clear();
    HostSettings::Settings settings;
    if (!HostGameMachine::LoadSettings(folder, settings))
    {
        error = "Not a game machine: " + folder;
        return false;
    }
    const std::string directory = Directory(folder);
    if (!HostSettings::MakeDirectories(directory))
    {
        error = "The folder " + directory + " could not be created.";
        return false;
    }
    Info info;
    info.time = (long long)time(nullptr);
    for (unsigned number = 0;; ++number)
    {
        char suffix[8];
        snprintf(suffix, sizeof suffix, "-%02u", number);
        info.id = TimeText(info.time, "%Y%m%d-%H%M%S") + suffix;
        if (!Present(directory + "/" + info.id) && !Present(directory + "/.neu-" + info.id))
            break;
    }
    info.kind = request.kind.empty() ? std::string("manual") : request.kind;
    info.name = CleanName(request.name);
    if (info.name.empty())
        info.name = DefaultName(info.kind, info.time);
    info.parent = Current(folder);
    info.game = request.game.empty() || !request.machine ? std::string("off") : request.game;
    info.program = settings.game_program;
    info.revision = request.revision;
    info.engine = info.game == "off" ? std::string() : request.engine;
    info.machine = request.machine_info;
    info.view = request.view;
    info.files = request.files;
    const std::string partial = directory + "/.neu-" + info.id;
    const auto fail = [&](const std::string &text) {
        RemoveTree(partial);
        error = text;
        return false;
    };
    if (mkdir(partial.c_str(), 0755))
        return fail("The folder " + partial + " could not be created.");
    if (info.game != "off")
    {
        if (!WriteFile(MachineFile(partial), request.machine->data(), request.machine->size()))
            return fail("Could not write the machine state. Check available disk space.");
        info.machine_size = request.machine->size();
        info.machine_sum = unsigned(crc32(crc32(0, nullptr, 0), request.machine->data(), uInt(request.machine->size())));
    }
    Stop("save-machine");
    if (request.picture)
        WritePicture(PictureFile(partial), request.picture, request.picture_width, request.picture_height);
    for (char letter = 'C'; letter <= 'Z'; ++letter)
    {
        const std::string layer = folder + "/" + LayerName(letter);
        if (!IsDirectory(layer))
            continue;
        if (!CopyTree(layer, partial + "/" + LayerName(letter), "save-copy"))
            return fail("Drive " + std::string(1, letter) + ": of the game machine could not be copied. Check available disk space.");
        info.layers += letter;
    }
    if (const std::string journal = folder + "/" + RegistryName; IsFile(journal))
    {
        if (!HostVolume::CopyFile(journal, partial + "/" + RegistryName))
            return fail("Could not copy the game machine's registry.");
        info.registry = true;
    }
    Stop("save-ini");
    if (!WriteFile(partial + "/zustand.ini", Serialize(info)))
        return fail("Could not write the state description.");
    Stop("save-rename");
    if (rename(partial.c_str(), (directory + "/" + info.id).c_str()))
        return fail("Could not store the state.");
    Stop("save-current");
    SetCurrent(folder, info.id);
    id = info.id;
    return true;
}

// --- Loading ----------------------------------------------------------------
inline std::string LoadFolder(const std::string &folder) { return folder + "/.laden"; }
// The swap of a load whose copies are whole: every layer of the game machine
// makes room for the copy of the state, folder by folder. It can be run
// again after it was cut short and ends in the same place.
inline bool Swap(const std::string &folder)
{
    const std::string staging = LoadFolder(folder);
    std::string mark;
    if (!ReadFile(staging + "/bereit", mark))
        return false;
    Info wanted;
    Parse(mark, wanted);
    bool okay = true;
    for (char letter = 'C'; letter <= 'Z'; ++letter)
    {
        const std::string name = LayerName(letter), live = folder + "/" + name, copy = staging + "/" + name, old = staging + "/alt-" + name;
        const bool in_state = wanted.layers.find(letter) != std::string::npos;
        // Not moved in yet, or the state has none: the live one goes.
        if (Present(live) && (Present(copy) || !in_state))
        {
            RemoveTree(old);
            okay = !rename(live.c_str(), old.c_str()) && okay;
        }
        Stop("load-swap");
        if (Present(copy))
            okay = !rename(copy.c_str(), live.c_str()) && okay;
        Stop("load-swap");
    }
    const std::string live = folder + "/" + RegistryName, copy = staging + "/" + RegistryName;
    if (Present(copy))
        okay = !rename(copy.c_str(), live.c_str()) && okay;
    else if (!wanted.registry)
        unlink(live.c_str());
    // A journal half written by a host that was cut off belongs to the old state.
    unlink((live + ".new").c_str());
    if (!okay)
        return false;
    Stop("load-current");
    SetCurrent(folder, wanted.id);
    return RemoveTree(staging);
}
// What a start of the game machine does first, while it holds the lock and
// before it opens a layer: a load that was cut short is finished when its
// copies were whole and dropped when not; folders of states that were being
// written or removed go.
inline void Recover(const std::string &folder)
{
    if (const std::string staging = LoadFolder(folder); Present(staging))
    {
        if (IsFile(staging + "/bereit"))
            Swap(folder);
        else
            RemoveTree(staging);
    }
    const std::string directory = Directory(folder);
    std::vector<std::string> names;
    if (DIR *handle = opendir(directory.c_str()))
    {
        while (const dirent *item = readdir(handle))
            if (!strncmp(item->d_name, ".neu-", 5) || !strncmp(item->d_name, ".weg-", 5))
                names.push_back(item->d_name);
        closedir(handle);
    }
    for (const auto &name : names)
        RemoveTree(directory + "/" + name);
}
// Puts the files of a state in the place of the layers and the journal of
// the game machine. The machine does not run and the caller holds its lock;
// what the machine had written since is gone afterwards, so the caller has
// saved it before (an automatic state). The state of the machine itself is
// the caller's to put into a machine it starts next.
inline bool Restore(const std::string &folder, const std::string &id, std::string &error)
{
    const std::string state = StateFolder(folder, id), staging = LoadFolder(folder);
    Info info;
    if (id.empty() || id.find('/') != std::string::npos || !ReadInfo(state, info) || !info.complete)
    {
        error = "The state " + id + " is missing or damaged.";
        return false;
    }
    Recover(folder);
    if (Present(staging))
    {
        error = "A previous load did not finish (" + staging + ").";
        return false;
    }
    const auto fail = [&](const std::string &text) {
        RemoveTree(staging);
        error = text;
        return false;
    };
    if (mkdir(staging.c_str(), 0755))
        return fail("The folder " + staging + " could not be created.");
    for (char letter : info.layers)
        if (!CopyTree(state + "/" + LayerName(letter), staging + "/" + LayerName(letter), "load-copy"))
            return fail("Drive " + std::string(1, letter) + ": of the saved state could not be copied. Check available disk space.");
    if (info.registry && !HostVolume::CopyFile(state + "/" + RegistryName, staging + "/" + RegistryName))
        return fail("Could not copy the state's registry.");
    Stop("load-ready");
    // From here on the load happens, now or at the next start.
    Info mark;
    mark.id = id;
    mark.layers = info.layers;
    mark.registry = info.registry;
    if (!WriteFile(staging + "/bereit", Serialize(mark)))
        return fail("Could not prepare to load the state.");
    Stop("load-marked");
    if (!Swap(folder))
    {
        error = "Could not replace the game machine's drives (" + staging + ").";
        return false;
    }
    return true;
}

// --- Changing the tree ------------------------------------------------------
inline bool Rewrite(const std::string &state, const Info &info) { return WriteFile(state + "/zustand.ini", Serialize(info)); }
// Removes a state. What came from it comes from its parent afterwards, and a
// machine that stood on it stands on its parent.
inline bool Delete(const std::string &folder, const std::string &id, std::string &error)
{
    const std::string directory = Directory(folder), state = directory + "/" + id;
    if (id.empty() || id[0] == '.' || id.find('/') != std::string::npos || !IsDirectory(state))
    {
        error = "The state " + id + " does not exist.";
        return false;
    }
    Info doomed;
    ReadInfo(state, doomed);
    std::vector<std::string> names;
    if (DIR *handle = opendir(directory.c_str()))
    {
        while (const dirent *item = readdir(handle))
            if (item->d_name[0] != '.' && id != item->d_name)
                names.push_back(item->d_name);
        closedir(handle);
    }
    for (const auto &name : names)
    {
        Info child;
        if (!ReadInfo(directory + "/" + name, child) || !child.complete || child.parent != id)
            continue;
        child.parent = doomed.parent == name ? std::string() : doomed.parent;
        if (!Rewrite(directory + "/" + name, child))
        {
            error = "The state " + name + " could not be reattached.";
            return false;
        }
    }
    Stop("delete-children");
    if (Current(folder) == id)
        SetCurrent(folder, doomed.parent);
    // Out of the list first, then removed.
    const std::string gone = directory + "/.weg-" + id;
    RemoveTree(gone);
    if (rename(state.c_str(), gone.c_str()))
    {
        error = "The state " + id + " could not be deleted.";
        return false;
    }
    Stop("delete-renamed");
    RemoveTree(gone);
    return true;
}
// An automatic state the person wants to keep: it is the person's from then
// on and no longer removed with the old ones.
inline bool Keep(const std::string &folder, const std::string &id, std::string &error)
{
    const std::string state = StateFolder(folder, id);
    Info info;
    if (id.empty() || id.find('/') != std::string::npos || !ReadInfo(state, info) || !info.complete)
    {
        error = "The state " + id + " is missing or damaged.";
        return false;
    }
    if (info.kind == "auto")
    {
        info.kind = "manual";
        if (!Rewrite(state, info))
        {
            error = "Could not update the state.";
            return false;
        }
    }
    return true;
}
// A name of the person's. An automatic state that gets one is kept as well.
inline bool Rename(const std::string &folder, const std::string &id, const std::string &name, std::string &error)
{
    const std::string state = StateFolder(folder, id);
    Info info;
    if (id.empty() || id.find('/') != std::string::npos || !ReadInfo(state, info) || !info.complete)
    {
        error = "The state " + id + " is missing or damaged.";
        return false;
    }
    const std::string clean = CleanName(name);
    if (clean.empty())
    {
        error = "The name is empty.";
        return false;
    }
    info.name = clean;
    if (info.kind == "auto")
        info.kind = "manual";
    if (!Rewrite(state, info))
    {
        error = "Could not save the name.";
        return false;
    }
    return true;
}
} // namespace HostSaveState
