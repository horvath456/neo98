// SPDX-License-Identifier: GPL-2.0-or-later
// New NEO98 file service. Shared RAM volume, independent of the host.
#pragma once
namespace Volume
{
struct Backend;
struct Entry;
}
namespace Files
{
using U = unsigned;
constexpr U Invalid = ~0u;
constexpr U DataCapacity = 32u*1024*1024;
constexpr U NodeCapacity = 256u;
struct TimeStamp {U date=0,time=0,hundredths=0;};
void SetClock(TimeStamp(*)());
// Volume size, chosen before the first use of System. The defaults are the
// RAM volume's own 256 nodes and 32 MiB. A mounted Win98 tree needs room for
// the directories a guest enumerates and for the originals it reads, so the
// host raises both; see src/system_tree.h.
void Configure(U nodes, U bytes);
// Shared native VCACHE page provider. Warm requests run outside guest fibers.
void RequestCachePages();
void ServiceCachePages();
unsigned CacheResidentPages();
unsigned CacheBufferLoans();
unsigned char* AcquireBuffer(U size);
void ReleaseBuffer(unsigned char*);
// Directory-change classes as the FILE_NOTIFY_CHANGE_* bits name them. The
// store reports the node of the directory that contains the changed entry.
enum Change {ChangeFileName=1,ChangeDirName=2,ChangeAttributes=4,ChangeSize=8,ChangeLastWrite=16};
using Observer=void(*)(void*context,U directory,U change);
class Session;
struct Node
{
    bool used = false;
    U parent = Invalid, attributes = 0, size = 0;
    char name[256] = {};
    unsigned char *data = nullptr;
    TimeStamp times[3];
    unsigned long long identity = 0;
    // Entries borrowed from the mounted system tree. `lower` means the bytes
    // still live there and `data` may be dropped again; the first write clears
    // it and the node becomes an ordinary RAM node. `populated` remembers that
    // a directory has already pulled in its lower children, `tombstone` that
    // the guest removed an entry the tree still holds.
    // `origin` stays set for the whole life of a borrowed entry, `lower` only
    // while its bytes are still in the tree; removing an entry the tree holds
    // needs a tombstone either way.
    // `upper` means the bytes live in the drive's writable volume provider
    // (src/volume.h) instead of in this node's `data`. Such a node has no RAM
    // cost and no size limit; `data` may still hold a readable copy that any
    // write drops again.
    // `opaque` marks a directory the guest created again after removing the
    // tree's directory of that name: the tree's entries below it stay hidden,
    // as in the new, empty directory of Win98.
    char alias[14] = {};
    // What a volatile provider (src/volume.h) last answered for the listing
    // of this directory.
    U stamp = 0;
    bool origin = false, lower = false, populated = false, tombstone = false, upper = false, opaque = false;
};
class Store
{
  public:
    char label[12] = "NEO98 RAM";
    Store();
    ~Store();
    Store(const Store &) = delete;
    Store &operator=(const Store &) = delete;
    const Node *Get(U node) const;
    U Lookup(const char *absolute, U &node) const;
    // The files Windows itself has open. KERNEL32 keeps the file of a running
    // program and of every DLL a process has loaded open, for the pages it
    // reads from it later: others may read the file, but not write it, delete
    // it or rename it. The process layer answers which files those are
    // (Application::ImageFileHeld in src/x86_modules.cpp); a tool that runs
    // no process leaves the question unset.
    static bool (*image_held)(const Store &, U node);
    bool ImageHeld(U node) const { return image_held && image_held(*this, node); }
    // Absolute path of one node, as the volume spells it.
    bool Path(U node, char (&out)[260]) const;
    // Bytes of a node that is still only known through the system tree or that
    // lives in a writable volume provider. After this call `Get(node)->data`
    // holds the whole file, until the next write drops the copy again.
    U Materialize(U node) const;
    const unsigned char *Bytes(U node) const;
    U Capacity() const { return capacity; }
    // Drives. 'C' always exists; the others appear when a provider is mounted
    // for them before the first use of this store. `type` is the GetDriveType
    // class, so a mounted directory can present itself as a CD-ROM.
    bool Drive(char letter) const;
    U DriveMask() const;
    U DriveType(char letter) const;
    const char *DriveLabel(char letter) const;
    bool SetDriveLabel(char letter, const char *);
    U DriveSerial(char letter) const;
    void DriveSpace(char letter, unsigned long long &total, unsigned long long &available) const;
    // Where the volume of one drive keeps its data, for logs and reports.
    const char *DriveDescription(char letter) const;
    // Whether the drive holds a medium. An empty CD-ROM drive exists
    // (GetDriveType answers DRIVE_CDROM) but every access to it fails with
    // ERROR_NOT_READY, as on Win98.
    bool Ready(char letter) const;
    // A medium change of one drive letter while the machine runs: the store
    // forgets everything it knew of the old medium, open handles on it stop
    // working, and it takes whatever Volume::Mount now names for the letter,
    // a disc, no disc, or no drive at all. Observers see the root change.
    // C: cannot change. Returns 0 or a Win32 error.
    U ChangeMedium(char letter);
    // Pushes everything the mounted providers still hold in memory out.
    void Flush();
    // After a saved state was loaded: the providers hold the open files again.
    void StateBind();
    U MakeDirectory(const char *absolute);
    U Remove(const char *absolute, bool directory);
    U Attributes(const char *absolute, U attributes);
    U Rename(const char *source, const char *destination, bool replace = false);
    U Seed(const char *absolute, const void *, U size);
    U Enumerate(U directory, U index, U &node) const;
    // A search cursor names a stable slot, so deleting a returned entry does
    // not shift the remaining entries past the caller. Start at zero.
    U EnumerateNext(U directory, U &cursor, U &node) const;
    void DataSpace(U&total,U&available) const;
    // Watch registration for the change-notification provider. Observers see
    // every mutation of this shared volume, including their own process's.
    bool AddObserver(void *context, Observer observer);
    void RemoveObserver(void *context);

