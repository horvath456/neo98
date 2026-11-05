// SPDX-License-Identifier: GPL-2.0-or-later
#include "registry.h"
#include "registry_hives.h"
#ifndef NEO98_NO_REGISTRY_IMAGE
#ifdef NEO98_REGISTRY_IMAGE_HEADER
#include NEO98_REGISTRY_IMAGE_HEADER
#else
#include "../build/generated/win98_registry_data.h"
#endif
#endif
#include "win98_browseui_registration.h"
#include "win98_opengl_registration.h"
#include <new>
#include <string.h>
namespace Registry
{
template <class T> bool Slots<T>::Reserve(U wanted)
{
    while ((count << BlockBits) < wanted)
    {
        if (count >= (Missing >> BlockBits))
            return false;
        auto **more = new (std::nothrow) T *[count + 1];
        auto *block = new (std::nothrow) T[BlockSize];
        if (!more || !block)
        {
            delete[] more;
            delete[] block;
            return false;
        }
        for (U i = 0; i < count; ++i)
            more[i] = blocks[i];
        more[count++] = block;
        delete[] blocks;
        blocks = more;
    }
    return true;
}
namespace
{
bool Equal(const char *a, const char *b)
{
    for (;; ++a, ++b)
    {
        unsigned x = (unsigned char)*a, y = (unsigned char)*b;
        if (x >= 'a' && x <= 'z')
            x -= 32;
        if (y >= 'a' && y <= 'z')
            y -= 32;
        if (x != y)
            return false;
        if (!x)
            return true;
    }
}
char *Copy(const char *text)
{
    unsigned n = strlen(text);
    char *copy = new char[n + 1];
    memcpy(copy, text, n + 1);
    return copy;
}
// Little-endian base-128 reader for the generated key and value streams.
struct Cursor
{
    const unsigned char *at;
    U Read()
    {
        U value = 0, shift = 0;
        for (;;)
        {
            U byte = *at++;
            value |= (byte & 0x7f) << shift;
            if (!(byte & 0x80))
                return value;
            shift += 7;
        }
    }
};
} // namespace
namespace
{
struct InitializedSystem
{
    Store *store = new Store;
    U error;
    InitializedSystem() : error(store->import_error)
    {
        if (!error)
            error = Win98BrowseUiRegistration::Install(*store);
        if (!error)
            error = Win98OpenGLRegistration::Install(*store);
        // Everything above is start-up state that every run rebuilds. From here
        // on the store records what the guest changes, and replays what an
        // earlier run recorded. src/registry_persist.cpp.
        const U restored = Restore(*store);
        if (!error)
            error = restored;
    }
};
InitializedSystem &SystemState()
{
    static InitializedSystem state;
    return state;
}
} // namespace
Store &System() { return *SystemState().store; }
U SystemInitializationError() { return SystemState().error; }
U Store::NewNode(U parent, const char *name, const char *klass)
{
    U index = Attach(parent, nullptr);
    if (index == Missing)
        return Missing;
    auto &n = nodes[index];
    n.name = Copy(name);
    n.klass = *klass ? Copy(klass) : "";
    n.owned = true;
    return index;
}
// Takes the next free slot and links it behind its parent's existing children.
// A null name leaves the caller to install an owned copy.
U Store::Attach(U parent, const char *name)
{
    U index = Missing;
    if (free_node != Missing)
    {
        index = free_node;
        free_node = nodes[index].sibling;
    }
    else
    {
        if (next_node == node_capacity)
        {
            if (!nodes.Reserve(node_capacity + 1))
                return Missing;
            node_capacity = nodes.count << nodes.BlockBits;
        }
        index = next_node++;
    }
    auto &n = nodes[index];
    n = Node{};
    n.used = true;
    n.parent = parent;
    if (name)
        n.name = name;
    if (parent != Missing)
    {
        U last = nodes[parent].child;
        if (last == Missing)
            nodes[parent].child = index;
        else
        {
            while (nodes[last].sibling != Missing)
                last = nodes[last].sibling;
            nodes[last].sibling = index;
        }
    }
    return index;
}
U Store::NewValue(U key)
{
    U index = Missing;
    if (free_value != Missing)
    {
        index = free_value;
        free_value = values[index].next;
    }
    else
    {
        if (next_value == value_capacity)
        {
            if (!values.Reserve(value_capacity + 1))
                return Missing;
            value_capacity = values.count << values.BlockBits;
        }
        index = next_value++;
    }
    auto &v = values[index];
    v = Value{};
    v.used = true;
    v.key = key;
    U last = nodes[key].values;
    if (last == Missing)
        nodes[key].values = index;
    else
    {
        while (values[last].next != Missing)
            last = values[last].next;
        values[last].next = index;
    }
    return index;
}
// Decodes the generated hive image: anchors resolve against the predefined roots,
// every other key is appended below its already created parent.
namespace
{
const Hives *start_hives = nullptr;
}
void StartFrom(const Hives *hives) { start_hives = hives; }
// The hives of the drive: src/registry_hives.h builds the list of keys, this
// hands them to the store the way Import below hands over the image.
U Store::ImportHives(const Hives &hives)
{
    using namespace HiveImport;
    hive_memory = new (std::nothrow) Arena;
    if (!hive_memory)
        return 8;
    // The lists of the import go with this function; names and payloads the
    // store points at live in hive_memory.
    auto *import = new (std::nothrow) HiveImport::Import(*hive_memory);
    if (!import)
        return 8;
    HiveImport::Import::Input input;
    input.system = hives.system;
    input.system_size = hives.system_size;
    input.user = hives.user;
    input.user_size = hives.user_size;
    input.netamd = hives.netamd;
    input.netamd_size = hives.netamd_size;
    input.netcli = hives.netcli;
    input.netcli_size = hives.netcli_size;
    U result = import->Run(input);
    U *index = result ? nullptr : new (std::nothrow) U[import->keys.count + 1];
    if (!result && !index)
        result = 8;
    U key_count = 0, value_count = 0;
    for (U i = 0; i < import->keys.count && !result; ++i)
    {
        const auto &key = import->keys[i];
        index[i] = Missing;
        if (key.removed)
            continue;
        ++key_count;
        if (key.anchor)
        {
            bool created = false;
            result = key.root >= 7 ? 13 : Open(roots[key.root], import->builder.Copy(key.path).p, true, index[i], created);
            continue;
        }
        if (key.parent == None || index[key.parent] == Missing)
        {
            result = 13;
            break;
        }
        // The predefined HKCR/HKCC aliases already have backing nodes. Fill
        // those nodes instead of hiding the imported hive behind duplicates.
        const char *name = import->builder.Copy(key.name).p;
        index[i] = Child(index[key.parent], name);
        if (index[i] == Missing)
            index[i] = Attach(index[key.parent], name);
        if (index[i] == Missing)
            result = 8;
    }
    for (U i = 0; i < import->keys.count && !result; ++i)
    {
        if (import->keys[i].removed)
            continue;
        for (U at = import->keys[i].first; at != None && !result; at = import->values[at].next)
        {
            const auto &value = import->values[at];
            const U slot = NewValue(index[i]);
            if (slot == Missing)
            {
                result = 8;
                break;
            }
            // CREG keeps text without a terminator; the store keeps one.
            const bool text = value.type == 1 || value.type == 2 || value.type == 7;
            const U size = value.size + (text && (!value.size || value.data[value.size - 1]) ? 1 : 0);
            unsigned char *data = size ? hive_memory->Take(size) : nullptr;
            if (size && !data)
            {
                result = 8;
                break;
            }
            if (value.size)
                memcpy(data, value.data, value.size);
            if (size > value.size)
                data[value.size] = 0;
            auto &v = values[slot];
            v.name = import->builder.Copy(value.name).p;
            v.type = value.type;
            v.size = size;
            v.data = data;
            bytes += size;
            ++value_count;
        }
    }
    if (!result && hive_memory->failed)
        result = 8;
    start.source = 2;
    start.keys = key_count;
    start.values = value_count;
    start.network = import->network;
    delete[] index;
    delete import;
    return result;
}
#ifdef NEO98_NO_REGISTRY_IMAGE
namespace Win98RegistryData
{
static constexpr unsigned KeyCount = 0, ValueCount = 0;
}
U Store::Import() { return 0; }
#else
U Store::Import()
{
    using namespace Win98RegistryData;
    start.source = 1;
    start.keys = KeyCount;
    start.values = ValueCount;
    start.network = true;
    U *index = new U[KeyCount];
    Cursor keys{Keys}, stream{Values};
    U result = 0;
    for (U i = 0; i < KeyCount && !result; ++i)
    {
        U delta = keys.Read();
        if (!delta)
        {
            U root = keys.Read(), path = keys.Read();
            keys.Read();
            bool created = false;
            if (root >= 7)
                result = 13;
            else
                result = Open(roots[root], (const char *)Anchors + path, true, index[i], created);
            continue;
        }
        if (delta > i)
        {
            result = 13;
            break;
        }
        const char *name = (const char *)Names + keys.Read();
        index[i] = Child(index[i - delta], name);
        if (index[i] == Missing)
            index[i] = Attach(index[i - delta], name);
        if (index[i] == Missing)
            result = 8;
    }
    const unsigned char *payload = Payloads;
    for (U i = 0, key = 0; i < ValueCount && !result; ++i)
    {
        key += stream.Read();
        U name = stream.Read(), type = *stream.at++, size = stream.Read();
        if (key >= KeyCount)
        {
            result = 13;
            break;
        }
        U slot = NewValue(index[key]);
        if (slot == Missing)
        {
            result = 8;
            break;
        }
        auto &v = values[slot];
        v.name = (const char *)Names + name;
        v.type = type;
        v.size = size;
        v.data = size ? payload : nullptr;
        payload += size;
        bytes += size;
    }
    delete[] index;
    return result;
}
#endif
Store::Store()
{
    // The installed hives decide the first size; guest writes add blocks.
    const Hives *hives = start_hives;
    nodes.Reserve((hives ? 0 : Win98RegistryData::KeyCount) + 1024);
    values.Reserve((hives ? 0 : Win98RegistryData::ValueCount) + 1024);
    node_capacity = nodes.count << nodes.BlockBits;
    value_capacity = values.count << values.BlockBits;
    U machine = NewNode(Missing, "HKEY_LOCAL_MACHINE"), users = NewNode(Missing, "HKEY_USERS");
    roots[2] = machine;
    roots[3] = users;
    roots[4] = NewNode(Missing, "HKEY_PERFORMANCE_DATA");
    roots[6] = NewNode(Missing, "HKEY_DYN_DATA");
    bool created = false;
    Open(machine, "Software\\Classes", true, roots[0], created);
    Open(users, ".Default", true, roots[1], created);
    Open(machine, "Config\\0001", true, roots[5], created);
    import_error = hives ? ImportHives(*hives) : Import();
    PublishDevnodes(*this);
    // The only remaining seed: this port's own description, which no hive holds.
    U key = 0;
    Open(machine, "Software\\NEO98", true, key, created);
    const char architecture[] = "ARM64", execution[] = "Unicorn x86 translation",
               storage[] = "Native RAM registry imported from the installed Win98 hives";
    Set(key, "Architecture", 1, architecture, sizeof architecture);
    Set(key, "Execution", 1, execution, sizeof execution);
    Set(key, "Storage", 1, storage, sizeof storage);
}
Store::~Store()
{
    for (U i = 0; i < next_value; ++i)
        if (values[i].owned)
        {
            delete[] const_cast<char *>(values[i].name);
            delete[] const_cast<unsigned char *>(values[i].data);
        }
    for (U i = 0; i < next_node; ++i)
        if (nodes[i].owned)
        {
            delete[] const_cast<char *>(nodes[i].name);
            if (*nodes[i].klass)
                delete[] const_cast<char *>(nodes[i].klass);
        }
    delete hive_memory;
}
// "HKEY_LOCAL_MACHINE\\Software\\Microsoft", built from the topmost ancestor
// down. The persisted journal names every key this way.
bool Store::Path(U node, char *out, U capacity) const
{
    if (node >= node_capacity || !nodes[node].used || !out || capacity < 2)
        return false;
    const char *parts[64];
    U count = 0;
    for (U i = node; i != Missing && count < 64; i = nodes[i].parent)
        parts[count++] = nodes[i].name;
    if (count == 64)
        return false;
    U end = 0;
    while (count)
    {
        const char *part = parts[--count];
        const U size = (U)strlen(part);
        if (end + (end ? 1u : 0u) + size + 1 > capacity)
            return false;
        if (end)
            out[end++] = '\\';
        memcpy(out + end, part, size);
        end += size;
    }
    out[end] = 0;
    return true;
}
// The key one journal path starts at, by the name of its topmost ancestor.
U Store::Anchor(const char *path) const
{
    if (!path)
        return Missing;
    U length = 0;
    while (path[length] && path[length] != '\\')
        ++length;
    for (U i = 0; i < next_node; ++i)
        if (nodes[i].used && !nodes[i].deleted && nodes[i].parent == Missing &&
            strlen(nodes[i].name) == length && !strncmp(nodes[i].name, path, length))
            return i;
    return Missing;
}
U Store::Child(U parent, const char *name) const
{
    for (U i = nodes[parent].child; i != Missing; i = nodes[i].sibling)
        if (nodes[i].used && !nodes[i].deleted && Equal(nodes[i].name, name))
            return i;
    return Missing;
}
U Store::Open(U parent, const char *path, bool create, U &node, bool &created, const char *klass)
{
    created = false;
    if (parent >= node_capacity || !nodes[parent].used)
        return 6;
    if (nodes[parent].deleted)
        return 1018;
    if (!path)
        path = "";
    if (!klass)
        klass = "";
    if (strlen(path) > 4095 || strlen(klass) >= 128)
        return 87;
    // Validate the whole path before creating any intermediate key.
    for (const char *p = path; *p;)
    {
        unsigned n = 0;
        while (*p && *p != '\\')
        {
            ++n;
            ++p;
        }
        if (n > 255)
            return 87;
        if (*p)
            ++p;
    }
    U current = parent;
    for (const char *p = path; *p;)
    {
        while (*p == '\\')
            ++p;
        if (!*p)
            break;
        char part[256];
        U n = 0;
        while (*p && *p != '\\')
            part[n++] = *p++;
        part[n] = 0;
        U next = Child(current, part);
        if (next == Missing)
        {
            if (!create)
                return 2;
            const char *rest = p;
            while (*rest == '\\')
                ++rest;
            next = NewNode(current, part, *rest ? "" : klass);
            if (next == Missing)
                return 8;
            created = true;
            char full[1024];
            if (Path(next, full, sizeof full))
                Note(1, full, nullptr, 0, nullptr, 0);
        }
        else
            created = false;
        current = next;
    }
    node = current;
    return 0;
}
U Store::EnumKey(U parent, U index, U &node) const
{
    if (parent >= node_capacity || !nodes[parent].used)
        return 6;
    for (U i = nodes[parent].child; i != Missing; i = nodes[i].sibling)
        if (nodes[i].used && !nodes[i].deleted && !index--)
        {
            node = i;
            return 0;
        }
    return 259;
}
U Store::EnumValue(U key, U index, const Value *&out) const
{
    if (key < node_capacity && nodes[key].used)
        for (U i = nodes[key].values; i != Missing; i = values[i].next)
            if (values[i].used && !index--)
            {
                out = &values[i];
                return 0;
            }
    out = nullptr;
    return 259;
}
U Store::Query(U key, const char *name, const Value *&out) const
{
    if (!name)
        name = "";
    if (key < node_capacity && nodes[key].used)
        for (U i = nodes[key].values; i != Missing; i = values[i].next)
            if (values[i].used && Equal(values[i].name, name))
            {
                out = &values[i];
                return 0;
            }
    out = nullptr;
    return 2;
}
// Frees the payload of a value without touching its position in the key's chain.
void Store::Clear(Value &v)
{
    bytes -= v.size;
    if (v.owned)
    {
        delete[] const_cast<char *>(v.name);
        delete[] const_cast<unsigned char *>(v.data);
    }
    U key = v.key, next = v.next;
    v = Value{};
    v.key = key;
    v.next = next;
    v.used = true;
}
U Store::Set(U key, const char *name, U type, const void *data, U size)
{
    if (key >= node_capacity || !nodes[key].used)
        return 6;
    if (nodes[key].deleted)
        return 1018;
    if (!name)
        name = "";
    if (strlen(name) > 255 || size > 65536 || (size && !data))
        return 87;
    U slot = Missing;
    for (U i = nodes[key].values; i != Missing; i = values[i].next)
        if (values[i].used && Equal(values[i].name, name))
        {
            slot = i;
            break;
        }
    bool fresh = slot == Missing;
    if (fresh)
        slot = NewValue(key);
    if (slot == Missing || size > ~0u - (bytes - values[slot].size))
    {
        if (fresh && slot != Missing)
            DropValue(slot);
        return 8;
    }
    unsigned char *copy = size ? new unsigned char[size] : nullptr;
    char *label = Copy(name);
    if ((size && !copy) || !label)
    {
        delete[] copy;
        delete[] label;
        if (fresh)
            DropValue(slot);
        return 8;
    }
    if (size)
        memcpy(copy, data, size);
    auto &target = values[slot];
    Clear(target);
    target.type = type;
    target.size = size;
    target.data = copy;
    target.name = label;
    target.owned = true;
    bytes += size;
    char full[1024];
    if (Path(key, full, sizeof full))
        Note(3, full, name, type, data, size);
    return 0;
}
// Unlinks a value from its key, releases its payload and returns the slot.
void Store::DropValue(U index)
{
    U key = values[index].key;
    if (key < node_capacity)
    {
        U *link = &nodes[key].values;
        while (*link != Missing && *link != index)
            link = &values[*link].next;
        if (*link == index)
            *link = values[index].next;
    }
    Clear(values[index]);
    values[index] = Value{};
    values[index].next = free_value;
    free_value = index;
}
U Store::DeleteValue(U key, const char *name)
{
    if (!name)
        name = "";
    if (key < node_capacity && nodes[key].used)
        for (U i = nodes[key].values; i != Missing; i = values[i].next)
            if (values[i].used && Equal(values[i].name, name))
            {
                DropValue(i);
                char full[1024];
                if (Path(key, full, sizeof full))
                    Note(4, full, name, 0, nullptr, 0);
                return 0;
            }
    return 2;
}
void Store::Recycle(U node)
{
    if (nodes[node].owned)
    {
        delete[] const_cast<char *>(nodes[node].name);
        if (*nodes[node].klass)
            delete[] const_cast<char *>(nodes[node].klass);
    }
    nodes[node] = Node{};
    nodes[node].sibling = free_node;
    free_node = node;
}
void Store::Release(U node)
{
    if (node < node_capacity && nodes[node].references)
        --nodes[node].references;
    if (node < node_capacity && nodes[node].deleted && !nodes[node].references)
        Recycle(node);
}
void Store::Unlink(U node)
{
    U parent = nodes[node].parent;
    if (parent == Missing || parent >= node_capacity)
        return;
    U *link = &nodes[parent].child;
    while (*link != Missing && *link != node)
        link = &nodes[*link].sibling;
    if (*link == node)
        *link = nodes[node].sibling;
    nodes[node].sibling = Missing;
    nodes[node].parent = Missing;
}
void Store::Remove(U node)
{
    while (nodes[node].child != Missing)
        Remove(nodes[node].child);
    while (nodes[node].values != Missing)
        DropValue(nodes[node].values);
    Unlink(node);
    nodes[node].deleted = true;
    if (!nodes[node].references)
        Recycle(node);
}
U Store::DeleteKey(U parent, const char *path)
{
    // Win98 accepts an empty subkey to delete the key named by the handle.
    // Keep the root/alias protection below for that case as well.
    if (!path) return 87;
    U node = 0;
    bool created = false;
    U result = Open(parent, path, false, node, created);
    if (result)
        return result;
    // Never remove a predefined root or an ancestor of one of its aliases.
    for (U root : roots)
        for (U n = root; n != Missing; n = nodes[n].parent)
            if (n == node)
                return 5;
    char full[1024];
    const bool named = Path(node, full, sizeof full);
    Remove(node);
    if (named)
        Note(2, full, nullptr, 0, nullptr, 0);
    return 0;
}
U Session::Key(U handle, U &node) const
{
    if (handle >= 0x80000000 && handle <= 0x80000006)
        node = store.roots[handle - 0x80000000];
    else
    {
        const auto *entry = Find(handle);
        if (!entry)
            return 6;
        // A handle opened through a root that RegRemapPreDefKey has moved
        // since is stale: VMM 0x6021A4 marks it, and its next use is
        // ERROR_BADKEY.
        if (entry->root && store.root_epoch[entry->root - 1] != entry->epoch)
            return 1010;
        node = entry->node;
    }
    return store.nodes[node].deleted ? 1018 : 0;
}
U Session::Root(U handle) const
{
    if (handle >= 0x80000000 && handle <= 0x80000006)
        return handle - 0x80000000 + 1;
    const auto *entry = Find(handle);
    return entry ? entry->root : 0;
}
const Session::Handle *Session::Find(U handle) const
{
    if ((handle & 0xf0000000) != 0x50000000) return nullptr;
    for (U i = 0; i < handle_capacity; ++i)
        if (handles[i].used && handles[i].token == handle) return &handles[i];
    return nullptr;
}
bool Session::GrowHandles()
{
    if (handle_capacity > (~0u / sizeof(Handle)) / 2) return false;
    const U capacity = handle_capacity ? handle_capacity * 2 : 128;
    auto *grown = new (std::nothrow) Handle[capacity];
    if (!grown) return false;
    for (U i = 0; i < handle_capacity; ++i) grown[i] = handles[i];
    delete[] handles;
    handles = grown;
    handle_capacity = capacity;
    return true;
}
U Session::Open(U parent, const char *path, bool create, U &handle, bool &created, const char *klass)
{
    U key = 0;
    U result = Key(parent, key);
    if (result)
        return result;
    if ((!path || !*path) && parent >= 0x80000000 && parent <= 0x80000006)
    {
        handle = parent;
        created = false;
        return 0;
    }
    U node = 0;
    result = store.Open(key, path, create, node, created, klass);
    if (result) return result;
    U slot = handle_capacity;
    for (U i = 0; i < handle_capacity; ++i)
        if (!handles[i].used)
        {
            slot = i;
            break;
        }
    // dxdiag retains more than 128 registry handles while gathering device
    // information. The handle id is independent of the growing table, so
    // moving storage and reusing a slot never revive a closed handle.
    if (next_handle > 0x07ffffff || (slot == handle_capacity && !GrowHandles())) return 8;
    auto &h = handles[slot];
    h.used = true;
    h.node = node;
    h.root = Root(parent);
    h.epoch = h.root ? store.root_epoch[h.root - 1] : 0;
    ++store.nodes[node].references;
    handle = h.token = 0x50000000 | (next_handle++ << 1);
    return 0;
}
U Session::Close(U handle)
{
    if (handle >= 0x80000000 && handle <= 0x80000006)
        return 0;
    auto *entry = const_cast<Handle *>(Find(handle));
    if (!entry) return 6;
    auto &h = *entry;
    store.Release(h.node);
    h.used = false;
    return 0;
}
Session::~Session()
{
    for (U i = 0; i < handle_capacity; ++i)
    {
        auto &h = handles[i];
        if (h.used)
            store.Release(h.node);
    }
    delete[] handles;
}
} // namespace Registry
