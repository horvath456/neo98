// SPDX-License-Identifier: GPL-2.0-or-later
// The six registry services of VMM behind ADVAPI32: RegLoadKeyA,
// RegUnLoadKeyA, RegSaveKeyA, RegReplaceKeyA, RegQueryMultipleValuesA and
// RegRemapPreDefKey.
// ADVAPI32 checks its own arguments and
// hands the call to a KERNEL32 VxDCall ordinal with the service number:
// 10021h load, 10022h unload, 10023h save, 10024h remap, 10026h query
// multiple, 10027h replace. A key with bit 0 set is a network key and goes to
// the provider DLL instead; without a provider the answer is 45Ah. The file
// names go through KERNEL32 ordinal 11, CharToOemBuffA, into a buffer of 104h
// bytes; this port keeps the ANSI name, which is the same name for every file
// name that survives the round trip to OEM and back.
// The services themselves are VMM code, read from VMM32.VXD of the Win98 SE CD
// (LE image, object N at N*100000h): load 0xD040C2, unload 0xD04206, save
// 0xD04517 over 0xD0449E, remap 0xD036C0, query multiple 0xD03D58, replace
// 0xD04668. Every answer goes through CWDE, so only the low word counts. The
// file format is src/win98_creg.h.
#include "x86_app.h"
#include "win98_creg.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
namespace
{
constexpr unsigned BadKey = 1010, BadDatabase = 1009, IoFailed = 1016, MoreData = 234,
                   InvalidParameter = 87, NoProvider = 0x45a, CannotRead = 1012;
// A hive RegLoadKey mounted. The list lives in VMM's data and is one for the
// whole system.
struct Mount
{
    unsigned node = Registry::Missing;
    char path[300] = {}, file[260] = {}, name[256] = {};
    unsigned root = 0; // 80000002h or 80000003h
};
Mount mounts[16];
// VMM's flag +69h bit 0 on the record of a hive, set by a RegReplaceKey that
// succeeded, for SYSTEM.DAT and USER.DAT. VMM swaps whole files
// at the end; this port loads its registry from the imported hives and its
// journal, not from SYSTEM.DAT, so it also keeps the names of the replaced
// keys and carries them over at the same moment.
struct PendingHive
{
    bool flagged = false;
    unsigned count = 0;
    char keys[8][256] = {};
};
PendingHive pending_hives[2];
const char *const HiveFiles[2] = {"C:\\WINDOWS\\SYSTEM.DAT", "C:\\WINDOWS\\USER.DAT"};
// Replace the final three filename characters with two tildes and a letter.
void TemporaryName(const char *path, char letter, char (&out)[260])
{
    snprintf(out, sizeof out, "%s", path);
    const unsigned length = (unsigned)strlen(out);
    if (length >= 3)
    {
        out[length - 3] = '~';
        out[length - 2] = '~';
        out[length - 1] = letter;
    }
}
bool SameName(const char *a, const char *b);
// The keys of one direct child of the hive root, built below a node of the
// store: the replaced key of a "~~R" copy.
struct KeySink : Win98Creg::Sink
{
    Registry::Store &store;
    unsigned root;
    const char *only;
    unsigned *nodes = nullptr, count = 0, capacity = 0;
    KeySink(Registry::Store &s, unsigned r, const char *k) : store(s), root(r), only(k) {}
    ~KeySink() { delete[] nodes; }
    unsigned Key(unsigned parent, const char *name, unsigned length) override
    {
        char text[256];
        if (length > 255) length = 255;
        memcpy(text, name, length);
        text[length] = 0;
        unsigned node = Registry::Missing;
        const bool inside = parent == ~0u ? SameName(text, only) : nodes[parent] != Registry::Missing;
        if (inside && !strchr(text, '\\'))
        {
            bool created = false;
            if (store.Open(parent == ~0u ? root : nodes[parent], text, true, node, created))
                return ~0u;
            store.Release(node);
        }
        if (count == capacity)
        {
            capacity = capacity ? capacity * 2 : 256;
            auto *grown = new unsigned[capacity];
            if (count) memcpy(grown, nodes, count * sizeof *nodes);
            delete[] nodes;
            nodes = grown;
        }
        nodes[count] = node;
        return count++;
    }
    bool Value(unsigned key, const char *name, unsigned name_length, unsigned type, const unsigned char *data,
               unsigned size) override
    {
        if (key == ~0u || nodes[key] == Registry::Missing)
            return true;
        char text[256];
        if (name_length > 255) name_length = 255;
        memcpy(text, name, name_length);
        text[name_length] = 0;
        unsigned char *copy = new unsigned char[size + 1];
        if (size) memcpy(copy, data, size);
        unsigned stored = size;
        if ((type == 1 || type == 2 || type == 7) && (!size || data[size - 1]))
            copy[stored++] = 0;
        const bool ok = !store.Set(nodes[key], text, type, copy, stored);
        delete[] copy;
        return ok;
    }
};
// ADVAPI32 : the predefined roots and every handle with bit 0 clear
// are local; this port's own handles count as local whatever their low bit.
bool LocalKey(const Registry::Session &session, unsigned key)
{
    if (key >= 0x80000000u && key <= 0x80000006u)
        return true;
    unsigned node = 0;
    const unsigned r = session.Key(key, node);
    return r != 6 || !(key & 1);
}
// VMM 0xD04078 over 0x603840 and 0x603892: the name of a hive is one
// component. One leading backslash is allowed, no other; the component is
// shorter than 256 characters and every character is 20h or above.
bool HiveName(const char *text, char (&out)[256])
{
    if (!text)
        return false;
    if (*text == '\\')
        ++text;
    unsigned length = 0;
    for (const char *p = text; *p; ++p, ++length)
        if ((unsigned char)*p < 0x20 || *p == '\\' || length >= 255)
            return false;
    if (!length)
        return false;
    memcpy(out, text, length + 1);
    return true;
}
bool SameName(const char *a, const char *b)
{
    for (;; ++a, ++b)
    {
        unsigned x = (unsigned char)*a, y = (unsigned char)*b;
        if (x >= 'a' && x <= 'z') x -= 32;
        if (y >= 'a' && y <= 'z') y -= 32;
        if (x != y) return false;
        if (!x) return true;
    }
}
// A sink that only counts, for the check of a hive before anything is built.
struct CountSink : Win98Creg::Sink
{
    unsigned keys = 0;
    unsigned Key(unsigned, const char *, unsigned) override { return keys++; }
    bool Value(unsigned, const char *, unsigned, unsigned, const unsigned char *, unsigned) override { return true; }
};
// A sink that builds the keys of a hive below one node of the store.
struct StoreSink : Win98Creg::Sink
{
    Registry::Store &store;
    unsigned root;
    unsigned *nodes = nullptr, count = 0, capacity = 0;
    StoreSink(Registry::Store &s, unsigned r) : store(s), root(r) {}
    ~StoreSink() { delete[] nodes; }
    unsigned Key(unsigned parent, const char *name, unsigned length) override
    {
        char text[256];
        if (length > 255) length = 255;
        memcpy(text, name, length);
        text[length] = 0;
        // A key name with a backslash cannot be opened by its name; VMM keeps
        // it all the same. The store splits on backslashes, so such a key is
        // refused with the answer of a damaged hive.
        if (strchr(text, '\\'))
            return ~0u;
        const unsigned parent_node = parent == ~0u ? root : nodes[parent];
        unsigned node = 0;
        bool created = false;
        if (store.Open(parent_node, text, true, node, created))
            return ~0u;
        store.Release(node);
        if (count == capacity)
        {
            capacity = capacity ? capacity * 2 : 256;
            auto *grown = new unsigned[capacity];
            if (count) memcpy(grown, nodes, count * sizeof *nodes);
            delete[] nodes;
            nodes = grown;
        }
        nodes[count] = node;
        return count++;
    }
    bool Value(unsigned key, const char *name, unsigned name_length, unsigned type, const unsigned char *data,
               unsigned size) override
    {
        char text[256];
        if (name_length > 255) name_length = 255;
        memcpy(text, name, name_length);
        text[name_length] = 0;
        // CREG keeps text without its terminator; the store keeps one, as the
        // import of the installed hives does (tools/import_registry.py).
        unsigned char *copy = new unsigned char[size + 1];
        if (size) memcpy(copy, data, size);
        unsigned stored = size;
        if ((type == 1 || type == 2 || type == 7) && (!size || data[size - 1]))
            copy[stored++] = 0;
        const bool ok = !store.Set(key == ~0u ? root : nodes[key], text, type, copy, stored);
        delete[] copy;
        return ok;
    }
};
} // namespace

