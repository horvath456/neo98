// SPDX-License-Identifier: GPL-2.0-or-later
// Writable volume provider for src/files.h.
// A drive letter of the NEO98 volume can be backed by a provider that stores
// its entries somewhere durable. The provider sees normalized guest paths such
// as "C:\\WINDOWS\\SYSTEM\\USER32.DLL" and answers with Win32 error numbers.
// Everything the guest can observe (sharing modes, open-handle rules, the
// handle table, change notifications, the enumeration contract) stays in
// Files::Store; the provider only holds names, metadata and bytes.
// Nothing here touches a host: the POSIX implementation lives outside src/ in
// tools/host_volume.h, the same way tools/system_tree_host.h implements the
// read-only system tree.
#pragma once
#include "files.h"
namespace Volume
{
using U = unsigned;
// Drive classes as GetDriveType names them; Files::Store passes them on.
constexpr U Fixed = 3, Cdrom = 5;
struct Entry
{
    char name[256] = {};   // long name, as the volume spells it
    char alias[14] = {};   // 8.3 alias, empty when the long name already is one
    U attributes = 0, size = 0;
    Files::TimeStamp times[3];
    // A removed entry of a layer below this one. The provider keeps the record
    // so the deletion survives a restart; Files::Store turns it into the
    // tombstone node it already uses for the read-only system tree.
    bool tombstone = false;
    // A directory of this layer that the guest created again after removing
    // the directory of the same name below. Like the new directory of Win98 it
    // starts empty: nothing of the layer below shows through.
    bool opaque = false;
};
struct Backend
{
    virtual ~Backend() {}
    virtual bool Find(const char *absolute, Entry &out) = 0;
    virtual bool Child(const char *directory, U index, Entry &out) = 0;
    virtual U Read(const char *absolute, U offset, void *buffer, U size, U &done) = 0;
    virtual U Write(const char *absolute, U offset, const void *buffer, U size, U &done) = 0;
    virtual U Resize(const char *absolute, U size) = 0;
    // Creates the entry and reports the metadata it received, including the
    // 8.3 alias the provider generated for it.
    virtual U Create(const char *absolute, U attributes, bool directory, Entry &out) = 0;
    // Removes the entry. An entry that only exists in a layer below becomes a
    // tombstone instead, so the deletion is durable as well.
    virtual U Remove(const char *absolute, bool directory, bool tombstone) = 0;
    virtual U Rename(const char *source, const char *destination) = 0;
    // Copy on write of a file the layer below this volume holds, after Create
    // made its entry here: the provider fetches the bytes itself, as a block
    // clone where the medium can, so a large file neither passes through the
    // node table nor costs its size. False when the provider cannot; the
    // caller then writes the bytes (Files::Store::Promote).
    virtual bool Import(const char *absolute) { (void)absolute; return false; }
    // A provider whose entries can change behind the node table of
    // Files::Store: the write layer of a game machine, whose drive below
    // belongs to another machine and whose DOS programs reach it from their
    // own thread. The store then asks the provider
    // again at every lookup by name, and at the start of every listing
    // whether the directory is still the one it listed (Stamp: a number that
    // changes when the directory changed other than through the store).
    virtual bool Volatile() { return false; }
    virtual U Stamp(const char *directory) { (void)directory; return 0; }
    // A handle of the store opened the file, or closed it. A volatile
    // provider keeps a file of the drive below readable for as long as it is
    // open, whatever happens to it there, and refuses to remove or rename a
    // file another side has open.
    virtual void Retain(const char *absolute) { (void)absolute; }
    virtual void Release(const char *absolute) { (void)absolute; }
    virtual U SetAttributes(const char *absolute, U attributes) = 0;
    virtual U SetTimes(const char *absolute, const Files::TimeStamp *times) = 0;
    // Real figures of the medium in bytes, before any FAT32 capping.
    virtual void Space(unsigned long long &total, unsigned long long &available) = 0;
    virtual const char *Label() = 0;
    virtual bool SetLabel(const char *) = 0;
    virtual U Serial() = 0;
    // Writes out whatever the provider still holds in memory, such as the
    // directory sidecars. FlushFileBuffers and a closing handle call it.
    virtual void Flush() = 0;
    virtual const char *Description() const = 0;
};
// Registration before the first use of Files::System. Files::Store reads this
// table in its constructor, so a runner mounts its drives during start-up and
// a test constructs its own Store afterwards.
void Mount(char letter, Backend *, U type = Fixed, bool writable = true);
Backend *Mounted(char letter);
// Whether the letter is a drive at all. A CD-ROM drive stays a drive while it
// holds no disc: Mount(letter, nullptr, Cdrom, false).
bool Exists(char letter);
U Type(char letter);
bool Writable(char letter);
void Clear();
} // namespace Volume
