// SPDX-License-Identifier: GPL-2.0-or-later
// New native registry service. RAM storage seeded from the installed Win98 hives.
#pragma once
namespace Registry
{
using U = unsigned;
constexpr U Missing = ~0u;
struct Node
{
    bool used = false, deleted = false, owned = false;
    U parent = Missing, references = 0, child = Missing, sibling = Missing, values = Missing;
    // Volatile CONFIGMG identity; registry enumeration order can change when
    // another device instance is created. These fields are not hive values.
    mutable U devnode_handle = 0;
    U devnode_parent = 0;
    const char *name = "", *klass = "";
};
struct Value
{
    bool used = false, owned = false;
    U key = 0, type = 0, size = 0, next = Missing;
    const char *name = "";
    const unsigned char *data = nullptr;
};
// Slots in blocks of 4096: the table grows by whole blocks, the way the Win98
// registry grows with its writes, and a slot never moves, so a Value pointer
// from Query or EnumValue stays valid while other writes add slots.
template <class T> struct Slots
{
    static constexpr U BlockBits = 12, BlockSize = 1u << BlockBits;
    T **blocks = nullptr;
    U count = 0;
    Slots() = default;
    Slots(const Slots &) = delete;
    Slots &operator=(const Slots &) = delete;
    ~Slots()
    {
        for (U i = 0; i < count; ++i)
            delete[] blocks[i];
        delete[] blocks;
    }
    T &operator[](U i) { return blocks[i >> BlockBits][i & (BlockSize - 1)]; }
    const T &operator[](U i) const { return blocks[i >> BlockBits][i & (BlockSize - 1)]; }
    // Room for at least `wanted` slots; false when memory runs out.
    bool Reserve(U wanted);
};
// What a store starts from. Win98 reads C:\\WINDOWS\\SYSTEM.DAT and
// USER.DAT when it starts; a runner that has the drive hands their bytes, and
// those of NETAMD.INF and NETCLI.INF out of C:\\WINDOWS\\INF, to StartFrom
// before the first use of System, and the store decodes them
// (src/registry_hives.h). The bytes are only read while a store is built.
// Without them the store decodes the image tools/import_registry.py generated
// from the hives at build time, which the tests without a drive
// keep; a program built with NEO98_NO_REGISTRY_IMAGE carries no such
// image and starts empty then.
struct Hives
{
    const unsigned char *system = nullptr, *user = nullptr, *netamd = nullptr, *netcli = nullptr;
    U system_size = 0, user_size = 0, netamd_size = 0, netcli_size = 0;
};
void StartFrom(const Hives *);
struct Start
{
    U source = 0; // 0 nothing, 1 the generated image, 2 the hives
    U keys = 0, values = 0;
    bool network = false; // the keys of the LAN card were added (the image always has them)
};
namespace HiveImport
{
struct Arena;
}
class Store
{
  public:
    Store();
    ~Store();
    Store(const Store &) = delete;
    Store &operator=(const Store &) = delete;
    // Sized from the imported hive image and grown with the guest's writes;
    // Win98 has no ceiling there either.
    Slots<Node> nodes;
    Slots<Value> values;
    U node_capacity = 0, value_capacity = 0;
    U roots[7] = {};
    mutable U next_devnode = 0;
    // RegRemapPreDefKey (VMM 0xD036C0) moves a predefined root to another key
    // and makes every handle that was opened through the old root stale. The
    // epoch of a root counts those moves; a handle remembers the root it came
    // from and the epoch it was opened in.
    U root_epoch[7] = {};
    // Result of decoding the hives or the generated image; 0 when all of it loaded.
    U import_error = 0;
    Start start;
    U Open(U parent, const char *path, bool create, U &node, bool &created, const char *klass = "");
    U EnumKey(U parent, U index, U &node) const;
    U EnumValue(U key, U index, const Value *&value) const;
    U Query(U key, const char *name, const Value *&value) const;
    U Set(U key, const char *name, U type, const void *data, U size);
    U DeleteValue(U key, const char *name);
    U DeleteKey(U parent, const char *path);
    void Release(U node);
    // Full path of one key, "HKEY_LOCAL_MACHINE\\Software\\...". The persisted
    // journal of src/registry_persist.cpp names keys this way.
    bool Path(U node, char *out, U capacity) const;
    U Anchor(const char *path) const;