// Reads a whole file of the guest file system. Zero or IoFailed.
unsigned Application::HiveReadFile(const char *path, unsigned char *&bytes, unsigned &size)
{
    bytes = nullptr;
    size = 0;
    unsigned handle = 0, status = 0;
    if (files.Open(path, 1, 3, 3, 0, handle, status))
        return IoFailed;
    unsigned done = 0;
    unsigned result = files.Size(handle, size) || size > 64u << 20 ? IoFailed : 0;
    if (!result)
    {
        bytes = new unsigned char[size ? size : 1];
        if (size && (files.Read(handle, bytes, size, done) || done != size))
            result = IoFailed;
    }
    files.Close(handle);
    if (result)
    {
        delete[] bytes;
        bytes = nullptr;
    }
    return result;
}
// Creates or truncates a file and writes the bytes (0xD0365C, action 12h).
unsigned Application::HiveWriteFile(const char *path, const unsigned char *bytes, unsigned size)
{
    unsigned handle = 0, status = 0;
    if (files.Open(path, 3, 0, 2, 0, handle, status))
        return IoFailed;
    unsigned done = 0;
    const unsigned result = files.Write(handle, bytes, size, done) || done != size ? IoFailed : 0;
    files.Close(handle);
    return result;
}
// The recursive copy of 0xD0440F over 0xD042E8 into a writer: values first,
// then subkeys, no deeper than 20h levels. A text value loses its
// terminator; a value over 4000h bytes is left out, because the query into
// the buffer of 4001h bytes fails for it.
void Application::HiveCollect(void *opaque, unsigned node, unsigned writer_key, unsigned depth)
{
    auto &writer = *static_cast<Win98Creg::Writer *>(opaque);
    auto &store = registry.store;
    const Registry::Value *value = nullptr;
    for (unsigned i = 0; !store.EnumValue(node, i, value); ++i)
    {
        if (value->size > 0x4000)
            continue;
        unsigned size = value->size;
        if (value->type == 1 && size && !value->data[size - 1])
            --size;
        writer.Value(writer_key, value->name, (unsigned)strlen(value->name), value->type, value->data, size);
    }
    if (depth >= 0x20)
        return;
    unsigned child = 0;
    for (unsigned i = 0; !store.EnumKey(node, i, child); ++i)
    {
        const char *name = store.nodes[child].name;
        const unsigned key = writer.Key(writer_key, name, (unsigned)strlen(name));
        if (key != ~0u)
            HiveCollect(opaque, child, key, depth + 1);
    }
}
// RegSaveKey's core, 0xD0449E: a new hive of version 1.0 with 80h in the
// header word at +16h, then the copy, then the write. On any error the file
// goes again.
unsigned Application::HiveSave(unsigned node, const char *path, unsigned word16)
{
    Win98Creg::Writer writer;
    const unsigned root = writer.Key(~0u, "", 0);
    HiveCollect(&writer, node, root, 0);
    unsigned size = 0;
    unsigned char *bytes = writer.Finish(word16, size);
    unsigned result = bytes ? HiveWriteFile(path, bytes, size) : IoFailed;
    delete[] bytes;
    if (result)
    {
        char absolute[260];
        if (!files.FullPath(path, absolute))
        {
            files.store.Attributes(absolute, 0);
            files.store.Remove(absolute, false);
        }
    }
    return result;
}
// Writes a mounted hive back to its file, as VMM's flush (0x6021FC) does.
unsigned Application::HiveFlush(unsigned node)
{
    for (auto &mount : mounts)
        if (mount.node == node)
            return HiveSave(node, mount.file, 1);
    return 0;
}
// True and the mount when a key lies in a hive RegLoadKey mounted.
bool Application::HiveOfKey(unsigned node, unsigned &mount_node)
{
    for (unsigned n = node; n != Registry::Missing; n = registry.store.nodes[n].parent)
        for (auto &mount : mounts)
            if (mount.path[0] && mount.node == n)
            {
                mount_node = n;
                return true;
            }
    return false;
}