  private:
    friend class Session;
    // A pointer, not an array: the capacity is chosen at construction, and a
    // const lookup may still borrow an entry from the system tree into it.
    Node *nodes = nullptr;
    U capacity = NodeCapacity, data_capacity = DataCapacity;
    // One file object, shared by every duplicate of its handle. KERNEL32
    // keeps the path of a file opened with FILE_FLAG_DELETE_ON_CLOSE in its
    // object and deletes the file when the object goes.
    struct Cursor {U position=0,references=1;char *delete_on_close=nullptr;};
    struct OpenFile
    {
        Session *owner = nullptr;
        U node = 0, access = 0, share = 0, generation = 1, mapping_access = 0;
        Cursor *cursor = nullptr;
        bool inherit = false, write_attributes = false, null_device = false;
    } handles[256];
    // A handle whose file was on a medium that left the drive: node is
    // Invalid, everything but CloseHandle fails with ERROR_NOT_READY.
    struct RangeLock {Session *owner=nullptr;U handle=0,node=0;unsigned long long first=0,size=0;} locks[256];
    mutable U bytes = 0;
    mutable unsigned long long next_identity = 1;
    struct Watcher {void *context=nullptr;Observer observer=nullptr;} observers[16];
    void Notify(U directory, U change) const;
    U Parent(const char *absolute, U &parent, const char *&name) const;
    U Child(U parent, const char *name) const;
    U New(U parent, const char *name, U attributes, U &node);
    U Resize(U node, U size);
    bool Opened(U node) const;
    // System-tree layer. Borrow pulls one entry in, Populate a whole directory,
    // Reclaim drops borrowed file bytes again when the volume runs out of room.
    U Borrow(U parent, const char *name) const;
    bool TreeHidden(U directory) const;
    void Populate(U directory) const;
    bool Reclaim(U needed, U keep) const;
    void Tombstone(U parent, const char *name);
    // Writable volume layer. One slot per drive letter; slot 2 is C:.
    struct Drives
    {
        U root = Invalid;
        Volume::Backend *backend = nullptr;
        U type = 3;
        bool writable = true, present = true;
        // The provider's entries can change behind this node table
        // (Volume::Backend::Volatile): lookups and listings ask it again.
        bool live = false;
    } volumes[26];
    bool Live(U node) const;
    bool Forget(U node) const;
    void Refresh(U node, const Volume::Entry &entry) const;
    void Sync(U directory) const;
    void Drop(U node) const;
    void Persist(U node) const;
    Volume::Backend *Provider(U node) const;
    bool Persistent(U node) const;
    // A mounted read-only medium such as the CD-ROM drive.
    bool Sealed(U node) const;
    U DriveOf(U node) const;
    U Adopt(U parent, const char *name) const;
    void Fill(U directory) const;
    // Copy-on-write: make the node's bytes live in the writable provider.
    U Promote(U node);
    // A directory of the tree with everything below it, before it moves.
    U PromoteTree(U node);
};
Store &System();
bool IsDeviceName(const char *name);
class Session
{
  public:
    explicit Session(Store &s) : store(s) {}
    ~Session();
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;
    Store &store;
    // The process current directory, as Win98 keeps it: one per drive, plus the
    // current drive. `current` is always the directory of the current drive, so
    // every existing reader of it keeps working.
    char current[260] = "C:\\";
    char directories[26][260] = {};
    U FullPath(const char *path, char (&out)[260]) const;
    U SetDirectory(const char *path);
    // The remembered directory of one drive, "X:\\" when none was set.
    const char *Directory(char letter, char (&out)[260]) const;
    // Takes an already resolved directory as the current one, parking the
    // directory of the drive that is being left.
    void Adopt(const char *absolute);
    U Open(const char *path, U access, U share, U disposition, U attributes, U &handle, U &status);
    U Close(U handle);
    U OpenNull(U access, U &handle);
    bool IsNull(U handle) const;
    U Duplicate(U handle,Session &target,U access,bool same_access,bool inherit,U &result);
    U Lock(U handle,unsigned long long first,unsigned long long size,bool unlock);
    U Read(U handle, void *out, U size, U &done);
    U Write(U handle, const void *data, U size, U &done);
    U Seek(U handle, long long distance, U method, U &position);
    U Size(U handle, U &size) const;
    U Information(U handle, const Node *&node) const;
    U Copy(const char *source, const char *destination, bool fail_if_exists);
    U Truncate(U handle);
    bool Owns(U handle) const;
    U MappingAccess(U handle) const;
    void SetMappingAccess(U handle, U access);
    U LegacyHandle(U handle);
    bool SetInheritable(U handle, bool inherit);
    bool SetWriteAttributes(U handle,bool enabled);
    bool SetDeleteOnClose(U handle, const char *absolute);
    bool IsInheritable(U handle) const;
    bool HasInheritableHandles() const;
    U Inherit(Session &source);
    U ValidateTimeHandle(U handle,bool write) const;
    U GetTime(U handle,U index,TimeStamp&) const;
    U SetTime(U handle,U index,const TimeStamp&);

  private:
    struct Alias {U visible=0,actual=0;} aliases[256];
    Store::OpenFile *Handle(U handle) const;
};
} // namespace Files
