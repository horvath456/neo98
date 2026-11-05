// SPDX-License-Identifier: GPL-2.0-or-later
// The writable host volume, checked twice over the same directory: one run
// writes, a second run in a fresh process state finds everything again.
//
#include "../src/files.h"
#include "../src/volume.h"
#include "host_volume.h"
#include "system_tree_host.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

using namespace Files;
static unsigned checks = 0;
#define CHECK(condition)                                                                           \
    do                                                                                             \
    {                                                                                              \
        ++checks;                                                                                  \
        if (!(condition))                                                                          \
        {                                                                                          \
            fprintf(stderr, "host volume check failed at line %d: %s\n", __LINE__, #condition);     \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

// 1 March 2001, 08:30:00, so a creation time is the guest clock's and not the
// host clock's.
static TimeStamp GuestClock() { return TimeStamp{(21u << 9) | (3u << 5) | 1u, (8u << 11) | (30u << 5), 50}; }

static const Node *Find(Store &store, const char *path)
{
    unsigned node = 0;
    return store.Lookup(path, node) ? nullptr : store.Get(node);
}

static int First(Store &store, const char *directory, char (&out)[256])
{
    unsigned node = 0, id = 0;
    if (store.Lookup(directory, node) || store.Enumerate(node, 0, id))
        return 0;
    snprintf(out, sizeof out, "%s", store.Get(id)->name);
    return 1;
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3)
    {
        fprintf(stderr, "usage: test-host-volume SCRATCHDIR [WIN98ROOT]\n");
        return 2;
    }
    const std::string scratch = argv[1];
    const std::string drive_c = scratch + "/drive-c", drive_d = scratch + "/drive-d",
                      cdrom = scratch + "/cdrom";
    SetClock(GuestClock);
    unsigned handle = 0, other = 0, status = 0, done = 0, id = 0;
    char text[256] = {};

    // --- First run: everything the guest can do to a volume -----------------
    {
        Volume::Clear();
        HostVolume::Drive c, d, e;
        CHECK(c.Open(drive_c.c_str(), true, "NEO98"));
        CHECK(d.Open(drive_d.c_str(), true, "DATEN"));
        CHECK(e.Open(cdrom.c_str(), false, "CD"));
        Volume::Mount('C', &c, Volume::Fixed, true);
        Volume::Mount('D', &d, Volume::Fixed, true);
        Volume::Mount('E', &e, Volume::Cdrom, false);
        Store store;
        Session session(store), second(store);

        CHECK(store.Drive('C') && store.Drive('D') && store.Drive('E') && !store.Drive('F'));
        CHECK(store.DriveType('C') == 3 && store.DriveType('E') == 5);
        CHECK(!strcmp(store.DriveLabel('D'), "DATEN"));
        unsigned long long total = 0, available = 0;
        store.DriveSpace('C', total, available);
        CHECK(total > available && available > 0);

        // A long name, its 8.3 alias and case-insensitive lookup.
        CHECK(!store.MakeDirectory("C:\\Program Files"));
        CHECK(!session.Open("C:\\Program Files\\Long Name File.txt", 3, 0, 2, 0, handle, status));
        CHECK(!session.Write(handle, "hello", 5, done) && done == 5);
        CHECK(!session.Close(handle));
        const Node *node = Find(store, "C:\\Program Files\\Long Name File.txt");
        CHECK(node && !strcmp(node->alias, "LONGNA~1.TXT") && node->size == 5);
        CHECK(Find(store, "C:\\PROGRA~1\\LONGNA~1.TXT") == node);
        CHECK(Find(store, "c:\\program files\\long name file.TXT") == node);
        // The creation time is the guest clock's, not the host clock's.
        CHECK(node->times[0].date == GuestClock().date);

        // A second long name in the same directory takes the next tail.
        CHECK(!store.Seed("C:\\Program Files\\Long Name Files.txt", "x", 1));
        CHECK(!strcmp(Find(store, "C:\\Program Files\\Long Name Files.txt")->alias, "LONGNA~2.TXT"));

        // Sharing: an open exclusive handle blocks a second open and a delete.
        CHECK(!session.Open("C:\\Program Files\\Long Name File.txt", 3, 0, 3, 0, handle, status));
        CHECK(second.Open("C:\\PROGRA~1\\LONGNA~1.TXT", 1, 1, 3, 0, other, status) == 32);
        CHECK(store.Remove("C:\\Program Files\\Long Name File.txt", false) == 32);
        CHECK(!session.Close(handle));

        // Attributes, including the read-only rule of the existing contract.
        CHECK(!store.Attributes("C:\\Program Files\\Long Name File.txt", 1));
        CHECK(session.Open("C:\\Program Files\\Long Name File.txt", 2, 0, 3, 0, handle, status) == 5);
        CHECK(store.Remove("C:\\Program Files\\Long Name File.txt", false) == 5);
        CHECK(!store.Attributes("C:\\Program Files\\Long Name File.txt", 32));

        // A file larger than the RAM volume's 8 MiB per-file limit.
        CHECK(!session.Open("C:\\WINDOWS\\TEMP\\big.bin", 3, 0, 2, 0, handle, status));
        static unsigned char block[64 * 1024];
        memset(block, 0x5a, sizeof block);
        for (unsigned i = 0; i < 160; ++i)
            CHECK(!session.Write(handle, block, sizeof block, done) && done == sizeof block);
        unsigned size = 0;
        CHECK(!session.Size(handle, size) && size == 160u * sizeof block);
        CHECK(!session.Close(handle));

        // Rename, delete, and a directory that keeps its entries.
        CHECK(!store.MakeDirectory("C:\\WINDOWS\\TEMP\\Eigene Dateien"));
        CHECK(!store.Seed("C:\\WINDOWS\\TEMP\\Eigene Dateien\\Notiz.txt", "abc", 3));
        CHECK(!store.Rename("C:\\WINDOWS\\TEMP\\Eigene Dateien\\Notiz.txt",
                            "C:\\WINDOWS\\TEMP\\Eigene Dateien\\Umbenannt.txt"));
        CHECK(!store.Seed("C:\\WINDOWS\\TEMP\\weg.txt", "x", 1));
        CHECK(!store.Remove("C:\\WINDOWS\\TEMP\\weg.txt", false));
        CHECK(store.Lookup("C:\\WINDOWS\\TEMP\\weg.txt", id) == 2);

        // Per-drive current directory, as Win98 keeps it per process.
        CHECK(!store.MakeDirectory("D:\\Spiele"));
        CHECK(!store.Seed("D:\\Spiele\\start.bat", "rem", 3));
        CHECK(!session.SetDirectory("D:\\Spiele"));
        CHECK(!session.SetDirectory("C:\\WINDOWS\\TEMP"));
        char full[260];
        CHECK(!session.FullPath("D:start.bat", full) && !strcmp(full, "D:\\Spiele\\start.bat"));
        CHECK(!session.FullPath("nested", full) && !strcmp(full, "C:\\WINDOWS\\TEMP\\nested"));
        // A rename across drives is not a rename.
        CHECK(store.Rename("D:\\Spiele\\start.bat", "C:\\WINDOWS\\TEMP\\start.bat") == 17);

        // The CD-ROM is read-only for everything that writes.
        CHECK(!store.Lookup("E:\\", id));
        CHECK(store.MakeDirectory("E:\\neu") == 19);
        CHECK(session.Open("E:\\neu.txt", 3, 0, 2, 0, handle, status) == 19);
        store.Flush();
    }

    // --- Second run: a fresh store over the same directories ----------------
    {
        Volume::Clear();
        HostVolume::Drive c, d, e;
        CHECK(c.Open(drive_c.c_str(), true, "NEO98"));
        CHECK(d.Open(drive_d.c_str(), true, "DATEN"));
        CHECK(e.Open(cdrom.c_str(), false, "CD"));
        Volume::Mount('C', &c, Volume::Fixed, true);
        Volume::Mount('D', &d, Volume::Fixed, true);
        Volume::Mount('E', &e, Volume::Cdrom, false);
        Store store;
        Session session(store);

        const Node *node = Find(store, "C:\\Program Files\\Long Name File.txt");
        CHECK(node && node->size == 5 && node->attributes == 32);
        // The alias is the one of the first run, out of the sidecar.
        CHECK(!strcmp(node->alias, "LONGNA~1.TXT"));
        CHECK(!strcmp(Find(store, "C:\\Program Files\\Long Name Files.txt")->alias, "LONGNA~2.TXT"));
        CHECK(node->times[0].date == GuestClock().date && node->times[0].time == GuestClock().time);
        CHECK(!session.Open("C:\\PROGRA~1\\LONGNA~1.TXT", 1, 1, 3, 0, handle, status));
        CHECK(!session.Read(handle, text, sizeof text, done) && done == 5 && !memcmp(text, "hello", 5));
        CHECK(!session.Close(handle));

        unsigned size = 0;
        CHECK(!session.Open("C:\\WINDOWS\\TEMP\\big.bin", 1, 1, 3, 0, handle, status));
        CHECK(!session.Size(handle, size) && size == 160u * 64u * 1024u);
        CHECK(!session.Seek(handle, 160u * 64u * 1024u - 1, 0, size));
        CHECK(!session.Read(handle, text, 1, done) && done == 1 && (unsigned char)text[0] == 0x5a);
        CHECK(!session.Close(handle));

        CHECK(Find(store, "C:\\WINDOWS\\TEMP\\Eigene Dateien\\Umbenannt.txt"));
        CHECK(store.Lookup("C:\\WINDOWS\\TEMP\\Eigene Dateien\\Notiz.txt", id) == 2);
        CHECK(store.Lookup("C:\\WINDOWS\\TEMP\\weg.txt", id) == 2);
        CHECK(Find(store, "D:\\Spiele\\start.bat"));
        CHECK(!strcmp(store.DriveLabel('D'), "DATEN"));

        // The directory enumeration is stable across runs.
        char first[256] = {};
        CHECK(First(store, "C:\\Program Files", first));
        CHECK(!strcmp(first, "Long Name File.txt"));
        store.Flush();
    }
    // DOS writes through its own host directory view. Windows must see new,
    // renamed and removed files even after it has cached the directory.
    {
        Volume::Clear();
        HostVolume::Drive d;
        CHECK(d.Open(drive_d.c_str(), true, "DATEN"));
        Volume::Mount('D', &d, Volume::Fixed, true);
        Store store;
        Session session(store);
        CHECK(!store.MakeDirectory("D:\\External"));
        CHECK(!store.Lookup("D:\\External", id));
        unsigned child = 0;
        CHECK(store.Enumerate(id, 0, child) != 0);
        CHECK(store.Lookup("D:\\External\\DOS.OUT", child) == 2);
        const auto path = drive_d + "/External/DOS.OUT";
        FILE *file = fopen(path.c_str(), "wb");
        CHECK(file && fwrite("DOS output", 1, 10, file) == 10);
        CHECK(!fclose(file));
        CHECK(!store.Enumerate(id, 0, child));
        CHECK(!strcmp(store.Get(child)->name, "DOS.OUT"));
        CHECK(!session.Open("D:\\External\\DOS.OUT", 1, 1, 3, 0, handle, status));
        CHECK(!session.Read(handle, text, sizeof text, done) && done == 10 && !memcmp(text, "DOS output", 10));
        CHECK(!session.Close(handle));
        const auto renamed = drive_d + "/External/RENAMED.OUT";
        CHECK(!rename(path.c_str(), renamed.c_str()));
        CHECK(store.Lookup("D:\\External\\DOS.OUT", child) == 2);
        CHECK(!store.Lookup("D:\\External\\RENAMED.OUT", child));
        CHECK(!unlink(renamed.c_str()));
        CHECK(store.Lookup("D:\\External\\RENAMED.OUT", child) == 2);
        CHECK(store.Enumerate(id, 0, child) != 0);
    }
    // --- The read-only Win98 tree under the writable volume -----------------
    // The overlay: a tree file is copied up on its first write, a removed tree
    // entry leaves a tombstone, and both survive the next run.
    if (argc == 3)
    {
        const std::string overlay = scratch + "/overlay";
        const char *tree_file = "C:\\WINDOWS\\SYSTEM.INI";
        const char *removed = "C:\\WINDOWS\\WIN.INI";
        const char *replaced = "C:\\WINDOWS\\SYSTEM\\RSABASE.DLL";
        // a directory of the tree emptied and removed, as WININIT's
        // DIRNUL or Explorer does it, then made again. Win98 has an empty
        // directory then; the tree's files must not come back.
        const char *folder = "C:\\WINDOWS\\SYSTEM\\COLOR";
        const char *folder_file = "C:\\WINDOWS\\SYSTEM\\COLOR\\MNB22G15.ICM";
        unsigned original_size = 0;
        for (unsigned pass = 0; pass < 3; ++pass)
        {
            Volume::Clear();
            SystemTree::Mount(nullptr);
            CHECK(SystemTreeHost::Mount(argv[2]));
            HostVolume::Drive c;
            CHECK(c.Open(overlay.c_str(), true, "NEO98"));
            Volume::Mount('C', &c, Volume::Fixed, true);
            Store store;
            Session session(store);
            const Node *node = Find(store, tree_file);
            CHECK(node && node->size);
            char child[256] = {}, path[300];
            if (pass == 2)
            {
                // The directory made again is still there and still empty,
                // and it goes again without a trace of the tree.
                CHECK(Find(store, folder) && !First(store, folder, child));
                CHECK(store.Lookup(folder_file, id) == 2);
                CHECK(!store.Remove(folder, true));
                CHECK(store.Lookup(folder, id) == 2);
                CHECK(store.Lookup(folder_file, id) == 3);
                store.Flush();
                continue;
            }
            if (!pass)
            {
                CHECK(Find(store, folder_file));
                unsigned removed_files = 0;
                while (First(store, folder, child) && removed_files < 64)
                {
                    snprintf(path, sizeof path, "%s\\%s", folder, child);
                    CHECK(!store.Remove(path, false));
                    ++removed_files;
                }
                CHECK(removed_files >= 2 && !First(store, folder, child));
                CHECK(!store.Remove(folder, true));
                CHECK(store.Lookup(folder, id) == 2);
                CHECK(store.Lookup(folder_file, id) == 3);
                CHECK(!store.MakeDirectory(folder));
                CHECK(Find(store, folder) && !First(store, folder, child));
                CHECK(store.Lookup(folder_file, id) == 2);
            }
            if (pass == 1)
            {
                // After the restart: made again, empty, the tree hidden below.
                CHECK(Find(store, folder) && !First(store, folder, child));
                CHECK(store.Lookup(folder_file, id) == 2);
            }
            if (!pass)
            {
                original_size = node->size;
                CHECK(!node->upper);   // still only in the read-only tree
                CHECK(!session.Open(tree_file, 3, 0, 3, 0, handle, status));
                CHECK(!session.Seek(handle, 0, 2, done));
                CHECK(!session.Write(handle, ";neo98\n", 7, done) && done == 7);
                CHECK(!session.Close(handle));
                CHECK(Find(store, tree_file)->upper);
                CHECK(Find(store, removed));
                CHECK(!store.Remove(removed, false));
                CHECK(store.Lookup(removed, id) == 2);
                CHECK(!store.Seed("C:\\WINDOWS\\NEO98NEU.TXT", "neu", 3));
                // SETUPAPI replacing a tree file: the old bytes were read,
                // the file deleted and a temporary file moved onto its name.
                CHECK(store.Lookup(replaced, id) == 0 && store.Bytes(id));
                CHECK(!store.Remove(replaced, false));
                CHECK(!store.Seed("C:\\WINDOWS\\SYSTEM\\~AD1234.TMP", "neu!", 4));
                CHECK(!store.Rename("C:\\WINDOWS\\SYSTEM\\~AD1234.TMP", replaced));
                CHECK(Find(store, replaced) && Find(store, replaced)->size == 4);
            }
            else
            {
                CHECK(node->upper && node->size == original_size + 7);
                CHECK(!session.Open(tree_file, 1, 1, 3, 0, handle, status));
                CHECK(!session.Seek(handle, node->size - 7, 0, done));
                CHECK(!session.Read(handle, text, 7, done) && done == 7 && !memcmp(text, ";neo98\n", 7));
                CHECK(!session.Close(handle));
                CHECK(store.Lookup(removed, id) == 2);
                CHECK(Find(store, "C:\\WINDOWS\\NEO98NEU.TXT"));
                // Everything else of the tree is still there and unchanged.
                CHECK(Find(store, "C:\\WINDOWS\\SYSTEM\\USER32.DLL"));
                // The moved file kept the name; removing it again must not
                // bring the tree's file back.
                CHECK(Find(store, replaced) && Find(store, replaced)->size == 4);
                CHECK(!store.Remove(replaced, false));
                CHECK(store.Lookup(replaced, id) == 2);
            }
            store.Flush();
        }
        SystemTree::Mount(nullptr);
    }
    printf("HOST VOLUME PASS %u checks\n", checks);
    return 0;
}