unsigned Application::RegistryHiveApi(ApiName name, const unsigned *a)
{
    auto &store = registry.store;
    const auto string_ok = [&](unsigned address) {
        return address && virtual_memory.Access(address, 1, false);
    };
    switch (name.id)
    {
    // then VMM 0xD040C2.
    API_CASE("RegLoadKeyA")
    {
        if (!LocalKey(registry, a[0]))
            return NoProvider;
        if (!string_ok(a[2]))
            return InvalidParameter;
        char file[260];
        if (files.FullPath(ReadString(a[2]), file))
            snprintf(file, sizeof file, "%s", ReadString(a[2]));
        if (a[0] != 0x80000002u && a[0] != 0x80000003u)
            return BadKey;
        char hive[256];
        if (!a[1] || !virtual_memory.Access(a[1], 1, false) || !HiveName(ReadString(a[1]), hive))
            return BadKey;
        unsigned parent = 0;
        registry.Key(a[0], parent);
        unsigned existing = 0;
        bool created = false;
        if (!store.Open(parent, hive, false, existing, created))
        {
            store.Release(existing);
            return BadKey;
        }
        // 0xD038F2: the file must be there and be a hive. It is read whole,
        // checked, and only then does a key appear.
        unsigned char *bytes = nullptr;
        unsigned size = 0;
        if (unsigned r = HiveReadFile(file, bytes, size))
            return r;
        CountSink count;
        unsigned sink_error = 0;
        unsigned r = Win98Creg::Read(bytes, size, count, sink_error);
        if (r)
        {
            delete[] bytes;
            return r;
        }
        Mount *slot = nullptr;
        for (auto &mount : mounts)
            if (!mount.path[0])
            {
                slot = &mount;
                break;
            }
        if (!slot)
        {
            delete[] bytes;
            return 14;
        }
        char parent_path[260];
        store.Path(parent, parent_path, sizeof parent_path);
        snprintf(slot->path, sizeof slot->path, "%s\\%s", parent_path, hive);
        snprintf(slot->file, sizeof slot->file, "%s", file);
        snprintf(slot->name, sizeof slot->name, "%s", hive);
        slot->root = a[0];
        Registry::Mount(slot->path, true);
        unsigned node = 0;
        r = store.Open(parent, hive, true, node, created);
        if (!r)
        {
            StoreSink sink(store, node);
            r = Win98Creg::Read(bytes, size, sink, sink_error);
            if (r)
            {
                store.Release(node);
                store.DeleteKey(parent, hive);
            }
            else
                slot->node = node; // the mount keeps the reference of the open
        }
        delete[] bytes;
        if (r)
        {
            Registry::Mount(slot->path, false);
            *slot = {};
            return r == 8 ? BadDatabase : r;
        }
        return 0;
    }
    // then VMM 0xD04206. The hive is flushed, whatever the flush
    // answers, and goes; handles into it are not checked.
    API_CASE("RegUnLoadKeyA")
    {
        if (!LocalKey(registry, a[0]))
            return NoProvider;
        if (a[0] != 0x80000002u && a[0] != 0x80000003u)
            return BadKey;
        char hive[256];
        if (!a[1] || !virtual_memory.Access(a[1], 1, false) || !HiveName(ReadString(a[1]), hive))
            return BadKey;
        for (auto &mount : mounts)
            if (mount.path[0] && mount.root == a[0] && SameName(mount.name, hive))
            {
                HiveFlush(mount.node);
                unsigned parent = 0;
                registry.Key(a[0], parent);
                store.Release(mount.node);
                store.DeleteKey(parent, hive);
                Registry::Mount(mount.path, false);
                mount = {};
                return 0;
            }
        return BadKey;
    }
    // then VMM 0xD04517 over 0xD0449E. Any key may be saved; an
    // existing file is overwritten. The security attributes are not read.
    API_CASE("RegSaveKeyA")
    {
        if (!LocalKey(registry, a[0]))
            return NoProvider;
        if (!string_ok(a[1]))
            return InvalidParameter;
        char file[260];
        if (files.FullPath(ReadString(a[1]), file))
            snprintf(file, sizeof file, "%s", ReadString(a[1]));
        unsigned node = 0;
        if (unsigned r = registry.Key(a[0], node))
            return r == 6 ? BadKey : r;
        return HiveSave(node, file, 0x80);
    }
    // then VMM 0xD04668.
    API_CASE("RegReplaceKeyA")
    {
        if (!LocalKey(registry, a[0]))
            return NoProvider;
        if (!string_ok(a[2]) || !string_ok(a[3]))
            return InvalidParameter;
        if (a[1] && !virtual_memory.Access(a[1], 1, false))
            return InvalidParameter;
        char fresh[260], old[260];
        if (files.FullPath(ReadString(a[2]), fresh))
            snprintf(fresh, sizeof fresh, "%s", ReadString(a[2]));
        if (files.FullPath(ReadString(a[3]), old))
            snprintf(old, sizeof old, "%s", ReadString(a[3]));
        return HiveReplace(a[0], a[1] ? ReadString(a[1]) : nullptr, fresh, old);
    }
    // then VMM 0xD03D58.
    API_CASE("RegQueryMultipleValuesA")
    {
        const unsigned list = a[1], count = a[2], buffer = a[3], total = a[4];
        if (!virtual_memory.Access(list, count * 16, true) || !virtual_memory.Access(total, 4, true))
            return InvalidParameter;
        const unsigned capacity = Read32(total);
        if (capacity && !virtual_memory.Access(buffer, capacity, true))
            return InvalidParameter;
        if (!LocalKey(registry, a[0]))
            return NoProvider;
        for (unsigned i = 0; i < count; ++i)
            if (!string_ok(Read32(list + i * 16)))
                return InvalidParameter;
        unsigned node = 0;
        if (unsigned r = registry.Key(a[0], node))
            return r == 6 ? BadKey : r;
        // Pass one: every value, its type and length; the sum goes out even
        // when it does not fit.
        unsigned sum = 0;
        for (unsigned i = 0; i < count; ++i)
        {
            const unsigned entry = list + i * 16;
            const Registry::Value *value = nullptr;
            if (store.Query(node, ReadString(Read32(entry)), value))
                return 2;
            Write32(entry + 4, value->size);
            Write32(entry + 8, 0);
            Write32(entry + 12, value->type);
            sum += value->size;
        }
        Write32(total, sum);
        if (sum > capacity)
            return MoreData;
        // Pass two: the data back to back in list order, without alignment.
        unsigned at = buffer;
        for (unsigned i = 0; i < count; ++i)
        {
            const unsigned entry = list + i * 16;
            const Registry::Value *value = nullptr;
            if (store.Query(node, ReadString(Read32(entry)), value) ||
                (value->size && !Store(at, value->data, value->size)))
                return CannotRead;
            Write32(entry + 8, at);
            at += value->size;
        }
        return 0;
    }
    // then VMM 0xD036C0: HKEY_CURRENT_USER may move to a key that
    // was opened through HKEY_USERS, HKEY_CURRENT_CONFIG to one opened
    // through HKEY_LOCAL_MACHINE. The root slot keeps its own reference, and
    // every handle opened through the old root turns stale.
    API_CASE("RegRemapPreDefKey")
    {
        unsigned node = 0;
        if (unsigned r = registry.Key(a[0], node))
            return r == 6 ? BadKey : r;
        const unsigned through = registry.Root(a[0]);
        unsigned slot = 0;
        if (a[1] == 0x80000001u && through == 4)
            slot = 1;
        else if (a[1] == 0x80000005u && through == 3)
            slot = 5;
        else
            return BadKey;
        ++store.nodes[node].references;
        const unsigned previous = store.roots[slot];
        store.roots[slot] = node;
        ++store.root_epoch[slot];
        if (previous != node && store.nodes[previous].references)
            store.Release(previous);
        return 0;
    }
    default:
        break;
    }
    Fail(name);
    return 0;
}

