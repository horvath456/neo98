// SPDX-License-Identifier: GPL-2.0-or-later
// The rules of the write layer that a random comparison with a plain drive
// cannot show : what happens when
// the drive below changes while or between runs, and what the second way
// into the layer, the one the DOS machine takes, shares with the store.
// aliases an alias the layer issued stays the layer's when the
// drive below later gets an entry with the same alias; an entry
// of the drive below keeps the alias the machine first saw.
// open 200 files of the drive below open at once, removed below, all
// still readable; no descriptor is left when they are closed.
// changes a file added, removed or replaced below while the machine
// runs: found, "file not found", the new contents; never a file
// that opens and reads as empty.
// two ways the store and the DOS machine's way (Layered::Foreign)
// see each other's creations, deletions, renames and directory
// removals at once and after a restart.
// lazy the folder of a layer appears with the first write.
// test-layer-rules WORK
// WORK is an empty folder; tools/test_layer_random.py runs this.
#include "../src/files.h"
#include "../src/volume.h"
#include "host_volume.h"
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <vector>

using namespace Files;

namespace
{
unsigned failed = 0, passed = 0;
void Check(const char *name, bool condition)
{
    printf("%s %s\n", condition ? "ok  " : "FAIL", name);
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
bool HostExists(const std::string &path)
{
    struct stat info;
    return !stat(path.c_str(), &info);
}
std::string ReadHost(const std::string &path)
{
    std::string text;
    if (FILE *file = fopen(path.c_str(), "rb"))
    {
        char block[4096];
        size_t got;
        while ((got = fread(block, 1, sizeof block, file)) > 0)
            text.append(block, got);
        fclose(file);
    }
    return text;
}
unsigned Descriptors()
{
    unsigned count = 0;
    if (DIR *directory = opendir("/dev/fd"))
    {
        while (readdir(directory))
            ++count;
        closedir(directory);
    }
    return count;
}
TimeStamp Clock() { return TimeStamp{(21u << 9) | (3u << 5) | 4u, (10u << 11) | (30u << 5), 0}; }

// A drive C: for the store: a plain folder, or a folder below a layer.
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
    // "name=alias" of every entry, in the order of the listing.
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
    std::string Alias(const char *path)
    {
        unsigned node = 0;
        return store->Lookup(path, node) || !store->Get(node) ? std::string("?") : std::string(store->Get(node)->alias);
    }
    bool Exists(const char *path)
    {
        unsigned node = 0;
        return !store->Lookup(path, node);
    }
};
bool Has(const std::vector<std::string> &list, const std::string &name)
{
    for (const auto &entry : list)
        if (entry.compare(0, name.size() + 1, name + "=") == 0)
            return true;
    return false;
}
// The normal machine uses its drive: every directory gets its sidecar.
void UseAsPlain(const std::string &folder, const std::vector<const char *> &directories)
{
    Machine machine;
    if (!machine.Plain(folder))
        return;
    for (const char *directory : directories)
        machine.List(directory);
    machine.Stop();
}

void Aliases(const std::string &work)
{
    const std::string base = work + "/alias-base", layer = work + "/alias-layer";
    mkdir(base.c_str(), 0755);
    mkdir((base + "/GAME").c_str(), 0755);
    WriteHost(base + "/GAME/Long Name One.txt", "one");
    UseAsPlain(base, {"C:\\", "C:\\GAME"});
    Machine machine;
    Check("alias: layer mounts", machine.Layer(base, layer));
    Check("alias: the file below has ~1", machine.Alias("C:\\GAME\\Long Name One.txt") == "LONGNA~1.TXT");
    Check("alias: a file of the layer", !machine.Write("C:\\GAME\\Long Name Two.txt", "two"));
    Check("alias: it gets ~2", machine.Alias("C:\\GAME\\Long Name Two.txt") == "LONGNA~2.TXT");
    machine.Stop();
    // The normal machine installs something: for it ~2 is free.
    Check("alias: the normal machine mounts", machine.Plain(base));
    Check("alias: the normal machine writes a file", !machine.Write("C:\\GAME\\Long Name Three.txt", "three"));
    const std::string below = machine.Alias("C:\\GAME\\Long Name Three.txt");
    Check("alias: below it has the alias the layer uses (the collision)", below == "LONGNA~2.TXT");
    machine.Stop();
    std::string third;
    for (int round = 0; round < 2; ++round)
    {
        Check("alias: layer mounts again", machine.Layer(base, layer));
        const auto list = machine.List("C:\\GAME");
        unsigned twice = 0;
        for (size_t i = 0; i < list.size(); ++i)
            for (size_t k = i + 1; k < list.size(); ++k)
                if (list[i].substr(list[i].find('=')) == list[k].substr(list[k].find('=')) && list[i].back() != '=')
                    ++twice;
        Check("alias: no alias twice in the listing", list.size() == 3 && !twice);
        Check("alias: the layer's file kept ~2", machine.Alias("C:\\GAME\\Long Name Two.txt") == "LONGNA~2.TXT");
        Check("alias: ~2 opens the layer's file", machine.Read("C:\\GAME\\LONGNA~2.TXT") == "two");
        const std::string now = machine.Alias("C:\\GAME\\Long Name Three.txt");
        Check("alias: the later file below got another one", !now.empty() && now != "LONGNA~2.TXT" && now != "LONGNA~1.TXT");
        Check("alias: and opens by it", machine.Read(("C:\\GAME\\" + now).c_str()) == "three");
        if (round)
            Check("alias: the same after a restart", now == third);
        third = now;
        machine.Stop();
    }
    Check("alias: the layer wrote the pin down", ReadHost(layer + "/GAME/.neo98-index").find("\n4\t") != std::string::npos);
    // A file put into the folder below by hand: no sidecar names its alias.
    WriteHost(base + "/GAME/Hand Added File.dat", "hand");
    Check("alias: layer mounts a third time", machine.Layer(base, layer));
    const std::string hand = machine.Alias("C:\\GAME\\Hand Added File.dat");
    Check("alias: the file added by hand has an alias", hand == "HANDAD~1.DAT");
    machine.Stop();
    // One that sorts before it would take ~1 from it.
    WriteHost(base + "/GAME/Hand Added Earlier.dat", "earlier");
    Check("alias: layer mounts a fourth time", machine.Layer(base, layer));
    Check("alias: the name the machine saw keeps its alias", machine.Alias("C:\\GAME\\Hand Added File.dat") == hand);
    Check("alias: the newcomer gets the next", machine.Alias("C:\\GAME\\Hand Added Earlier.dat") == "HANDAD~2.DAT");
    Check("alias: the alias opens the file it named before", machine.Read("C:\\GAME\\HANDAD~1.DAT") == "hand");
    // Copy on write keeps the alias the layer gave.
    unsigned handle = 0, status = 0, done = 0;
    Check("alias: open the later file below for writing", !machine.session->Open("C:\\GAME\\Long Name Three.txt", 3, 3, 3, 0, handle, status));
    machine.session->Write(handle, "THREE", 5, done);
    machine.session->Close(handle);
    Check("alias: copied up it keeps its alias", machine.Alias("C:\\GAME\\Long Name Three.txt") == third);
    machine.Stop();
    Check("alias: layer mounts a fifth time", machine.Layer(base, layer));
    Check("alias: also after a restart", machine.Alias("C:\\GAME\\Long Name Three.txt") == third &&
                                           machine.Read(("C:\\GAME\\" + third).c_str()) == "THREE");
    Check("alias: the file below is as it was", ReadHost(base + "/GAME/Long Name Three.txt") == "three");
    machine.Stop();
}

void OpenFiles(const std::string &work)
{
    const std::string base = work + "/open-base", layer = work + "/open-layer";
    mkdir(base.c_str(), 0755);
    mkdir((base + "/PACK").c_str(), 0755);
    constexpr unsigned Count = 200;
    // Pack files of 5 MiB each, sparse: only their ends are written.
    for (unsigned i = 0; i < Count; ++i)
    {
        char name[64];
        snprintf(name, sizeof name, "/PACK/PACK%03u.DAT", i);
        if (FILE *file = fopen((base + name).c_str(), "wb"))
        {
            fprintf(file, "head %03u", i);
            fseek(file, 5 * 1024 * 1024, SEEK_SET);
            fprintf(file, "tail %03u", i);
            fclose(file);
        }
    }
    UseAsPlain(base, {"C:\\", "C:\\PACK"});
    const unsigned before = Descriptors();
    Machine machine;
    Check("open: layer mounts", machine.Layer(base, layer));
    unsigned handles[Count] = {}, status = 0, done = 0, opened = 0, good = 0;
    for (unsigned i = 0; i < Count; ++i)
    {
        char path[64];
        snprintf(path, sizeof path, "C:\\PACK\\PACK%03u.DAT", i);
        if (!machine.session->Open(path, 1, 3, 3, 0, handles[i], status))
            ++opened;
    }
    Check("open: 200 files of the drive below are open", opened == Count);
    Check("open: each holds a descriptor of its own", machine.layered->HeldOpen() == Count);
    // The normal machine removes the game.
    for (unsigned i = 0; i < Count; ++i)
    {
        char name[64];
        snprintf(name, sizeof name, "/PACK/PACK%03u.DAT", i);
        unlink((base + name).c_str());
    }
    for (unsigned i = 0; i < Count; ++i)
    {
        char text[16] = {}, want[16], position_text[16] = {};
        unsigned position = 0;
        snprintf(want, sizeof want, "head %03u", i);
        const bool head = !machine.session->Read(handles[i], text, 8, done) && done == 8 && !strcmp(text, want);
        snprintf(want, sizeof want, "tail %03u", i);
        machine.session->Seek(handles[i], 5 * 1024 * 1024, 0, position);
        const bool tail = !machine.session->Read(handles[i], position_text, 8, done) && done == 8 && !strcmp(position_text, want);
        good += head && tail ? 1u : 0u;
    }
    Check("open: all 200 read their first and last bytes after they were removed below", good == Count);
    unsigned error = 0;
    machine.Read("C:\\PACK\\PACK007.DAT", &error);
    Check("open: a new open of one of them finds the file while it is open", error == 0);
    for (unsigned i = 0; i < Count; ++i)
        machine.session->Close(handles[i]);
    Check("open: no descriptor is held after the last close", machine.layered->HeldOpen() == 0);
    machine.Read("C:\\PACK\\PACK007.DAT", &error);
    Check("open: closed, the removed file is not found", error == 2);
    machine.Stop();
    Check("open: the process has the descriptors it started with", Descriptors() == before);
}

void Changes(const std::string &work)
{
    const std::string base = work + "/change-base", layer = work + "/change-layer";
    mkdir(base.c_str(), 0755);
    mkdir((base + "/DATA").c_str(), 0755);
    mkdir((base + "/DATA/SUB").c_str(), 0755);
    WriteHost(base + "/DATA/OLD.TXT", "old");
    WriteHost(base + "/DATA/CHANGE.TXT", "before");
    WriteHost(base + "/DATA/KEEP.TXT", "keep");
    WriteHost(base + "/DATA/MINE.TXT", "below");
    WriteHost(base + "/DATA/SUB/INNER.TXT", "inner");
    UseAsPlain(base, {"C:\\", "C:\\DATA", "C:\\DATA\\SUB"});
    Machine machine;
    Check("change: layer mounts", machine.Layer(base, layer));
    auto list = machine.List("C:\\DATA");
    Check("change: the directory is listed", list.size() == 5 && Has(list, "OLD.TXT"));
    Check("change: files are read", machine.Read("C:\\DATA\\OLD.TXT") == "old" && machine.Read("C:\\DATA\\CHANGE.TXT") == "before");
    Check("change: the machine writes one of them", !machine.Write("C:\\DATA\\MINE.TXT", "mine"));
    // The normal machine works on its drive meanwhile.
    unlink((base + "/DATA/OLD.TXT").c_str());
    WriteHost(base + "/DATA/NEW.TXT", "new file");
    WriteHost(base + "/DATA/CHANGE.TXT", "after the change");
    WriteHost(base + "/DATA/MINE.TXT", "below, changed");
    unsigned error = 0;
    const std::string gone = machine.Read("C:\\DATA\\OLD.TXT", &error);
    Check("change: a file removed below is an ordinary file not found", error == 2 && gone.empty());
    Check("change: a file added below is found", machine.Read("C:\\DATA\\NEW.TXT") == "new file");
    Check("change: a file replaced below reads as it is now", machine.Read("C:\\DATA\\CHANGE.TXT") == "after the change");
    Check("change: the machine's own file hides the change below", machine.Read("C:\\DATA\\MINE.TXT") == "mine");
    list = machine.List("C:\\DATA");
    Check("change: the next listing has the new file and not the removed one",
          list.size() == 5 && Has(list, "NEW.TXT") && !Has(list, "OLD.TXT"));
    // Listed, then removed below before it is opened.
    list = machine.List("C:\\DATA");
    unlink((base + "/DATA/KEEP.TXT").c_str());
    machine.Read("C:\\DATA\\KEEP.TXT", &error);
    Check("change: a listed file that went away below does not open", error == 2);
    Check("change: its attributes are not found either", !machine.Exists("C:\\DATA\\KEEP.TXT"));
    // A whole directory goes below.
    Check("change: a file in a subdirectory", machine.Read("C:\\DATA\\SUB\\INNER.TXT") == "inner");
    unlink((base + "/DATA/SUB/INNER.TXT").c_str());
    unlink((base + "/DATA/SUB/.neo98-index").c_str());
    rmdir((base + "/DATA/SUB").c_str());
    machine.Read("C:\\DATA\\SUB\\INNER.TXT", &error);
    Check("change: a file of a directory removed below is not found", error == 2 || error == 3);
    Check("change: the directory is gone from the next listing", !Has(machine.List("C:\\DATA"), "SUB"));
    // And one comes.
    mkdir((base + "/DATA/Later Folder").c_str(), 0755);
    WriteHost(base + "/DATA/Later Folder/in.txt", "in");
    Check("change: a directory made below is found with its file", machine.Read("C:\\DATA\\Later Folder\\in.txt") == "in");
    machine.Stop();
}

void TwoWays(const std::string &work)
{
    const std::string base = work + "/two-base", layer = work + "/two-layer";
    mkdir(base.c_str(), 0755);
    mkdir((base + "/GAME").c_str(), 0755);
    mkdir((base + "/GAME/SAVES").c_str(), 0755);
    mkdir((base + "/GAME/Long Folder Name").c_str(), 0755);
    for (const char *name : {"A.DAT", "B.DAT", "C.DAT", "D.DAT", "E.DAT", "Long File Name.dat"})
        WriteHost(base + "/GAME/" + name, std::string("base ") + name);
    WriteHost(base + "/GAME/SAVES/S1.SAV", "save one");
    WriteHost(base + "/GAME/Long Folder Name/X.TXT", "x");
    UseAsPlain(base, {"C:\\", "C:\\GAME", "C:\\GAME\\SAVES", "C:\\GAME\\Long Folder Name"});
    const std::string before = ReadHost(base + "/GAME/.neo98-index");
    Machine machine;
    Check("two: layer mounts", machine.Layer(base, layer));
    Volume::Backend *dos = machine.layered->Foreign();
    Volume::Entry entry;
    unsigned done = 0, error = 0;
    char block[64] = {};
    Check("two: the store lists the directory", machine.List("C:\\GAME").size() == 8);
    // The DOS side deletes a file of the drive below.
    Check("two: DOS finds a file below by its DOS name", dos->Find("C:\\GAME\\LONGFI~1.DAT", entry) && !strcmp(entry.name, "Long File Name.dat"));
    Check("two: DOS deletes a file below", !dos->Remove("C:\\GAME\\A.DAT", false, false));
    machine.Read("C:\\GAME\\A.DAT", &error);
    Check("two: it is gone for Windows at once", error == 2 && !Has(machine.List("C:\\GAME"), "A.DAT"));
    // Windows deletes one.
    Check("two: Windows deletes a file below", !machine.store->Remove("C:\\GAME\\B.DAT", false));
    Check("two: it is gone for DOS at once", !dos->Find("C:\\GAME\\B.DAT", entry));
    // DOS renames, Windows renames.
    Check("two: DOS renames a file below", !dos->Rename("C:\\GAME\\C.DAT", "C:\\GAME\\C2.DAT"));
    Check("two: Windows sees the new name and not the old",
          machine.Read("C:\\GAME\\C2.DAT") == "base C.DAT" && !machine.Exists("C:\\GAME\\C.DAT"));
    Check("two: Windows renames a file below", !machine.store->Rename("C:\\GAME\\D.DAT", "C:\\GAME\\D2.DAT"));
    Check("two: DOS sees the new name and not the old", dos->Find("C:\\GAME\\D2.DAT", entry) && !dos->Find("C:\\GAME\\D.DAT", entry));
    // DOS writes a save; Windows reads it; Windows changes it; DOS reads that.
    Check("two: DOS creates a file", !dos->Create("C:\\GAME\\SAVES\\S2.SAV", 32, false, entry) &&
                                         !dos->Write("C:\\GAME\\SAVES\\S2.SAV", 0, "dos save", 8, done));
    Check("two: Windows reads it", machine.Read("C:\\GAME\\SAVES\\S2.SAV") == "dos save");
    Check("two: Windows writes over it", !machine.Write("C:\\GAME\\SAVES\\S2.SAV", "windows wrote this"));
    Check("two: DOS reads what Windows wrote", !dos->Read("C:\\GAME\\SAVES\\S2.SAV", 0, block, sizeof block, done) && done == 18 &&
                                                   !memcmp(block, "windows wrote this", 18));
    Check("two: DOS writes into a file below", !dos->Write("C:\\GAME\\E.DAT", 0, "DOS!", 4, done));
    Check("two: Windows reads the changed file", machine.Read("C:\\GAME\\E.DAT") == "DOS! E.DAT");
    // A directory of the drive below: emptied and removed by DOS, made again by Windows.
    Check("two: DOS empties and removes a directory below", !dos->Remove("C:\\GAME\\LONGFO~1\\X.TXT", false, false) &&
                                                                !dos->Remove("C:\\GAME\\LONGFO~1", true, false));
    Check("two: the directory is gone for Windows", !machine.Exists("C:\\GAME\\Long Folder Name"));
    Check("two: Windows makes a directory of that name", !machine.store->MakeDirectory("C:\\GAME\\Long Folder Name"));
    Check("two: it is empty on both sides", machine.List("C:\\GAME\\Long Folder Name").empty() &&
                                                !dos->Child("C:\\GAME\\LONGFO~1", 0, entry) && !dos->Find("C:\\GAME\\LONGFO~1\\X.TXT", entry));
    // And the other way round.
    Check("two: Windows removes a directory below, with its files",
          !machine.store->Remove("C:\\GAME\\SAVES\\S1.SAV", false) && !machine.store->Remove("C:\\GAME\\SAVES\\S2.SAV", false) &&
              !machine.store->Remove("C:\\GAME\\SAVES", true));
    Check("two: DOS makes it again and writes into it", !dos->Create("C:\\GAME\\SAVES", 16, true, entry) &&
                                                            !dos->Create("C:\\GAME\\SAVES\\S3.SAV", 32, false, entry) &&
                                                            !dos->Write("C:\\GAME\\SAVES\\S3.SAV", 0, "three", 5, done));
    const auto saves = machine.List("C:\\GAME\\SAVES");
    Check("two: Windows sees the new directory with the one file", saves.size() == 1 && Has(saves, "S3.SAV"));
    // Nobody removes or renames a file the other side has open.
    unsigned handle = 0, status = 0;
    Check("two: Windows opens a file", !machine.session->Open("C:\\GAME\\E.DAT", 1, 3, 3, 0, handle, status));
    Check("two: DOS cannot delete it meanwhile", dos->Remove("C:\\GAME\\E.DAT", false, false) == 32);
    Check("two: nor rename it", dos->Rename("C:\\GAME\\E.DAT", "C:\\GAME\\E9.DAT") == 32);
    machine.session->Close(handle);
    dos->Retain("C:\\GAME\\E.DAT");
    Check("two: Windows cannot delete a file DOS has open", machine.store->Remove("C:\\GAME\\E.DAT", false) == 32);
    dos->Release("C:\\GAME\\E.DAT");
    Check("two: closed on both sides, it can go", !machine.store->Remove("C:\\GAME\\E.DAT", false));
    // The sidecars of the layer are no files to either side.
    bool sidecar = false;
    for (unsigned index = 0; dos->Child("C:\\GAME", index, entry); ++index)
        sidecar = sidecar || !strncmp(entry.name, ".neo98", 6);
    Check("two: DOS sees no sidecar of the layer", !sidecar && HostExists(layer + "/GAME/.neo98-index"));
    machine.Stop();
    // Everything holds after a restart, on both ways.
    Check("two: layer mounts again", machine.Layer(base, layer));
    dos = machine.layered->Foreign();
    const auto game = machine.List("C:\\GAME");
    Check("two: after a restart Windows sees what both sides left",
          game.size() == 5 && Has(game, "C2.DAT") && Has(game, "D2.DAT") && Has(game, "Long File Name.dat") && Has(game, "SAVES") &&
              Has(game, "Long Folder Name") && !Has(game, "A.DAT") && !Has(game, "B.DAT") && !Has(game, "E.DAT"));
    unsigned count = 0;
    for (unsigned index = 0; dos->Child("C:\\GAME", index, entry); ++index)
        ++count;
    Check("two: and DOS the same five", count == 5 && !dos->Find("C:\\GAME\\A.DAT", entry) && !dos->Find("C:\\GAME\\C.DAT", entry) &&
                                            dos->Find("C:\\GAME\\C2.DAT", entry));
    Check("two: the directories made anew stay empty of what lies below",
          machine.List("C:\\GAME\\Long Folder Name").empty() && machine.List("C:\\GAME\\SAVES").size() == 1 &&
              !dos->Find("C:\\GAME\\SAVES\\S1.SAV", entry));
    machine.Stop();
    Check("two: the drive below has every file it had",
          ReadHost(base + "/GAME/A.DAT") == "base A.DAT" && ReadHost(base + "/GAME/C.DAT") == "base C.DAT" &&
              ReadHost(base + "/GAME/E.DAT") == "base E.DAT" && ReadHost(base + "/GAME/SAVES/S1.SAV") == "save one" &&
              ReadHost(base + "/GAME/Long Folder Name/X.TXT") == "x" && ReadHost(base + "/GAME/.neo98-index") == before);
}

void Lazy(const std::string &work)
{
    const std::string base = work + "/lazy-base", layer = work + "/lazy-machine/D-Schicht";
    mkdir(base.c_str(), 0755);
    mkdir((base + "/GAME").c_str(), 0755);
    WriteHost(base + "/GAME/GAME.EXE", "program");
    WriteHost(base + "/GAME/Config File.ini", "volume=3");
    UseAsPlain(base, {"C:\\", "C:\\GAME"});
    Machine machine;
    Check("lazy: a layer whose folder does not exist mounts", machine.Layer(base, layer));
    Check("lazy: reading and listing", machine.Read("C:\\GAME\\GAME.EXE") == "program" && machine.List("C:\\GAME").size() == 2);
    Check("lazy: makes no folder", !HostExists(layer));
    const unsigned serial = machine.store->DriveSerial('C');
    Check("lazy: the game writes into its own folder", !machine.Write("C:\\GAME\\Config File.ini", "volume=9") &&
                                                           !machine.Write("C:\\GAME\\SAVE.DAT", "saved"));
    Check("lazy: now the folder is there, with the file", ReadHost(layer + "/GAME/Config File.ini") == "volume=9");
    Check("lazy: the drive below is unchanged", ReadHost(base + "/GAME/Config File.ini") == "volume=3" && !HostExists(base + "/GAME/SAVE.DAT"));
    Check("lazy: the drive keeps its serial number", machine.store->DriveSerial('C') == serial);
    machine.Stop();
    Check("lazy: layer mounts again", machine.Layer(base, layer));
    Check("lazy: and has what was written", machine.Read("C:\\GAME\\Config File.ini") == "volume=9" && machine.Read("C:\\GAME\\SAVE.DAT") == "saved" &&
                                                machine.store->DriveSerial('C') == serial);
    machine.Stop();
}

// The entry of a directory keeps the times of its creation, on a plain drive
// and on a layer alike.
void DirectoryTimes(const std::string &work)
{
    const std::string plain = work + "/time-plain", base = work + "/time-base", layer = work + "/time-layer";
    mkdir(plain.c_str(), 0755);
    mkdir(base.c_str(), 0755);
    for (int layered = 0; layered < 2; ++layered)
    {
        Machine machine;
        Check(layered ? "times: layer mounts" : "times: plain drive mounts", layered ? machine.Layer(base, layer) : machine.Plain(plain));
        Check("times: a directory is made", !machine.store->MakeDirectory("C:\\MADE"));
        unsigned node = 0;
        machine.store->Lookup("C:\\MADE", node);
        const TimeStamp made = machine.store->Get(node)->times[2];
        Check("times: its write time is its creation time", made.date == Clock().date && made.time == Clock().time);
        Check("times: a file is written into it", !machine.Write("C:\\MADE\\F.TXT", "f"));
        machine.Stop();
        Check("times: mounted again", layered ? machine.Layer(base, layer) : machine.Plain(plain));
        machine.store->Lookup("C:\\MADE", node);
        const Node *entry = machine.store->Get(node);
        Check("times: the directory still has the times of its creation",
              entry && entry->times[2].date == made.date && entry->times[2].time == made.time && entry->times[0].date == made.date &&
                  entry->times[1].date == made.date);
        machine.Stop();
    }
}
} // namespace

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: test-layer-rules WORK\n");
        return 2;
    }
    Files::Configure(24576, 192u * 1024 * 1024);
    SetClock(Clock);
    const std::string work = argv[1];
    Aliases(work);
    OpenFiles(work);
    Changes(work);
    TwoWays(work);
    Lazy(work);
    DirectoryTimes(work);
    printf("LAYER RULES passed=%u failed=%u %s\n", passed, failed, failed ? "FAIL" : "PASS");
    return failed ? 1 : 0;
}
