// SPDX-License-Identifier: GPL-2.0-or-later
#include "files.h"
#include "system_tree.h"
#include "volume.h"
#include "win98_cache_pool.h"
#include "win98_vmm_bitmap.h"
#include <new>
#include <stdio.h>
#include <string.h>
namespace Files
{
namespace
{
struct CachePool {
 Win98CachePool::State state;
 unsigned bitmap[4]{},next[128]{},head=0,loans=0;unsigned char*pages[128]{};bool queued=false;
 ~CachePool(){for(auto*p:pages)delete[]p;}
 void Schedule(){queued=true;}
 unsigned Claim(unsigned capacity){return Win98VmmBitmap::Claim(bitmap,capacity);}
 void Release(unsigned slot){Win98VmmBitmap::Release(bitmap,slot);}
 bool Commit(unsigned slot){pages[slot-1]=new unsigned char[4096];return pages[slot-1]!=nullptr;}
 // Prepend a page to the VCACHE free list.
 void Publish(unsigned slot){next[slot-1]=head;head=slot;}
};
CachePool& Cache(){static CachePool pool;return pool;}
TimeStamp(*clock_provider)()=nullptr;
// Before the host installs its clock, seeded RAM files use the FAT epoch.
// A zero DOS date has no month/day and breaks programs preserving timestamps.
TimeStamp CurrentTime(){return clock_provider?clock_provider():TimeStamp{0x21,0,0};}
TimeStamp AccessTime(TimeStamp value){value.time=0;value.hundredths=0;return value;}
TimeStamp WriteTime(TimeStamp value){value.hundredths=0;return value;}
unsigned Fold(unsigned c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }
bool Equal(const char *a, const char *b)
{
    while (*a && Fold((unsigned char)*a) == Fold((unsigned char)*b))
        ++a, ++b;
    return *a == *b;
}
bool Device(const char *name)
{
    char base[5] = {};
    U n = 0;
    while (name[n] && name[n] != '.' && n < 4)
    {
        base[n] = Fold((unsigned char)name[n]);
        ++n;
    }
    if (name[n] && name[n] != '.')
        return false;
    return Equal(base, "CON") || Equal(base, "PRN") || Equal(base, "AUX") || Equal(base, "NUL") ||
           (n == 4 && base[3] >= '1' && base[3] <= '9' &&
            ((!strncmp(base, "COM", 3)) || (!strncmp(base, "LPT", 3))));
}
U configured_nodes = NodeCapacity, configured_bytes = DataCapacity;
} // namespace
bool IsDeviceName(const char *name) { return Device(name); }
void Configure(U nodes, U bytes)
{
    if (nodes >= NodeCapacity)
        configured_nodes = nodes;
    if (bytes >= DataCapacity)
        configured_bytes = bytes;
}
void RequestCachePages(){auto&pool=Cache();Win98CachePool::Request(pool.state,pool);}
void ServiceCachePages(){auto&pool=Cache();if(pool.queued){pool.queued=false;Win98CachePool::Fill(pool.state,pool);}}
unsigned CacheResidentPages(){return Cache().state.resident;}
unsigned CacheBufferLoans(){return Cache().loans;}
unsigned char* AcquireBuffer(U size){
 auto&pool=Cache();if(size&&size<=4096&&pool.head){unsigned slot=pool.head;pool.head=pool.next[slot-1];++pool.loans;return pool.pages[slot-1];}
 return size?new(std::nothrow) unsigned char[size]:nullptr;
}
void ReleaseBuffer(unsigned char*buffer){
 if(!buffer)return;auto&pool=Cache();for(unsigned i=0;i<128;++i)if(pool.pages[i]==buffer){pool.Publish(i+1);return;}delete[]buffer;
}
void SetClock(TimeStamp(*provider)()){clock_provider=provider;}
Store::Store()
{
    capacity = configured_nodes;
    data_capacity = configured_bytes;
    nodes = new Node[capacity];
    nodes[0].used = true;
    nodes[0].attributes = 16;
    strcpy(nodes[0].name, "C:");
    // C: is always there; every other letter appears when a provider was
    // mounted for it before this store was built. src/volume.h.
    volumes[2] = Drives{0, Volume::Mounted('C'), Volume::Type('C'), Volume::Writable('C')};
    volumes[2].live = volumes[2].backend && volumes[2].backend->Volatile();
    for (U i = 0; i < 26; ++i)
    {
        const char letter = char('A' + i);
        auto *backend = Volume::Mounted(letter);
        if (i == 2 || !Volume::Exists(letter))
            continue;
        U node = Invalid;
        for (U n = 1; n < capacity; ++n)
            if (!nodes[n].used)
            {
                node = n;
                break;
            }
        if (node == Invalid)
            break;
        auto &root = nodes[node];
        root = Node{};
        root.used = true;
        root.attributes = 16;
        root.identity = next_identity++;
        root.name[0] = letter;
        root.name[1] = ':';
        volumes[i] = Drives{node, backend, Volume::Type(letter), Volume::Writable(letter), backend != nullptr};
        volumes[i].live = backend && backend->Volatile();
    }
    if (volumes[2].backend)
        snprintf(label, sizeof label, "%s", volumes[2].backend->Label());
    // The folders Win98 Setup leaves on every machine that the runtime needs
    // before any program runs; on the mounted Win98 folder they exist
    // already. TEMP and TMP name C:\WINDOWS\TEMP as IO.SYS sets them without
    // an AUTOEXEC.BAT line (src/x86_environment.cpp). No C:\TEMP and no
    // C:\Documents: Win98 has neither.
    MakeDirectory("C:\\WINDOWS");
    MakeDirectory("C:\\WINDOWS\\SYSTEM");
    MakeDirectory("C:\\WINDOWS\\TEMP");
}
Store::~Store()
{
    for (U i = 0; i < capacity; ++i)
        delete[] nodes[i].data;
    delete[] nodes;
}
Store &System()
{
    static Store system;
    return system;
}
const Node *Store::Get(U n) const { return n < capacity && nodes[n].used && !nodes[n].tombstone ? &nodes[n] : nullptr; }
void Store::DataSpace(U&total,U&available)const
{
    // A host-backed C: reports the medium, capped to what a 32-bit byte count
    // can name; the RAM volume reports its own quota.
    if (volumes[2].backend)
    {
        unsigned long long whole = 0, free = 0;
        DriveSpace('C', whole, free);
        const unsigned long long cap = 0x7ffff000ull;
        total = U(whole > cap ? cap : whole);
        available = U(free > cap ? cap : free);
        return;
    }
    total=data_capacity;available=data_capacity-bytes;
}
U Store::DriveOf(U node) const
{
    if (node >= capacity || !nodes[node].used)
        return 26;
    U root = node;
    while (nodes[root].parent != Invalid)
        root = nodes[root].parent;
    for (U i = 0; i < 26; ++i)
        if (volumes[i].root == root)
            return i;
    return 26;
}
Volume::Backend *Store::Provider(U node) const
{
    const U drive = DriveOf(node);
    return drive < 26 ? volumes[drive].backend : nullptr;
}
bool Store::Persistent(U node) const
{
    const U drive = DriveOf(node);
    return drive < 26 && volumes[drive].backend && volumes[drive].writable;
}
void Store::Flush()
{
    for (auto &volume : volumes)
        if (volume.backend)
            volume.backend->Flush();
}
// A store out of a saved state (src/x86_state_machine.cpp): the handles came
// with the state, with their nodes and positions; what a provider holds for
// an open file (Volume::Backend::Retain) is of the process that saved. Every
// open handle holds its file again, as Session::Open made it.
void Store::StateBind()
{
    for (auto &h : handles)
    {
        char held[260];
        if (h.owner && h.node != Invalid && Live(h.node) && Path(h.node, held))
            Provider(h.node)->Retain(held);
    }
}
// A volatile provider (src/volume.h): its entries can change without this
// node table having done it. The drive below the write layer of a game
// machine belongs to the normal machine, and a DOS program of the game
// machine reaches the layer from the thread of its own machine. A node of
// such a drive is what the store last saw, no more: every lookup by name
// asks the provider again, and so does the start of every listing.
bool Store::Live(U node) const
{
    const U drive = DriveOf(node);
    return drive < 26 && volumes[drive].live;
}
// Takes a node the provider no longer has out of the table, with everything
// below it. A file that is open stays (its handle goes on working on what
// the provider kept for it), and with it the directories above it.
bool Store::Forget(U node) const
{
    bool kept = Opened(node);
    for (U i = 1; i < capacity; ++i)
        if (nodes[i].used && nodes[i].parent == node && !Forget(i))
            kept = true;
    if (kept)
        return false;
    Drop(node);
    nodes[node] = Node{};
    return true;
}
// What the provider says about an entry now.
void Store::Refresh(U node, const Volume::Entry &entry) const
{
    auto &n = nodes[node];
    if (!(n.attributes & 16) &&
        (entry.size != n.size || entry.times[2].date != n.times[2].date || entry.times[2].time != n.times[2].time))
    {
        Drop(node);
        n.size = entry.size;
        for (U k = 0; k < 3; ++k)
            n.times[k] = entry.times[k];
    }
    const U attributes = entry.attributes ? entry.attributes : 128;
    if ((attributes & 16) == (n.attributes & 16))
        n.attributes = attributes;
    strcpy(n.alias, entry.alias);
}
// The start of a listing: when the directory changed behind the table, every
// entry the table has of it is checked and the directory is read again.
void Store::Sync(U directory) const
{
    auto *backend = Provider(directory);
    char path[260];
    if (!backend || !Path(directory, path))
        return;
    const U stamp = backend->Stamp(path);
    if (stamp == nodes[directory].stamp)
        return;
    nodes[directory].stamp = stamp;
    for (U i = 1; i < capacity; ++i)
    {
        if (!nodes[i].used || nodes[i].parent != directory || !nodes[i].upper)
            continue;
        char child[260];
        Volume::Entry entry;
        if (!Path(i, child))
            continue;
        if (backend->Find(child, entry))
            Refresh(i, entry);
        else
            Forget(i);
    }
    nodes[directory].populated = false;
}
bool Store::Sealed(U node) const
{
    const U drive = DriveOf(node);
    return drive < 26 && volumes[drive].backend && !volumes[drive].writable;
}
bool Store::Drive(char letter) const { const U i = Fold((unsigned char)letter) - 'A'; return i < 26 && volumes[i].root != Invalid; }
U Store::DriveMask() const { U mask = 0; for (U i = 0; i < 26; ++i) if (volumes[i].root != Invalid) mask |= 1u << i; return mask; }
U Store::DriveType(char letter) const { const U i = Fold((unsigned char)letter) - 'A'; return i < 26 && volumes[i].root != Invalid ? volumes[i].type : 1; }
const char *Store::DriveLabel(char letter) const
{
    const U i = Fold((unsigned char)letter) - 'A';
    if (i >= 26 || volumes[i].root == Invalid)
        return nullptr;
    return volumes[i].backend ? volumes[i].backend->Label() : label;
}
bool Store::SetDriveLabel(char letter, const char *text)
{
    const U i = Fold((unsigned char)letter) - 'A';
    if (i >= 26 || volumes[i].root == Invalid || !volumes[i].writable)
        return false;
    if (i == 2)
        snprintf(label, sizeof label, "%s", text);
    return volumes[i].backend ? volumes[i].backend->SetLabel(text) : true;
}
U Store::DriveSerial(char letter) const
{
    const U i = Fold((unsigned char)letter) - 'A';
    return i < 26 && volumes[i].backend ? volumes[i].backend->Serial() : 0;
}
void Store::DriveSpace(char letter, unsigned long long &total, unsigned long long &available) const
{
    total = available = 0;
    const U i = Fold((unsigned char)letter) - 'A';
    if (i >= 26 || volumes[i].root == Invalid)
        return;
    if (!volumes[i].backend)
    {
        U whole = 0, free = 0;
        DataSpace(whole, free);
        total = whole;
        available = free;
        return;
    }
    volumes[i].backend->Space(total, available);
    if (!volumes[i].writable)
        available = 0;
}
bool Store::Ready(char letter) const
{
    const U i = Fold((unsigned char)letter) - 'A';
    return i < 26 && volumes[i].root != Invalid && volumes[i].present;
}
U Store::ChangeMedium(char letter)
{
    const U i = Fold((unsigned char)letter) - 'A';
    if (i >= 26 || i == 2)
        return 87;
    auto &volume = volumes[i];
    if (volume.root != Invalid)
    {
        // Everything below the root belonged to the old medium. Collect first:
        // DriveOf walks the parents, which must still be there.
        bool *gone = new bool[capacity]();
        for (U n = 1; n < capacity; ++n)
            gone[n] = nodes[n].used && n != volume.root && DriveOf(n) == i;
        for (auto &h : handles)
            if (h.owner && h.node < capacity && (gone[h.node] || h.node == volume.root))
                h.node = Invalid;
        for (auto &lock : locks)
            if (lock.owner && lock.node < capacity && gone[lock.node])
                lock = {};
        for (U n = 1; n < capacity; ++n)
            if (gone[n])
            {
                Drop(n);
                delete[] nodes[n].data;
                nodes[n] = Node{};
            }
        delete[] gone;
    }
    if (!Volume::Exists(letter))
    {
        // The letter is no drive any more.
        if (volume.root != Invalid)
            nodes[volume.root] = Node{};
        volume = Drives{};
        return 0;
    }
    if (volume.root == Invalid)
    {
        U node = Invalid;
        for (U n = 1; n < capacity; ++n)
            if (!nodes[n].used)
            {
                node = n;
                break;
            }
        if (node == Invalid)
            return 8;
        auto &root = nodes[node];
        root = Node{};
        root.used = true;
        root.attributes = 16;
        root.identity = next_identity++;
        root.name[0] = char('A' + i);
        root.name[1] = ':';
        volume.root = node;
    }
    else
    {
        // The next listing reads the new medium.
        nodes[volume.root].populated = false;
        nodes[volume.root].identity = next_identity++;
    }
    auto *backend = Volume::Mounted(letter);
    volume.backend = backend;
    volume.type = Volume::Type(letter);
    volume.writable = Volume::Writable(letter);
    volume.present = backend != nullptr;
    volume.live = backend && backend->Volatile();
    Notify(volume.root, ChangeFileName | ChangeDirName);
    return 0;
}
const char *Store::DriveDescription(char letter) const
{
    const U i = Fold((unsigned char)letter) - 'A';
    if (i >= 26 || volumes[i].root == Invalid)
        return nullptr;
    return volumes[i].backend ? volumes[i].backend->Description() : "RAM";
}
bool Store::Path(U node, char (&out)[260]) const
{
    if (node >= capacity || !nodes[node].used)
        return false;
    const char *parts[64];
    U count = 0;
    U root = node;
    while (nodes[root].parent != Invalid && count < 64)
    {
        parts[count++] = nodes[root].name;
        root = nodes[root].parent;
    }
    if (count == 64)
        return false;
    out[0] = nodes[root].name[0];
    out[1] = ':';
    out[2] = '\\';
    out[3] = 0;
    U end = 3;
    while (count)
    {
        const char *part = parts[--count];
        const U size = strlen(part);
        if (end + (end > 3) + size >= 260)
            return false;
        if (end > 3)
            out[end++] = '\\';
        memcpy(out + end, part, size + 1);
        end += size;
    }
    return true;
}
// Whether the tree is hidden in this directory: it, or a directory above it,
// was made anew where the guest had removed a directory of the tree. Nothing
// of the tree shows through below such a directory, however deep.
bool Store::TreeHidden(U directory) const
{
    for (U p = directory; p != Invalid && p < capacity; p = nodes[p].parent)
        if (nodes[p].opaque)
            return true;
    return false;
}
// Pull one entry of the mounted tree into the volume. The copy carries the
// original metadata; its bytes stay in the tree until the first read.
U Store::Borrow(U parent, const char *name) const
{
    auto *backend = SystemTree::Mounted();
    if (!backend || parent >= capacity || !nodes[parent].used || !(nodes[parent].attributes & 16) ||
        TreeHidden(parent))
        return Invalid;
    char directory[260], path[260];
    if (!Path(parent, directory) || strlen(directory) + strlen(name) + 2 > sizeof path)
        return Invalid;
    snprintf(path, sizeof path, "%s%s%s", directory, directory[strlen(directory) - 1] == '\\' ? "" : "\\", name);
    SystemTree::Entry entry;
    if (!backend->Find(path, entry))
        return Invalid;
    U node = Invalid;
    for (U i = 1; i < capacity; ++i)
        if (!nodes[i].used)
        {
            node = i;
            break;
        }
    if (node == Invalid)
        return Invalid;
    auto &n = nodes[node];
    n = Node{};
    n.used = true;
    n.identity = next_identity++;
    n.parent = parent;
    n.attributes = entry.attributes ? entry.attributes : 128;
    n.size = entry.size;
    n.origin = true;
    n.lower = true;
    strcpy(n.name, entry.name);
    strcpy(n.alias, entry.alias);
    for (U i = 0; i < 3; ++i)
        n.times[i] = entry.times[i];
    return node;
}
// The writable volume of this drive is the layer above the system tree. Its
// entries carry their own metadata; a tombstone record hides a tree entry the
// guest removed in an earlier run.
U Store::Adopt(U parent, const char *name) const
{
    auto *backend = Provider(parent);
    if (!backend || parent >= capacity || !nodes[parent].used || !(nodes[parent].attributes & 16))
        return Invalid;
    char directory[260], path[260];
    if (!Path(parent, directory) || strlen(directory) + strlen(name) + 2 > sizeof path)
        return Invalid;
    snprintf(path, sizeof path, "%s%s%s", directory, directory[strlen(directory) - 1] == '\\' ? "" : "\\", name);
    Volume::Entry entry;
    if (!backend->Find(path, entry))
        return Invalid;
    U node = Invalid;
    for (U i = 1; i < capacity; ++i)
        if (!nodes[i].used)
        {
            node = i;
            break;
        }
    if (node == Invalid)
        return Invalid;
    auto &n = nodes[node];
    n = Node{};
    n.used = true;
    n.identity = next_identity++;
    n.parent = parent;
    n.attributes = entry.attributes ? entry.attributes : 128;
    n.size = entry.size;
    n.upper = !entry.tombstone;
    n.tombstone = entry.tombstone;
    n.origin = entry.tombstone || entry.opaque;
    n.opaque = entry.opaque;
    // A file of the volume that replaced one of the read-only tree still has
    // to leave a tombstone when it goes, or the tree's file would come back.
    // Below an opaque directory the tree is hidden anyway.
    SystemTree::Entry lower;
    if (!n.origin && !TreeHidden(parent) && DriveOf(parent) == 2 && SystemTree::Mounted() &&
        SystemTree::Mounted()->Find(path, lower))
        n.origin = true;
    strcpy(n.name, entry.name);
    strcpy(n.alias, entry.alias);
    for (U i = 0; i < 3; ++i)
        n.times[i] = entry.times[i];
    return node;
}
void Store::Fill(U directory) const
{
    auto *backend = Provider(directory);
    if (!backend)
        return;
    char path[260];
    if (!Path(directory, path))
        return;
    Volume::Entry entry;
    for (U index = 0; backend->Child(path, index, entry); ++index)
    {
        bool known = false;
        for (U i = 1; i < capacity && !known; ++i)
            known = nodes[i].used && nodes[i].parent == directory &&
                    (Equal(nodes[i].name, entry.name) || (*nodes[i].alias && Equal(nodes[i].alias, entry.name)));
        if (!known)
            Adopt(directory, entry.name);
    }
}
void Store::Populate(U directory) const
{
    if (directory >= capacity || !nodes[directory].used || nodes[directory].populated)
        return;
    nodes[directory].populated = true;
    Fill(directory);
    auto *backend = SystemTree::Mounted();
    if (!backend || DriveOf(directory) != 2 || TreeHidden(directory))
        return;
    char path[260];
    if (!Path(directory, path))
        return;
    SystemTree::Entry entry;
    for (U index = 0; backend->Child(path, index, entry); ++index)
    {
        bool known = false;
        for (U i = 1; i < capacity && !known; ++i)
            known = nodes[i].used && nodes[i].parent == directory &&
                    (Equal(nodes[i].name, entry.name) || (*nodes[i].alias && Equal(nodes[i].alias, entry.name)));
        if (!known)
            Borrow(directory, entry.name);
    }
}
// Borrowed file bytes are a cache of the tree and can always be read again.
bool Store::Reclaim(U needed, U keep) const
{
    if (needed <= data_capacity - bytes)
        return true;
    for (U i = 1; i < capacity; ++i)
    {
        auto &n = nodes[i];
        if (i == keep || !n.used || !(n.lower || n.upper) || !n.data || Opened(i))
            continue;
        bytes -= n.size;
        delete[] n.data;
        n.data = nullptr;
        if (needed <= data_capacity - bytes)
            return true;
    }
    return needed <= data_capacity - bytes;
}
// Drops the readable copy of an entry whose bytes live in a layer below or
// above this node table. It can always be fetched again.
void Store::Drop(U node) const
{
    auto &n = nodes[node];
    if (!n.data)
        return;
    bytes -= n.size;
    delete[] n.data;
    n.data = nullptr;
}
U Store::Materialize(U node) const
{
    if (node >= capacity || !nodes[node].used)
        return 6;
    auto &n = nodes[node];
    if (!(n.lower || n.upper) || n.data || !n.size || (n.attributes & 16))
        return 0;
    char path[260];
    if (!Path(node, path))
        return 3;
    auto *tree = n.lower ? SystemTree::Mounted() : nullptr;
    auto *volume = n.upper ? Provider(node) : nullptr;
    if (!tree && !volume)
        return 3;
    if (!Reclaim(n.size, node))
        return 112;
    unsigned char *data = new unsigned char[n.size];
    if (!data)
        return 8;
    U done = 0;
    if (volume)
        volume->Read(path, 0, data, n.size, done);
    else
        done = tree->Read(path, 0, data, n.size);
    if (done != n.size)
    {
        delete[] data;
        return 1117;
    }
    n.data = data;
    bytes += n.size;
    return 0;
}
const unsigned char *Store::Bytes(U node) const
{
    if (Materialize(node))
        return nullptr;
    return node < capacity && nodes[node].used ? nodes[node].data : nullptr;
}
void Store::Tombstone(U parent, const char *name)
{
    if (!SystemTree::Mounted())
        return;
    const U borrowed = Borrow(parent, name);
    if (borrowed == Invalid)
        return;
    nodes[borrowed].tombstone = true;
    // The writable volume records the same removal, so it survives a restart.
    if (auto *backend = Persistent(parent) ? Provider(parent) : nullptr)
    {
        char path[260];
        if (Path(borrowed, path))
            backend->Remove(path, (nodes[borrowed].attributes & 16) != 0, true);
    }
}
bool Store::AddObserver(void *context, Observer observer)
{
    for (auto &w : observers)
        if (w.context == context && w.observer == observer)
            return true;
    for (auto &w : observers)
        if (!w.observer)
        {
            w.context = context;
            w.observer = observer;
            return true;
        }
    return false;
}
void Store::RemoveObserver(void *context)
{
    for (auto &w : observers)
        if (w.context == context)
            w = Watcher{};
}
// The reported node is the directory holding the changed entry, so a watcher
// can test its own directory and, walking parents, its subtree.
void Store::Notify(U directory, U change) const
{
    if (directory == Invalid)
        return;
    for (const auto &w : observers)
        if (w.observer)
            w.observer(w.context, directory, change);
}
U Store::Child(U parent, const char *name) const
{
    U alias = Invalid;
    for (U i = 1; i < capacity; ++i)
    {
        if (!nodes[i].used || nodes[i].parent != parent)
            continue;
        if (Equal(nodes[i].name, name))
            return nodes[i].tombstone ? Invalid : i;
        // A borrowed entry also answers to the 8.3 alias the tree recorded.
        if (alias == Invalid && *nodes[i].alias && Equal(nodes[i].alias, name))
            alias = nodes[i].tombstone ? Invalid : i;
    }
    if (alias != Invalid)
        return alias;
    // The writable volume wins over the read-only tree below it.
    const U adopted = Adopt(parent, name);
    if (adopted != Invalid)
        return nodes[adopted].tombstone ? Invalid : adopted;
    const U borrowed = Borrow(parent, name);
    return borrowed != Invalid && !nodes[borrowed].tombstone ? borrowed : Invalid;
}
U Store::Lookup(const char *path, U &node) const
{
    if (!path || path[1] != ':' || path[2] != '\\')
        return 3;
    const U drive = Fold((unsigned char)path[0]) - 'A';
    if (drive >= 26 || volumes[drive].root == Invalid)
        return 15;
    // An empty CD-ROM drive: ERROR_NOT_READY, for the root as for any path.
    if (!volumes[drive].present)
        return 21;
    node = volumes[drive].root;
    for (const char *p = path + 3; *p;)
    {
        if (!(nodes[node].attributes & 16))
            return 3;
        char part[256];
        U n = 0;
        while (*p && *p != '\\')
        {
            if (n == 255)
                return 206;
            part[n++] = *p++;
        }
        part[n] = 0;
        node = Child(node, part);
        if (node == Invalid)
            return *p ? 3 : 2;
        if (*p)
            ++p;
    }
    // The table knows the entry; a volatile provider says whether it is
    // still there and what it is now.
    if (volumes[drive].live && nodes[node].upper && nodes[node].parent != Invalid)
    {
        Volume::Entry entry;
        if (volumes[drive].backend->Find(path, entry))
            Refresh(node, entry);
        else if (Forget(node))
            return 2;
    }
    return 0;
}
U Store::Parent(const char *path, U &parent, const char *&name) const
{
    if (!path || strlen(path) >= 260)
        return 206;
    const char *slash = strrchr(path, '\\');
    if (!slash || !slash[1])
        return 5;
    char folder[260];
    U n = slash - path;
    if (n == 2)
        ++n;
    memcpy(folder, path, n);
    folder[n] = 0;
    U error = Lookup(folder, parent);
    if (error)
        return error == 2 ? 3 : error;
    if (!(nodes[parent].attributes & 16))
        return 3;
    name = slash + 1;
    return strlen(name) > 255 ? 206 : 0;
}
U Store::New(U parent, const char *name, U attributes, U &node)
{
    if (!*name || strlen(name) > 255)
        return 123;
    if (Sealed(parent))
        return 19;
    // A tombstone hides a tree entry the guest removed; recreating that name
    // reuses its slot, so the tree cannot reappear underneath the new file.
    bool hidden = false;
    for (U i = 1; i < capacity; ++i)
        if (nodes[i].used && nodes[i].tombstone && nodes[i].parent == parent && Equal(nodes[i].name, name))
        {
            nodes[i] = Node{};
            hidden = true;
            break;
        }
    for (U i = 1; i < capacity; ++i)
        if (!nodes[i].used)
        {
            auto &n = nodes[i];
            n = Node{};
            n.used = true;
            // The tree still holds this name: removing the new file again has
            // to leave a tombstone, or the tree entry would come back.
            n.origin = hidden;
            // A directory in place of a removed tree directory starts empty.
            n.opaque = hidden && (attributes & 16);
            n.identity = next_identity++;
            n.parent = parent;
            n.attributes = attributes;
            strcpy(n.name, name);
            n.times[0]=CurrentTime();n.times[1]=AccessTime(n.times[0]);n.times[2]=WriteTime(n.times[0]);
            // A drive with a writable volume keeps the entry there, not here.
            if (auto *backend = Persistent(parent) ? Provider(parent) : nullptr)
            {
                char path[260];
                Volume::Entry entry;
                // The guest clock owns the creation time, not the host clock.
                for (U k = 0; k < 3; ++k)
                    entry.times[k] = n.times[k];
                entry.opaque = n.opaque;
                U error = Path(i, path) ? 0u : 3u;
                if (!error)
                    error = backend->Create(path, attributes, (attributes & 16) != 0, entry);
                if (error)
                {
                    n = Node{};
                    return error;
                }
                n.upper = true;
                strcpy(n.name, entry.name);
                strcpy(n.alias, entry.alias);
                for (U k = 0; k < 3; ++k)
                    n.times[k] = entry.times[k];
            }
            node = i;
            return 0;
        }
    return 112;
}
// Copy on write: an entry that still lives in the read-only tree, or only in
// this node table, moves into the drive's writable volume before it changes.
U Store::Promote(U id)
{
    auto &n = nodes[id];
    if (n.upper || !Persistent(id))
        return 0;
    auto *backend = Provider(id);
    char path[260];
    if (!Path(id, path))
        return 3;
    Volume::Entry entry;
    // The provider keeps this alias when the directory still has it free, so a
    // file copied out of the tree answers to the same 8.3 name as before.
    snprintf(entry.alias, sizeof entry.alias, "%s", n.alias);
    for (U k = 0; k < 3; ++k)
        entry.times[k] = n.times[k];
    U error = backend->Create(path, n.attributes, (n.attributes & 16) != 0, entry);
    // The volume may hold the directory already: it made it for an entry
    // that was created below it (the provider's own bookkeeping).
    if (error == 183 && (n.attributes & 16) && backend->Find(path, entry) && !entry.tombstone)
        error = 0;
    if (error)
        return error;
    // A file of the tree: the provider copies it up itself where it can (a
    // block clone on the host), whatever its size. Its bytes are then those
    // the tree holds now.
    bool imported = false;
    if (n.lower && !(n.attributes & 16) && n.size && backend->Import(path))
    {
        Volume::Entry now;
        if (backend->Find(path, now))
        {
            Drop(id);
            n.size = now.size;
        }
        imported = true;
    }
    else if (n.lower && (error = Materialize(id)))
    {
        backend->Remove(path, (n.attributes & 16) != 0, false);
        return error;
    }
    if (n.size && !imported)
    {
        U done = 0;
        error = n.data ? backend->Write(path, 0, n.data, n.size, done) : backend->Resize(path, n.size);
        if (!error && n.data && done != n.size)
            error = 112;
        if (error)
        {
            backend->Remove(path, false, false);
            return error;
        }
    }
    backend->SetAttributes(path, n.attributes);
    backend->SetTimes(path, n.times);
    strcpy(n.alias, entry.alias);
    n.lower = false;
    n.upper = true;
    Drop(id);
    return 0;
}
U Store::PromoteTree(U id)
{
    Populate(id);
    // A node table that is full has not taken every entry of the directory
    // in; moving it then would lose the rest.
    bool room = false;
    for (U i = 1; i < capacity && !room; ++i)
        room = !nodes[i].used;
    if (!room)
        return 5;
    if (const U error = Promote(id))
        return error;
    for (U i = 1; i < capacity; ++i)
    {
        if (!nodes[i].used || nodes[i].tombstone || nodes[i].parent != id)
            continue;
        if (Opened(i))
            return 32;
        if (const U error = (nodes[i].attributes & 16) ? PromoteTree(i) : Promote(i))
            return error;
    }
    return 0;
}
U Store::Resize(U id, U size)
{
    auto &n = nodes[id];
    if (const U error = Promote(id))
        return error;
    // A file of the writable volume has no node-table size limit at all; the
    // provider owns its bytes and reports the medium's own errors.
    if (n.upper)
    {
        if (size == n.size)
            return 0;
        auto *backend = Provider(id);
        char path[260];
        if (!backend || !Path(id, path))
            return 3;
        const U error = backend->Resize(path, size);
        if (error)
            return error;
        Drop(id);
        n.size = size;
        return 0;
    }
    // Resizing makes this a RAM file. Its old bytes must be present first, so
    // that a partial overwrite keeps what the tree held.
    if (n.lower)
    {
        if (size)
        {
            const U error = Materialize(id);
            if (error)
                return error;
        }
        else if (!n.data)
            n.size = 0;
        n.lower = false;
    }
    if (size > 8 * 1024 * 1024 || size > data_capacity - (bytes - n.size))
        if (!Reclaim(size, id) || size > 8 * 1024 * 1024 || size > data_capacity - (bytes - n.size))
            return 112;
    if (size == n.size)
        return 0;
    unsigned char *data = size ? new unsigned char[size] : nullptr;
    if (size && !data)
        return 8;
    U common = size < n.size ? size : n.size;
    if (common)
        memcpy(data, n.data, common);
    if (size > common)
        memset(data + common, 0, size - common);
    bytes = bytes - n.size + size;
    delete[] n.data;
    n.data = data;
    n.size = size;
    return 0;
}
bool Store::Opened(U node) const
{
    for (const auto &h : handles)
        if (h.owner && h.node == node)
            return true;
    return false;
}
U Store::MakeDirectory(const char *path)
{
    U parent, node;
    const char *name;
    U error = Parent(path, parent, name);
    if (error)
        return error;
    if (Child(parent, name) != Invalid)
        return 183;
    error = New(parent, name, 16, node);
    if (!error)
        Notify(parent, ChangeDirName);
    return error;
}
U Store::Remove(const char *path, bool directory)
{
    U id;
    U error = Lookup(path, id);
    if (error)
        return error;
    auto &n = nodes[id];
    if (n.parent == Invalid || bool(n.attributes & 16) != directory)
        return 5;
    if (Sealed(id))
        return 19;
    if (!directory && (n.attributes & 1))
        return 5;
    if (Opened(id))
        return 32;
    if (directory && Live(id))
        Sync(id);
    Populate(id);
    for (U i = 1; i < capacity; ++i)
        if (nodes[i].used && !nodes[i].tombstone && nodes[i].parent == id)
            return 145;
    const bool borrowed = n.origin;
    if (Persistent(id))
    {
        char full[260];
        if (!Path(id, full))
            return 3;
        // `borrowed` says the read-only tree still holds this name, so the
        // provider has to leave a tombstone instead of just unlinking.
        const U e = Provider(id)->Remove(full, directory, borrowed);
        if (e)
            return e;
        Drop(id);
    }
    else
        Resize(id, 0);
    const U parent = n.parent;
    char name[256];
    strcpy(name, n.name);
    // Only tombstones are left below a directory that goes; its own tombstone
    // hides the tree below it from now on.
    if (directory)
        for (U i = 1; i < capacity; ++i)
            if (nodes[i].used && nodes[i].parent == id)
                nodes[i] = Node{};
    n = Node{};
    // The tree still holds the entry, so the slot stays as a tombstone.
    if (borrowed)
    {
        n.used = true;
        n.tombstone = true;
        n.parent = parent;
        n.attributes = directory ? 16u : 128u;
        strcpy(n.name, name);
    }
    Notify(parent, directory ? ChangeDirName : ChangeFileName);
    return 0;
}
// Pushes the node's own metadata back into the writable volume. Bytes travel
// through Read/Write; attributes and timestamps travel here.
void Store::Persist(U node) const
{
    if (node >= capacity || !nodes[node].used || !nodes[node].upper)
        return;
    auto *backend = Provider(node);
    char path[260];
    if (!backend || !Path(node, path))
        return;
    backend->SetAttributes(path, nodes[node].attributes);
    backend->SetTimes(path, nodes[node].times);
}
U Store::Attributes(const char *path, U attrs)
{
    if (attrs & ~0xb7u)
        return 120;
    U id;
    U error = Lookup(path, id);
    if (error)
        return error;
    if (Sealed(id))
        return 19;
    if ((error = Promote(id)))
        return error;
    nodes[id].attributes = (attrs & 0x27) | (nodes[id].attributes & 16);
    if (!nodes[id].attributes)
        nodes[id].attributes = 128;
    Persist(id);
    Notify(nodes[id].parent, ChangeAttributes);
    return 0;
}
U Store::Rename(const char *source, const char *destination, bool replace)
{
    U id, parent;
    const char *name;
    U error = Lookup(source, id);
    if (error)
        return error;
    if (nodes[id].parent == Invalid)
        return 5;
    error = Parent(destination, parent, name);
    if (error)
        return error;
    if (DriveOf(id) != DriveOf(parent))
        return 17;
    if (Sealed(id))
        return 19;
    U existing = Child(parent, name);
    if (existing != Invalid && existing != id && !replace)
        return 183;
    if (existing != Invalid && existing != id && ((nodes[existing].attributes & 17) || (nodes[id].attributes & 16))) return 5;
    if (existing != Invalid && existing != id && Opened(existing)) return 32;
    if (Opened(id))
        return 32;
    for (U p = parent; p != Invalid; p = nodes[p].parent)
        if (p == id)
            return 5;
    // A directory the read-only tree still holds leaves its entries behind
    // under the old tree path when it moves. On a RAM drive that is refused;
    // with a writable volume above the tree the whole directory is copied up
    // first, and the tombstone of the old name hides what the tree keeps.
    //
    bool moved_tree = false;
    if ((nodes[id].attributes & 16) && nodes[id].origin)
    {
        if (!Persistent(id) || !SystemTree::Mounted())
            return 5;
        if (const U e = PromoteTree(id))
            return e;
        moved_tree = true;
    }
    // An entry that is replaced may stand where the tree holds one; the entry
    // that takes its name then has to leave a tombstone when it goes again.
    bool replaced_tree = false;
    if (existing != Invalid && existing != id) {
        replaced_tree = nodes[existing].origin;
        if (Persistent(existing))
        {
            char victim[260];
            if (!Path(existing, victim))
                return 3;
            const U e = Provider(existing)->Remove(victim, false, nodes[existing].origin);
            if (e)
                return e;
            Drop(existing);
        }
        else { bytes -= nodes[existing].size;delete[] nodes[existing].data; }
        nodes[existing] = {};
    }
    // A tombstone under the destination name hides a tree entry the guest
    // removed. The moved entry takes that name over; Child answers with the
    // first matching slot, so the tombstone must go, and the entry inherits
    // the duty to leave one when it is removed again (SETUPAPI's
    // DeleteFile(target) + MoveFile(temp, target) when it replaces a file of
    // the tree).
    bool hidden = replaced_tree;
    for (U i = 1; i < capacity; ++i)
        if (i != id && nodes[i].used && nodes[i].tombstone && nodes[i].parent == parent && Equal(nodes[i].name, name))
        {
            nodes[i] = Node{};
            hidden = true;
        }
    const U source_parent = nodes[id].parent;
    char previous[256];
    strcpy(previous, nodes[id].name);
    const bool borrowed = nodes[id].origin;
    if (Persistent(id))
    {
        // Copy on write first: after this the entry exists in the writable
        // volume and the provider can move it there.
        if (const U e = Promote(id))
            return e;
        char from[260], to[260];
        if (!Path(id, from) || !Path(parent, to))
            return 3;
        const U end = strlen(to);
        if (end + strlen(name) + 2 > sizeof to)
            return 206;
        snprintf(to + end, sizeof to - end, "%s%s", to[end - 1] == '\\' ? "" : "\\", name);
        if (const U e = Provider(id)->Rename(from, to))
            return e;
        Drop(id);
    }
    else if (nodes[id].lower && Materialize(id))
        return 112;
    nodes[id].lower = false;
    nodes[id].origin = hidden;
    // A directory that takes the name of a removed tree directory hides what
    // the tree holds there, like one created anew.
    if (hidden && (nodes[id].attributes & 16))
        nodes[id].opaque = true;
    nodes[id].parent = parent;
    strcpy(nodes[id].name, name);
    // Under its new name the tree holds nothing of the moved directory: what
    // goes from it later needs no tombstone.
    if (moved_tree)
        for (bool changed = true; changed;)
        {
            changed = false;
            for (U i = 1; i < capacity; ++i)
            {
                if (!nodes[i].used || nodes[i].tombstone || !nodes[i].origin || i == id)
                    continue;
                for (U p = nodes[i].parent; p != Invalid; p = nodes[p].parent)
                    if (p == id)
                    {
                        nodes[i].origin = false;
                        changed = true;
                        break;
                    }
            }
        }
    // The 8.3 alias belongs to the old name; the provider issued a new one.
    nodes[id].alias[0] = 0;
    if (nodes[id].upper)
    {
        char moved[260];
        Volume::Entry record;
        if (Path(id, moved) && Provider(id)->Find(moved, record))
            strcpy(nodes[id].alias, record.alias);
    }
    // The tree still holds the old name; hide it so it does not reappear.
    // A rename that only changes case keeps the name, so it still hides
    // the tree entry itself.
    if (borrowed && !(parent == source_parent && Equal(previous, name)))
        Tombstone(source_parent, previous);
    else if (borrowed)
        nodes[id].origin = true;
    const U change = (nodes[id].attributes & 16) ? ChangeDirName : ChangeFileName;
    Notify(source_parent, change);
    if (parent != source_parent)
        Notify(parent, change);
    return 0;
}
U Store::Enumerate(U directory, U index, U &node) const
{
    if (!Get(directory) || !(nodes[directory].attributes & 16))
        return 3;
    if (!index && Live(directory))
        Sync(directory);
    Populate(directory);
    for (U i = 1; i < capacity; ++i)
        if (nodes[i].used && !nodes[i].tombstone && nodes[i].parent == directory && !index--)
        {
            node = i;
            return 0;
        }
    return 18;
}
U Store::EnumerateNext(U directory, U &cursor, U &node) const
{
    if (!Get(directory) || !(nodes[directory].attributes & 16)) return 3;
    if (!cursor && Live(directory)) Sync(directory);
    Populate(directory);
    if (!cursor) cursor=1;
    while (cursor<capacity) {
        const U i=cursor++;
        if (nodes[i].used && !nodes[i].tombstone && nodes[i].parent==directory) {
            node=i;return 0;
        }
    }
    return 18;
}
U Store::Seed(const char *absolute, const void *data, U size)
{
    Session session(*this);
    U h, status, done;
    // Seeding is the harness planting bytes, not a guest write: a read-only
    // entry of the mounted tree gives way, as a file copied over it would.
    U existing = Invalid;
    if (!Lookup(absolute, existing) && existing != Invalid && (nodes[existing].attributes & 1))
    {
        nodes[existing].attributes &= ~1u;
        // A volatile provider is asked again by the Open below.
        if (Live(existing))
            Persist(existing);
    }
    U e = session.Open(absolute, 2, 0, 2, 32, h, status);
    if (!e)
        e = session.Write(h, data, size, done);
    return e;
}
Session::~Session()
{
    for (U i = 0; i < 256; ++i)
        if (store.handles[i].owner == this)
            Close(0x40000000 | (store.handles[i].generation << 8) | i);
}
U Session::FullPath(const char *path, char (&out)[260]) const
{
    if (!path || !*path)
        return 123;
    U length = strlen(path);
    if (length >= 260)
        return 206;
    char input[520], base[260];
    const char *p = path;
    bool slash = *p == '\\' || *p == '/';
    if (slash && (p[1] == '\\' || p[1] == '/'))
        return 120; // No network namespace or device paths.
    char letter = current[0];
    if (p[1] == ':')
    {
        letter = char(Fold((unsigned char)p[0]));
        if (!store.Drive(letter))
            return 15;
        p += 2;
        slash = *p == '\\' || *p == '/';
    }
    if (slash)
    {
        base[0] = letter;
        base[1] = ':';
        base[2] = 0;
    }
    else
        Directory(letter, base);
    strcpy(input, base);
    U n = strlen(input);
    if (!slash && input[n - 1] != '\\')
        input[n++] = '\\';
    strcpy(input + n, p);
    out[0] = letter;
    out[1] = ':';
    out[2] = '\\';
    out[3] = 0;
    U end = 3;
    for (p = input + 2; *p;)
    {
        while (*p == '\\' || *p == '/')
            ++p;
        if (!*p)
            break;
        char part[256];
        U size = 0;
        while (*p && *p != '\\' && *p != '/')
        {
            unsigned char c = *p++;
            if (c < 32 || c == ':' || c == '"' || c == '*' || c == '?' || c == '<' || c == '>' || c == '|')
                return 123;
            if (size == 255)
                return 206;
            part[size++] = c;
        }
        part[size] = 0;
        if (!strcmp(part, "."))
            continue;
        if (!strcmp(part, ".."))
        {
            while (end > 3 && out[--end] != '\\')
            {
            }
            out[end] = 0;
            continue;
        }
        while (size && (part[size - 1] == ' ' || part[size - 1] == '.'))
            part[--size] = 0;
        if (!size)
            return 123;
        if (Device(part))
            return 120;
        if (end + (end > 3) + size >= 260)
            return 206;
        if (end > 3)
            out[end++] = '\\';
        memcpy(out + end, part, size + 1);
        end += size;
    }
    return 0;
}
U Session::SetDirectory(const char *path)
{
    char absolute[260];
    U id;
    U e = FullPath(path, absolute);
    if (!e)
        e = store.Lookup(absolute, id);
    if (!e && !(store.nodes[id].attributes & 16))
        e = 267;
    if (!e)
    {
        // Win98 keeps one current directory per drive per process. The one of
        // the current drive stays in `current`, where every existing reader
        // finds it; leaving a drive parks its directory in the table.
        const U previous = Fold((unsigned char)current[0]) - 'A';
        if (previous < 26)
            strcpy(directories[previous], current);
        strcpy(current, absolute);
    }
    return e;
}
void Session::Adopt(const char *absolute)
{
    const U previous = Fold((unsigned char)current[0]) - 'A';
    if (previous < 26)
        strcpy(directories[previous], current);
    strcpy(current, absolute);
}
const char *Session::Directory(char letter, char (&out)[260]) const
{
    const unsigned c = Fold((unsigned char)letter);
    if (c == Fold((unsigned char)current[0]))
    {
        strcpy(out, current);
        return out;
    }
    const U index = c - 'A';
    if (index < 26 && directories[index][0])
        strcpy(out, directories[index]);
    else
    {
        out[0] = char(c);
        out[1] = ':';
        out[2] = '\\';
        out[3] = 0;
    }
    return out;
}
Store::OpenFile *Session::Handle(U h) const
{
    for(const auto &alias:aliases)if(alias.visible==h&&alias.actual){h=alias.actual;break;}
    if ((h & 0xff000000) != 0x40000000)
        return nullptr;
    auto &file = store.handles[h & 255];
    return file.owner == this && file.generation == ((h >> 8) & 65535) ? &file : nullptr;
}
bool Session::Owns(U h) const { return Handle(h) != nullptr; }
bool Session::IsNull(U h) const {const auto *p=Handle(h);return p&&p->null_device;}
U Session::OpenNull(U access,U &handle) {
    handle=Invalid;
    for(U i=0;i<256;++i)if(!store.handles[i].owner){
        auto &h=store.handles[i];auto *cursor=new Store::Cursor;
        if(!cursor)return 8;
        const U generation=h.generation;h={};h.generation=generation;
        h.owner=this;h.node=Invalid;h.access=access;h.share=3;h.null_device=true;h.cursor=cursor;
        handle=0x40000000|(generation<<8)|i;return 0;
    }
    return 4;
}

U Session::MappingAccess(U h) const { const auto *p=Handle(h); return p?p->mapping_access:0; }
void Session::SetMappingAccess(U h,U access) { if(auto *p=Handle(h)) p->mapping_access=access; }
U Session::LegacyHandle(U handle)
{
    auto *file = Handle(handle);
    if (!file) return Invalid;
    const U actual = 0x40000000 | (file->generation << 8) | U(file - store.handles);
    for (const auto &alias : aliases)
        if (alias.actual == actual && alias.visible >= 5 && alias.visible < 256)
            return alias.visible;
    // HFILE is a DOS handle, not a Win32 handle or a pointer. Keep the
    // existing session alias table so close, inheritance and saved states
    // retain the same file object. 0..4 are DOS standard devices; 256 and
    // above belong to the LZ32 bridge.
    for (U visible = 5; visible < 256; ++visible)
        if (!Owns(visible))
            for (auto &alias : aliases)
                if (!alias.actual)
                {
                    alias = {visible, actual};
                    return visible;
                }
    return Invalid;
}
bool Session::SetWriteAttributes(U h,bool enabled){auto*file=Handle(h);if(!file)return false;file->write_attributes=enabled;return true;}
bool Session::SetInheritable(U h,bool inherit){auto*file=Handle(h);if(!file)return false;file->inherit=inherit;return true;}
bool Session::HasInheritableHandles() const {for(const auto &h:store.handles)if(h.owner==this&&h.inherit)return true;return false;}
bool Session::IsInheritable(U h) const {const auto*file=Handle(h);return file&&file->inherit;}
U Session::Information(U handle, const Node *&node) const
{
    const auto *file = Handle(handle);
    node = file ? store.Get(file->node) : nullptr;
    return !file ? 6 : file->node == Invalid ? 21 : 0;
}
U Session::Copy(const char *source, const char *destination, bool fail_if_exists)
{
    char from[260], to[260];
    U error = FullPath(source, from);
    if (!error) error = FullPath(destination, to);
    if (error) return error;
    U source_id, destination_id;
    error = store.Lookup(from, source_id);
    if (error) return error;
    const auto &node = store.nodes[source_id];
    U target_error = store.Lookup(to, destination_id);
    // KERNEL32/293: copying a path onto itself is ERROR_FILE_EXISTS.
    if (!target_error && source_id == destination_id) return 80;
    if (!target_error && (store.nodes[destination_id].attributes & ~node.attributes & 7)) return 5;
    if (target_error && target_error != 2) return target_error;
    U input, output, status;
    error = Open(from, 1, 3, 3, 0, input, status);
    if (error) return error;
    error = Open(to, 2, 0, fail_if_exists ? 1 : 2, node.attributes, output, status);
    if (!error) {
        // Block by block, so a copy never needs the whole file in memory.
        unsigned char buffer[8192];
        for (U remaining = node.size; remaining && !error;) {
            U want = remaining < sizeof buffer ? remaining : U(sizeof buffer), got = 0, done = 0;
            error = Read(input, buffer, want, got);
            if (!error && !got) error = 1117;
            if (!error) error = Write(output, buffer, got, done);
            if (!error && done != got) error = 112;
            if (!error) remaining -= got;
        }
        // 1E1: Get/SetFileTime with only lpLastWriteTime non-null.
        if (!error) error = SetTime(output, 2, node.times[2]);
        Close(output);
        if (!error) error = store.Attributes(to, node.attributes);
        else {
            store.Attributes(to, node.attributes & ~1u);
            store.Remove(to, false);
        }
    }
    Close(input);
    return error;
}
U Session::ValidateTimeHandle(U h,bool write) const {const auto*file=Handle(h);return !file?6:file->node==Invalid?21:write&&!file->write_attributes?5:0;}
U Session::GetTime(U h,U index,TimeStamp&out) const {const auto*file=Handle(h);if(!file)return 6;if(file->node==Invalid)return 21;if(index>2)return 87;out=store.nodes[file->node].times[index];return 0;}
U Session::SetTime(U h,U index,const TimeStamp&value){U error=ValidateTimeHandle(h,true);if(error)return error;if(index>2)return 87;auto stamp=value;if(index==1)stamp=AccessTime(stamp);if(index==2)stamp=WriteTime(stamp);const U node=Handle(h)->node;if((error=store.Promote(node)))return error;store.nodes[node].times[index]=stamp;store.Persist(node);if(index==2)store.Notify(store.nodes[node].parent,ChangeLastWrite);return 0;}
U Session::Open(const char *path, U access, U share, U disposition, U attrs, U &handle, U &status)
{
    handle = Invalid;
    status = 0;
    if (access & ~3u || share & ~3u || attrs & ~0xa7u)
        return 120;
    if (disposition < 1 || disposition > 5)
        return 87;
    char absolute[260];
    U e = FullPath(path, absolute), id = Invalid;
    if (e)
        return e;
    e = store.Lookup(absolute, id);
    bool exists = e == 0;
    if (e && e != 2)
        return e;
    if (exists && (store.nodes[id].attributes & 16))
        return 5;
    if (exists && disposition == 1)
        return 80;
    if (!exists && (disposition == 3 || disposition == 5))
        return 2;
    if (exists && (store.nodes[id].attributes & 1) && ((access & 2) || disposition == 2 || disposition == 5))
        return 5;
    if (exists && ((access & 2) || disposition == 2 || disposition == 5) && store.Sealed(id))
        return 19;
    if (disposition == 5 && !(access & 2))
        return 5;
    U slot = Invalid;
    for (U i = 0; i < 256; ++i)
    {
        auto &h = store.handles[i];
        if (!h.owner && slot == Invalid)
            slot = i;
        if (h.owner && exists && h.node == id && ((access & ~h.share) || (h.access & ~share)))
            return 32;
    }
    if (slot == Invalid)
        return 4;
    if (!exists)
    {
        U parent;
        const char *name;
        e = store.Parent(absolute, parent, name);
        if (e)
            return e;
        e = store.New(parent, name, (attrs & 0x27) | 32, id);
        if (e)
            return e;
        store.Notify(parent, ChangeFileName);
    }
    else if (disposition == 2 || disposition == 5)
    {
        if ((e = store.Resize(id, 0)))
            return e;
        store.nodes[id].times[2]=WriteTime(CurrentTime());
        if (disposition == 2)
            store.nodes[id].attributes = (attrs & 0x27) | 32;
        store.Persist(id);
        store.Notify(store.nodes[id].parent, ChangeSize | ChangeLastWrite | ChangeAttributes);
    }
    if (exists && (disposition == 2 || disposition == 4))
        status = 183;
    auto &h = store.handles[slot];
    h.cursor = new Store::Cursor;
    if(!h.cursor)return 8;
    h.owner = this;
    h.node = id;
    h.access = access;
    h.null_device = false;
    h.mapping_access = access;
    h.share = share;
    h.inherit = false;
    h.write_attributes=(access&2)!=0;
    handle = 0x40000000 | (h.generation << 8) | slot;
    if (store.Live(id))
    {
        char held[260];
        if (store.Path(id, held))
            store.Provider(id)->Retain(held);
    }
    return 0;
}
U Session::Inherit(Session &source)
{
    if(&store!=&source.store)return 6;
    for(unsigned i=0;i<256;++i){
        const auto &h=store.handles[i];if(h.owner!=&source||!h.inherit)continue;
        U visible=0x40000000|(h.generation<<8)|i;
        for(const auto &alias:source.aliases)if(alias.actual==visible){visible=alias.visible;break;}
        U actual=0,error=source.Duplicate(visible,*this,0,true,true,actual);if(error)return error;
        bool added=false;for(auto &alias:aliases)if(!alias.actual){alias={visible,actual};added=true;break;}
        if(!added){Close(actual);return 4;}
    }
    return 0;
}
U Session::Duplicate(U handle,Session &target,U access,bool same_access,bool inherit,U &result)
{
    auto *source=Handle(handle);if(!source||&store!=&target.store)return 6;
    if(same_access)access=source->access;
    if(access&~source->access)return 5;
    for(unsigned i=0;i<256;++i)if(!store.handles[i].owner){
        auto &h=store.handles[i];const U generation=h.generation;
        h=*source;h.generation=generation;h.owner=&target;h.access=access;h.mapping_access&=access;h.inherit=inherit;
        ++h.cursor->references;result=0x40000000|(generation<<8)|i;
        if(h.node!=Invalid&&store.Live(h.node)){char held[260];if(store.Path(h.node,held))store.Provider(h.node)->Retain(held);}
        return 0;
    }
    return 4;
}
U Session::Close(U handle)
{
    auto *h = Handle(handle);
    if (!h)
        return 6;
    for(auto &lock:store.locks)if(lock.owner==this&&lock.handle==handle)lock={};
    if (h->node != Invalid && store.Live(h->node))
    {
        char held[260];
        if (store.Path(h->node, held))
            store.Provider(h->node)->Release(held);
    }
    U actual=0x40000000|(h->generation<<8)|U(h-store.handles);
    for(auto &alias:aliases)if(alias.actual==actual)alias={};
    char *remove=nullptr;
    if(!--h->cursor->references){remove=h->cursor->delete_on_close;delete h->cursor;}
    h->cursor=nullptr;
    h->owner = nullptr;
    h->generation = h->generation % 65535 + 1;
    // The last handle of a FILE_FLAG_DELETE_ON_CLOSE file is gone: the file
    // goes with it, as KERNEL32 deletes the path its object kept. ADVPACK
    // probes every destination folder with such a file (TMP4352$.TMP).
    if(remove){store.Remove(remove,false);delete[] remove;}
    return 0;
}
bool Session::SetDeleteOnClose(U handle, const char *absolute)
{
    auto *h=Handle(handle);
    if(!h||!absolute)return false;
    const size_t length=strlen(absolute);
    char *copy=new char[length+1];
    memcpy(copy,absolute,length+1);
    delete[] h->cursor->delete_on_close;
    h->cursor->delete_on_close=copy;
    return true;
}
U Session::Lock(U handle,unsigned long long first,unsigned long long size,bool unlock)
{
    auto *h=Handle(handle);if(!h)return 6;if(h->node==Invalid)return 21;
    if(first>0xffffffffull||size>0xffffffffull)return 87;
    if(!size)return 0;
    if(unlock){for(auto &lock:store.locks)if(lock.owner==this&&lock.handle==handle&&lock.first==first&&lock.size==size){lock={};return 0;}return 158;}
    Store::RangeLock *free=nullptr;
    for(auto &lock:store.locks){if(!lock.owner){if(!free)free=&lock;continue;}
        if(lock.node==h->node&&first<lock.first+lock.size&&lock.first<first+size)return 33;}
    if(!free)return 8;*free={this,handle,h->node,first,size};return 0;
}
// A file of the tree up to this size is fetched whole on its first read and
// served from memory; a larger one is read where it lies.
constexpr U LowerInPlace = 4u * 1024 * 1024;
U Session::Read(U handle, void *out, U size, U &done)
{
    done = 0;
    auto *h = Handle(handle);
    if (!h)
        return 6;
    if (h->null_device) { return (h->access & 1) ? 0 : 5; }
    if (h->node == Invalid)
        return 21;
    if (!(h->access & 1))
        return 5;
    auto &n = store.nodes[h->node];
    for(const auto &lock:store.locks)if(lock.owner&&lock.node==h->node&&(lock.owner!=this||lock.handle!=handle)&&size&&h->cursor->position<lock.first+lock.size&&lock.first<(unsigned long long)h->cursor->position+size)return 33;
    if (h->cursor->position >= n.size)
        return 0;
    // A file of the writable volume is read straight out of it; nothing of it
    // has to fit into the node table.
    if (n.upper)
    {
        auto *backend = store.Provider(h->node);
        char path[260];
        if (!backend || !store.Path(h->node, path))
            return 3;
        const U want = size < n.size - h->cursor->position ? size : n.size - h->cursor->position;
        U got = 0;
        const U error = backend->Read(path, h->cursor->position, out, want, got);
        if (error)
            return error;
        done = got;
        h->cursor->position += got;
        // The access date stays in the node table until other metadata is
        // written back; a read must not cost a sidecar update.
        if (got)
            n.times[1] = AccessTime(CurrentTime());
        return 0;
    }
    // A large file of the tree is read in place like a file of the volume:
    // it need not fit into the node table (a game's data file below a write
    // layer).
    if (n.lower && !n.data && n.size > LowerInPlace)
    {
        auto *tree = SystemTree::Mounted();
        char path[260];
        if (!tree || !store.Path(h->node, path))
            return 3;
        const U want = size < n.size - h->cursor->position ? size : n.size - h->cursor->position;
        done = tree->Read(path, h->cursor->position, out, want);
        h->cursor->position += done;
        if (done)
            n.times[1] = AccessTime(CurrentTime());
        return 0;
    }
    // Bytes of a borrowed entry arrive from the system tree on first read.
    if (n.lower && !n.data)
    {
        const U error = store.Materialize(h->node);
        if (error)
            return error;
    }
    done = size < n.size - h->cursor->position ? size : n.size - h->cursor->position;
    if (done)
        memcpy(out, n.data + h->cursor->position, done);
    h->cursor->position += done;
    if(done)n.times[1]=AccessTime(CurrentTime());
    return 0;
}
U Session::Write(U handle, const void *data, U size, U &done)
{
    done = 0;
    auto *h = Handle(handle);
    if (!h)
        return 6;
    if (h->null_device) { if(!(h->access & 2))return 5;done=size;return 0; }
    if (h->node == Invalid)
        return 21;
    if (!(h->access & 2))
        return 5;
    if (!size)
        return 0;
    for(const auto &lock:store.locks)if(lock.owner&&lock.node==h->node&&(lock.owner!=this||lock.handle!=handle)&&h->cursor->position<lock.first+lock.size&&lock.first<(unsigned long long)h->cursor->position+size)return 33;
    if (size > ~h->cursor->position)
        return 112;
    auto &n = store.nodes[h->node];
    // The first write moves a borrowed entry into the writable volume, or, on
    // a RAM drive, into this node table. The tree stays untouched either way.
    U e = store.Promote(h->node);
    if (e)
        return e;
    if (n.upper)
    {
        auto *backend = store.Provider(h->node);
        char path[260];
        if (!backend || !store.Path(h->node, path))
            return 3;
        U written = 0;
        e = backend->Write(path, h->cursor->position, data, size, written);
        if (e)
            return e;
        if (written != size)
            return 112;
        store.Drop(h->node);
        if (h->cursor->position + size > n.size)
            n.size = h->cursor->position + size;
    }
    else
    {
        if (n.lower)
            e = store.Resize(h->node, n.size);
        if (!e && h->cursor->position + size > n.size)
            e = store.Resize(h->node, h->cursor->position + size);
        if (e)
            return e;
        memcpy(n.data + h->cursor->position, data, size);
    }
    h->cursor->position += size;
    n.attributes = (n.attributes & ~128u) | 32;
    done = size;
    n.times[2]=WriteTime(CurrentTime());
    store.Persist(h->node);
    store.Notify(n.parent, ChangeSize | ChangeLastWrite);
    return 0;
}
U Session::Seek(U handle, long long distance, U method, U &position)
{
    auto *h = Handle(handle);
    if (!h)
        return 6;
    if (h->null_device) { position=0;return 0; }
    if (h->node == Invalid)
        return 21;
    if (method > 2)
        return 87;
    long long base = method == 0 ? 0 : method == 1 ? h->cursor->position : store.nodes[h->node].size;
    if (distance < -base)
        return 131;
    if (distance > 0xffffffffLL - base)
        return 87;
    h->cursor->position = position = base + distance;
    return 0;
}
U Session::Size(U handle, U &size) const
{
    auto *h = Handle(handle);
    if (!h)
        return 6;
    if (h->null_device) { size=0;return 0; }
    if (h->node == Invalid)
        return 21;
    size = store.nodes[h->node].size;
    return 0;
}
U Session::Truncate(U handle)
{
    auto *h = Handle(handle);
    if (!h)
        return 6;
    if (h->null_device) { return 0; }
    if (h->node == Invalid)
        return 21;
    if (!(h->access & 2))
        return 5;
    U e = store.Resize(h->node, h->cursor->position);
    if (!e){
        store.nodes[h->node].attributes = (store.nodes[h->node].attributes & ~128u) | 32;
        store.nodes[h->node].times[2]=WriteTime(CurrentTime());
        store.Persist(h->node);
        store.Notify(store.nodes[h->node].parent, ChangeSize | ChangeLastWrite);
    }
    return e;
}
} // namespace Files
