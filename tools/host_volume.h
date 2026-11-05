// SPDX-License-Identifier: GPL-2.0-or-later
// POSIX implementation of the writable volume provider of src/volume.h.
// One host directory is one drive. The host file system cannot hold what a
// FAT32 directory holds, so every directory carries a sidecar file next to its
// entries:
//.neo98-index one line per entry:
// flags<TAB>attributes<TAB>cdate,ctime,chundredths<TAB>alias<TAB>name.neo98-volume at the root of the drive: label and serial number
// `flags` bit 0 marks a tombstone: an entry the guest removed although the
// read-only Win98 tree below still has it. Bit 1 marks an opaque directory:
// one the guest created again after removing the tree's directory of that
// name, so the tree's entries below it stay hidden. Bit 2 marks a pin: no
// entry of this directory, only the 8.3 alias an entry of the drive below
// answers to in this layer. Bit 3 marks an entry made anew where one
// of the drive below was removed: it is listed where it was made, not where
// the entry below stands. The other fields are the DOS attribute byte, the
// creation time FAT keeps and the 8.3 alias. Access and write times of a
// file come from the host file itself; a directory keeps the times of its
// creation, as a FAT directory entry does.
// A file put into the directory by hand appears in the guest on the next
// lookup: the provider merges what it reads from the sidecar with what the
// directory actually contains and writes the missing records back.
// This header lives outside src/ because it opens host files, exactly like
// tools/system_tree_host.h.
#pragma once
#include "legacy_data.h"
#include "../src/volume.h"
#include "../src/system_tree.h"
#include "host_cd_image.h"
#include "../src/win98_short_alias.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <string>
#include <sys/stat.h>
#ifdef __APPLE__
#include <sys/clonefile.h>
#endif
#ifdef __linux__
#include <linux/fs.h>
#include <sys/ioctl.h>
#endif
#include <mutex>
#include <sys/resource.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace HostVolume
{
using U = unsigned;
inline U Fold(U c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }
inline std::string Upper(const std::string &text)
{
    std::string out = text;
    for (auto &c : out)
        c = char(Fold((unsigned char)c));
    return out;
}
inline bool Same(const std::string &a, const std::string &b)
{
    return a.size() == b.size() && Upper(a) == Upper(b);
}
// A guest file name is a string of single bytes in the ANSI code page; a macOS
// directory wants UTF-8 and refuses a name that is not. Every byte above 127
// therefore travels as the two-byte UTF-8 form of the code point with the same
// number, which is reversible for all 256 byte values. A host name this runtime
// did not write and that needs more than that keeps a filler byte instead;
// there is no single-byte spelling for it.
inline std::string ToHost(const std::string &guest)
{
    std::string out;
    for (unsigned char c : guest)
    {
        if (c < 0x80)
            out += char(c);
        else
        {
            out += char(0xc0 | (c >> 6));
            out += char(0x80 | (c & 0x3f));
        }
    }
    return out;
}
inline std::string ToGuest(const std::string &host)
{
    std::string out;
    for (size_t i = 0; i < host.size(); ++i)
    {
        const unsigned char c = (unsigned char)host[i];
        if (c < 0x80)
        {
            out += char(c);
            continue;
        }
        if ((c & 0xe0) == 0xc0 && i + 1 < host.size() && ((unsigned char)host[i + 1] & 0xc0) == 0x80)
        {
            out += char(((c & 0x1f) << 6) | ((unsigned char)host[i + 1] & 0x3f));
            ++i;
            continue;
        }
        out += '_';
        while (i + 1 < host.size() && ((unsigned char)host[i + 1] & 0xc0) == 0x80)
            ++i;
    }
    return out;
}
// FAT date and time words out of a host timestamp and back.
inline void Encode(time_t when, U &date, U &time)
{
    struct tm parts = {};
    localtime_r(&when, &parts);
    const int year = parts.tm_year + 1900;
    date = U(((year < 1980 ? 0 : year - 1980) << 9) | ((parts.tm_mon + 1) << 5) | parts.tm_mday);
    time = U((parts.tm_hour << 11) | (parts.tm_min << 5) | (parts.tm_sec / 2));
}
inline time_t Decode(U date, U time)
{
    if (!date)
        return 0;
    struct tm parts = {};
    parts.tm_year = int((date >> 9) & 127) + 1980 - 1900;
    parts.tm_mon = int((date >> 5) & 15) - 1;
    parts.tm_mday = int(date & 31);
    parts.tm_hour = int((time >> 11) & 31);
    parts.tm_min = int((time >> 5) & 63);
    parts.tm_sec = int((time & 31) * 2);
    parts.tm_isdst = -1;
    return mktime(&parts);
}

// A copy of one file of this host: the one place that knows how the medium
// copies cheaply. On APFS that is a block clone (clonefile), which costs no
// space until one of the two files changes; everywhere else, and wherever
// the clone is refused (another volume, another file system), the bytes are
// copied. Linux asks the filesystem for a reflink with FICLONE. The mode
// bits come along.
inline bool CopyFile(const std::string &from, const std::string &to)
{
    unlink(to.c_str());
    static const bool plain = getenv("NEO98_COPY_PLAIN") && *getenv("NEO98_COPY_PLAIN") == '1';
#ifdef __APPLE__
    // NEO98_COPY_PLAIN=1 leaves the clone out: the tests take the way of a
    // medium that cannot clone as well.
    if (!plain && !clonefile(from.c_str(), to.c_str(), 0))
        return true;
#endif
    struct stat info;
    const int in = ::open(from.c_str(), O_RDONLY);
    if (in < 0 || fstat(in, &info))
    {
        if (in >= 0)
            close(in);
        return false;
    }
    const int out = ::open(to.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
    bool okay = out >= 0;
#ifdef __linux__
    if (okay && !plain && !ioctl(out, FICLONE, in))
    {
        okay = !fchmod(out, info.st_mode & 0777);
        close(in);
        okay = !close(out) && okay;
        if (!okay) unlink(to.c_str());
        return okay;
    }
#endif
    static char block[1 << 20];
    for (ssize_t got; okay && (got = read(in, block, sizeof block)) != 0;)
        okay = got > 0 && write(out, block, size_t(got)) == got;
    close(in);
    if (out >= 0)
    {
        fchmod(out, info.st_mode & 0777);
        okay = !close(out) && okay;
    }
    if (!okay)
        unlink(to.c_str());
    return okay;
}

class Layered;
class Drive final : public Volume::Backend
{
    friend class Layered;

  public:
    // `root` is created when it is missing. A read-only drive (a CD-ROM) never
    // writes anything, not even a sidecar.
    bool Open(const char *root_directory, bool writable_drive, const char *fallback_label)
    {
        root = root_directory;
        while (root.size() > 1 && root.back() == '/')
            root.pop_back();
        writable = writable_drive;
        struct stat info;
        if (stat(root.c_str(), &info))
        {
            if (!writable || mkdir(root.c_str(), 0755))
                return false;
        }
        else if (!S_ISDIR(info.st_mode))
            return false;
        description = root + (writable ? " (writable)" : " (read-only)");
        LoadVolume(fallback_label);
        return true;
    }
    ~Drive() override { Flush(); }

    bool Volatile() override { return true; }
    U Stamp(const char *directory) override
    {
        Place place;
        if (!Locate(directory, place, true) || !EnterFresh(place.relative, place.host))
            return ~0u;
        return outside_changes;
    }
    bool Find(const char *absolute, Volume::Entry &out) override
    {
        Place place;
        if (!Locate(absolute, place, true) || !place.record)
            return false;
        return Describe(*place.record, place.host, out) || place.record->tombstone;
    }
    bool Child(const char *directory, U index, Volume::Entry &out) override
    {
        Place place;
        if (!Locate(directory, place, !index))
            return false;
        Folder *folder = index ? Enter(place.relative, place.host) : EnterFresh(place.relative, place.host);
        if (!folder || index >= folder->records.size())
            return false;
        const auto &record = folder->records[index];
        Describe(record, place.host + "/" + ToHost(record.name), out);
        return true;
    }
    U Read(const char *absolute, U offset, void *buffer, U size, U &done) override
    {
        done = 0;
        Place place;
        if (!Locate(absolute, place) || !place.record || place.record->tombstone)
            return 2;
        const int file = Handle(place.host, false);
        if (file < 0)
            return 5;
        const ssize_t got = pread(file, buffer, size, off_t(offset));
        if (got < 0)
            return 30;
        done = U(got);
        return 0;
    }
    U Write(const char *absolute, U offset, const void *buffer, U size, U &done) override
    {
        done = 0;
        if (!writable)
            return 19;
        Place place;
        if (!Locate(absolute, place) || !place.record || place.record->tombstone)
            return 2;
        const int file = Handle(place.host, true);
        if (file < 0)
            return 5;
        const ssize_t put = pwrite(file, buffer, size, off_t(offset));
        if (put < 0)
            return errno == ENOSPC ? 112u : 30u;
        done = U(put);
        return U(put) == size ? 0u : 112u;
    }
    U Resize(const char *absolute, U size) override
    {
        if (!writable)
            return 19;
        Place place;
        if (!Locate(absolute, place) || !place.record)
            return 2;
        Release(place.host);
        if (truncate(place.host.c_str(), off_t(size)))
            return errno == ENOSPC ? 112u : 30u;
        return 0;
    }
    U Create(const char *absolute, U attributes, bool directory, Volume::Entry &out) override
    {
        if (!writable)
            return 19;
        Provision(absolute);
        Place place;
        if (!Locate(absolute, place))
            return 3;
        if (place.record && !place.record->tombstone)
            return directory ? 183u : 80u;
        if (place.name.empty())
            return 123;
        // A name that comes back where a tombstone stood is spelled as it
        // is created now, not as the removed entry was, and it is a new
        // entry of the directory: it stands behind the others.
        const bool replaces = place.record && place.record->tombstone;
        if (replaces)
        {
            Folder *parent = Enter(place.parent, place.parent_host);
            if (!parent)
                return 3;
            parent->records.erase(parent->records.begin() + (place.record - parent->records.data()));
            place.record = nullptr;
            place.name = Leaf(absolute);
            place.host = place.parent_host + "/" + ToHost(place.name);
        }
        const std::string host = place.host;
        if (directory)
        {
            if (mkdir(host.c_str(), 0755) && errno != EEXIST)
                return 5;
        }
        else
        {
            const int file = ::open(host.c_str(), O_CREAT | O_RDWR, Mode(attributes));
            if (file < 0)
                return 5;
            close(file);
        }
        Folder *folder = Enter(place.parent, place.parent_host);
        if (!folder)
            return 3;
        // The alias the layer pinned for an entry of this name below.
        const std::string pinned = TakePin(*folder, place.name);
        folder->records.push_back(Record{});
        Record *record = &folder->records.back();
        record->name = place.name;
        // A directory created where the guest removed one of the tree stays
        // opaque; the caller asks for it the same way (Files::Store::New).
        record->opaque = directory && (replaces || out.opaque);
        record->replaced = replaces;
        record->attributes = attributes ? attributes : (directory ? 16u : 128u);
        if (directory)
            record->attributes |= 16;
        record->created = out.times[0];
        Alias(*folder, *record, out.alias[0] ? out.alias : pinned.c_str());
        folder->dirty = true;
        Save(*folder);
        Stamp(host, out.times);
        Describe(*record, host, out);
        return 0;
    }
    U Remove(const char *absolute, bool directory, bool tombstone) override
    {
        if (!writable)
            return 19;
        // A tombstone for an entry of the read-only tree may be the first
        // thing this volume records in that directory.
        if (tombstone)
            Provision(absolute);
        Place place;
        if (!Locate(absolute, place))
            return 3;
        Folder *folder = Enter(place.parent, place.parent_host);
        if (!folder)
            return 3;
        Release(place.host);
        struct stat info;
        if (!stat(place.host.c_str(), &info))
        {
            // The sidecar of a directory is part of that directory, so it goes
            // before the directory itself can go. An entry left inside keeps
            // both: the caller then gets its 145 with nothing lost. Tombstones
            // of tree entries do not count: the directory's own tombstone in
            // its parent hides everything below it.
            Folder *inside = directory ? Enter(place.relative, place.host) : nullptr;
            if (inside && std::all_of(inside->records.begin(), inside->records.end(),
                                      [](const Record &child) { return child.tombstone || child.pin; }))
            {
                folders.erase(Upper(place.relative));
                unlink((place.host + "/.neo98-index").c_str());
            }
            if (directory ? rmdir(place.host.c_str()) : unlink(place.host.c_str()))
                return errno == ENOTEMPTY ? 145u : 5u;
            // A removed directory takes its sidecar with it.
            folders.erase(Upper(place.relative));
        }
        Record *record = place.record;
        if (!record && tombstone)
        {
            folder->records.push_back(Record{});
            record = &folder->records.back();
            record->name = place.name;
            record->attributes = directory ? 16u : 128u;
        }
        if (record && tombstone)
        {
            record->tombstone = true;
            record->opaque = record->replaced = false;
            record->alias.clear();
        }
        else if (record)
            folder->records.erase(folder->records.begin() + (record - folder->records.data()));
        folder->dirty = true;
        Save(*folder);
        return 0;
    }
    U Rename(const char *source, const char *destination) override
    {
        if (!writable)
            return 19;
        // The directory the entry moves into may so far exist only in the
        // tree below this layer.
        Provision(destination);
        Place from, to;
        if (!Locate(source, from) || !from.record || !Locate(destination, to))
            return 2;
        if (to.record && !to.record->tombstone)
            return 183;
        if (to.record)
        {
            to.name = Leaf(destination);
            to.host = to.parent_host + "/" + ToHost(to.name);
        }
        Release(from.host);
        Release(to.host);
        // A directory takes its sidecar and those of everything below it
        // along: what they still hold in memory is written first, and none
        // of them stays under its old key.
        if (from.record->attributes & 16)
        {
            const std::string key = Upper(from.relative), inside = key + "\\";
            for (auto it = folders.begin(); it != folders.end();)
                if (it->first == key || !it->first.compare(0, inside.size(), inside))
                {
                    if (it->second.dirty)
                        Save(it->second);
                    it = folders.erase(it);
                }
                else
                    ++it;
            Release("");
        }
        if (rename(from.host.c_str(), to.host.c_str()))
            return 5;
        Folder *source_folder = Enter(from.parent, from.parent_host);
        Folder *target_folder = Enter(to.parent, to.parent_host);
        if (!source_folder || !target_folder)
            return 3;
        Record moved = *from.record;
        // A directory moved onto the name of a removed tree directory hides
        // the tree there, as one created in its place does.
        moved.replaced = to.record != nullptr;
        moved.opaque = to.record && (moved.attributes & 16);
        const std::string source_name = moved.name;
        moved.name = to.name;
        moved.alias.clear();
        // The entry stands behind the others of the directory it moves
        // into; the tombstone it takes the name of goes. Its alias is found
        // while its old entry still stands, as a FAT directory gets the new
        // entry before it loses the old one.
        for (auto it = target_folder->records.begin(); it != target_folder->records.end(); ++it)
            if (it->tombstone && Same(it->name, to.name))
            {
                target_folder->records.erase(it);
                break;
            }
        const std::string pinned = TakePin(*target_folder, to.name);
        target_folder->records.push_back(moved);
        Alias(*target_folder, target_folder->records.back(), pinned.c_str());
        for (auto it = source_folder->records.begin(); it != source_folder->records.end(); ++it)
            if (!it->pin && !it->tombstone && Same(it->name, source_name))
            {
                source_folder->records.erase(it);
                break;
            }
        // A renamed directory keeps its own sidecar, but not under the old key.
        folders.erase(Upper(from.relative));
        source_folder->dirty = target_folder->dirty = true;
        Save(*source_folder);
        if (target_folder != source_folder)
            Save(*target_folder);
        return 0;
    }
    // The drive below this one, when this is the write layer above the
    // read-only Win98 tree: aliases issued here stay clear of the names
    // there, and a file is copied up from there (Import).
    void Below(Drive *lower) { below = lower; }
    bool Import(const char *absolute) override
    {
        if (!writable || !below)
            return false;
        Place place, source;
        if (!Locate(absolute, place) || !place.record || place.record->tombstone ||
            !below->Locate(absolute, source) || !source.record || source.record->tombstone)
            return false;
        struct stat info;
        if (stat(source.host.c_str(), &info) || !S_ISREG(info.st_mode))
            return false;
        Release(place.host);
        // Create left an empty file; the copy takes its place.
        if (CopyFile(source.host, place.host))
        {
            chmod(place.host.c_str(), 0644);
            return true;
        }
        // Back to the empty file of Create; the caller writes the bytes.
        const int empty = ::open(place.host.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0644);
        if (empty >= 0)
            close(empty);
        return false;
    }
    U SetAttributes(const char *absolute, U attributes) override
    {
        if (!writable)
            return 19;
        Place place;
        if (!Locate(absolute, place) || !place.record)
            return 2;
        place.record->attributes = attributes;
        Folder *folder = Enter(place.parent, place.parent_host);
        if (folder)
            folder->dirty = true;
        // The read-only bit is the one thing the host mode bits can carry, and
        // only for a file. On a FAT directory the bit forbids nothing: Win98
        // creates, renames and deletes inside it, and the shell sets it on a
        // folder with a desktop.ini (IE4UINIT marks C:\Program Files so). A
        // host directory without write permission refused every new file
        // there, and ADVPACK's probe of its destination failed; the
        // bit stays in the sidecar record.
        struct stat info;
        if (!stat(place.host.c_str(), &info))
            chmod(place.host.c_str(), info.st_mode & S_IFDIR ? 0755u : Mode(attributes));
        return 0;
    }
    U SetTimes(const char *absolute, const Files::TimeStamp *times) override
    {
        if (!writable)
            return 19;
        Place place;
        if (!Locate(absolute, place) || !place.record)
            return 2;
        if (times[0].date && (place.record->created.date != times[0].date ||
                              place.record->created.time != times[0].time ||
                              place.record->created.hundredths != times[0].hundredths))
        {
            place.record->created = times[0];
            if (Folder *folder = Enter(place.parent, place.parent_host))
                folder->dirty = true;
        }
        Stamp(place.host, times);
        return 0;
    }
    void Space(unsigned long long &total, unsigned long long &available) override
    {
        total = available = 0;
        struct statvfs info;
        if (statvfs(root.c_str(), &info))
            return;
        total = (unsigned long long)info.f_blocks * info.f_frsize;
        available = (unsigned long long)info.f_bavail * info.f_frsize;
    }
    const char *Label() override { return label.c_str(); }
    bool SetLabel(const char *text) override
    {
        if (!writable)
            return false;
        label = text ? text : "";
        if (label.size() > 11)
            label.resize(11);
        SaveVolume();
        return true;
    }
    U Serial() override { return serial; }
    void Flush() override
    {
        for (auto &entry : folders)
            if (entry.second.dirty)
                Save(entry.second);
        Release("");
    }
    const char *Description() const override { return description.c_str(); }

  private:
    struct Record
    {
        std::string name, alias;
        U attributes = 128;
        Files::TimeStamp created;
        bool tombstone = false, opaque = false;
        // The layer's own alias for an entry of the drive below, and an
        // entry made where one of the drive below was removed (flags bits 2
        // and 3). `generated`: the alias was made up when the directory was
        // read and stands in no sidecar (a read-only drive writes none).
        bool pin = false, replaced = false, generated = false;
    };
    struct Folder
    {
        std::string host, relative;
        std::vector<Record> records;
        bool dirty = false;
        // The host directory as it was when it was read.
        struct timespec changed = {};
        ino_t inode = 0;
    };
    // Where one guest path lands: its parent folder, its own name, its host
    // path and, when it exists, its record.
    struct Place
    {
        std::string relative, parent, parent_host, name, host;
        Record *record = nullptr;
    };
    std::string root, description, label = "Neo98";
    bool writable = true;
    Drive *below = nullptr;
    // A read-only drive keeps the files it last read open: a program reads
    // several data files in turn, and a file of the tree that goes away
    // while a program reads it stays readable.
    struct Reader
    {
        std::string path;
        int file = -1;
        unsigned long used = 0;
    };
    std::vector<Reader> readers;
    unsigned long reads = 0;
    // Whether the tree below shows through in the directory `relative` of
    // this layer: not below a directory made anew in place of a removed one.
    bool Shows(const std::string &relative)
    {
        for (size_t at = 0; at <= relative.size(); ++at)
        {
            if (at != relative.size() && relative[at] != '\\')
                continue;
            if (!at)
                continue;
            Place place;
            if (!Locate(("C:\\" + relative.substr(0, at)).c_str(), place))
                return true; // nothing of this path is in the layer
            if (place.record && (place.record->opaque || place.record->tombstone))
                return false;
        }
        return true;
    }
    // Whether the directory `relative` of this drive has an entry that
    // answers to `candidate`. An entry the layer `above` has a record for
    // does not count: the layer's own record stands for it, or it is a
    // tombstone and the entry is gone.
    bool Names(const std::string &relative, const char *candidate, const Folder &above)
    {
        Place place;
        if (!Locate(("C:\\" + relative).c_str(), place) || (!relative.empty() && (!place.record || place.record->tombstone)))
            return false;
        Folder *folder = Enter(place.relative, place.host);
        if (!folder)
            return false;
        for (const auto &other : folder->records)
        {
            if (other.tombstone || !(Same(other.name, candidate) || Same(other.alias, candidate)))
                continue;
            bool replaced = false;
            for (const auto &record : above.records)
                replaced = replaced || Same(record.name, other.name);
            if (!replaced)
                return true;
        }
        return false;
    }
    U serial = 0;
    std::unordered_map<std::string, Folder> folders;
    U outside_changes = 0;
    // One open descriptor at a time: a guest reads and writes one file in long
    // runs, and this keeps every other path free of descriptor bookkeeping.
    int open_file = -1;
    bool open_writable = false;
    std::string open_path;

    static U Mode(U attributes) { return attributes & 1 ? 0444u : 0644u; }
    // When the entries of a host directory last changed.
    static struct timespec Changed(const struct stat &info)
    {
#ifdef __APPLE__
        return info.st_mtimespec;
#else
        return info.st_mtim;
#endif
    }
    // The last component of a guest path, as the caller spelled it.
    static std::string Leaf(const char *absolute)
    {
        std::string text = absolute ? absolute : "";
        while (!text.empty() && (text.back() == '\\' || text.back() == '/'))
            text.pop_back();
        const auto slash = text.find_last_of("\\/");
        return slash == std::string::npos ? text : text.substr(slash + 1);
    }
    int Handle(const std::string &path, bool write)
    {
        if (!writable && !write)
        {
            Reader *oldest = nullptr;
            for (auto &reader : readers)
            {
                if (reader.path == path)
                {
                    reader.used = ++reads;
                    return reader.file;
                }
                if (!oldest || reader.used < oldest->used)
                    oldest = &reader;
            }
            const int file = ::open(path.c_str(), O_RDONLY);
            if (file < 0)
                return file;
            if (readers.size() < 24)
            {
                readers.push_back(Reader{});
                oldest = &readers.back();
            }
            else
                close(oldest->file);
            *oldest = Reader{path, file, ++reads};
            return file;
        }
        if (open_file >= 0 && open_path == path && (!write || open_writable))
            return open_file;
        Release("");
        open_file = ::open(path.c_str(), write ? O_RDWR : O_RDONLY);
        // A handle the guest opened for writing keeps writing after the file
        // became FILE_ATTRIBUTE_READONLY (mode 0444): CopyFile creates its
        // target with the read-only attribute of a CD source and then fills
        // it. The open descriptor stays writable when
        // the mode goes back.
        struct stat info;
        if (open_file < 0 && write && errno == EACCES && !stat(path.c_str(), &info) && S_ISREG(info.st_mode) &&
            !chmod(path.c_str(), (info.st_mode & 0777) | S_IWUSR))
        {
            open_file = ::open(path.c_str(), O_RDWR);
            chmod(path.c_str(), info.st_mode & 0777);
        }
        if (open_file < 0 && write)
            open_file = ::open(path.c_str(), O_RDWR | O_CREAT, 0644);
        open_path = path;
        open_writable = write;
        return open_file;
    }
    void Release(const std::string &path)
    {
        for (size_t i = 0; i < readers.size();)
            if (path.empty() || readers[i].path == path)
            {
                close(readers[i].file);
                readers.erase(readers.begin() + long(i));
            }
            else
                ++i;
        if (open_file >= 0 && (path.empty() || open_path == path))
        {
            close(open_file);
            open_file = -1;
            open_path.clear();
        }
    }
    void Stamp(const std::string &path, const Files::TimeStamp *times)
    {
        struct timeval values[2] = {};
        values[0].tv_sec = Decode(times[1].date, 0);
        values[1].tv_sec = Decode(times[2].date, times[2].time);
        if (values[0].tv_sec && values[1].tv_sec)
            utimes(path.c_str(), values);
    }
    static bool Describe(const Record &record, const std::string &host, Volume::Entry &out)
    {
        out = Volume::Entry{};
        snprintf(out.name, sizeof out.name, "%s", record.name.c_str());
        snprintf(out.alias, sizeof out.alias, "%s", record.alias.c_str());
        out.attributes = record.attributes;
        out.tombstone = record.tombstone;
        out.opaque = record.opaque && !record.tombstone;
        out.times[0] = record.created;
        struct stat info;
        if (stat(host.c_str(), &info))
            return false;
        out.size = S_ISDIR(info.st_mode) ? 0u : U(info.st_size);
        U date = 0, time = 0;
        Encode(info.st_atime, date, time);
        out.times[1] = Files::TimeStamp{date, 0, 0};
        Encode(info.st_mtime, date, time);
        out.times[2] = Files::TimeStamp{date, time, 0};
        if (!out.times[0].date)
            out.times[0] = out.times[2];
        // The entry of a directory keeps the times of its creation: a FAT
        // directory entry is not written again when the directory's
        // contents change, and no call of Win98 sets its times.
        else if (S_ISDIR(info.st_mode))
        {
            out.times[1] = Files::TimeStamp{record.created.date, 0, 0};
            out.times[2] = Files::TimeStamp{record.created.date, record.created.time, 0};
        }
        return true;
    }
    // Takes the pin of `name` out of the directory and answers its alias.
    static std::string TakePin(Folder &folder, const std::string &name)
    {
        for (auto it = folder.records.begin(); it != folder.records.end(); ++it)
            if (it->pin && Same(it->name, name))
            {
                const std::string alias = it->alias;
                folder.records.erase(it);
                folder.dirty = true;
                return alias;
            }
        return std::string();
    }
    // The directory `relative` of this drive, or null.
    Folder *Directory(const std::string &relative)
    {
        if (relative.empty())
            return Enter("", root);
        Place place;
        if (!Locate(("C:\\" + relative).c_str(), place) || !place.record || place.record->tombstone ||
            !(place.record->attributes & 16))
            return nullptr;
        return Enter(place.relative, place.host);
    }
    // The host path of a guest path below the root, long names.
    std::string HostPath(const std::string &relative) const
    {
        std::string host = root, part;
        for (size_t at = 0; at <= relative.size(); ++at)
            if (at == relative.size() || relative[at] == '\\')
            {
                if (!part.empty())
                    host += "/" + ToHost(part);
                part.clear();
            }
            else
                part += relative[at];
        return host;
    }
    // Forgets what was read of the directory `relative` and of everything
    // below it; the next lookup reads the host again.
    void Forget(const std::string &relative)
    {
        const std::string key = Upper(relative), inside = key.empty() ? key : key + "\\";
        for (auto it = folders.begin(); it != folders.end();)
            if (it->first == key || !it->first.compare(0, inside.size(), inside))
            {
                if (it->second.dirty)
                    Save(it->second);
                it = folders.erase(it);
            }
            else
                ++it;
        Release("");
    }
    // The layer's alias for the entry `name` of the drive below, in the
    // directory `relative` of this layer.
    void Pin(const std::string &relative, const std::string &name, const std::string &alias)
    {
        Provision(("C:\\" + (relative.empty() ? name : relative + "\\" + name)).c_str());
        Folder *folder = Directory(relative);
        if (!folder)
            return;
        TakePin(*folder, name);
        Record record;
        record.pin = true;
        record.name = name;
        record.alias = alias;
        folder->records.push_back(record);
        folder->dirty = true;
        Save(*folder);
    }
    // Puts the entries of the directory `relative` into the order of `names`;
    // what `names` does not name follows in its old order.
    void Reorder(const std::string &relative, const std::vector<std::string> &names)
    {
        Folder *folder = Directory(relative);
        if (!folder)
            return;
        std::vector<Record> ordered;
        std::vector<bool> taken(folder->records.size(), false);
        for (const auto &name : names)
            for (size_t i = 0; i < folder->records.size(); ++i)
                if (!taken[i] && !folder->records[i].pin && !folder->records[i].tombstone && Same(folder->records[i].name, name))
                {
                    taken[i] = true;
                    ordered.push_back(folder->records[i]);
                    break;
                }
        for (size_t i = 0; i < folder->records.size(); ++i)
            if (!taken[i])
                ordered.push_back(folder->records[i]);
        folder->records.swap(ordered);
        folder->dirty = true;
        Save(*folder);
    }
    // Reads the sidecar of one directory and merges in whatever the directory
    // actually holds, so a file dropped in by hand becomes a guest file.
    // DOS and host tools write these directories without going through this
    // provider. Refresh at lookup boundaries, never halfway through a mutation
    // that still holds pointers into the old directory records.
    Folder *EnterFresh(const std::string &relative, const std::string &host)
    {
        const auto known = folders.find(Upper(relative));
        if (known != folders.end())
        {
            struct stat info;
            const bool there = !stat(host.c_str(), &info);
            const auto changed = there ? Changed(info) : timespec{};
            if (!there || info.st_ino != known->second.inode ||
                changed.tv_sec != known->second.changed.tv_sec || changed.tv_nsec != known->second.changed.tv_nsec)
            {
                Forget(relative);
                ++outside_changes;
            }
        }
        return Enter(relative, host);
    }
    Folder *Enter(const std::string &relative, const std::string &host)
    {
        const std::string key = Upper(relative);
        auto found = folders.find(key);
        if (found != folders.end())
            return &found->second;
        struct stat info;
        if (stat(host.c_str(), &info) || !S_ISDIR(info.st_mode))
            return nullptr;
        Folder folder;
        folder.host = host;
        folder.relative = relative;
        folder.changed = Changed(info);
        folder.inode = info.st_ino;
        FILE *file = fopen(LegacyData::ReadPath(host + "/.neo98-index").c_str(), "r");
        if (file)
        {
            char line[2048];
            while (fgets(line, sizeof line, file))
            {
                if (line[0] == '#')
                    continue;
                char *fields[5] = {};
                char *cursor = line;
                U count = 0;
                for (; count < 5; ++count)
                {
                    fields[count] = cursor;
                    char *tab = strchr(cursor, '\t');
                    if (!tab)
                        break;
                    *tab = 0;
                    cursor = tab + 1;
                }
                if (count != 4)
                    continue;
                char *end = fields[4] + strlen(fields[4]);
                while (end > fields[4] && (end[-1] == '\n' || end[-1] == '\r'))
                    *--end = 0;
                Record record;
                const U flags = (U)strtoul(fields[0], nullptr, 10);
                record.tombstone = (flags & 1) != 0;
                record.opaque = (flags & 2) != 0;
                record.pin = (flags & 4) != 0;
                record.replaced = (flags & 8) != 0;
                record.attributes = (U)strtoul(fields[1], nullptr, 10);
                U c[3] = {};
                sscanf(fields[2], "%u,%u,%u", &c[0], &c[1], &c[2]);
                record.created = Files::TimeStamp{c[0], c[1], c[2]};
                record.alias = fields[3];
                record.name = fields[4];
                if (!record.name.empty())
                    folder.records.push_back(record);
            }
            fclose(file);
        }
        // Everything the sidecar does not know about, in the order the host
        // reports it. A sidecar record whose file disappeared is dropped
        // unless it is a tombstone.
        std::vector<std::string> present;
        if (DIR *directory = opendir(host.c_str()))
        {
            while (auto *entry = readdir(directory))
            {
                const std::string name = entry->d_name;
                if (name == "." || name == ".." || LegacyData::IsMetadata(name))
                    continue;
                present.push_back(ToGuest(name));
            }
            closedir(directory);
            std::sort(present.begin(), present.end());
        }
        for (auto it = folder.records.begin(); it != folder.records.end();)
        {
            bool alive = it->tombstone || it->pin;
            for (const auto &name : present)
                alive = alive || Same(name, it->name);
            if (alive)
                ++it;
            else
            {
                it = folder.records.erase(it);
                folder.dirty = true;
            }
        }
        for (const auto &name : present)
        {
            bool known = false;
            for (const auto &record : folder.records)
                known = known || (!record.pin && Same(record.name, name));
            if (known)
                continue;
            Record record;
            record.name = name;
            struct stat child;
            if (!stat((host + "/" + ToHost(name)).c_str(), &child))
            {
                record.attributes = S_ISDIR(child.st_mode) ? 16u : ((child.st_mode & 0222) ? 32u : 33u);
                U date = 0, time = 0;
                Encode(child.st_mtime, date, time);
                record.created = Files::TimeStamp{date, time, 0};
            }
            folder.records.push_back(record);
            folder.dirty = true;
        }
        auto &stored = folders.emplace(key, std::move(folder)).first->second;
        for (auto &record : stored.records)
            if (record.alias.empty() && !record.tombstone)
            {
                Alias(stored, record, "");
                if (!record.alias.empty())
                    stored.dirty = record.generated = true;
            }
        if (stored.dirty && writable)
            Save(stored);
        return &stored;
    }
    void Save(Folder &folder)
    {
        if (!writable)
        {
            folder.dirty = false;
            return;
        }
        const std::string path = folder.host + "/.neo98-index";
        const std::string temporary = path + ".new";
        FILE *file = fopen(temporary.c_str(), "w");
        if (!file)
            return;
        fprintf(file, "# NEO98 volume index 1: flags\tattributes\tcdate,ctime,chundredths\talias\tname\n");
        for (const auto &record : folder.records)
            fprintf(file, "%u\t%u\t%u,%u,%u\t%s\t%s\n",
                    (record.tombstone ? 1u : 0u) | (record.opaque ? 2u : 0u) | (record.pin ? 4u : 0u) | (record.replaced ? 8u : 0u),
                    record.attributes,
                    record.created.date, record.created.time, record.created.hundredths,
                    record.alias.c_str(), record.name.c_str());
        fclose(file);
        rename(temporary.c_str(), path.c_str());
        folder.dirty = false;
        struct stat info;
        if (!stat(folder.host.c_str(), &info))
        {
            folder.changed = Changed(info);
            folder.inode = info.st_ino;
        }
    }
    // Gives one record its 8.3 alias. A preferred alias is kept when the
    // directory still has it free, so a file promoted out of the read-only
    // Win98 tree answers to the name it had there.
    void Alias(Folder &folder, Record &record, const char *preferred)
    {
        struct Environment
        {
            Folder &folder;
            Record &record;
            Drive *below, *self;
            bool Taken(const char *candidate)
            {
                // A tombstone is no entry: the alias of a removed file is free.
                for (const auto &other : folder.records)
                    if (&other != &record && !other.tombstone && (Same(other.name, candidate) || Same(other.alias, candidate)))
                        return true;
                // The same directory of the tree below: a name there must not
                // answer to an alias issued here. The entry of the same name
                // is this one (a file copied up keeps its alias).
                return below && self->Shows(folder.relative) && below->Names(folder.relative, candidate, folder);
            }
        } environment{folder, record, below, this};
        char alias[13] = {};
        if (preferred && *preferred && !environment.Taken(preferred) &&
            !Win98ShortAlias::Short(record.name.c_str()))
        {
            record.alias = preferred;
            return;
        }
        if (Win98ShortAlias::Generate(environment, record.name.c_str(), alias))
            record.alias = alias;
        else
            record.alias.clear();
    }
    // Makes sure every directory above `absolute` exists here. The guest may
    // write into a directory that so far only the read-only Win98 tree below
    // has, and the overlay then needs its own copy of that directory.
    void Provision(const char *absolute)
    {
        if (!absolute || !absolute[0] || absolute[1] != ':')
            return;
        std::string built(absolute, 2);
        const char *p = absolute + 2;
        while (*p)
        {
            while (*p == '\\' || *p == '/')
                ++p;
            std::string part;
            while (*p && *p != '\\' && *p != '/')
                part += *p++;
            if (part.empty())
                break;
            const char *rest = p;
            while (*rest == '\\' || *rest == '/')
                ++rest;
            if (!*rest)
                break;   // The last component is the caller's own entry.
            built += "\\" + part;
            Place place;
            if (!Locate(built.c_str(), place) || (place.record && !place.record->tombstone))
                continue;
            if (mkdir(place.host.c_str(), 0755) && errno != EEXIST)
                return;
            Folder *folder = Enter(place.parent, place.parent_host);
            if (!folder)
                return;
            // The alias the layer pinned for the directory below; its record
            // may move when the pin goes.
            std::string alias = TakePin(*folder, place.name);
            Record *record = nullptr;
            for (auto &known : folder->records)
                if (Same(known.name, place.name))
                    record = &known;
            if (!record)
            {
                folder->records.push_back(Record{});
                record = &folder->records.back();
                record->name = place.name;
            }
            record->tombstone = false;
            record->attributes = 16;
            record->alias.clear();
            // The layer's own record of a directory of the tree below: with
            // the alias, the attributes and the creation time it has there.
            if (below && Shows(place.parent))
            {
                Place lower;
                if (below->Locate(built.c_str(), lower) && lower.record && !lower.record->tombstone &&
                    (lower.record->attributes & 16))
                {
                    if (alias.empty())
                        alias = lower.record->alias;
                    record->attributes = lower.record->attributes;
                    record->created = lower.record->created;
                }
            }
            Alias(*folder, *record, alias.c_str());
            folder->dirty = true;
            Save(*folder);
        }
    }
    // Walks a guest path component by component. Every component may be a long
    // name or an 8.3 alias, compared without case.
    bool Locate(const char *absolute, Place &place, bool fresh = false)
    {
        place = Place{};
        if (!absolute || !absolute[0] || absolute[1] != ':')
            return false;
        std::string relative, host = root;
        Folder *folder = fresh ? EnterFresh("", root) : Enter("", root);
        if (!folder)
            return false;
        place.parent = "";
        place.parent_host = root;
        place.host = root;
        const char *p = absolute + 2;
        while (*p)
        {
            while (*p == '\\' || *p == '/')
                ++p;
            if (!*p)
                break;
            std::string part;
            while (*p && *p != '\\' && *p != '/')
                part += *p++;
            if (!folder)
                return false;
            place.parent = relative;
            place.parent_host = host;
            place.name = part;
            place.record = nullptr;
            for (auto &record : folder->records)
                if (!record.pin && (Same(record.name, part) || (!record.alias.empty() && Same(record.alias, part))))
                {
                    place.record = &record;
                    place.name = record.name;
                    break;
                }
            relative += (relative.empty() ? "" : "\\") + place.name;
            host += "/" + ToHost(place.name);
            place.relative = relative;
            place.host = host;
            const bool more = *p != 0;
            folder = place.record && (place.record->attributes & 16) && !place.record->tombstone
                         ? (fresh ? EnterFresh(relative, host) : Enter(relative, host))
                         : nullptr;
            if (more && !folder)
                return false;
        }
        return true;
    }
    void LoadVolume(const char *fallback)
    {
        label = fallback && *fallback ? fallback : "Neo98";
        if (label.size() > 11)
            label.resize(11);
        FILE *file = fopen(LegacyData::ReadPath(root + "/.neo98-volume").c_str(), "r");
        if (file)
        {
            char line[256] = {};
            if (fgets(line, sizeof line, file))
            {
                char text[64] = {};
                unsigned value = 0;
                if (sscanf(line, "%x\t%63[^\n]", &value, text) >= 1)
                {
                    serial = value;
                    if (*text)
                        label = text;
                }
            }
            fclose(file);
        }
        if (!serial)
        {
            // A serial that is stable for this directory: the guest sees the
            // same volume identity on every run.
            struct stat info;
            serial = 0x98000000u;
            if (!stat(root.c_str(), &info))
                serial ^= U(info.st_ino) * 2654435761u;
            for (unsigned char c : root)
                serial = (serial ^ c) * 16777619u;
            if (!serial)
                serial = 0x98A0198Au;
            SaveVolume();
        }
    }
    void SaveVolume()
    {
        if (!writable)
            return;
        FILE *file = fopen((root + "/.neo98-volume").c_str(), "w");
        if (!file)
            return;
        fprintf(file, "%08x\t%s\n", serial, label.c_str());
        fclose(file);
    }
};

// A write layer above a drive that is only read: the drive C: of a game
// machine above the drive C: of the normal machine, and each of its other
// hard disks likewise.
// To Files::Store this is one writable drive. Everything that makes it two
// folders is here, and everybody who reaches the drive comes through here:
// the store for the Windows programs, the DOS machine for the DOS programs
// (Foreign), each from its own thread.
// lookup the layer first, then the drive below; by long name or alias
// read where the file lies
// write, resize, attributes, times
// copy the file up first (CopyFile: a block clone where the
// medium can), with its alias, attributes and times
// create in the layer; directories above it are made there as needed
// remove, rename
// an entry of the drive below leaves a tombstone in the layer; a
// directory of the drive below is copied up whole before it moves
// remove a directory and make it again
// the new one is opaque: nothing of the drive below shows in it
// list the entries of the drive below in their order there, without
// what the layer removed or made anew, then the layer's own
// The records are those of the layer's Drive: tombstones, opaque directories,
// pins and the mark of an entry made anew, in the sidecars of the layer
// folder. Nothing in the layer folder names a host path, so a copy of the
// folder is a snapshot of everything the machine changed.
// 8.3 aliases. An alias the layer has issued is the layer's for
// good. An entry of the drive below answers to the alias it has there,
// unless the layer already uses that alias in the directory, or the drive
// below never wrote it down (a file put into the folder by hand): then the
// layer gives the entry an alias and writes it into its own sidecar (a pin),
// and the entry keeps it across restarts.
// The drive below may change while the layer is in use; another machine
// writes it. The rule: a lookup by name and an open always see the file as
// it is now, or see that it is gone (an ordinary "file not found"); a
// listing is read again when the host directory changed since it was read;
// a file that is open stays readable until it is closed (Retain), whatever
// became of it below. The layer folder itself is only written through here.
class Layered final : public Volume::Backend
{
  public:
    // The folder of the layer is made by the first write.
    bool Open(const char *layer_directory, Drive *drive_below)
    {
        lower = drive_below;
        root = layer_directory ? layer_directory : "";
        while (root.size() > 1 && root.back() == '/')
            root.pop_back();
        if (!lower || root.empty())
            return false;
        description = root + " (layer over " + lower->root + ")";
        foreign.owner = this;
        struct stat info;
        if (!stat(root.c_str(), &info))
            return S_ISDIR(info.st_mode) && Start();
        return true;
    }
    // The folder of the layer and the folder of the drive below it.
    const std::string &Root() const { return root; }
    const std::string &BelowRoot() const { return lower->root; }
    // The same drive for the DOS machine: what it changes, the store has not
    // done itself and has to notice (Stamp).
    Volume::Backend *Foreign() { return &foreign; }

    bool Volatile() override { return true; }
    bool Find(const char *absolute, Volume::Entry &out) override
    {
        Guard guard(gate);
        Spot spot;
        if (!Resolve(absolute, spot) || spot.root)
            return false;
        if (spot.index < 0)
        {
            // The drive below may have got the entry since its directory was read.
            if (!Stale(spot.parent) || !Resolve(absolute, spot) || spot.root || spot.index < 0)
                return false;
        }
        const View &view = views[spot.parent];
        if (Describe(view, view.items[size_t(spot.index)], out))
            return true;
        // The entry went away below.
        Dropped(spot.parent);
        return false;
    }
    bool Child(const char *directory, U index, Volume::Entry &out) override
    {
        Guard guard(gate);
        std::string key;
        if (!Listing(directory, key))
            return false;
        if (!index && Stale(key) && !Listing(directory, key))
            return false;
        const View &view = views[key];
        if (index >= view.items.size())
            return false;
        // An entry that went away below after the directory was read is
        // named all the same; the lookup of it then says that it is gone.
        if (!Describe(view, view.items[index], out))
        {
            out = Volume::Entry{};
            snprintf(out.name, sizeof out.name, "%s", view.items[index].name.c_str());
        }
        return true;
    }
    U Stamp(const char *directory) override
    {
        Guard guard(gate);
        std::string key;
        if (!Listing(directory, key))
            return ~0u;
        Stale(key);
        return outside[key];
    }
    U Read(const char *absolute, U offset, void *buffer, U size, U &done) override
    {
        Guard guard(gate);
        done = 0;
        // A file somebody has open: by the descriptor it was opened with.
        const auto held = spelled.find(absolute);
        if (held != spelled.end())
        {
            const auto pin = retained.find(held->second);
            if (pin != retained.end() && pin->second.file >= 0)
            {
                const ssize_t got = pread(pin->second.file, buffer, size, off_t(offset));
                if (got < 0)
                    return 30;
                done = U(got);
                return 0;
            }
        }
        Spot spot;
        if (!Resolve(absolute, spot) || spot.root || spot.index < 0)
            return 2;
        const bool above = views[spot.parent].items[size_t(spot.index)].upper;
        if (!above)
            Noticed(spot.relative);
        return (above ? upper : *lower).Read(Absolute(spot.relative).c_str(), offset, buffer, size, done);
    }
    U Write(const char *absolute, U offset, const void *buffer, U size, U &done) override
    {
        Guard guard(gate);
        done = 0;
        std::string relative;
        if (const U error = Own(absolute, relative, false))
            return error;
        Noted(Above(relative));
        return upper.Write(Absolute(relative).c_str(), offset, buffer, size, done);
    }
    U Resize(const char *absolute, U size) override
    {
        Guard guard(gate);
        std::string relative;
        if (const U error = Own(absolute, relative, false))
            return error;
        Noted(Above(relative));
        return upper.Resize(Absolute(relative).c_str(), size);
    }
    U Create(const char *absolute, U attributes, bool directory, Volume::Entry &out) override
    {
        Guard guard(gate);
        Spot spot;
        if (!Resolve(absolute, spot) || spot.root)
            return 3;
        if (spot.index >= 0)
            return directory ? 183u : 80u;
        if (!Start())
            return 5;
        const U error = upper.Create(Absolute(spot.relative).c_str(), attributes, directory, out);
        out.opaque = false;
        Touched(spot.parent_relative, directory);
        return error;
    }
    U Remove(const char *absolute, bool directory, bool) override
    {
        Guard guard(gate);
        Spot spot;
        if (!Resolve(absolute, spot) || spot.root)
            return 3;
        if (spot.index < 0)
            return 2;
        const Item item = views[spot.parent].items[size_t(spot.index)];
        if (bool(item.attributes & 16) != directory)
            return 5;
        if (Held(spot.relative, directory))
            return 32;
        if (directory)
        {
            std::string key;
            if (Listing(absolute, key) && !views[key].items.empty())
                return 145;
        }
        if (!Start())
            return 5;
        // What the drive below holds under this name stays hidden.
        const U error = upper.Remove(Absolute(spot.relative).c_str(), directory, item.lower);
        Touched(spot.parent_relative, directory);
        return error;
    }
    U Rename(const char *source, const char *destination) override
    {
        Guard guard(gate);
        Spot from, to;
        if (!Resolve(source, from) || from.root || from.index < 0 || !Resolve(destination, to) || to.root)
            return 2;
        if (to.index >= 0)
            return 183;
        const Item item = views[from.parent].items[size_t(from.index)];
        const bool directory = (item.attributes & 16) != 0;
        if (Held(from.relative, directory))
            return 32;
        if (!Start())
            return 5;
        // The entry has to lie in the layer before it can move there.
        if (const U error = !item.lower ? 0u : directory ? RaiseTree(from.relative) : item.upper ? 0u : Raise(from.relative))
            return error;
        const U error = upper.Rename(Absolute(from.relative).c_str(), Absolute(to.relative).c_str());
        if (!error && item.lower)
            upper.Remove(Absolute(from.relative).c_str(), directory, true);
        Touched(from.parent_relative, directory);
        Touched(to.parent_relative, directory);
        return error;
    }
    U SetAttributes(const char *absolute, U attributes) override
    {
        Guard guard(gate);
        std::string relative;
        if (const U error = Own(absolute, relative, true))
            return error;
        const U error = upper.SetAttributes(Absolute(relative).c_str(), attributes);
        if (Item *item = error ? nullptr : Entry(relative))
            item->attributes = attributes;
        Noted(Above(relative));
        return error;
    }
    U SetTimes(const char *absolute, const Files::TimeStamp *times) override
    {
        Guard guard(gate);
        std::string relative;
        if (const U error = Own(absolute, relative, true))
            return error;
        const U error = upper.SetTimes(Absolute(relative).c_str(), times);
        if (Item *item = error || !times[0].date ? nullptr : Entry(relative))
            item->created = times[0];
        Noted(Above(relative));
        return error;
    }
    void Space(unsigned long long &total, unsigned long long &available) override
    {
        Guard guard(gate);
        total = available = 0;
        // The medium the layer lies on, also before its folder is made.
        std::string place = root;
        struct statvfs info;
        while (statvfs(place.c_str(), &info))
        {
            const auto slash = place.rfind('/');
            if (slash == std::string::npos || !slash)
                return;
            place.resize(slash);
        }
        total = (unsigned long long)info.f_blocks * info.f_frsize;
        available = (unsigned long long)info.f_bavail * info.f_frsize;
    }
    const char *Label() override
    {
        Guard guard(gate);
        return started ? upper.Label() : lower->Label();
    }
    bool SetLabel(const char *text) override
    {
        Guard guard(gate);
        return Start() && upper.SetLabel(text);
    }
    U Serial() override
    {
        Guard guard(gate);
        return started ? upper.Serial() : lower->Serial();
    }
    void Flush() override
    {
        Guard guard(gate);
        if (started)
            upper.Flush();
    }
    const char *Description() const override { return description.c_str(); }
    void Retain(const char *absolute) override
    {
        Guard guard(gate);
        Spot spot;
        if (!Resolve(absolute, spot) || spot.root || spot.index < 0)
            return;
        const Item &item = views[spot.parent].items[size_t(spot.index)];
        if (item.attributes & 16)
            return;
        const std::string key = Upper(spot.relative);
        auto &pin = retained[key];
        // A file of the drive below is held open: it stays readable when it
        // is removed or replaced there.
        if (!pin.count++ && !item.upper)
        {
            pin.file = ::open(lower->HostPath(spot.relative).c_str(), O_RDONLY | O_CLOEXEC);
            Noticed(spot.relative);
        }
        pin.spellings.push_back(absolute);
        spelled[absolute] = key;
    }
    void Release(const char *absolute) override
    {
        Guard guard(gate);
        const auto held = spelled.find(absolute);
        if (held == spelled.end())
            return;
        const auto pin = retained.find(held->second);
        if (pin == retained.end())
        {
            spelled.erase(held);
            return;
        }
        if (--pin->second.count)
            return;
        if (pin->second.file >= 0)
            close(pin->second.file);
        // `held` names the key that goes with the pin.
        const std::vector<std::string> spellings = pin->second.spellings;
        retained.erase(pin);
        for (const auto &text : spellings)
            spelled.erase(text);
    }
    // A file of the drive below the machine has read or opened: what the
    // machine holds in memory came from it, so a saved state of the machine
    // fits only while the file is the same. The path is
    // the one below the folder of the drive below, as this Mac spells it;
    // size and time are those of the first read.
    struct BelowFile
    {
        std::string path;
        unsigned long long size = 0;
        long long seconds = 0;
        long nanoseconds = 0;
        // Who read it: the store (the Windows side), the DOS machine through
        // Foreign, or both. A state of a DOS machine alone depends only on
        // what the DOS machine read.
        bool store = false, foreign = false;
    };
    std::vector<BelowFile> BelowFiles()
    {
        Guard guard(gate);
        std::vector<BelowFile> files;
        for (const auto &entry : noticed)
            files.push_back(entry.second);
        std::sort(files.begin(), files.end(), [](const BelowFile &a, const BelowFile &b) { return a.path < b.path; });
        return files;
    }
    // The files a loaded state had read: the machine holds them again.
    void NoticeBelowFiles(const std::vector<BelowFile> &files)
    {
        Guard guard(gate);
        for (const auto &file : files)
        {
            auto &known = noticed.emplace(Upper(file.path), file).first->second;
            known.store = known.store || file.store;
            known.foreign = known.foreign || file.foreign;
        }
    }
    // How many files of the drive below are held open, for the tests.
    unsigned HeldOpen()
    {
        Guard guard(gate);
        unsigned count = 0;
        for (const auto &pin : retained)
            count += pin.second.file >= 0 ? 1u : 0u;
        return count;
    }
    // The host file behind a guest path, in the layer or below it; empty
    // when there is none.
    std::string HostFile(const char *absolute)
    {
        Guard guard(gate);
        Spot spot;
        if (!Resolve(absolute, spot))
            return std::string();
        if (spot.root)
            return lower->root;
        if (spot.index < 0)
            return std::string();
        return (views[spot.parent].items[size_t(spot.index)].upper ? upper : *lower).HostPath(spot.relative);
    }

  private:
    using Guard = std::lock_guard<std::recursive_mutex>;
    // One entry of a directory as the machine sees it. `upper`: it lies in
    // the layer. `lower`: the drive below holds an entry of this name, shown
    // through or hidden by the layer's own.
    struct Item
    {
        std::string name, alias;
        U attributes = 128;
        Files::TimeStamp created;
        bool upper = false, lower = false;
    };
    // One directory as the machine sees it.
    struct View
    {
        std::string relative;
        std::vector<Item> items;
        std::unordered_map<std::string, unsigned> index;
        // The host directory below as it was when this was put together.
        bool below = false;
        struct timespec changed = {};
        ino_t inode = 0;
    };
    // Where a guest path lands: the key of the view of its directory, its
    // entry there (or -1), and its path below the root in long names.
    struct Spot
    {
        std::string parent, parent_relative, relative;
        int index = -1;
        bool root = false;
    };
    struct Pin
    {
        unsigned count = 0;
        int file = -1;
        std::vector<std::string> spellings;
    };
    // The DOS machine's way in: the same calls, marked as not the store's.
    struct Outside final : Volume::Backend
    {
        Layered *owner = nullptr;
        struct Mark
        {
            Layered &layer;
            Guard guard;
            explicit Mark(Layered *l) : layer(*l), guard(l->gate) { ++layer.strangers; }
            ~Mark() { --layer.strangers; }
        };
        bool Volatile() override { return true; }
        bool Find(const char *a, Volume::Entry &o) override { Mark m(owner); return owner->Find(a, o); }
        bool Child(const char *d, U i, Volume::Entry &o) override { Mark m(owner); return owner->Child(d, i, o); }
        U Stamp(const char *d) override { Mark m(owner); return owner->Stamp(d); }
        U Read(const char *a, U o, void *b, U s, U &d) override { Mark m(owner); return owner->Read(a, o, b, s, d); }
        U Write(const char *a, U o, const void *b, U s, U &d) override { Mark m(owner); return owner->Write(a, o, b, s, d); }
        U Resize(const char *a, U s) override { Mark m(owner); return owner->Resize(a, s); }
        U Create(const char *a, U t, bool d, Volume::Entry &o) override { Mark m(owner); return owner->Create(a, t, d, o); }
        U Remove(const char *a, bool d, bool t) override { Mark m(owner); return owner->Remove(a, d, t); }
        U Rename(const char *s, const char *d) override { Mark m(owner); return owner->Rename(s, d); }
        U SetAttributes(const char *a, U t) override { Mark m(owner); return owner->SetAttributes(a, t); }
        U SetTimes(const char *a, const Files::TimeStamp *t) override { Mark m(owner); return owner->SetTimes(a, t); }
        void Space(unsigned long long &t, unsigned long long &a) override { owner->Space(t, a); }
        const char *Label() override { return owner->Label(); }
        bool SetLabel(const char *t) override { return owner->SetLabel(t); }
        U Serial() override { return owner->Serial(); }
        void Flush() override { owner->Flush(); }
        const char *Description() const override { return owner->Description(); }
        void Retain(const char *a) override { Mark m(owner); owner->Retain(a); }
        void Release(const char *a) override { owner->Release(a); }
    } foreign;

    std::recursive_mutex gate;
    std::string root, description;
    Drive upper, *lower = nullptr;
    bool started = false;
    unsigned strangers = 0;
    std::unordered_map<std::string, View> views;
    // Per directory: how often it changed other than through the store.
    std::unordered_map<std::string, U> outside;
    std::unordered_map<std::string, Pin> retained;
    std::unordered_map<std::string, std::string> spelled;
    // By the host path below the drive below, folded.
    std::unordered_map<std::string, BelowFile> noticed;
    void Noticed(const std::string &relative)
    {
        const std::string host = lower->HostPath(relative);
        if (host.size() <= lower->root.size() + 1)
            return;
        BelowFile file;
        file.path = host.substr(lower->root.size() + 1);
        const std::string key = Upper(file.path);
        if (const auto known = noticed.find(key); known != noticed.end())
        {
            (strangers ? known->second.foreign : known->second.store) = true;
            return;
        }
        (strangers ? file.foreign : file.store) = true;
        struct stat info;
        if (stat(host.c_str(), &info) || !S_ISREG(info.st_mode))
            return;
        file.size = (unsigned long long)info.st_size;
#ifdef __APPLE__
        file.seconds = info.st_mtimespec.tv_sec;
        file.nanoseconds = info.st_mtimespec.tv_nsec;
#else
        file.seconds = info.st_mtim.tv_sec;
        file.nanoseconds = info.st_mtim.tv_nsec;
#endif
        noticed.emplace(key, file);
    }

    static std::string Absolute(const std::string &relative) { return "C:\\" + relative; }
    static std::string Join(const std::string &directory, const std::string &name)
    {
        return directory.empty() ? name : directory + "\\" + name;
    }
    static std::string Above(const std::string &relative)
    {
        const auto slash = relative.rfind('\\');
        return slash == std::string::npos ? std::string() : relative.substr(0, slash);
    }
    // Makes the folder of the layer and opens it. The layer is the same
    // volume to the guest as the drive below: serial number and label.
    bool Start()
    {
        if (started)
            return true;
        std::string built;
        for (size_t at = 1; at <= root.size(); ++at)
            if (at == root.size() || root[at] == '/')
            {
                built = root.substr(0, at);
                mkdir(built.c_str(), 0755);
            }
        struct stat info;
        if (stat((root + "/.neo98-volume").c_str(), &info))
            if (FILE *file = fopen((root + "/.neo98-volume").c_str(), "w"))
            {
                fprintf(file, "%08x\t%s\n", lower->Serial(), lower->Label());
                fclose(file);
            }
        if (!upper.Open(root.c_str(), true, lower->Label()))
            return false;
        upper.Below(lower);
        started = true;
        return true;
    }
    // The directory `relative` (long names) as the machine sees it; null
    // when neither side has it.
    View *Look(const std::string &relative)
    {
        const std::string key = Upper(relative);
        const auto known = views.find(key);
        if (known != views.end())
            return &known->second;
        Drive::Folder *above = started ? upper.Directory(relative) : nullptr;
        // Not below a directory made anew in place of a removed one.
        const bool shows = !started || upper.Shows(relative);
        Drive::Folder *below = shows ? lower->Directory(relative) : nullptr;
        if (!above && !below)
            return nullptr;
        View view;
        view.relative = relative;
        if (below)
        {
            view.below = true;
            view.changed = below->changed;
            view.inode = below->inode;
        }
        std::unordered_map<std::string, const Drive::Record *> mine, theirs;
        if (above)
            for (const auto &record : above->records)
                mine[Upper(record.name)] = &record;
        // The entries of the drive below in their order, where the layer has
        // not removed them or put something new in their place.
        std::vector<size_t> unsettled;
        if (below)
            for (const auto &record : below->records)
            {
                if (record.tombstone || record.pin)
                    continue;
                const std::string name = Upper(record.name);
                theirs[name] = &record;
                const auto known_above = mine.find(name);
                const Drive::Record *own = known_above == mine.end() ? nullptr : known_above->second;
                if (own && (own->tombstone || own->replaced || own->opaque))
                    continue;
                Item item;
                item.lower = true;
                if (own && !own->pin)
                {
                    // Copied up: the layer's own record stands for it.
                    item.name = own->name;
                    item.alias = own->alias;
                    item.attributes = own->attributes;
                    item.created = own->created;
                    item.upper = true;
                }
                else
                {
                    item.name = record.name;
                    item.alias = own ? own->alias : record.alias;
                    item.attributes = record.attributes;
                    item.created = record.created;
                    if (!own && !item.alias.empty())
                        unsettled.push_back(view.items.size());
                }
                view.items.push_back(item);
            }
        if (above)
            for (const auto &record : above->records)
            {
                if (record.tombstone || record.pin)
                    continue;
                const bool held_below = theirs.count(Upper(record.name)) != 0;
                if (held_below && !record.replaced && !record.opaque)
                    continue;
                Item item;
                item.name = record.name;
                item.alias = record.alias;
                item.attributes = record.attributes;
                item.created = record.created;
                item.upper = true;
                item.lower = held_below;
                view.items.push_back(item);
            }
        // Names first, then the aliases the layer has settled, then those of
        // the drive below: one of those that is taken, or that the drive
        // below made up without writing it down, becomes the layer's.
        for (size_t i = 0; i < view.items.size(); ++i)
            view.index.emplace(Upper(view.items[i].name), unsigned(i));
        std::vector<bool> open(view.items.size(), false);
        for (const size_t i : unsettled)
            open[i] = true;
        for (size_t i = 0; i < view.items.size(); ++i)
            if (!open[i] && !view.items[i].alias.empty())
                view.index.emplace(Upper(view.items[i].alias), unsigned(i));
        std::vector<size_t> pins;
        for (const size_t i : unsettled)
        {
            Item &item = view.items[i];
            const Drive::Record *record = theirs[Upper(item.name)];
            if (view.index.emplace(Upper(item.alias), unsigned(i)).second)
            {
                if (record->generated)
                    pins.push_back(i);
                continue;
            }
            struct Environment
            {
                View &view;
                bool Taken(const char *candidate) { return view.index.count(Upper(candidate)) != 0; }
            } environment{view};
            char alias[13] = {};
            if (Win98ShortAlias::Generate(environment, item.name.c_str(), alias))
            {
                item.alias = alias;
                view.index.emplace(Upper(item.alias), unsigned(i));
                pins.push_back(i);
            }
            else
                item.alias.clear();
        }
        // `above` and `below` are not used past this point: a pin may make
        // the directory in the layer.
        if (!pins.empty() && Start())
            for (const size_t i : pins)
                upper.Pin(relative, view.items[i].name, view.items[i].alias);
        return &views.emplace(key, std::move(view)).first->second;
    }
    // Walks a guest path through the directories as the machine sees them.
    // False when a directory on the way does not exist.
    bool Resolve(const char *absolute, Spot &spot)
    {
        spot = Spot{};
        if (!absolute || !absolute[0] || absolute[1] != ':')
            return false;
        std::vector<std::string> parts;
        for (const char *p = absolute + 2; *p;)
        {
            while (*p == '\\' || *p == '/')
                ++p;
            std::string part;
            while (*p && *p != '\\' && *p != '/')
                part += *p++;
            if (!part.empty())
                parts.push_back(part);
        }
        std::string relative;
        View *view = Look(relative);
        if (!view)
            return false;
        if (parts.empty())
        {
            spot.root = true;
            return true;
        }
        for (size_t i = 0; i < parts.size(); ++i)
        {
            const auto hit = view->index.find(Upper(parts[i]));
            if (i + 1 == parts.size())
            {
                spot.parent = Upper(relative);
                spot.parent_relative = relative;
                spot.index = hit == view->index.end() ? -1 : int(hit->second);
                spot.relative = Join(relative, spot.index < 0 ? parts[i] : view->items[hit->second].name);
                return true;
            }
            if (hit == view->index.end() || !(view->items[hit->second].attributes & 16))
                return false;
            relative = Join(relative, view->items[hit->second].name);
            view = Look(relative);
            if (!view)
                return false;
        }
        return false;
    }
    // The key of the view of a directory named by a guest path.
    bool Listing(const char *directory, std::string &key)
    {
        Spot spot;
        if (!Resolve(directory, spot))
            return false;
        if (!spot.root && (spot.index < 0 || !(views[spot.parent].items[size_t(spot.index)].attributes & 16)))
            return false;
        const std::string relative = spot.root ? std::string() : spot.relative;
        if (!Look(relative))
            return false;
        key = Upper(relative);
        return true;
    }
    bool Describe(const View &view, const Item &item, Volume::Entry &out)
    {
        Drive::Record record;
        record.name = item.name;
        record.alias = item.alias;
        record.attributes = item.attributes;
        record.created = item.created;
        const std::string relative = Join(view.relative, item.name);
        return Drive::Describe(record, (item.upper ? upper : *lower).HostPath(relative), out);
    }
    // Whether the host directory below changed since the view was put
    // together; the view and what was read of the directory then go.
    bool Stale(const std::string &key)
    {
        const auto known = views.find(key);
        if (known == views.end() || !known->second.below)
            return false;
        struct stat info;
        const bool there = !stat(lower->HostPath(known->second.relative).c_str(), &info);
        const struct timespec changed = there ? Drive::Changed(info) : timespec{};
        if (there && info.st_ino == known->second.inode && changed.tv_sec == known->second.changed.tv_sec &&
            changed.tv_nsec == known->second.changed.tv_nsec)
            return false;
        Dropped(key);
        return true;
    }
    // The directory changed below: it and everything under it is read again.
    void Dropped(const std::string &key)
    {
        const auto known = views.find(key);
        if (known != views.end())
            lower->Forget(known->second.relative);
        const std::string inside = key.empty() ? key : key + "\\";
        for (auto it = views.begin(); it != views.end();)
            if (it->first == key || !it->first.compare(0, inside.size(), inside))
            {
                ++outside[it->first];
                it = views.erase(it);
            }
            else
                ++it;
        ++outside[key];
    }
    // The layer changed in the directory `relative`. Directories that came
    // or went change what shows through below them as well.
    void Touched(const std::string &relative, bool directories)
    {
        const std::string key = Upper(relative);
        if (strangers)
            ++outside[key];
        if (directories)
            views.clear();
        else
            views.erase(key);
    }
    // The contents or the times of an entry of `relative` changed; what the
    // views hold of it is still right.
    void Noted(const std::string &relative)
    {
        if (strangers)
            ++outside[Upper(relative)];
    }
    // The entry of a path in long names, in the view of its directory.
    Item *Entry(const std::string &relative)
    {
        Spot spot;
        if (!Resolve(Absolute(relative).c_str(), spot) || spot.root || spot.index < 0)
            return nullptr;
        return &views[spot.parent].items[size_t(spot.index)];
    }
    // Whether somebody has the file open, or a file below the directory.
    bool Held(const std::string &relative, bool directory)
    {
        const std::string key = Upper(relative), inside = key + "\\";
        if (!directory)
            return retained.count(key) != 0;
        for (const auto &pin : retained)
            if (!pin.first.compare(0, inside.size(), inside))
                return true;
        return false;
    }
    // Copy on write: the file of the drive below gets its place in the
    // layer, with its alias, attributes and times.
    U Raise(const std::string &relative)
    {
        Spot spot;
        const std::string absolute = Absolute(relative);
        if (!Resolve(absolute.c_str(), spot) || spot.root || spot.index < 0)
            return 2;
        const View &view = views[spot.parent];
        const Item item = view.items[size_t(spot.index)];
        if (item.upper)
            return 0;
        Volume::Entry entry, made;
        if (!Describe(view, item, entry) || !Start())
            return 2;
        snprintf(made.alias, sizeof made.alias, "%s", item.alias.c_str());
        for (unsigned i = 0; i < 3; ++i)
            made.times[i] = entry.times[i];
        const std::string own = Absolute(spot.relative);
        if (const U error = upper.Create(own.c_str(), item.attributes, false, made))
            return error;
        if (!CopyFile(lower->HostPath(spot.relative), upper.HostPath(spot.relative)))
        {
            upper.Remove(own.c_str(), false, false);
            Touched(spot.parent_relative, false);
            return 112;
        }
        chmod(upper.HostPath(spot.relative).c_str(), 0644);
        upper.SetAttributes(own.c_str(), item.attributes);
        upper.SetTimes(own.c_str(), entry.times);
        // Whoever has the file open reads the layer's from now on.
        const auto pin = retained.find(Upper(spot.relative));
        if (pin != retained.end() && pin->second.file >= 0)
        {
            close(pin->second.file);
            pin->second.file = -1;
        }
        Touched(spot.parent_relative, false);
        return 0;
    }
    // A directory of the drive below with everything in it, before it moves:
    // under its new name the drive below holds nothing of it.
    U RaiseTree(const std::string &relative)
    {
        View *view = Look(relative);
        if (!view)
            return 2;
        const std::vector<Item> items = view->items;
        // Its own record in the layer, as the directory is below.
        upper.Provision(Absolute(Join(relative, "x")).c_str());
        std::vector<std::string> names;
        for (const auto &item : items)
        {
            names.push_back(item.name);
            const std::string child = Join(relative, item.name);
            if (const U error = (item.attributes & 16) ? RaiseTree(child) : item.upper ? 0u : Raise(child))
                return error;
        }
        // The layer's directory lists them as the machine saw them.
        upper.Reorder(relative, names);
        views.clear();
        return 0;
    }
    // Before a change of the entry itself: it has to lie in the layer.
    U Own(const char *absolute, std::string &relative, bool directories)
    {
        Spot spot;
        if (!Resolve(absolute, spot) || spot.root || spot.index < 0)
            return 2;
        const Item item = views[spot.parent].items[size_t(spot.index)];
        relative = spot.relative;
        if (item.attributes & 16)
        {
            if (!directories)
                return 5;
            if (!Start())
                return 5;
            upper.Provision(Absolute(Join(relative, "x")).c_str());
            if (Item *own = Entry(relative))
                own->upper = true;
            return 0;
        }
        if (item.upper)
            return 0;
        if (!Start())
            return 5;
        return Raise(relative);
    }
};
// The layers of this process, by drive letter.
inline Layered *&Layer(char letter)
{
    static Layered *layers[26] = {};
    return layers[(Fold((unsigned char)letter) - 'A') % 26];
}

// What a runner accepts on the command line. The host and the probe default to
// the writable host volume; a test that wants the RAM volume passes --ram-drive.
struct Options
{
    const char *drive_c = nullptr;
    bool ram = false;
    struct Extra
    {
        char letter = 0;
        const char *directory = nullptr;
        bool cdrom = false;
        // A fixed disk the guest may read but not write. The program
        // collection is mounted this way: it is the source, and a writable
        // mount would put the.neo98-index sidecars of every listed directory
        // into it.
        bool readonly = false;
        // The hard disk of another machine below this one: `directory` is
        // then the write layer above it and nothing is written to `below`
        // (--drive-below X=DIRECTORY beside --drive X=LAYER; a game machine
        // mounts the drives D: to Z: of the normal machine so).
        const char *below = nullptr;
    } extra[8];
    unsigned count = 0;
};
inline Options Scan(int argc, char **argv)
{
    Options options;
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--drive-c") && i + 1 < argc)
        {
            options.drive_c = argv[++i];
            continue;
        }
        if (!strcmp(argv[i], "--ram-drive"))
        {
            options.ram = true;
            continue;
        }
        if (!strcmp(argv[i], "--drive") && i + 1 < argc && options.count < 8)
        {
            const char *text = argv[++i];
            const char *equals = strchr(text, '=');
            if (!equals || equals == text)
                continue;
            options.extra[options.count++] = {char(Fold((unsigned char)text[0])), equals + 1, false,
                                              false};
            continue;
        }
        if (!strcmp(argv[i], "--drive-readonly") && i + 1 < argc && options.count < 8)
        {
            const char *text = argv[++i];
            const char *equals = strchr(text, '=');
            if (!equals || equals == text)
                continue;
            options.extra[options.count++] = {char(Fold((unsigned char)text[0])), equals + 1, false,
                                              true};
            continue;
        }
        if (!strcmp(argv[i], "--cdrom") && i + 1 < argc && options.count < 8)
        {
            const char *path = argv[++i]; char letter = 'D';
            if (path[0] && path[1] == '=' && Fold((unsigned char)path[0]) >= 'D' && Fold((unsigned char)path[0]) <= 'Z')
            { letter = char(Fold((unsigned char)path[0])); path += 2; }
            options.extra[options.count++] = {letter, path, true, true};
            continue;
        }
    }
    for (int i = 1; i + 1 < argc; ++i)
        if (!strcmp(argv[i], "--drive-below"))
        {
            const char *text = argv[++i];
            const char *equals = strchr(text, '=');
            for (unsigned n = 0; equals && equals != text && n < options.count; ++n)
                if (!options.extra[n].cdrom && !options.extra[n].readonly &&
                    options.extra[n].letter == char(Fold((unsigned char)text[0])))
                    options.extra[n].below = equals + 1;
        }
    // A CD-ROM takes the first free letter after the mounted hard disks.
    for (unsigned i = 0; i < options.count; ++i)
        if (options.extra[i].cdrom)
        {
            char letter = 'D';
            for (bool again = true; again;)
            {
                again = false;
                for (unsigned n = 0; n < options.count; ++n)
                    if (n != i && options.extra[n].letter == letter)
                    {
                        ++letter;
                        again = true;
                    }
            }
            options.extra[i].letter = letter;
        }
    return options;
}
// The last component of a path, as the volume label of a mounted directory.
inline std::string LeafLabel(const char *path)
{
    std::string text = path ? path : "";
    while (text.size() > 1 && text.back() == '/')
        text.pop_back();
    const auto slash = text.rfind('/');
    if (slash != std::string::npos)
        text = text.substr(slash + 1);
    if (text.size() > 11)
        text.resize(11);
    for (auto &c : text)
        c = char(Fold((unsigned char)c));
    return text.empty() ? std::string("Neo98") : text;
}
// The Win98 folder `python3 neo98.py setup` creates once is drive C:
// (README.md); NEO98_WIN98_ROOT names another one.
inline const char *DefaultDriveC()
{
    const char *root = getenv("NEO98_WIN98_ROOT");
    return root && *root ? root : "build/win98-c";
}
// Whether two paths name the same directory.
inline bool SameDirectory(const char *a, const char *b)
{
    if (!a || !b)
        return false;
    char left[PATH_MAX], right[PATH_MAX];
    return realpath(a, left) && realpath(b, right) && !strcmp(left, right);
}
// The folder of the read-only Win98 tree, set by SystemTreeHost::Mount.
inline std::string &TreeRoot()
{
    static std::string root;
    return root;
}
// The provider of that tree: a writable drive C: above it is its write layer.
inline Drive *&TreeDrive()
{
    static Drive *drive = nullptr;
    return drive;
}
// Mounts everything the options ask for. Call before the first use of
// Files::System. Returns false when a directory cannot be opened.
inline bool Mount(const Options &options, const char *default_root = nullptr)
{
    static Drive drives[9];
    unsigned next = 0;
    // A file that is open in a layer holds a descriptor of the drive below
    // (Layered::Retain); the store has 256 handles.
    const auto descriptors = [] {
        struct rlimit limit;
        if (!getrlimit(RLIMIT_NOFILE, &limit) && limit.rlim_cur < 4096)
        {
            limit.rlim_cur = limit.rlim_max < 4096 ? limit.rlim_max : 4096;
            setrlimit(RLIMIT_NOFILE, &limit);
        }
    };
    if (!options.ram)
    {
        const char *root = options.drive_c ? options.drive_c : default_root ? default_root : DefaultDriveC();
        // Drive C: is the Win98 folder itself: everything the tree below
        // would lend is already here, and a write must not copy a file of the
        // folder onto itself. The tree layer goes.
        if (SystemTree::Mounted() && SameDirectory(root, TreeRoot().c_str()))
        {
            SystemTree::Mount(nullptr);
            TreeRoot().clear();
            TreeDrive() = nullptr;
        }
        if (SystemTree::Mounted() && TreeDrive())
        {
            // Another folder above the Win98 folder: its write layer. The
            // layering is the provider's (Layered), for the store as for the
            // DOS machine; the store sees one writable drive and no tree.
            static Layered layer;
            if (!layer.Open(root, TreeDrive()))
                return false;
            SystemTree::Mount(nullptr);
            Layer('C') = &layer;
            descriptors();
            Volume::Mount('C', &layer, Volume::Fixed, true);
        }
        else
        {
            if (!drives[next].Open(root, true, "Neo98"))
                return false;
            Volume::Mount('C', &drives[next++], Volume::Fixed, true);
        }
    }
    for (unsigned i = 0; i < options.count && next < 9; ++i)
    {
        const auto &extra = options.extra[i];
        if (!extra.letter || extra.letter == 'C')
            continue;
        const bool writable = !extra.cdrom && !extra.readonly;
        // --cdrom with a CUE sheet or an ISO file mounts the image: its data
        // track is the drive, its audio tracks play through MSCDEX
        // (tools/host_cd_image.h).
        const std::string path = extra.directory ? extra.directory : "";
        const bool image = extra.cdrom && path.size() > 4 &&
                           (!strcasecmp(path.c_str() + path.size() - 4, ".cue") ||
                            !strcasecmp(path.c_str() + path.size() - 4, ".iso"));
        if (image)
        {
            static HostCdImage::Image images[4];
            static unsigned used = 0;
            std::string error;
            if (used == 4 || !images[used].Open(path.c_str(), error))
            {
                fprintf(stderr, "CD image %s: %s\n", path.c_str(), used == 4 ? "too many images" : error.c_str());
                return false;
            }
            Volume::Mount(extra.letter, &images[used], Volume::Cdrom, false);
            CdAudio::Insert(extra.letter, &images[used]);
            ++used;
            continue;
        }
        if (extra.below)
        {
            // The drive of another machine, read only, below a layer.
            static Layered layers[8];
            static unsigned layered = 0;
            if (!drives[next].Open(extra.below, false, LeafLabel(extra.below).c_str()) ||
                !layers[layered].Open(extra.directory, &drives[next]))
                return false;
            ++next;
            Layer(extra.letter) = &layers[layered];
            descriptors();
            Volume::Mount(extra.letter, &layers[layered++], Volume::Fixed, true);
            continue;
        }
        if (!drives[next].Open(extra.directory, writable, LeafLabel(extra.directory).c_str()))
            return false;
        Volume::Mount(extra.letter, &drives[next++], extra.cdrom ? Volume::Cdrom : Volume::Fixed,
                      writable);
    }
    return true;
}
} // namespace HostVolume
