// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Pe32 {
using U32 = unsigned;
using Size = __SIZE_TYPE__;
enum Permission : unsigned { Read = 1, Write = 2, Execute = 4 };
// NEO98 guest address space. Neither PE parsing nor Windows code depends on
// Circle or Unicorn. A future native x86 process supplies another backend.
class AddressSpace {
public:
    // Map supplies zero-filled storage. The caller owns cleanup of a partially
    // loaded address space after failure, as it owns the process itself.
    virtual bool Map(U32 address, U32 size, unsigned permissions) = 0;
    virtual bool Store(U32 address, const void *data, Size size) = 0;
    virtual bool Fetch(U32, void *, U32) { return false; }
    virtual bool Protect(U32 address, U32 size, unsigned permissions) = 0;
    // Reset image pages to reserved before committing headers and sections.
    virtual bool ReserveImage(U32 address, U32 size) { return Protect(address, size, 0); }
    virtual unsigned ImageDataPageSize() const { return 0; }
    virtual bool RelaxImageData(U32, unsigned) { return true; }
protected:
    ~AddressSpace() = default;
};
class Image {
public:
    using ImportResolver = bool (*)(void *context, const char *dll, const char *name,
                                    unsigned ordinal, U32 *address);
    Image() = default;
    Image(const Image &) = delete;
    Image &operator=(const Image &) = delete;
    ~Image();
    bool Parse(const void *file, Size length);
    bool Load(AddressSpace &space);
    bool Load(AddressSpace &space, U32 address);
    // An ARM64 image that names no absolute address has no relocation
    // directory and can still be moved; only the stripped flag forbids it.
    bool CanRelocate() const { return wide ? !(characteristics & 1) : relocations && relocation_size && !(characteristics & 1); }
    // False for an image whose preferred base lies above 4 GiB: it has no
    // preferred base inside the address space and is always moved.
    bool HasPreferredBase() const { return !preferred_high; }
    bool IsDll() const { return characteristics & 0x2000; }
    // A PE32+ image for ARM64 (machine AA64h); everything else here is PE32
    // for the i386. docs/ARM64-APPS.md.
    bool Arm64() const { return wide; }
    bool HasTls() const { return tls_size != 0; }
    // Loaded address of IMAGE_TLS_DIRECTORY32, zero without a TLS directory.
    U32 TlsDirectory() const { return tls_size ? base + tls : 0; }
    U32 TlsDirectorySize() const { return tls_size; }
    U32 PreferredBase() const { return preferred; }
    U32 Subsystem() const { return subsystem; }
    U32 SubsystemVersion() const { return subsystem_version; }
    // The stack a new thread of this program gets: KERNEL32 passes
    // both to (Application::CreateThread).
    U32 StackReserve() const { return stack_reserve; }
    U32 StackCommit() const { return stack_commit; }
    struct Symbol { U32 address = 0; char forwarder[256] = {}; };
    bool FindExport(const char *name, U32 ordinal, Symbol &, U32 hint = 0) const;
    U32 Export(const char *name) const;
    // Diagnostics only: the exported entry point at or below an address, with
    // the distance to it. Used by the crash report to name a guest EIP or a
    // return address. Returns false when the image has no export table or
    // nothing precedes the address inside the image.
    bool NearestExport(U32 address, char *out, unsigned capacity, U32 &offset) const;
    bool BindImports(AddressSpace &, ImportResolver, void *context) const;
    const unsigned char *Resource(unsigned type, unsigned id, U32 *size) const;
    struct ResourceKey { U32 id = 0; const unsigned short *name = nullptr; U32 length = 0; };
    struct ResourceEntry { U32 entry = 0, data = 0, size = 0, code_page = 0, language = 0; };
    // One directory label in file order. Named labels contain length-delimited
    // UTF-16LE bytes, not a host or guest NUL-terminated string.
    struct ResourceLabel { U32 id = 0, length = 0; const unsigned char *text = nullptr; };
    bool ResourceLabelAt(const ResourceKey *type, unsigned index, ResourceLabel &,
                         U32 *failure = nullptr) const;
    bool FindResource(const ResourceKey &type, const ResourceKey &name, U32 language,
                      ResourceEntry &, U32 *failure = nullptr) const;
    // The language ids under one type/name pair, for EnumResourceLanguages.
    unsigned ResourceLanguages(const ResourceKey &type, const ResourceKey &name, U32 *out,
                               unsigned capacity, U32 *failure = nullptr) const;
    // Datafile modules (LOAD_LIBRARY_AS_DATAFILE) keep the raw file bytes in
    // memory, so a resource address is a file offset. KERNEL32 lets
    // an RVA inside the headers pass through and otherwise finds the section
    // whose raw range covers it. Returns ~0u when no section does.
    U32 FileOffset(U32 rva) const;
    bool HasResources() const { return resources != 0; }
    U32 ResourceDirectory() const { return resources ? base + resources : 0; }
    U32 ResourceDirectorySize() const { return resource_size; }
    U32 Base() const { return base; }
    // The sections a shared session block has to cover. Win98 keeps a section
    // with IMAGE_SCN_MEM_SHARED (10000000h) once for the whole session, at
    // image base plus RVA in every process.
    struct SharedSection { U32 rva = 0, size = 0; char name[9] = {}; };
    unsigned SharedSections(SharedSection *out, unsigned capacity) const;
    U32 SizeOfImage() const { return image_size; }
    U32 SizeOfHeaders() const { return header_size; }
    U32 Entry() const { return entry ? base + entry : 0; }
    const char *Error() const { return error; }
private:
    const unsigned char *bytes = nullptr;
    Size length = 0;
    U32 preferred = 0, preferred_high = 0, base = 0, image_size = 0, header_size = 0, entry = 0;
    unsigned machine = 0;
    bool wide = false;
    U32 exports = 0, export_size = 0, subsystem_version = 0, subsystem = 0, stack_reserve = 0, stack_commit = 0;
    U32 imports = 0, import_size = 0, resources = 0, resource_size = 0;
    struct Section { U32 rva, virtual_size, raw, raw_size, flags; char name[9]; } sections[96];
    unsigned count = 0, characteristics = 0;
    U32 relocations = 0, relocation_size = 0, tls = 0, tls_size = 0;
    bool Relocate(AddressSpace &, U32 address) const;
    struct ExportMemory;
    const char *error = "No image";
    const unsigned char *Rva(U32 address, Size size) const;
    // A packer keeps the resource directory in the file but leaves the data
    // of most resources in a section that only exists unpacked in memory
    // (UPX: raw size zero, the data entries point into UPX0). Win98 reads
    // resources out of the mapped image, so a range the file cannot supply
    // comes from the address space the image was loaded into. One block per
    // range, refreshed on every request, freed with the image.
    struct LiveBlock { LiveBlock *next; U32 rva, size; unsigned char bytes[1]; };
    AddressSpace *live = nullptr;
    mutable LiveBlock *live_blocks = nullptr;
    const unsigned char *LiveRva(U32 address, Size size) const;
    bool String(U32 address, char *output, unsigned capacity) const;
};
}