  private:
    U bytes = 0, next_node = 0, next_value = 0, free_node = Missing, free_value = Missing;
    U NewNode(U parent, const char *name, const char *klass = "");
    U Attach(U parent, const char *name);
    U NewValue(U key);
    void DropValue(U index);
    U Import();
    U ImportHives(const Hives &);
    // Names and payloads of the decoded hives; the image needs none.
    HiveImport::Arena *hive_memory = nullptr;
    U Child(U parent, const char *name) const;
    void Unlink(U node);
    void Remove(U node);
    void Clear(Value &);
    void Recycle(U node);
};
// The devnodes of HKEY_DYN_DATA\Config Manager\Enum for the devices this
// port provides; src/config_manager.cpp.
void PublishDevnodes(Store &store);
// While one lives, changes of the store do not go into the journal: keys the
// port makes anew at every start from the host's devices (the camera).
struct Unrecorded
{
    Unrecorded();
    ~Unrecorded();
    Unrecorded(const Unrecorded &) = delete;
    Unrecorded &operator=(const Unrecorded &) = delete;
  private:
    bool was;
};
bool PublishHardwareDiagnostic(Store &store, U instance);
// The flags of a device in a hardware profile (CSConfigFlags, profile 0 the
// current one) and what follows from them; src/config_manager.cpp.
bool ProfileFlags(const Store &store, U profile, const char *hardware, U &flags);
bool SetProfileFlags(Store &store, U profile, const char *hardware, U flags);
bool DevnodeDisabled(const Store &store, const char *hardware);
constexpr U DevnodeProblemDisabled = 0x16; // CM_PROB_DISABLED
// DN_DRIVER_LOADED 2, DN_STARTED 8, DN_ROOT_ENUMERATED 1, DN_DISABLEABLE
// 2000h; a disabled devnode is not started and has DN_HAS_PROBLEM 400h.
constexpr U DevnodeStatus(bool disabled, bool root_enumerated)
{
    return (disabled ? 0x0400u : 0x000au) | 0x2000u | (root_enumerated ? 1u : 0u);
}
constexpr U DevnodeCapacity = 256;
U Devnodes(const Store &store, U enumeration, U *keys, U capacity);
U DevnodeHandle(const Store &store, U key);
// Persistence of everything the guest changes after the import.
// The imported hives are 18,858 keys of read-only start-up state. What a guest
// writes on top of them is small, so it is kept as a journal of changes, not as
// a copy of the whole registry: create key, delete key, set value, delete
// value, in the order they happened. Loading replays that journal after the
// import, so REGEDIT edits and an installer's keys are there again on the next
// start.
// The byte sink is a provider, so src/ stays free of host calls and a test can
// switch registry persistence on without file persistence and the other way
// round.
struct Storage
{
    virtual ~Storage() {}
    virtual U Size() = 0;
    virtual bool Load(unsigned char *buffer, U size) = 0;
    virtual bool Save(const unsigned char *buffer, U size) = 0;
};
void Attach(Storage *);
Storage *Attached();
// Change observers. `Note` calls every registered one before it decides
// whether the journal records the change, so an observer also sees the changes
// of a run without persistence. ADVAPI32's RegNotifyChangeKeyValue registers
// one. `kind` and `path` are the arguments of `Note` below.
typedef void (*Observer)(U kind, const char *path, const char *name);
bool Observe(Observer);
// One journal entry: 1 create key, 2 delete key, 3 set value, 4 delete value.
// Store calls it after a successful change.
void Note(U kind, const char *path, const char *name, U type, const void *data, U size);
// A hive RegLoadKey mounted (src/x86_registry_hives.cpp). Its keys live in
// its own file, so changes below the path stay out of the journal.
void Mount(const char *path, bool add);
bool Mounted(const char *path);
// Replays the stored journal into the store and starts recording. Called once
// after the import; without a storage it only starts recording.
U Restore(Store &);
// Writes the journal out. RegFlushKey and the end of a run call it.
bool Flush();
bool Dirty();
U Records();
// Installed data loader status; preserved for Application startup.
U SystemInitializationError();
Store &System();
class Session
{
  public:
    explicit Session(Store &s) : store(s) {}
    ~Session();
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;
    Store &store;
    U Key(U handle, U &node) const;
    // The predefined root a handle was opened through, 1 to 7 for
    // 80000000h to 80000006h, zero for none.
    U Root(U handle) const;
    U Open(U parent, const char *path, bool create, U &handle, bool &created, const char *klass = "");
    U Close(U handle);

  private:
    struct Handle
    {
        bool used = false;
        U node = 0, token = 0, root = 0, epoch = 0;
    };
    Handle *handles = nullptr;
    U handle_capacity = 0, next_handle = 1;
    const Handle *Find(U handle) const;
    bool GrowHandles();
};
} // namespace Registry