// RegReplaceKey, VMM 0xD04668. The key must lie directly below the root of
// HKEY_LOCAL_MACHINE or HKEY_USERS. The present subtree is saved into the old
// file at once; the new content goes into a copy of the hive file whose name
// ends in "~~R" instead of its last three characters, and the hive is flagged
// for the end of the session, where SystemExitHives swaps the files.
unsigned Application::HiveReplace(unsigned key, const char *subkey, const char *fresh, const char *old)
{
    auto &store = registry.store;
    unsigned node = 0;
    if (unsigned r = registry.Key(key, node))
        return r == 6 ? BadKey : r;
    unsigned target = node;
    bool created = false;
    if (subkey && *subkey)
    {
        if (unsigned r = store.Open(node, subkey, false, target, created))
            return r;
        store.Release(target);
    }
    const unsigned parent = store.nodes[target].parent;
    const char *hive_file = nullptr;
    if (parent == store.roots[2])
        hive_file = "C:\\WINDOWS\\SYSTEM.DAT";
    else if (parent == store.roots[3])
        hive_file = "C:\\WINDOWS\\USER.DAT";
    else
        return InvalidParameter;
    if (unsigned r = HiveSave(target, old, 0x80))
        return r;
    char temporary[260];
    snprintf(temporary, sizeof temporary, "%s", hive_file);
    const unsigned length = (unsigned)strlen(temporary);
    memcpy(temporary + length - 3, "~~R", 3);
    char absolute[260];
    unsigned exists = 0;
    const bool had_copy = !files.FullPath(temporary, absolute) && !files.store.Lookup(absolute, exists);
    const auto fail = [&](unsigned code) {
        if (!had_copy && !files.FullPath(temporary, absolute))
            files.store.Remove(absolute, false);
        if (!files.FullPath(old, absolute))
            files.store.Remove(absolute, false);
        return code;
    };
    // The replacement content.
    unsigned char *fresh_bytes = nullptr, *hive_bytes = nullptr;
    unsigned fresh_size = 0, hive_size = 0, sink_error = 0;
    if (unsigned r = HiveReadFile(fresh, fresh_bytes, fresh_size))
        return fail(r);
    if (unsigned r = HiveReadFile(had_copy ? temporary : hive_file, hive_bytes, hive_size))
    {
        delete[] fresh_bytes;
        return fail(r);
    }
    // The copy without the key, then the key with the new content: a writer
    // fed from both hives. A key of the copy whose ancestor is the replaced
    // key is left out.
    struct CopySink : Win98Creg::Sink
    {
        Win98Creg::Writer &writer;
        const char *skip;
        unsigned *map = nullptr, *skipped = nullptr, count = 0, capacity = 0, root;
        CopySink(Win98Creg::Writer &w, const char *s, unsigned r) : writer(w), skip(s), root(r) {}
        ~CopySink() { delete[] map; delete[] skipped; }
        unsigned Key(unsigned parent, const char *name, unsigned length) override
        {
            if (count == capacity)
            {
                capacity = capacity ? capacity * 2 : 256;
                auto *m = new unsigned[capacity], *s = new unsigned[capacity];
                if (count) memcpy(m, map, count * 4), memcpy(s, skipped, count * 4);
                delete[] map; delete[] skipped;
                map = m; skipped = s;
            }
            char text[256];
            if (length > 255) length = 255;
            memcpy(text, name, length);
            text[length] = 0;
            const bool gone = parent == ~0u ? skip && SameName(text, skip) : skipped[parent] != 0;
            skipped[count] = gone;
            map[count] = gone ? ~0u : writer.Key(parent == ~0u ? root : map[parent], name, length);
            return count++;
        }
        bool Value(unsigned key, const char *name, unsigned nl, unsigned type, const unsigned char *data,
                   unsigned size) override
        {
            if (key != ~0u && skipped[key]) return true;
            return writer.Value(key == ~0u ? root : map[key], name, nl, type, data, size);
        }
    };
    Win98Creg::Writer writer;
    const unsigned root = writer.Key(~0u, "", 0);
    const char *name = store.nodes[target].name;
    unsigned r = 0;
    {
        CopySink copy(writer, name, root);
        r = Win98Creg::Read(hive_bytes, hive_size, copy, sink_error);
    }
    if (!r)
    {
        const unsigned replaced = writer.Key(root, name, (unsigned)strlen(name));
        CopySink content(writer, nullptr, replaced);
        r = Win98Creg::Read(fresh_bytes, fresh_size, content, sink_error);
    }
    delete[] fresh_bytes;
    delete[] hive_bytes;
    if (r)
        return fail(r == 8 ? BadDatabase : r);
    unsigned size = 0;
    unsigned char *bytes = writer.Finish(1, size);
    r = bytes ? HiveWriteFile(temporary, bytes, size) : IoFailed;
    delete[] bytes;
    if (r)
        return fail(r);
    // The copy keeps the attributes of the hive, read only and hidden, which
    // is the backup name used at session shutdown.
    if (!files.FullPath(temporary, absolute))
        files.store.Attributes(absolute, 3);
    // The hive is flagged for the end of the session.
    auto &hive = pending_hives[parent == store.roots[2] ? 0 : 1];
    hive.flagged = true;
    bool known = false;
    for (unsigned i = 0; i < hive.count; ++i)
        known = known || SameName(hive.keys[i], name);
    if (!known && hive.count < 8)
        snprintf(hive.keys[hive.count++], sizeof hive.keys[0], "%s", name);
    return 0;
}
// At shutdown, process every flagged hive. With a copy
// "~~R" whose attributes are exactly 3, the hive becomes "~~S", the copy
// becomes the hive, "~~S" goes, and the hive is read only and hidden again.
// When a step fails, VMM puts the hive back and gives up (3F8h). Before the
// swap the port carries the replaced keys from the copy into its registry.
void Application::SystemExitHives()
{
    Files::Session files(Files::System());
    auto &store = Registry::System();
    for (unsigned h = 0; h < 2; ++h)
    {
        auto &flagged = pending_hives[h];
        if (!flagged.flagged)
            continue;
        const PendingHive keys = flagged;
        flagged = PendingHive{};
        char hive[260], replace[260], saved[260];
        if (files.FullPath(HiveFiles[h], hive))
            continue;
        TemporaryName(hive, 'R', replace);
        TemporaryName(hive, 'S', saved);
        unsigned node = 0;
        const auto *entry = !files.store.Lookup(replace, node) ? files.store.Get(node) : nullptr;
        if (!entry || entry->attributes != 3)
            continue;
        // The replaced keys, read from the copy into the store.
        unsigned handle = 0, status = 0, size = 0, done = 0;
        if (!files.Open(replace, 1, 3, 3, 0, handle, status))
        {
            unsigned char *bytes = nullptr;
            if (!files.Size(handle, size) && size <= 64u << 20)
            {
                bytes = new unsigned char[size ? size : 1];
                if (size && (files.Read(handle, bytes, size, done) || done != size))
                {
                    delete[] bytes;
                    bytes = nullptr;
                }
            }
            files.Close(handle);
            const unsigned root = store.roots[h ? 3 : 2];
            for (unsigned i = 0; bytes && i < keys.count; ++i)
            {
                store.DeleteKey(root, keys.keys[i]);
                KeySink sink(store, root, keys.keys[i]);
                unsigned sink_error = 0;
                Win98Creg::Read(bytes, size, sink, sink_error);
            }
            delete[] bytes;
        }
        // Replace the hive and remove its temporary backup.
        files.store.Remove(saved, false);
        if (files.store.Attributes(hive, 0) || files.store.Rename(hive, saved))
        {
            files.store.Attributes(hive, 3);
            continue;
        }
        if (files.store.Attributes(replace, 0) || files.store.Rename(replace, hive))
            files.store.Rename(saved, hive);
        else
            files.store.Remove(saved, false);
        files.store.Attributes(hive, 3);
    }
}
} // namespace X86
