// SPDX-License-Identifier: GPL-2.0-or-later
// The saved states of a game machine on disk (tools/host_savestate.h), without a machine: the folder, the tree, the copies of
// the layers and what happens when a write is cut short.
// basics a state of the files and one with a machine state and a picture
// tree parents, the state the machine stands on, the order of the
// list, deleting in the middle, a ring, a parent that is gone
// automatic the states "before loading": the newest five stay, a renamed
// one is the person's and stays
// names long names, UTF-8, control characters, an empty name
// fits files of the normal drive changed or gone, another engine of
// the supplier (and another revision of NEO98, which is fine),
// another format, another program, a machine set otherwise,
// folders that are no whole state
// layers a layer with tombstones, an opaque directory and alias pins is
// saved, changed and loaded: the layer is the copy byte for
// byte, with times and modes, and the machine sees what it saw
// cut saving, loading and deleting cut short at every step: a state
// is whole or not there, a load happened or did not
// test-savestate-store WORK
// WORK is an empty folder; tools/test_savestates.py runs this, once with
// block clones and once with NEO98_COPY_PLAIN=1.
#include "../src/files.h"
#include "../src/volume.h"
#include "host_savestate.h"
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <sys/wait.h>
#include <vector>

using namespace Files;
namespace State = HostSaveState;

