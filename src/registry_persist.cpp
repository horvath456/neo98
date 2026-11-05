// SPDX-License-Identifier: GPL-2.0-or-later
// The change journal of the native registry. src/registry.h describes why it
// is a journal and not a copy of the whole registry.
// Format, little endian throughout:
// "NEO98REG" 0x31 eight signature bytes and version 1
// record* in the order the changes happened
// record: kind byte, then its fields
// 1 create key path
// 2 delete key path
// 3 set value key path, value name, type (4), data (4 + bytes)
// 4 delete value key path, value name
// A path or name is a two-byte length and that many bytes, without a
// terminator.
#include "registry.h"
#include <new>
#include <stdio.h>
#include <string.h>
namespace Registry
{
namespace
{
const char Signature[9] = "NEO98REG";
// Accept existing journals; subsequent saves use the current signature.
const unsigned char LegacySignature[8] = {65, 82, 77, 57, 56, 82, 69, 71};
struct Journal
{
    unsigned char *bytes = nullptr;
    U size = 0, capacity = 0, records = 0;
    bool recording = false, dirty = false, full = false;
    Storage *storage = nullptr;
    ~Journal() { delete[] bytes; }
    bool Room(U wanted)
    {
        if (size + wanted <= capacity)
            return true;
        // No ceiling but the 32-bit length of the journal.
        if (size + wanted < size)
        {
            full = true;
            return false;
        }
        U next = capacity ? capacity : 4096;
        while (next < size + wanted)
            next = next > ~0u / 2 ? ~0u : next * 2;
        auto *grown = new (std::nothrow) unsigned char[next];
        if (!grown)
            return false;
        if (size)
            memcpy(grown, bytes, size);
        delete[] bytes;
        bytes = grown;
        capacity = next;
        return true;
    }
    void Byte(U value) { bytes[size++] = (unsigned char)value; }
    void Word(U value)
    {
        Byte(value);
        Byte(value >> 8);
    }
    void Long(U value)
    {
        Word(value);
        Word(value >> 16);
    }
    void Text(const char *text, U length)
    {
        Word(length);
        if (length)
            memcpy(bytes + size, text, length);
        size += length;
    }
};
Journal &Book()
{
    static Journal journal;
    return journal;
}
// Reader over a loaded journal.
struct Cursor
{
    const unsigned char *at, *end;
    bool okay = true;
    U Byte()
    {
        if (at >= end)
            return okay = false, 0u;
        return *at++;
    }
    U Word()
    {
        const U low = Byte();
        return low | (Byte() << 8);
    }
    U Long()
    {
        const U low = Word();
        return low | (Word() << 16);
    }
    const char *Text(U &length)
    {
        length = Word();
        if (!okay || U(end - at) < length)
            return okay = false, nullptr;
        const char *text = (const char *)at;
        at += length;
        return text;
    }
};
bool Copy(const char *text, U length, char *out, U capacity)
{
    if (length >= capacity)
        return false;
    if (length)
        memcpy(out, text, length);
    out[length] = 0;
    return true;
}
} // namespace

void Attach(Storage *storage) { Book().storage = storage; }
Unrecorded::Unrecorded() : was(Book().recording) { Book().recording = false; }
Unrecorded::~Unrecorded() { Book().recording = was; }
Storage *Attached() { return Book().storage; }
bool Dirty() { return Book().dirty; }
U Records() { return Book().records; }

// The journal writers. They do nothing until Restore has switched recording
// on, so neither the hive import nor the replay records itself.
// The observers of registry.h. Four slots are enough: one per registered
// reader of the change stream, and today only RegNotifyChangeKeyValue asks.
static Observer observers[4];
bool Observe(Observer observer)
{
    for (auto &slot : observers)
        if (!slot || slot == observer)
        {
            slot = observer;
            return true;
        }
    return false;
}
// The paths of the hives RegLoadKey mounted, see registry.h.
static char mounts[16][260];
void Mount(const char *path, bool add)
{
    for (auto &slot : mounts)
    {
        if (add && !slot[0])
        {
            strncpy(slot, path, sizeof slot - 1);
            return;
        }
        if (!add && slot[0] && !strcmp(slot, path))
        {
            slot[0] = 0;
            return;
        }
    }
}
bool Mounted(const char *path)
{
    for (const auto &slot : mounts)
    {
        if (!slot[0])
            continue;
        const U length = (U)strlen(slot);
        bool same = true;
        for (U i = 0; i < length && same; ++i)
        {
            unsigned x = (unsigned char)slot[i], y = (unsigned char)path[i];
            if (x >= 'a' && x <= 'z') x -= 32;
            if (y >= 'a' && y <= 'z') y -= 32;
            same = x == y;
        }
        if (same && (path[length] == 0 || path[length] == '\\'))
            return true;
    }
    return false;
}
void Note(U kind, const char *path, const char *name, U type, const void *data, U size)
{
    if (path)
        for (auto slot : observers)
            if (slot)
                slot(kind, path, name);
    auto &journal = Book();
    // HKEY_DYN_DATA is made anew at every start.
    if (!journal.recording || !path || Mounted(path) || !strncmp(path, "HKEY_DYN_DATA", 13))
        return;
    const U path_length = (U)strlen(path), name_length = name ? (U)strlen(name) : 0u;
    const U wanted = 1 + 2 + path_length + (kind >= 3 ? 2 + name_length : 0u) +
                     (kind == 3 ? 8 + size : 0u);
    if (!journal.Room(wanted))
        return;
    journal.Byte(kind);
    journal.Text(path, path_length);
    if (kind >= 3)
        journal.Text(name ? name : "", name_length);
    if (kind == 3)
    {
        journal.Long(type);
        journal.Long(size);
        if (size)
            memcpy(journal.bytes + journal.size, data, size);
        journal.size += size;
    }
    ++journal.records;
    journal.dirty = true;
}

U Restore(Store &store)
{
    auto &journal = Book();
    journal.recording = false;
    U result = 0;
    if (journal.storage)
    {
        const U size = journal.storage->Size();
        if (size > (U)sizeof Signature)
        {
            if (!journal.Room(size))
                return 8;
            if (!journal.storage->Load(journal.bytes, size))
                return 1117;
            journal.size = size;
            if ((memcmp(journal.bytes, Signature, 8) && memcmp(journal.bytes, LegacySignature, 8)) || journal.bytes[8] != '1')
            {
                journal.size = 0;
                return 1015;
            }
            // The devices of the machine come back with every start, as
            // Win98 finds a device again that is still plugged in: a key of a
            // device instance of the image under the journal, the keys above
            // and below it and the key of its driver are not deleted again
            // when the journal is replayed. "Remove" in the Device Manager
            // holds until the next start (README.md).
            struct Machine
            {
                char paths[2 * DevnodeCapacity][160];
                U count = 0;
                static bool Fold(const char *a, const char *b, U n)
                {
                    for (U i = 0; i < n; ++i)
                    {
                        U x = (unsigned char)a[i], y = (unsigned char)b[i];
                        if (x >= 'a' && x <= 'z') x -= 32;
                        if (y >= 'a' && y <= 'z') y -= 32;
                        if (x != y) return false;
                    }
                    return true;
                }
                bool Holds(const char *rest) const
                {
                    const U n = (U)strlen(rest);
                    for (U i = 0; i < count; ++i)
                    {
                        const U m = (U)strlen(paths[i]);
                        const U common = n < m ? n : m;
                        if (Fold(rest, paths[i], common) &&
                            (n == m || (n < m ? paths[i][n] == '\\' : rest[m] == '\\')))
                            return true;
                    }
                    return false;
                }
            };
            static Machine machine;
            machine.count = 0;
            {
                U enumeration = 0;
                bool made = false;
                static U instances[DevnodeCapacity];
                const U count = store.Open(store.roots[2], "Enum", false, enumeration, made)
                                    ? 0 : Devnodes(store, enumeration, instances, DevnodeCapacity);
                for (U i = 0; i < count; ++i)
                {
                    const U device = store.nodes[instances[i]].parent, enumerator = store.nodes[device].parent;
                    snprintf(machine.paths[machine.count++], sizeof machine.paths[0], "Enum\\%s\\%s\\%s",
                             store.nodes[enumerator].name, store.nodes[device].name, store.nodes[instances[i]].name);
                    const Value *driver = nullptr;
                    if (!store.Query(instances[i], "Driver", driver) && driver && driver->size && driver->size < 64)
                        snprintf(machine.paths[machine.count++], sizeof machine.paths[0],
                                 "System\\CurrentControlSet\\Services\\Class\\%.*s", (int)driver->size,
                                 (const char *)driver->data);
                }
            }
            Cursor cursor{journal.bytes + 9, journal.bytes + size};
            char path[1024], name[512];
            while (cursor.okay && cursor.at < cursor.end)
            {
                const U kind = cursor.Byte();
                U length = 0;
                const char *text = cursor.Text(length);
                if (!cursor.okay || !Copy(text, length, path, sizeof path))
                    break;
                if (kind >= 3)
                {
                    text = cursor.Text(length);
                    if (!cursor.okay || !Copy(text, length, name, sizeof name))
                        break;
                }
                const U anchor = store.Anchor(path);
                const char *rest = strchr(path, '\\');
                rest = rest ? rest + 1 : "";
                if (anchor == Missing)
                {
                    // An unknown root: skip the record's payload and go on.
                    if (kind == 3)
                    {
                        cursor.Long();
                        const U size_field = cursor.Long();
                        cursor.at += size_field;
                    }
                    ++journal.records;
                    continue;
                }
                U key = 0;
                bool created = false;
                if (kind == 1)
                    store.Open(anchor, rest, true, key, created);
                else if (kind == 2)
                {
                    if (anchor != store.roots[2] || !machine.Holds(rest))
                        store.DeleteKey(anchor, rest);
                }
                else if (kind == 3)
                {
                    const U type = cursor.Long(), size_field = cursor.Long();
                    if (!cursor.okay || U(cursor.end - cursor.at) < size_field)
                        break;
                    const unsigned char *data = cursor.at;
                    cursor.at += size_field;
                    if (!store.Open(anchor, rest, true, key, created))
                        store.Set(key, name, type, size_field ? data : nullptr, size_field);
                }
                else if (kind == 4)
                {
                    if (!store.Open(anchor, rest, false, key, created))
                        store.DeleteValue(key, name);
                }
                else
                    break;
                ++journal.records;
            }
            if (!cursor.okay)
                result = 1015;
            // The journal keeps only the records; the signature is written in
            // front of them again on the next flush.
            journal.size = size - 9;
            if (journal.size)
                memmove(journal.bytes, journal.bytes + 9, journal.size);
        }
    }
    // What the journal says of a device (disabled in this profile) is known
    // only now.
    PublishDevnodes(store);
    journal.recording = true;
    journal.dirty = false;
    return result;
}

bool Flush()
{
    auto &journal = Book();
    if (!journal.storage)
        return false;
    if (!journal.dirty)
        return true;
    // The signature is written in front of the records the journal collected.
    const U total = 9 + journal.size;
    auto *image = new unsigned char[total];
    if (!image)
        return false;
    memcpy(image, Signature, 8);
    image[8] = '1';
    if (journal.size)
        memcpy(image + 9, journal.bytes, journal.size);
    const bool okay = journal.storage->Save(image, total);
    delete[] image;
    if (okay)
        journal.dirty = false;
    return okay;
}
} // namespace Registry
