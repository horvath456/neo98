// SPDX-License-Identifier: GPL-2.0-or-later
// Compare plain and layered writable drives with a deterministic random
// sequence. Verify results, names, short aliases, attributes, sizes,
// contents, timestamps and directory order, including wildcard listings.
// Restart both stores periodically to test persistence. The bench mode
// measures lookup/read/list costs; prepare creates alias sidecars.
// tools/test_layer_random.py supplies isolated folders and seeds.
#include "../src/files.h"
#include "../src/volume.h"
#include "host_volume.h"
#include "system_tree_host.h"
#include <ctime>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace Files;

namespace
{
unsigned long long state = 1;
unsigned Random(unsigned below)
{
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return below ? unsigned((state >> 11) % below) : 0;
}
// The guest clock: it moves with the operations and not with the host, so
// both runs stamp the same times.
unsigned clock_step = 0;
TimeStamp Clock()
{
    const unsigned k = clock_step;
    return TimeStamp{(21u << 9) | (3u << 5) | (1u + (k / 43200) % 27), (((k / 1800) % 24) << 11) | (((k / 30) % 60) << 5) | (k % 30), 0};
}

const char *const directories[] = {"", "ALPHA", "Beta Dir Long Name", "ALPHA\\INNER", "gamma", "ALPHA\\INNER\\Deep Folder",
                                   "NEW1", "New Folder Two", "ALPHA\\made", "Beta Dir Long Name\\Beta Dir Long Sub"};
const char *const files[] = {"ONE.TXT", "two.txt", "Three Long Name.txt", "Three Long Name Second.txt",
                             "Three Long Name Third.text", "data.bin", "READ ME", "x.y.z", "UPPER.DAT", "Mixed.Case",
                             "big.bin"};
constexpr unsigned DirectoryCount = sizeof directories / sizeof *directories, FileCount = sizeof files / sizeof *files;

std::string Cased(std::string text)
{
    const unsigned how = Random(4);
    for (auto &c : text)
    {
        if (how == 1 && c >= 'a' && c <= 'z')
            c = char(c - 32);
        if (how == 2 && c >= 'A' && c <= 'Z')
            c = char(c + 32);
    }
    return text;
}
std::string DirectoryPath() { return std::string("C:\\") + Cased(directories[Random(DirectoryCount)]); }
std::string FilePath()
{
    const std::string directory = directories[Random(DirectoryCount)];
    return "C:\\" + Cased(directory.empty() ? std::string(files[Random(FileCount)]) : directory + "\\" + files[Random(FileCount)]);
}

unsigned Hash(const unsigned char *data, size_t size, unsigned hash = 2166136261u)
{
    for (size_t i = 0; i < size; ++i)
        hash = (hash ^ data[i]) * 16777619u;
    return hash;
}
// Size and hash of a file read through a session, or the error.
std::string Content(Session &session, const char *path)
{
    unsigned handle = 0, status = 0, done = 0, hash = 2166136261u, total = 0;
    if (const unsigned error = session.Open(path, 1, 3, 3, 0, handle, status))
        return "error " + std::to_string(error);
    static unsigned char block[65536];
    for (;;)
    {
        const unsigned error = session.Read(handle, block, sizeof block, done);
        if (error)
        {
            session.Close(handle);
            return "read error " + std::to_string(error);
        }
        if (!done)
            break;
        hash = Hash(block, done, hash);
        total += done;
    }
    session.Close(handle);
    char text[64];
    snprintf(text, sizeof text, "%u bytes %08x", total, hash);
    return text;
}
// The comparison of FindFirstFile (src/x86_files.cpp): a pattern is matched
// against the long name and against the 8.3 alias.
bool Match(const char *pattern, const char *name)
{
    const auto fold = [](unsigned c) { return c >= 'a' && c <= 'z' ? c - 32 : c; };
    if (!strcmp(pattern, "*.*"))
        pattern = "*";
    const char *star = nullptr, *retry = nullptr;
    while (*name)
    {
        if (*pattern == '?' || fold((unsigned char)*pattern) == fold((unsigned char)*name))
            ++pattern, ++name;
        else if (*pattern == '*')
            star = pattern++, retry = name;
        else if (star)
            pattern = star + 1, name = ++retry;
        else
            return false;
    }
    while (*pattern == '*')
        ++pattern;
    return !*pattern;
}
// One directory as a program sees it, in the order of the enumeration: the
// entries a pattern selects, each with alias, attributes, size, creation and
// write time. The write time of a directory is that of its creation (a FAT
// directory entry is not written again). The access date is compared where
// the store sets it, right after a call that reads or creates (Accessed):
// from one start to the next it is kept in the access time of the host
// file, which the host moves on its own when it reads the file.
void List(Store &store, Session &session, const std::string &path, bool deep, bool contents, std::vector<std::string> &out,
          const char *pattern = "*")
{
    unsigned node = 0, id = 0;
    if (const unsigned error = store.Lookup(path.c_str(), node))
    {
        out.push_back(path + " error " + std::to_string(error));
        return;
    }
    std::vector<std::string> lines, below;
    for (unsigned index = 0; !store.Enumerate(node, index, id); ++index)
    {
        const Node *entry = store.Get(id);
        if (!entry)
            continue;
        const std::string full = path + (path.back() == '\\' ? "" : "\\") + entry->name;
        if (deep && (entry->attributes & 16))
            below.push_back(full);
        if (!Match(pattern, entry->name) && !(entry->alias[0] && Match(pattern, entry->alias)))
            continue;
        char text[700];
        snprintf(text, sizeof text, "%s alias=%s attributes=%u size=%u created=%u,%u written=%u,%u %s", full.c_str(),
                 entry->alias, entry->attributes, (entry->attributes & 16) ? 0u : entry->size, entry->times[0].date,
                 entry->times[0].time, entry->times[2].date, entry->times[2].time,
                 contents && !(entry->attributes & 16) ? Content(session, full.c_str()).c_str() : "");
        lines.push_back(text);
    }
    std::sort(below.begin(), below.end());
    out.insert(out.end(), lines.begin(), lines.end());
    for (const auto &directory : below)
        List(store, session, directory, deep, contents, out);
}

// The access date the store holds for a file it has just read or created:
// the day of the guest clock.
unsigned Accessed(Store &store, const std::string &path)
{
    unsigned node = 0;
    return !store.Lookup(path.c_str(), node) && store.Get(node) ? store.Get(node)->times[1].date : 0u;
}

// The drives of one run. A plain folder, or a base folder below a layer.
struct Machine
{
    std::string plain, base, layer;
    HostVolume::Drive *drive = nullptr;
#ifndef NEO98_LAYER_BASELINE
    HostVolume::Layered *layered = nullptr;
#endif
    Store *store = nullptr;
    Session *session = nullptr;
    bool Start()
    {
        Volume::Clear();
        SystemTree::Mount(nullptr);
        drive = new HostVolume::Drive;
        if (!plain.empty())
        {
            if (!drive->Open(plain.c_str(), true, "NEO98"))
                return false;
        }
        else
        {
#ifndef NEO98_LAYER_BASELINE
            // As the host mounts a game machine: the drive of the normal
            // machine, read only, below the layer (HostVolume::Layered).
            layered = new HostVolume::Layered;
            if (!drive->Open(base.c_str(), false, "NEO98") || !layered->Open(layer.c_str(), drive))
                return false;
            Volume::Mount('C', layered, Volume::Fixed, true);
            store = new Store;
            session = new Session(*store);
            return true;
#else
            // Before the layer was the provider's: the tree, the volume above it.
            if (!SystemTreeHost::Mount(base.c_str()) || !drive->Open(layer.c_str(), true, "NEO98"))
                return false;
#endif
        }
        Volume::Mount('C', drive, Volume::Fixed, true);
        store = new Store;
        session = new Session(*store);
        return true;
    }
    void Stop()
    {
        delete session;
        store->Flush();
        delete store;
#ifndef NEO98_LAYER_BASELINE
        delete layered;
        layered = nullptr;
#endif
        delete drive;
        session = nullptr;
        store = nullptr;
        drive = nullptr;
    }
};

// One operation. The line it adds names the call and everything it answered.
void Operate(Machine &machine, unsigned step, std::vector<std::string> &out)
{
    Store &store = *machine.store;
    Session &session = *machine.session;
    clock_step = step;
    char line[900];
    unsigned handle = 0, status = 0, done = 0, position = 0;
    static unsigned char block[70000];
    const unsigned kind = Random(100);
    if (kind < 14)
    {
        // A new file, or a new content for one that is there.
        const std::string path = FilePath();
        const unsigned size = Random(8) ? Random(3000) : Random(70000), disposition = Random(4) ? 2u : 1u;
        for (unsigned i = 0; i < size; ++i)
            block[i] = (unsigned char)Random(256);
        unsigned error = session.Open(path.c_str(), 3, 0, disposition, Random(6) ? 0u : 1u, handle, status), wrote = 0;
        if (!error)
        {
            wrote = session.Write(handle, block, size, done);
            session.Close(handle);
        }
        // A file that was made now carries today as its access date.
        snprintf(line, sizeof line, "create %s disposition=%u size=%u -> %u status=%u write=%u done=%u accessed=%u", path.c_str(),
                 disposition, size, error, error ? 0 : status, wrote, done, !error && !status ? Accessed(store, path) : 0u);
    }
    else if (kind < 26)
    {
        // Bytes into the middle of a file, or at its end.
        const std::string path = FilePath();
        const bool append = Random(2) != 0;
        const unsigned size = 1 + Random(400), offset = Random(6000);
        for (unsigned i = 0; i < size; ++i)
            block[i] = (unsigned char)Random(256);
        unsigned error = session.Open(path.c_str(), 3, 3, Random(5) ? 3u : 4u, 0, handle, status), wrote = 0;
        if (!error)
        {
            session.Seek(handle, append ? 0 : offset, append ? 2 : 0, position);
            wrote = session.Write(handle, block, size, done);
            session.Close(handle);
        }
        snprintf(line, sizeof line, "write %s %s offset=%u size=%u -> %u at=%u write=%u done=%u", path.c_str(), append ? "append" : "seek",
                 offset, size, error, position, wrote, done);
    }
    else if (kind < 32)
    {
        const std::string path = FilePath();
        const unsigned size = Random(5000);
        unsigned error = session.Open(path.c_str(), 3, 3, 3, 0, handle, status), cut = 0;
        if (!error)
        {
            session.Seek(handle, size, 0, position);
            cut = session.Truncate(handle);
            session.Close(handle);
        }
        snprintf(line, sizeof line, "truncate %s size=%u -> %u truncate=%u", path.c_str(), size, error, cut);
    }
    else if (kind < 42)
    {
        const std::string path = FilePath();
        snprintf(line, sizeof line, "delete %s -> %u", path.c_str(), store.Remove(path.c_str(), false));
    }
    else if (kind < 54)
    {
        // A file onto a free or a taken name, in its directory or another.
        const std::string from = FilePath(), to = FilePath();
        const bool replace = Random(3) == 0;
        snprintf(line, sizeof line, "rename %s to %s replace=%u -> %u", from.c_str(), to.c_str(), replace ? 1 : 0,
                 store.Rename(from.c_str(), to.c_str(), replace));
    }
    else if (kind < 60)
    {
        const std::string from = DirectoryPath(), to = DirectoryPath();
        snprintf(line, sizeof line, "rename directory %s to %s -> %u", from.c_str(), to.c_str(),
                 from.size() > 3 && to.size() > 3 ? store.Rename(from.c_str(), to.c_str(), false) : 5u);
    }
    else if (kind < 66)
    {
        const std::string path = DirectoryPath();
        snprintf(line, sizeof line, "make directory %s -> %u", path.c_str(), path.size() > 3 ? store.MakeDirectory(path.c_str()) : 5u);
    }
    else if (kind < 70)
    {
        const std::string path = DirectoryPath();
        snprintf(line, sizeof line, "remove directory %s -> %u", path.c_str(), path.size() > 3 ? store.Remove(path.c_str(), true) : 5u);
    }
    else if (kind < 76)
    {
        const std::string path = Random(4) ? FilePath() : DirectoryPath();
        static const unsigned choices[] = {0, 1, 2, 3, 32, 33, 34, 128, 6};
        const unsigned attributes = choices[Random(9)];
        snprintf(line, sizeof line, "attributes %s %u -> %u", path.c_str(), attributes,
                 path.size() > 3 ? store.Attributes(path.c_str(), attributes) : 5u);
    }
    else if (kind < 81)
    {
        // Creation and write time through a handle, as SetFileTime does.
        const std::string path = FilePath();
        const unsigned index = Random(2) ? 2u : 0u;
        const TimeStamp stamp{(15u << 9) | ((1u + Random(12)) << 5) | (1u + Random(28)), (Random(24) << 11) | (Random(60) << 5) | Random(30), 0};
        unsigned error = session.Open(path.c_str(), 3, 3, 3, 0, handle, status), set = 0;
        if (!error)
        {
            set = session.SetTime(handle, index, stamp);
            session.Close(handle);
        }
        snprintf(line, sizeof line, "time %s index=%u %u,%u -> %u set=%u", path.c_str(), index, stamp.date, stamp.time, error, set);
    }
    else if (kind < 86)
    {
        const std::string from = FilePath(), to = FilePath();
        const bool fail = Random(2) != 0;
        snprintf(line, sizeof line, "copy %s to %s fail_if_exists=%u -> %u", from.c_str(), to.c_str(), fail ? 1 : 0,
                 session.Copy(from.c_str(), to.c_str(), fail));
    }
    else if (kind < 92)
    {
        // The file by another spelling, or by its 8.3 alias.
        std::string path = FilePath();
        unsigned node = 0;
        const unsigned error = store.Lookup(path.c_str(), node);
        std::string alias;
        if (!error && store.Get(node) && store.Get(node)->alias[0])
        {
            alias = path.substr(0, path.rfind('\\') + 1) + store.Get(node)->alias;
            path = alias;
        }
        const std::string content = Content(session, path.c_str());
        // Reading at least a byte makes today the access date.
        snprintf(line, sizeof line, "read %s -> lookup=%u %s accessed=%u", path.c_str(), error, content.c_str(),
                 content.compare(0, 5, "error") && content.compare(0, 8, "0 bytes ") ? Accessed(store, path) : 0u);
    }
    else
    {
        // A listing, every third one through a pattern, as FindFirstFile
        // selects: by long name or by alias.
        const std::string path = DirectoryPath();
        static const char *const patterns[] = {"*.*", "*", "*.txt", "T*", "?HREE*", "*~1*", "*.T?T", "*long*", "*.", "*.bin",
                                               "THREEL~?.*", "*a*", "new*"};
        const char *pattern = Random(3) ? "*" : patterns[Random(sizeof patterns / sizeof *patterns)];
        std::vector<std::string> lines;
        List(store, session, path, false, false, lines, pattern);
        std::string all = "list " + path + " " + pattern + " ->";
        for (const auto &entry : lines)
            all += "\n    " + entry;
        out.push_back(all);
        return;
    }
    out.push_back(line);
}

std::vector<std::string> Run(Machine &machine, unsigned long long seed, unsigned operations, bool &okay)
{
    std::vector<std::string> out;
    state = seed * 2654435761ull + 88172645463325252ull;
    SetClock(Clock);
    okay = machine.Start();
    if (!okay)
        return out;
    for (unsigned step = 0; step < operations; ++step)
    {
        Operate(machine, step, out);
        // A restart: everything has to come back from the folders.
        if (step % 61 == 60)
        {
            machine.Stop();
            if (!(okay = machine.Start()))
                return out;
            out.push_back("restart");
        }
    }
    machine.Stop();
    okay = machine.Start();
    if (!okay)
        return out;
    std::vector<std::string> tree;
    List(*machine.store, *machine.session, "C:\\", true, true, tree);
    std::string all = "final tree ->";
    for (const auto &entry : tree)
        all += "\n    " + entry;
    out.push_back(all);
    machine.Stop();
    return out;
}

// Processor time of this process, not the wall clock: the figures hold on
// a machine that is busy with other things.
double Now() { return double(std::clock()) * 1000.0 / CLOCKS_PER_SEC; }
// Every directory listed and every file looked up, opened and read; twice,
// the second time with what the store and the provider remember.
void Walk(Store &store, Session &session, const std::string &path, unsigned &count, unsigned long long &bytes, bool read)
{
    unsigned node = 0, id = 0;
    if (store.Lookup(path.c_str(), node))
        return;
    std::vector<std::pair<std::string, bool>> entries;
    for (unsigned index = 0; !store.Enumerate(node, index, id); ++index)
        if (const Node *entry = store.Get(id))
            entries.emplace_back(path + (path.back() == '\\' ? "" : "\\") + entry->name, (entry->attributes & 16) != 0);
    for (const auto &entry : entries)
    {
        if (entry.second)
        {
            Walk(store, session, entry.first, count, bytes, read);
            continue;
        }
        ++count;
        if (!read)
            continue;
        unsigned handle = 0, status = 0, done = 0;
        static unsigned char block[65536];
        if (session.Open(entry.first.c_str(), 1, 3, 3, 0, handle, status))
            continue;
        while (!session.Read(handle, block, sizeof block, done) && done)
            bytes += done;
        session.Close(handle);
    }
}
} // namespace