namespace
{
unsigned failed = 0, passed = 0;
void Check(const std::string &name, bool condition)
{
    printf("%s %s\n", condition ? "ok  " : "FAIL", name.c_str());
    ++(condition ? passed : failed);
}
void WriteHost(const std::string &path, const std::string &text)
{
    if (FILE *file = fopen(path.c_str(), "wb"))
    {
        fwrite(text.data(), 1, text.size(), file);
        fclose(file);
    }
}
std::string ReadHost(const std::string &path)
{
    std::string text;
    State::ReadFile(path, text);
    return text;
}
// Everything below a folder: kind, mode, time of modification of a file,
// and its bytes. Not written with the code under test.
using Digest = std::map<std::string, std::string>;
void Walk(const std::string &root, const std::string &relative, Digest &out)
{
    const std::string path = relative.empty() ? root : root + "/" + relative;
    DIR *handle = opendir(path.c_str());
    if (!handle)
        return;
    std::vector<std::string> names;
    while (const dirent *item = readdir(handle))
        if (strcmp(item->d_name, ".") && strcmp(item->d_name, ".."))
            names.push_back(item->d_name);
    closedir(handle);
    for (const auto &name : names)
    {
        const std::string below = relative.empty() ? name : relative + "/" + name, full = root + "/" + below;
        struct stat info;
        if (lstat(full.c_str(), &info))
            continue;
        if (S_ISDIR(info.st_mode))
        {
            out[below] = "directory";
            Walk(root, below, out);
            continue;
        }
        long long seconds = 0;
        long nanoseconds = 0;
        State::ModifiedTime(info, seconds, nanoseconds);
        char line[128];
        snprintf(line, sizeof line, "file mode=%o time=%lld.%09ld size=%lld ", unsigned(info.st_mode & 0777), seconds, nanoseconds,
                 (long long)info.st_size);
        out[below] = line + ReadHost(full);
    }
}
Digest DigestOf(const std::string &root)
{
    Digest out;
    Walk(root, "", out);
    return out;
}
std::string Difference(const Digest &a, const Digest &b)
{
    for (const auto &entry : a)
    {
        const auto other = b.find(entry.first);
        if (other == b.end())
            return "missing " + entry.first;
        if (other->second != entry.second)
            return "differs " + entry.first;
    }
    for (const auto &entry : b)
        if (!a.count(entry.first))
            return "added " + entry.first;
    return std::string();
}

// A drive C: of a game machine for the store: a folder below a layer.
struct Machine
{
    HostVolume::Drive *drive = nullptr;
    HostVolume::Layered *layered = nullptr;
    Store *store = nullptr;
    Session *session = nullptr;
    bool Plain(const std::string &folder)
    {
        Volume::Clear();
        SystemTree::Mount(nullptr);
        drive = new HostVolume::Drive;
        if (!drive->Open(folder.c_str(), true, "NEO98"))
            return false;
        Volume::Mount('C', drive, Volume::Fixed, true);
        store = new Store;
        session = new Session(*store);
        return true;
    }
    bool Layer(const std::string &base, const std::string &layer)
    {
        Volume::Clear();
        SystemTree::Mount(nullptr);
        drive = new HostVolume::Drive;
        layered = new HostVolume::Layered;
        if (!drive->Open(base.c_str(), false, "NEO98") || !layered->Open(layer.c_str(), drive))
            return false;
        Volume::Mount('C', layered, Volume::Fixed, true);
        store = new Store;
        session = new Session(*store);
        return true;
    }
    void Stop()
    {
        delete session;
        store->Flush();
        delete store;
        delete layered;
        delete drive;
        *this = Machine();
    }
    std::string Read(const char *path, unsigned *error_out = nullptr)
    {
        unsigned handle = 0, status = 0, done = 0;
        std::string text;
        const unsigned error = session->Open(path, 1, 3, 3, 0, handle, status);
        if (error_out)
            *error_out = error;
        if (error)
            return std::string();
        char block[4096];
        while (!session->Read(handle, block, sizeof block, done) && done)
            text.append(block, done);
        session->Close(handle);
        return text;
    }
    unsigned Write(const char *path, const std::string &text)
    {
        unsigned handle = 0, status = 0, done = 0;
        unsigned error = session->Open(path, 3, 0, 2, 0, handle, status);
        if (error)
            return error;
        error = session->Write(handle, text.data(), unsigned(text.size()), done);
        session->Close(handle);
        return error;
    }
    std::vector<std::string> List(const char *path)
    {
        std::vector<std::string> out;
        unsigned node = 0, id = 0;
        if (store->Lookup(path, node))
            return out;
        for (unsigned index = 0; !store->Enumerate(node, index, id); ++index)
            if (const Node *entry = store->Get(id))
                out.push_back(std::string(entry->name) + "=" + entry->alias);
        return out;
    }
    bool Exists(const char *path)
    {
        unsigned node = 0;
        return !store->Lookup(path, node);
    }
};

// A normal machine with a drive C: and a game machine of it, as
// HostGameMachine::Create leaves them, without a Windows in it.
struct Fixture
{
    std::string normal, folder, layer;
    State::Context context;
    explicit Fixture(const std::string &work, const char *name)
    {
        normal = work + "/" + name;
        mkdir(normal.c_str(), 0755);
        WriteHost(normal + "/.neo98-setup.json", "{}\n");
        mkdir((normal + "/SPIELE").c_str(), 0755);
        WriteHost(normal + "/SPIELE/SPIEL.EXE", "MZ the game");
        WriteHost(normal + "/SPIELE/DATA.PAK", std::string(70000, 'd'));
        WriteHost(normal + "/SPIELE/Long Name One.txt", "one");
        mkdir((normal + "/SPIELE/LEVELS").c_str(), 0755);
        WriteHost(normal + "/SPIELE/LEVELS/L1.MAP", "level one");
        WriteHost(normal + "/SPIELE/LEVELS/L2.MAP", "level two");
        mkdir((normal + "/SPIELE/OLD").c_str(), 0755);
        WriteHost(normal + "/SPIELE/OLD/KEEP.TXT", "keep");
        // The normal machine has used its drive: every directory has its sidecar.
        Machine machine;
        machine.Plain(normal);
        for (const char *directory : {"C:\\", "C:\\SPIELE", "C:\\SPIELE\\LEVELS", "C:\\SPIELE\\OLD"})
            machine.List(directory);
        machine.Stop();
        folder = HostGameMachine::Directory(normal) + "/Spiel";
        layer = HostGameMachine::Layer(folder);
        HostSettings::MakeDirectories(layer);
        HostSettings::Settings settings;
        settings.found = true;
        settings.machine = folder;
        settings.game_name = "Spiel";
        settings.game_program = "C:\\SPIELE\\SPIEL.EXE";
        settings.game_source = normal;
        HostGameMachine::SaveSettings(folder, settings);
        WriteHost(folder + "/C-Schicht.registry", "journal one");
        State::ContextOf(folder, "rev-1", context);
        context.engines = {{"dos", "dos-1-kern"}, {"windows", "windows-0"}};
    }
    std::vector<State::Entry> List() { return State::List(context); }
    State::Entry Find(const std::string &id)
    {
        for (const auto &entry : List())
            if (entry.id == id)
                return entry;
        return State::Entry();
    }
    // A state with a machine state of `bytes`.
    std::string Save(const std::string &name, const char *kind = "manual", const char *bytes = "machine", std::string *error_out = nullptr)
    {
        State::Request request;
        request.name = name;
        request.kind = kind;
        request.revision = context.revision;
        request.engine = "dos-1-kern";
        std::vector<unsigned char> machine(bytes, bytes + strlen(bytes));
        if (*bytes)
        {
            request.game = "dos";
            request.machine = &machine;
        }
        State::BaseFile file;
        if (State::Describe(normal, 'C', "SPIELE/SPIEL.EXE", file))
            request.files.push_back(file);
        if (State::Describe(normal, 'C', "SPIELE/DATA.PAK", file))
            request.files.push_back(file);
        request.machine_info = {{"memory", "16"}, {"type", "svga"}};
        std::string id, error;
        State::Save(folder, request, id, error);
        if (error_out)
            *error_out = error;
        // Two states never share a second in these tests' order of time.
        if (!id.empty())
            Age(id, ++clock);
        return id;
    }
    // Gives a state its place in time, so the order does not hang on the clock.
    void Age(const std::string &id, long long seconds)
    {
        State::Info info;
        if (State::ReadInfo(State::StateFolder(folder, id), info))
        {
            info.time = 1000000 + seconds;
            State::Rewrite(State::StateFolder(folder, id), info);
        }
    }
    long long clock = 0;
};

void Basics(const std::string &work)
{
    Fixture f(work, "basics");
    Check("basics: a game machine without states lists none", f.List().empty() && !State::Count(f.folder));
    Check("basics: and stands on none", State::Current(f.folder).empty());
    WriteHost(f.layer + "/SAVE.DAT", "level 3");
    std::string error;
    const std::string files = f.Save("Files only", "manual", "", &error);
    Check("basics: a state of the files is saved (" + error + ")", !files.empty());
    auto entry = f.Find(files);
    Check("basics: it is listed with its name", entry.name == "Files only" && entry.kind == "manual");
    Check("basics: its kind of game is off", entry.game == "off" && !entry.machine_size);
    Check("basics: it holds the layer and the journal", entry.layers == "C" && entry.registry);
    Check("basics: it can be loaded", entry.usable && entry.reason.empty() && !entry.damaged);
    Check("basics: the machine stands on it", State::Current(f.folder) == files && entry.current);
    Check("basics: the copy of the layer is there", ReadHost(entry.folder + "/C-Schicht/SAVE.DAT") == "level 3");
    Check("basics: the copy of the journal is there", ReadHost(entry.folder + "/C-Schicht.registry") == "journal one");
    Check("basics: no Maschine.bin for the files alone", !State::Present(State::MachineFile(entry.folder)));

    // With a machine state and a picture.
    std::vector<unsigned> pixels(64 * 48);
    for (size_t i = 0; i < pixels.size(); ++i)
        pixels[i] = unsigned(i * 2654435761u) & 0xffffff;
    std::vector<unsigned char> machine(300000);
    for (size_t i = 0; i < machine.size(); ++i)
        machine[i] = (unsigned char)(i * 7 + i / 251);
    State::Request request;
    request.kind = "quick";
    request.game = "dos";
    request.revision = "rev-1";
    request.engine = "dos-1-kern";
    request.machine = &machine;
    request.picture = pixels.data();
    request.picture_width = 64;
    request.picture_height = 48;
    request.machine_info = {{"memory", "16"}};
    std::string id;
    Check("basics: a state with a machine state is saved", State::Save(f.folder, request, id, error));
    entry = f.Find(id);
    Check("basics: it came from the first", entry.parent == files && entry.depth == 1);
    Check("basics: a name from kind and time", entry.name == "Quick save");
    Check("basics: the size of the machine state", entry.machine_size == machine.size() && entry.game == "dos");
    unsigned long long size = 0;
    Check("basics: its checksum", State::FileSum(State::MachineFile(entry.folder), size) == entry.machine_sum && size == machine.size());
    Check("basics: what the supplier said", entry.machine.size() == 1 && entry.machine[0].second == "16");
    std::vector<unsigned> back;
    unsigned width = 0, height = 0;
    Check("basics: the picture reads back", State::ReadPicture(State::PictureFile(entry.folder), back, width, height) && width == 64 &&
                                                height == 48 && back == pixels && entry.has_picture);
    Check("basics: two states", f.List().size() == 2 && State::Count(f.folder) == 2);
    Check("basics: the first is no longer the current one", !f.Find(files).current && f.Find(id).current);
    // What is not a game machine.
    Check("basics: no state for a folder that is no game machine", !State::Save(work, request, id, error) && !error.empty());
}

void Tree(const std::string &work)
{
    Fixture f(work, "tree");
    // A, B from A, then back to A, C from A, D from C; back to B, E from B.
    const std::string a = f.Save("A"), b = f.Save("B");
    std::string error;
    Check("tree: load A", State::Restore(f.folder, a, error) && State::Current(f.folder) == a);
    const std::string c = f.Save("C"), d = f.Save("D");
    Check("tree: load B", State::Restore(f.folder, b, error) && State::Current(f.folder) == b);
    const std::string e = f.Save("E");
    Check("tree: parents", f.Find(a).parent.empty() && f.Find(b).parent == a && f.Find(c).parent == a && f.Find(d).parent == c &&
                               f.Find(e).parent == b);
    std::string order, depths;
    for (const auto &entry : f.List())
    {
        order += entry.name;
        depths += char('0' + entry.depth);
    }
    Check("tree: the list follows the tree, the older branch first (" + order + " " + depths + ")", order == "ABECD" && depths == "01212");
    Check("tree: children are counted", f.Find(a).children == 2 && f.Find(b).children == 1 && f.Find(d).children == 0);
    Check("tree: the machine stands on E", f.Find(e).current);
    // Deleting in the middle: E hangs on A afterwards.
    Check("tree: delete B", State::Delete(f.folder, b, error));
    Check("tree: B is gone", f.Find(b).id.empty() && f.List().size() == 4);
    Check("tree: its child came to its parent", f.Find(e).parent == a && f.Find(e).depth == 1);
    Check("tree: the machine still stands on E", State::Current(f.folder) == e);
    // Deleting the state the machine stands on: it stands on the parent.
    Check("tree: delete E, the current one", State::Delete(f.folder, e, error) && State::Current(f.folder) == a);
    // Deleting the first: its children are first states.
    Check("tree: delete A", State::Delete(f.folder, a, error));
    Check("tree: C is a first state now", f.Find(c).parent.empty() && f.Find(c).depth == 0 && f.Find(d).depth == 1);
    Check("tree: nothing is current after its line is gone", State::Current(f.folder).empty());
    const std::string g = f.Save("G");
    Check("tree: a state saved then is a first one", f.Find(g).parent.empty());
    // A parent that is gone (a folder removed in the Finder) and a ring.
    State::RemoveTree(State::StateFolder(f.folder, c));
    Check("tree: a state whose parent folder is gone is a first one", f.Find(d).depth == 0 && f.Find(d).parent.empty());
    State::Info info;
    State::ReadInfo(State::StateFolder(f.folder, d), info);
    info.parent = g;
    State::Rewrite(State::StateFolder(f.folder, d), info);
    State::ReadInfo(State::StateFolder(f.folder, g), info);
    info.parent = d;
    State::Rewrite(State::StateFolder(f.folder, g), info);
    const auto ring = f.List();
    Check("tree: a ring of two is listed once each", ring.size() == 2 && ring[0].depth == 0 && ring[1].depth == 1);
    Check("tree: deleting what does not exist fails", !State::Delete(f.folder, "nichts", error) && !State::Delete(f.folder, "", error) &&
                                                         !State::Delete(f.folder, "../Spiel", error));
    // Thirty states in one line and in a bush.
    Fixture many(work, "tree-many");
    std::vector<std::string> ids;
    for (int i = 0; i < 30; ++i)
    {
        if (i % 7 == 6)
            State::SetCurrent(many.folder, ids[size_t(i) / 2]);
        ids.push_back(many.Save("Stand " + std::to_string(i)));
    }
    const auto listed = many.List();
    bool parents_first = listed.size() == 30;
    std::map<std::string, size_t> at;
    for (size_t i = 0; i < listed.size(); ++i)
        at[listed[i].id] = i;
    for (const auto &entry : listed)
        if (!entry.parent.empty())
            parents_first = parents_first && at[entry.parent] < at[entry.id] && listed[at[entry.parent]].depth + 1 == entry.depth;
    Check("tree: thirty states, each after its parent and one deeper", parents_first);
    // A copy of a state folder made by hand is a state of its own.
    State::CopyTree(State::StateFolder(many.folder, ids[3]), State::StateFolder(many.folder, ids[3] + " Kopie"), "test");
    Check("tree: a copy of a state folder is a state", many.List().size() == 31 && many.Find(ids[3] + " Kopie").usable);
}

void Automatic(const std::string &work)
{
    Fixture f(work, "automatic");
    const std::string first = f.Save("Mein Stand");
    std::vector<std::string> autos;
    for (int i = 0; i < 8; ++i)
    {
        autos.push_back(f.Save("", "auto"));
        State::SetCurrent(f.folder, first);
    }
    Check("automatic: a name that says what it is", f.Find(autos[0]).name == "Before loading" && f.Find(autos[0]).kind == "auto");
    // The person keeps the second oldest by naming it.
    std::string error;
    Check("automatic: rename one", State::Rename(f.folder, autos[1], "Doch wichtig", error));
    Check("automatic: it is the person's then", f.Find(autos[1]).kind == "manual" && f.Find(autos[1]).name == "Doch wichtig");
    // A state that came from an automatic one about to go.
    State::SetCurrent(f.folder, autos[0]);
    const std::string child = f.Save("Child of an automatic state");
    State::SetCurrent(f.folder, first);
    State::Prune(f.folder);
    unsigned left = 0;
    for (const auto &entry : f.List())
        left += entry.kind == "auto" ? 1u : 0u;
    Check("automatic: the newest five stay", left == 5 && f.Find(autos[0]).id.empty() && f.Find(autos[2]).id.empty() &&
                                                !f.Find(autos[3]).id.empty() && !f.Find(autos[7]).id.empty());
    Check("automatic: the renamed one stays", !f.Find(autos[1]).id.empty());
    Check("automatic: the manual ones stay", !f.Find(first).id.empty() && !f.Find(child).id.empty());
    Check("automatic: the child of a removed one hangs on its parent", f.Find(child).parent == first);
    // The one about to be loaded and the one the machine stands on stay,
    // however old.
    for (int i = 0; i < 4; ++i)
    {
        autos.push_back(f.Save("", "auto"));
        State::SetCurrent(f.folder, autos[3]);
    }
    State::Prune(f.folder, autos[4]);
    Check("automatic: the state being loaded and the current one are spared", !f.Find(autos[3]).id.empty() && !f.Find(autos[4]).id.empty() &&
                                                                                 f.Find(autos[5]).id.empty());
}

void Names(const std::string &work)
{
    Fixture f(work, "names");
    std::string long_name;
    for (int i = 0; i < 40; ++i)
        long_name += "\xc3\xa4x"; // UTF-8 accented character followed by x.
    const std::string id = f.Save(long_name);
    const auto entry = f.Find(id);
    Check("names: a long name is cut to 80 bytes", entry.name.size() <= 80 && entry.name.size() >= 77);
    Check("names: not inside a character", (static_cast<unsigned char>(entry.name.back()) & 0xc0) != 0xc0 &&
                                              entry.name == long_name.substr(0, entry.name.size()));
    std::string error;
    Check("names: control characters become blanks", State::Rename(f.folder, id, "  Line\none\ttwo = three  ", error) &&
                                                         f.Find(id).name == "Line one two = three");
    Check("names: a name may look like a line of the file", State::Rename(f.folder, id, "end = 1", error) && f.Find(id).name == "end = 1" &&
                                                                f.Find(id).usable);
    Check("names: an empty name is refused", !State::Rename(f.folder, id, " \t ", error) && !error.empty() && f.Find(id).name == "end = 1");
    Check("names: a state that is not there", !State::Rename(f.folder, "nichts", "x", error));
    Check("names: umlauts", State::Rename(f.folder, id, "Höhle – vor dem Drachen", error) && f.Find(id).name == "Höhle – vor dem Drachen");
    const std::string unnamed = f.Save("");
    Check("names: without a name it is the time", !f.Find(unnamed).name.compare(0, 11, "State from "));
}

void Fits(const std::string &work)
{
    Fixture f(work, "fits");
    const std::string id = f.Save("Stand");
    const std::string files = f.Save("Dateien", "manual", "");
    Check("fits: a fresh state fits", f.Find(id).usable);
    // Another revision of NEO98 is no reason: the state says who wrote it.
    f.context.revision = "rev-2";
    Check("fits: another revision of NEO98 fits", f.Find(id).usable && f.Find(id).revision == "rev-1");
    f.context.revision = "rev-1";
    Check("fits: the state names the engine of its supplier", f.Find(id).engine == "dos-1-kern" && f.Find(files).engine.empty());
    // Another engine of the supplier: its bytes are laid out otherwise.
    f.context.engines[0].second = "dos-1-anderer";
    Check("fits: another engine is refused, with both in the reason", !f.Find(id).usable && !f.Find(id).damaged &&
                                                                          f.Find(id).reason.find("dos-1-kern") != std::string::npos &&
                                                                          f.Find(id).reason.find("dos-1-anderer") != std::string::npos &&
                                                                          f.Find(id).reason.find("DOS machine") != std::string::npos);
    f.context.engines[0].second = "dos-2-kern";
    Check("fits: provider 2 reads provider 1 with the same core", f.Find(id).usable);
    f.context.engines[0].second = "dos-3-kern";
    Check("fits: provider 3 reads provider 1 with the same core", f.Find(id).usable);
    f.context.engines[0].second = "dos-3-other";
    Check("fits: migration refuses a different core", !f.Find(id).usable);
    f.context.engines[0].second = "dos-4-kern";
    Check("fits: an unknown provider version is refused", !f.Find(id).usable);
    Check("fits: the files alone fit every engine", f.Find(files).usable);
    f.context.engines.erase(f.context.engines.begin());
    Check("fits: a kind this host has no supplier for is refused", !f.Find(id).usable && f.Find(files).usable);
    f.context.engines = {{"dos", "dos-1-kern"}, {"windows", "windows-0"}};
    // What the machine says about itself.
    f.context.game = "dos";
    f.context.machine = {{"memory", "16"}, {"type", "svga"}};
    Check("fits: the same machine fits", f.Find(id).usable);
    f.context.machine = {{"memory", "32"}, {"type", "svga"}};
    Check("fits: another memory size is refused", !f.Find(id).usable && f.Find(id).reason.find("32") != std::string::npos);
    f.context.game = "windows";
    f.context.machine.clear();
    Check("fits: another kind of program is refused", !f.Find(id).usable);
    f.context.game.clear();
    // Another program in the settings.
    f.context.program = "C:\\SPIELE\\ANDERES.EXE";
    Check("fits: another program is refused", !f.Find(id).usable && f.Find(id).reason.find("SPIEL.EXE") != std::string::npos);
    f.context.program = "c:\\spiele\\spiel.exe";
    Check("fits: the program without regard to case", f.Find(id).usable);
    // A file of the normal drive: same size, later time.
    struct stat before;
    stat((f.normal + "/SPIELE/SPIEL.EXE").c_str(), &before);
#ifdef __APPLE__
    struct timespec times[2] = {before.st_atimespec, before.st_mtimespec};
#else
    struct timespec times[2] = {before.st_atim, before.st_mtim};
#endif
    const auto original_time = times[1];
    times[1].tv_sec += 5;
    utimensat(AT_FDCWD, (f.normal + "/SPIELE/SPIEL.EXE").c_str(), times, 0);
    Check("fits: a file with another time is refused", !f.Find(id).usable && f.Find(id).reason.find("C:\\SPIELE\\SPIEL.EXE") != std::string::npos);
    Check("fits: the files alone still fit", f.Find(files).usable);
    times[1] = original_time;
    utimensat(AT_FDCWD, (f.normal + "/SPIELE/SPIEL.EXE").c_str(), times, 0);
    Check("fits: with the old time it fits again", f.Find(id).usable);
    // Another size with the same time (a patch that keeps the date).
    WriteHost(f.normal + "/SPIELE/DATA.PAK", std::string(70001, 'd'));
    Check("fits: a file with another size is refused", !f.Find(id).usable && f.Find(id).reason.find("DATA.PAK") != std::string::npos);
    unlink((f.normal + "/SPIELE/SPIEL.EXE").c_str());
    Check("fits: two files are counted", f.Find(id).reason.find("missing") != std::string::npos &&
                                             f.Find(id).reason.find("1 more") != std::string::npos);
    std::string error;
    Check("fits: a state that does not fit is still renamed and deleted", State::Rename(f.folder, id, "Incompatible", error));
    // A state saved now fits the files as they are now.
    const std::string later = f.Save("After the patch");
    Check("fits: a state made after the change fits", f.Find(later).usable);

    // Folders that are no whole state.
    Fixture g(work, "fits-damaged");
    const std::string whole = g.Save("Ganz");
    const std::string no_ini = g.Save("Without description"), cut = g.Save("Truncated"), no_machine = g.Save("Without machine"),
                      short_machine = g.Save("Short machine"), no_layer = g.Save("Without layer"), no_registry = g.Save("Without journal"),
                      format = g.Save("Anderes Format");
    unlink((State::StateFolder(g.folder, no_ini) + "/zustand.ini").c_str());
    std::string text = ReadHost(State::StateFolder(g.folder, cut) + "/zustand.ini");
    WriteHost(State::StateFolder(g.folder, cut) + "/zustand.ini", text.substr(0, text.size() / 2));
    unlink(State::MachineFile(State::StateFolder(g.folder, no_machine)).c_str());
    WriteHost(State::MachineFile(State::StateFolder(g.folder, short_machine)), "mach");
    State::RemoveTree(State::StateFolder(g.folder, no_layer) + "/C-Schicht");
    unlink((State::StateFolder(g.folder, no_registry) + "/C-Schicht.registry").c_str());
    State::Info info;
    State::ReadInfo(State::StateFolder(g.folder, format), info);
    info.format = 7;
    State::Rewrite(State::StateFolder(g.folder, format), info);
    mkdir((State::Directory(g.folder) + "/Empty Folder").c_str(), 0755);
    mkdir((State::Directory(g.folder) + "/.neu-20260101-000000-00").c_str(), 0755);
    WriteHost(State::Directory(g.folder) + "/Notiz.txt", "none");
    Check("fits: the whole one fits", g.Find(whole).usable);
    for (const auto &broken : {no_ini, cut, no_machine, short_machine, no_layer, no_registry, std::string("Empty Folder")})
    {
        const auto entry = g.Find(broken);
        Check("fits: " + (entry.name.empty() ? broken : entry.name) + " is listed as damaged", !entry.id.empty() && !entry.usable &&
                                                                                               entry.damaged && !entry.reason.empty());
    }
    Check("fits: another format is refused, not damaged", !g.Find(format).usable && !g.Find(format).damaged &&
                                                              g.Find(format).reason.find("format") != std::string::npos);
    Check("fits: a folder being written and a file are not listed", g.List().size() == 9);
    Check("fits: a damaged state is not loaded", !State::Restore(g.folder, no_ini, error) && !State::Restore(g.folder, cut, error) &&
                                                     !State::Restore(g.folder, "Empty Folder", error));
    Check("fits: a damaged state is deleted", State::Delete(g.folder, no_ini, error) && State::Delete(g.folder, "Empty Folder", error) &&
                                                  g.List().size() == 7);
}

// What the machine sees of its drive, by walking it through the store.
std::string GuestView(Machine &machine, const std::string &directory)
{
    std::string out;
    for (const auto &entry : machine.List(directory.c_str()))
    {
        const std::string name = entry.substr(0, entry.find('='));
        const std::string path = directory + (directory.back() == '\\' ? "" : "\\") + name;
        unsigned node = 0;
        if (machine.store->Lookup(path.c_str(), node) || !machine.store->Get(node))
            continue;
        const Node *found = machine.store->Get(node);
        out += path + " [" + entry.substr(entry.find('=') + 1) + "]";
        if (found->attributes & 16)
            out += " <DIR>\n" + GuestView(machine, path);
        else
            out += " " + machine.Read(path.c_str()) .substr(0, 40) + "\n";
    }
    return out;
}

void Layers(const std::string &work)
{
    Fixture f(work, "layers");
    const std::string second = HostGameMachine::DriveLayer(f.folder, 'D');
    Machine machine;
    Check("layers: the layer mounts", machine.Layer(f.normal, f.layer));
    // What a game does: a new file, a long name beside one of the drive
    // below, a file of the drive below written, one deleted, a directory
    // renamed, a directory removed and made again, a read-only file.
    Check("layers: a new file", !machine.Write("C:\\SPIELE\\SAVE1.DAT", "save one"));
    Check("layers: a long name", !machine.Write("C:\\SPIELE\\Long Name Two.txt", "two"));
    Check("layers: a file of the drive below is written", !machine.Write("C:\\SPIELE\\DATA.PAK", "changed pak"));
    Check("layers: a file of the drive below is deleted", !machine.store->Remove("C:\\SPIELE\\LEVELS\\L2.MAP", false));
    Check("layers: a directory of the drive below is renamed", !machine.store->Rename("C:\\SPIELE\\LEVELS", "C:\\SPIELE\\WORLDS"));
    Check("layers: a directory is emptied and removed", !machine.store->Remove("C:\\SPIELE\\OLD\\KEEP.TXT", false) &&
                                                             !machine.store->Remove("C:\\SPIELE\\OLD", true));
    Check("layers: and made again", !machine.store->MakeDirectory("C:\\SPIELE\\OLD") && !machine.Write("C:\\SPIELE\\OLD\\NEW.TXT", "new"));
    Check("layers: a read-only file", !machine.Write("C:\\SPIELE\\LOCKED.CFG", "locked") && !machine.store->Attributes("C:\\SPIELE\\LOCKED.CFG", 1));
    machine.Stop();
    // What a start of the machine sees of it (a store that has run lists a
    // directory in the order it met the names, a fresh one in the order of
    // the drive; the starts are what is compared).
    Check("layers: mounts for a look", machine.Layer(f.normal, f.layer));
    const std::string view = GuestView(machine, "C:\\");
    machine.Stop();
    const std::string sidecar = ReadHost(f.layer + "/SPIELE/.neo98-index");
    Check("layers: the layer holds a tombstone", sidecar.find("\n1\t") != std::string::npos || !sidecar.compare(0, 2, "1\t"));
    Check("layers: and an opaque directory", sidecar.find("\n10\t") != std::string::npos || sidecar.find("\n2\t") != std::string::npos);
    // A second drive with a layer and an old time on a file.
    HostSettings::MakeDirectories(second + "/SUB");
    WriteHost(second + "/SUB/ON-D.TXT", "on d");
    struct timespec old[2] = {{1000000000, 123456789}, {1000000000, 123456789}};
    utimensat(AT_FDCWD, (second + "/SUB/ON-D.TXT").c_str(), old, 0);
    utimensat(AT_FDCWD, (f.layer + "/SPIELE/SAVE1.DAT").c_str(), old, 0);
    WriteHost(f.folder + "/C-Schicht.registry", "journal at the state");

    const Digest layer_then = DigestOf(f.layer), second_then = DigestOf(second);
    Check("layers: the layer holds something", layer_then.size() >= 8 && second_then.size() == 2);
    std::string error;
    const std::string id = f.Save("Before migration", "manual", "machine", &error);
    Check("layers: saved (" + error + ")", !id.empty());
    const std::string state = State::StateFolder(f.folder, id);
    Check("layers: the copy equals the layer", Difference(layer_then, DigestOf(state + "/C-Schicht")).empty());
    Check("layers: the copy of the second drive too", Difference(second_then, DigestOf(state + "/D-Schicht")).empty());
    Check("layers: both letters are named", f.Find(id).layers == "CD");
    Check("layers: saving left the layer alone", Difference(layer_then, DigestOf(f.layer)).empty());

    // The game goes on: everything changes, a third drive gets a layer.
    Check("layers: mounts again", machine.Layer(f.normal, f.layer));
    Check("layers: the machine sees what it saw", GuestView(machine, "C:\\") == view);
    machine.Write("C:\\SPIELE\\SAVE1.DAT", "save one, later and longer");
    machine.Write("C:\\SPIELE\\SAVE2.DAT", "second save");
    machine.store->Remove("C:\\SPIELE\\Long Name Two.txt", false);
    machine.store->Remove("C:\\SPIELE\\Long Name One.txt", false);
    machine.store->Rename("C:\\SPIELE\\WORLDS", "C:\\SPIELE\\PLANETS");
    machine.store->Remove("C:\\SPIELE\\OLD\\NEW.TXT", false);
    machine.store->Attributes("C:\\SPIELE\\LOCKED.CFG", 0);
    machine.Write("C:\\SPIELE\\LOCKED.CFG", "open");
    machine.store->MakeDirectory("C:\\NEWDIR");
    machine.Write("C:\\NEWDIR\\Another Long Name.txt", "x");
    Check("layers: the view changed", GuestView(machine, "C:\\") != view);
    machine.Stop();
    State::RemoveTree(second);
    HostSettings::MakeDirectories(HostGameMachine::DriveLayer(f.folder, 'E'));
    WriteHost(HostGameMachine::DriveLayer(f.folder, 'E') + "/LATER.TXT", "later");
    WriteHost(f.folder + "/C-Schicht.registry", "journal later, longer");
    WriteHost(f.folder + "/C-Schicht.registry.new", "half a journal");
    Check("layers: the layer differs now", !Difference(layer_then, DigestOf(f.layer)).empty());
    // What was saved does not change with the layer.
    Check("layers: the state did not change with it", Difference(layer_then, DigestOf(state + "/C-Schicht")).empty());

    const std::string kept = f.Save("", "auto", "");
    const Digest layer_later = DigestOf(f.layer);
    Check("layers: load", State::Restore(f.folder, id, error));
    const std::string what = Difference(layer_then, DigestOf(f.layer));
    Check("layers: the layer is the copy, byte for byte, with times and modes (" + what + ")", what.empty());
    Check("layers: the second drive is back", Difference(second_then, DigestOf(second)).empty());
    Check("layers: the drive that got its layer later has none", !State::Present(HostGameMachine::DriveLayer(f.folder, 'E')));
    Check("layers: the journal is the one of the state", ReadHost(f.folder + "/C-Schicht.registry") == "journal at the state" &&
                                                           !State::Present(f.folder + "/C-Schicht.registry.new"));
    Check("layers: nothing of the load is left", !State::Present(State::LoadFolder(f.folder)));
    Check("layers: the machine stands on the state", State::Current(f.folder) == id);
    Check("layers: the state is still whole", Difference(layer_then, DigestOf(state + "/C-Schicht")).empty() && f.Find(id).usable);
    Check("layers: mounts a third time", machine.Layer(f.normal, f.layer));
    Check("layers: the machine sees what it saw when it was saved", GuestView(machine, "C:\\") == view);
    Check("layers: the deleted file stays deleted", !machine.Exists("C:\\SPIELE\\WORLDS\\L2.MAP") && !machine.Exists("C:\\SPIELE\\LEVELS"));
    Check("layers: the opaque directory hides the drive below", !machine.Exists("C:\\SPIELE\\OLD\\KEEP.TXT") && machine.Exists("C:\\SPIELE\\OLD\\NEW.TXT"));
    Check("layers: the alias of the layer's long name", machine.Read("C:\\SPIELE\\LONGNA~2.TXT") == "two");
    // Writing after the load does not reach the state (a clone is a copy).
    machine.Write("C:\\SPIELE\\SAVE1.DAT", "after the load");
    machine.Stop();
    Check("layers: a write after the load does not change the state", Difference(layer_then, DigestOf(state + "/C-Schicht")).empty());
    // And forward again: the automatic state holds the later files.
    Check("layers: load the state from before the load", State::Restore(f.folder, kept, error));
    Check("layers: the later layer is back", Difference(layer_later, DigestOf(f.layer)).empty());
    Check("layers: with its third drive", ReadHost(HostGameMachine::DriveLayer(f.folder, 'E') + "/LATER.TXT") == "later" && !State::Present(second));
    Check("layers: and its journal", ReadHost(f.folder + "/C-Schicht.registry") == "journal later, longer");
    // The drive below was never written.
    Check("layers: the normal drive is as it was", ReadHost(f.normal + "/SPIELE/DATA.PAK") == std::string(70000, 'd') &&
                                                     ReadHost(f.normal + "/SPIELE/LEVELS/L2.MAP") == "level two");
    // A game machine without a journal and without a layer folder.
    Fixture bare(work, "layers-bare");
    unlink((bare.folder + "/C-Schicht.registry").c_str());
    State::RemoveTree(bare.layer);
    const std::string empty = bare.Save("Leer", "manual", "");
    Check("layers: a game machine that wrote nothing is saved", !empty.empty() && bare.Find(empty).layers.empty() && !bare.Find(empty).registry);
    HostSettings::MakeDirectories(bare.layer);
    WriteHost(bare.layer + "/X.TXT", "x");
    WriteHost(bare.folder + "/C-Schicht.registry", "journal");
    Check("layers: and loaded", State::Restore(bare.folder, empty, error) && !State::Present(bare.layer) &&
                                    !State::Present(bare.folder + "/C-Schicht.registry"));
}

// Runs `work` in a child that ends at `step`; true when it ended there.
template <class Work> bool CutAt(const std::string &step, Work work)
{
    fflush(stdout);
    const pid_t child = fork();
    if (!child)
    {
        setenv("NEO98_SAVESTATE_STOP", step.c_str(), 1);
        State::StopCounts().clear();
        work();
        _exit(0);
    }
    int status = 0;
    waitpid(child, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 86;
}

void Cut(const std::string &work)
{
    Fixture f(work, "cut");
    HostSettings::MakeDirectories(f.layer + "/SPIELE");
    for (int i = 0; i < 6; ++i)
        WriteHost(f.layer + "/SPIELE/SAVE" + std::to_string(i) + ".DAT", "state one " + std::to_string(i));
    HostSettings::MakeDirectories(HostGameMachine::DriveLayer(f.folder, 'D'));
    WriteHost(HostGameMachine::DriveLayer(f.folder, 'D') + "/D.TXT", "d one");
    const std::string one = f.Save("Eins");
    const Digest layer_one = DigestOf(f.layer);
    for (int i = 0; i < 6; ++i)
        WriteHost(f.layer + "/SPIELE/SAVE" + std::to_string(i) + ".DAT", "state two, longer " + std::to_string(i));
    WriteHost(f.layer + "/SPIELE/EXTRA.DAT", "only in two");
    State::RemoveTree(HostGameMachine::DriveLayer(f.folder, 'D'));
    HostSettings::MakeDirectories(HostGameMachine::DriveLayer(f.folder, 'E'));
    WriteHost(HostGameMachine::DriveLayer(f.folder, 'E') + "/E.TXT", "e two");
    WriteHost(f.folder + "/C-Schicht.registry", "journal two");
    const Digest layer_two = DigestOf(f.layer), folder_two = DigestOf(f.folder);

    // Saving, cut at every step before the state is in its place.
    for (const char *step : {"save-machine", "save-copy:1", "save-copy:4", "save-copy:7", "save-ini", "save-rename"})
    {
        const bool cut = CutAt(step, [&] { f.Save("Halb"); });
        Check(std::string("cut: saving ends at ") + step, cut);
        Check(std::string("cut: ") + step + " leaves no state in the list", f.List().size() == 1 && State::Count(f.folder) == 1);
        Check(std::string("cut: ") + step + " leaves the machine on its state", State::Current(f.folder) == one);
        State::Recover(f.folder);
        const std::string what = Difference(folder_two, DigestOf(f.folder));
        Check(std::string("cut: after the next start nothing of it is left (") + what + ")", what.empty());
    }
    // Cut after the rename: the state is whole, the machine does not stand on it yet.
    Check("cut: saving ends at save-current", CutAt("save-current", [&] { f.Save("Ganz"); }));
    State::Recover(f.folder);
    auto list = f.List();
    Check("cut: the state is whole", list.size() == 2 && list[1].usable && list[1].name == "Ganz" && list[1].parent == one);
    Check("cut: the machine stands on the one before", State::Current(f.folder) == one);
    std::string error;
    Check("cut: it is deleted again", State::Delete(f.folder, list[1].id, error));
    const Digest states_before = DigestOf(State::StateFolder(f.folder, one));

    // Loading, cut before the mark: nothing happened.
    for (const char *step : {"load-copy:1", "load-copy:5", "load-copy:7", "load-ready"})
    {
        Check(std::string("cut: loading ends at ") + step, CutAt(step, [&] { std::string e; State::Restore(f.folder, one, e); }));
        Check(std::string("cut: ") + step + " left the layer alone", Difference(layer_two, DigestOf(f.layer)).empty());
        State::Recover(f.folder);
        const std::string what = Difference(folder_two, DigestOf(f.folder));
        Check(std::string("cut: the next start drops the copies (") + what + ")", what.empty());
    }
    // Cut after the mark: the next start finishes the load.
    const std::vector<std::string> swaps = {"load-marked", "load-swap:1", "load-swap:2", "load-swap:3", "load-swap:4", "load-swap:5",
                                            "load-swap:6", "load-swap:20", "load-swap:48", "load-current"};
    for (const auto &step : swaps)
    {
        Check("cut: loading ends at " + step, CutAt(step, [&] { std::string e; State::Restore(f.folder, one, e); }));
        State::Recover(f.folder);
        const std::string what = Difference(layer_one, DigestOf(f.layer));
        Check("cut: after " + step + " the next start finishes the load (" + what + ")", what.empty());
        Check("cut: " + step + ": the second drive is the state's", ReadHost(HostGameMachine::DriveLayer(f.folder, 'D') + "/D.TXT") == "d one" &&
                                                                      !State::Present(HostGameMachine::DriveLayer(f.folder, 'E')));
        Check("cut: " + step + ": the journal is the state's", ReadHost(f.folder + "/C-Schicht.registry") == "journal one");
        Check("cut: " + step + ": nothing is left of the load", !State::Present(State::LoadFolder(f.folder)) && State::Current(f.folder) == one);
        Check("cut: " + step + ": the states are as they were", Difference(states_before, DigestOf(State::StateFolder(f.folder, one))).empty() &&
                                                                    f.List().size() == 1);
        // Back to the second layer for the next round.
        State::RemoveTree(f.layer);
        State::RemoveTree(HostGameMachine::DriveLayer(f.folder, 'D'));
        HostSettings::MakeDirectories(f.layer + "/SPIELE");
        for (int i = 0; i < 6; ++i)
            WriteHost(f.layer + "/SPIELE/SAVE" + std::to_string(i) + ".DAT", "state two, longer " + std::to_string(i));
        WriteHost(f.layer + "/SPIELE/EXTRA.DAT", "only in two");
        HostSettings::MakeDirectories(HostGameMachine::DriveLayer(f.folder, 'E'));
        WriteHost(HostGameMachine::DriveLayer(f.folder, 'E') + "/E.TXT", "e two");
        WriteHost(f.folder + "/C-Schicht.registry", "journal two");
    }
    // A load run twice in a row and one over a load that was cut.
    Check("cut: a load cut before the mark, then a whole one", CutAt("load-copy:2", [&] { std::string e; State::Restore(f.folder, one, e); }) &&
                                                                   State::Restore(f.folder, one, error) &&
                                                                   Difference(layer_one, DigestOf(f.layer)).empty());
    Check("cut: the same load again changes nothing", State::Restore(f.folder, one, error) && Difference(layer_one, DigestOf(f.layer)).empty());

    // Deleting in the middle, cut.
    const std::string two = f.Save("Two"), three = f.Save("Three");
    Check("cut: deleting ends after the children moved", CutAt("delete-children", [&] { std::string e; State::Delete(f.folder, two, e); }));
    State::Recover(f.folder);
    Check("cut: the state is still there, its child already hangs higher", f.Find(two).usable && f.Find(three).parent == one && f.Find(three).usable);
    Check("cut: deleting ends after the folder left the list", CutAt("delete-renamed", [&] { std::string e; State::Delete(f.folder, two, e); }));
    Check("cut: the state is not listed", f.Find(two).id.empty() && f.List().size() == 2);
    State::Recover(f.folder);
    bool leftovers = false;
    if (DIR *handle = opendir(State::Directory(f.folder).c_str()))
    {
        while (const dirent *item = readdir(handle))
            leftovers = leftovers || !strncmp(item->d_name, ".weg-", 5) || !strncmp(item->d_name, ".neu-", 5);
        closedir(handle);
    }
    Check("cut: the next start removes what was left of it", !leftovers);
}
} // namespace

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: test-savestate-store WORK\n");
        return 2;
    }
    const std::string work = argv[1];
    printf("copies: %s\n", getenv("NEO98_COPY_PLAIN") && *getenv("NEO98_COPY_PLAIN") == '1' ? "plain" : "clone");
    Basics(work);
    Tree(work);
    Automatic(work);
    Names(work);
    Fits(work);
    Layers(work);
    Cut(work);
    printf("savestate store: %u passed, %u failed\n", passed, failed);
    return failed ? 1 : 0;
}