int main(int argc, char **argv)
{
    const std::string mode = argc > 1 ? argv[1] : "";
    // The limits of the host's store, for the plain run as for the layered.
    Files::Configure(24576, 192u * 1024 * 1024);
    if (mode == "prepare" && argc == 3)
    {
        Machine machine;
        machine.plain = argv[2];
        if (!machine.Start())
            return 2;
        std::vector<std::string> lines;
        List(*machine.store, *machine.session, "C:\\", true, false, lines);
        machine.Stop();
        printf("LAYER RANDOM prepared %zu entries\n", lines.size());
        return 0;
    }
    if (mode == "compare" && argc == 7)
    {
        const unsigned long long seed = strtoull(argv[2], nullptr, 10);
        const unsigned operations = unsigned(strtoul(argv[3], nullptr, 10));
        Machine plain, layered;
        plain.plain = argv[4];
        layered.base = argv[5];
        layered.layer = argv[6];
        bool first_okay = false, second_okay = false;
        const auto first = Run(plain, seed, operations, first_okay);
        const auto second = Run(layered, seed, operations, second_okay);
        if (!first_okay || !second_okay)
        {
            printf("LAYER RANDOM seed=%llu cannot mount (plain=%d layered=%d)\n", seed, first_okay, second_okay);
            return 2;
        }
        // LAYER_RANDOM_DUMP=FOLDER keeps both transcripts, to read side by side.
        if (const char *folder = getenv("LAYER_RANDOM_DUMP"))
            for (int which = 0; which < 2; ++which)
                if (FILE *file = fopen((std::string(folder) + (which ? "/layered.txt" : "/plain.txt")).c_str(), "w"))
                {
                    for (const auto &line : which ? second : first)
                        fprintf(file, "%s\n", line.c_str());
                    fclose(file);
                }
        unsigned different = 0, errors = 0;
        for (size_t i = 0; i < first.size() || i < second.size(); ++i)
        {
            const std::string a = i < first.size() ? first[i] : "(nothing)", b = i < second.size() ? second[i] : "(nothing)";
            if (a.find("-> 0") != std::string::npos)
                ++errors;
            if (a == b)
                continue;
            if (different++ >= 12)
                continue;
            // Of two listings only the entries that are not in both.
            const auto only = [](const std::string &mine, const std::string &other) {
                std::string out;
                for (size_t from = 0; from < mine.size();)
                {
                    size_t end = mine.find('\n', from);
                    if (end == std::string::npos)
                        end = mine.size();
                    const std::string line = mine.substr(from, end - from);
                    if (!from || ("\n" + other + "\n").find("\n" + line + "\n") == std::string::npos)
                        out += line + "\n";
                    from = end + 1;
                }
                return out;
            };
            printf("LAYER RANDOM seed=%llu line %zu differs\n  plain:   %s  layered: %s", seed, i, only(a, b).c_str(), only(b, a).c_str());
        }
        printf("LAYER RANDOM seed=%llu operations=%u lines=%zu succeeded=%u different=%u %s\n", seed, operations, first.size(), errors,
               different, different ? "FAIL" : "PASS");
        return different ? 1 : 0;
    }
    if (mode == "bench" && (argc == 4 || argc == 5))
    {
        Machine machine;
        if (!strcmp(argv[2], "plain"))
            machine.plain = argv[3];
        else if (argc == 5)
        {
            machine.base = argv[3];
            machine.layer = argv[4];
        }
        else
            return 2;
        SetClock(Clock);
        double from = Now();
        if (!machine.Start())
            return 2;
        const double mount = Now() - from;
        double times[4] = {};
        unsigned counts[4] = {};
        unsigned long long bytes = 0;
        // List only, then read, then both again.
        for (unsigned pass = 0; pass < 4; ++pass)
        {
            from = Now();
            Walk(*machine.store, *machine.session, "C:\\", counts[pass], bytes, pass % 2 == 1);
            times[pass] = Now() - from;
        }
        machine.Stop();
        printf("LAYER BENCH %s files=%u bytes=%llu mount=%.1f list=%.1f read=%.1f list_again=%.1f read_again=%.1f\n", argv[2], counts[0],
               bytes / 2, mount, times[0], times[1], times[2], times[3]);
        return 0;
    }
    fprintf(stderr, "usage: test-layer-random prepare FOLDER | compare SEED OPERATIONS PLAIN BASE LAYER | bench plain FOLDER | bench layer BASE LAYER\n");
    return 2;
}
