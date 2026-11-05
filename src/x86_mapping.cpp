// SPDX-License-Identifier: GPL-2.0-or-later
// Native object/handle/backing providers for the implemented KERNEL32 mappings.
#include "win98_mapping.h"
#include "win98_vmm_mapping.h"
#include "x86_app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
namespace X86
{
struct Application::MappingSystem
{
    using U = unsigned;
    using Object = Win98Mapping::Object;
    using View = Win98Mapping::View;
    using File = Win98Mapping::File;
    struct Record
    {
        Object data{};
        U references = 0;
        bool used = false, named = false;
        char name[Application::KernelObjectNameBytes]{};
        // A file mapping is an anonymous mapping whose pages are filled from
        // this snapshot of the file as they commit. A writable one keeps its
        // own duplicated file handle and writes the changed bytes back at
        // flush, unmap and process exit. Win98 has one page cache behind the
        // file; NEO98 copies at those boundaries instead.
        unsigned char *snapshot = nullptr;
        U snapshot_size = 0;
        U file_handle = 0, file_bytes = 0;
        bool file_writable = false;
        // An object duplicated into another process shares this section: a
        // view's pages are filled from it when they commit and written back
        // to it when the view unmaps, so both processes see one block.
        struct Section { unsigned char *bytes = nullptr; U size = 0, references = 0; } *section = nullptr;
        // The section is copied into this object's pages once. Every
        // MapViewOfFile commits again, also for an object that is already
        // mapped, and a second copy would undo what the guest wrote in
        // between. Foreign writes arrive through WriteBack instead.
        bool section_loaded = false;
        // How far a session block has been filled from the file's snapshot,
        // in bytes from its base. A view commits only the length the caller
        // asked for, and the next, longer view of the same object commits the
        // rest: those pages still have to come from the file. DIRECTDB maps
        // the header of a store (25C4h bytes) and then the whole file; with
        // one fill for the object everything behind the header read as zeros
        // and went back into the file as zeros.
        U snapshot_loaded = 0;
        // How far the object's reservation is actually committed, in bytes
        // from its base. MapViewOfFile commits only the length the caller
        // asked for, and an object of 11.140 bytes mapped with length 1.140
        // has one page and not three. The section copy has to stay inside
        // that, in both directions.
        U committed = 0;
        // A named object is backed by a session block: one set of host bytes
        // at one guest address in every process, so a pointer stored inside
        // the block means the same thing on both sides. See
        //
        bool session = false;
    } objects[64];
    static void ReleaseSection(Record &record)
    {
        if (record.section && !--record.section->references) { free(record.section->bytes); delete record.section; }
        record.section = nullptr;
    }
    // The view's current bytes go back to the shared section, and from there
    // into the views the other processes of the session hold on it. Without
    // the second step a reader that mapped first would keep the bytes its
    // pages were committed with. Win98 has one set of physical pages for all
    // of them; NEO98 copies at these boundaries instead, so a process sees a
    // foreign write after the writer flushes or unmaps.
    static U Smallest(U a, U b) { return a < b ? a : b; }
    // The bytes the guest changed in a writable file mapping go back into the
    // file. The range is bounded by the mapping size and by how far the object
    // is actually committed, because reading beyond that violates the page
    // protection of its own reservation.
    void WriteFileBack(Record &record, Object &object)
    {
        if (!record.file_writable || !record.snapshot || !object.base || !app.cpu) return;
        const U bytes = Smallest(Smallest(record.file_bytes, object.size), record.committed);
        if (!bytes) return;
        if (!app.files.Owns(record.file_handle))
        {
            if (app.log) app.log("MAPPING writeback without a file handle");
            return;
        }
        if (!app.Read(object.base, record.snapshot, bytes)) return;
        U position = 0, start = 0, done = 0, restored = 0;
        app.files.Seek(record.file_handle, 0, 1, position);
        app.files.Seek(record.file_handle, 0, 0, start);
        app.files.Write(record.file_handle, record.snapshot, bytes, done);
        app.files.Seek(record.file_handle, position, 0, restored);
    }
    void WriteBack(Object &object)
    {
        auto *record = Find(object);
        if (record) WriteFileBack(*record, object);
        // A session block needs no reconciliation: the other processes read
        // the very bytes this one wrote.
        if (record && record->session) return;
        if (!record || !record->section || !object.base) return;
        const U bytes = Smallest(Smallest(record->section->size, object.size), record->committed);
        if (!bytes) return;
        app.Read(object.base, record->section->bytes, bytes);
        for (auto *other = systems; other; other = other->next_system)
        {
            if (other == this) continue;
            for (auto &peer : other->objects)
                if (peer.used && peer.section == record->section && peer.data.base && peer.committed)
                    other->app.Store(peer.data.base, record->section->bytes,
                                     Smallest(Smallest(bytes, peer.data.size), peer.committed));
        }
    }
    Application &app;
    Win98Mapping::State state{};
    Object *active = nullptr;
    U create_protection = 0, view_count = 0;
    // The mapping objects themselves stay with their process, because a view
    // and its pages are an address in that process. What is session-wide is
    // the name and the block of bytes behind it: a second CreateFileMappingA
    // or an OpenFileMappingA in another process gets its own object record
    // bound to the same section.
    struct Named
    {
        bool used = false;
        char name[Application::KernelObjectNameBytes]{};
        U size = 0, protection = 0, references = 0;
        // The entry belongs to the session, not to a process. The bytes behind
        // it are a session block whose guest address is the same in every
        // process, so a program may store pointers inside its own shared block
        // and read them back in another one; MPR and OLE32 both do. Before the
        // session arena existed the name was pinned to its creating process,
        // which is what reported.
        bool session = false;
        Record::Section *section = nullptr;
    };
    static Named named[64];
    // Every live mapping system of the session, so a write-back can reach the
    // views the other processes hold on the same section.
    static MappingSystem *systems;
    MappingSystem *next_system = nullptr;
    static Named *FindNamed(const char *name)
    {
        for (auto &entry : named)
            if (entry.used && !strcmp(entry.name, name))
                return &entry;
        return nullptr;
    }
    Named *FindOwnNamed(const char *name) { return FindNamed(name); }
    void ReleaseNamed(const char *name, Record::Section *section)
    {
        auto *entry = FindNamed(name);
        if (!entry || (!entry->session && entry->section != section)) return;
        if (entry->references && !--entry->references) *entry = {};
    }
    explicit MappingSystem(Application &a) : app(a) { next_system = systems; systems = this; }
    ~MappingSystem()
    {
        for (auto **link = &systems; *link; link = &(*link)->next_system)
            if (*link == this) { *link = next_system; break; }
        // Native process teardown: a writable file mapping still has to reach
        // its file, and no Windows thread remains to use these views.
        for (auto &record : objects)
            if (record.used) WriteFileBack(record, record.data);
        while (state.views)
        {
            View *next = state.views->next;
            FreeView(*state.views);
            state.views = next;
        }
        for (auto &record : objects)
            if (record.used)
                Win98Mapping::Destroy(*this, state, record.data);
        for (auto &record : objects)
        {
            if (record.file_handle && app.files.Owns(record.file_handle)) app.files.Close(record.file_handle);
            free(record.snapshot);
            ReleaseSection(record);
        }
        FreeTable(state.slots);
    }
    Record *Find(Object &object)
    {
        if (!object.id || object.id > 64)
            return nullptr;
        auto &record = objects[object.id - 1];
        return record.used && &record.data == &object ? &record : nullptr;
    }
    void Error(U value) { app.last_error = value; }
    U Read32(U address) { return app.Read32(address); }
    // These providers never yield in the current cooperative process. They
    // require actual namespace locking before concurrent native CPUs are added.
    void Lock() {}
    void Unlock() {}
    U OpenNamed(U address, U, U flags)
    {
        if (!address)
            return ~0u;
        char name[Application::KernelObjectNameBytes];
        {
            const char *text = app.ReadString(address);
            if (app.state == Failed)
                return 0;
            // an over-long name is ERROR_INVALID_NAME. Unlike the
            // other three types, Windows 98 does not fold an empty name to NULL.
            if (strlen(text) > Application::KernelObjectNameLimit)
            {
                Error(123);
                return 0;
            }
            strcpy(name, text);
        }
        const U kind = Application::NamedObjectKind(name);
        if (kind && kind != 5)
        {
            Error(6);
            return 0;
        }
        // A name this process already owns answers with that very object, as
        // the original does: one record, one view address, one block of bytes.
        // OLE32 opens its own section a second time this way (30039EA8) and
        // counts in the view it gets back.
        for (auto &record : objects)
            if (record.used && record.named && !strcmp(record.name, name))
            {
                const U handle = CreateHandle(record.data, flags);
                return handle == ~0u ? 0 : handle;
            }
        auto *entry = FindNamed(name);
        if (!entry)
            return ~0u;
        // A name of another process becomes an object record of this one,
        // bound to the session's section.
        // The recursion carries no name; the binding happens below, so the
        // session entry keeps exactly one record per process reference.
        const U saved = create_protection;
        create_protection = entry->protection;
        const U handle = Win98Mapping::Create(*this, state, ~0u, 0, entry->protection, 0, entry->size, 0);
        create_protection = saved;
        if (!handle)
            return 0;
        auto *slot = app.FindKernelHandle(handle);
        if (!slot || slot->kind != 5 || slot->object >= 64)
            return 0;
        auto &record = objects[slot->object];
        free(record.snapshot);
        record.snapshot = nullptr;
        record.snapshot_size = 0;
        ReleaseSection(record);
        record.session = entry->session;
        if (!record.session)
        {
            record.section = entry->section;
            ++record.section->references;
        }
        record.named = true;
        strcpy(record.name, name);
        record.data.name = slot->object + 1;   // ReleaseName's handle on this record
        ++entry->references;
        slot->inherit = (flags & 0x80000000u) != 0;
        slot->access = 0xf001f;
        return handle;
    }
    Object *AllocateObject(U, U)
    {
        // SEC_RESERVE (0x04000000) gets pager 0xff in the original Create:
        // MapViewOfFile only reserves, and VirtualAlloc(MEM_COMMIT) on the
        // view commits (Application::MappedViewCommit). QUARTZ.DLL builds
        // the shared data of its filter graphs this way.
        for (U i = 0; i < 64; ++i)
            if (!objects[i].used)
            {
                auto &record = objects[i];
                record = {};
                record.used = true;
                record.data.id = i + 1;
                return &record.data;
            }
        Error(8);
        return nullptr;
    }
    U BindName(Object &object, U address)
    {
        const char *name = app.ReadString(address);
        if (app.state == Failed)
            return 0;
        if (strlen(name) > Application::KernelObjectNameLimit)
        {
            Error(123);
            return 0;
        }
        auto *record = Find(object);
        if (!record)
            return 0;
        record->named = true;
        strcpy(record->name, name);
        return object.id;
    }
    void ReleaseName(U id)
    {
        if (id && id <= 64)
        {
            auto &record = objects[id - 1];
            if (record.named)
                ReleaseNamed(record.name, record.section);
            record.named = false;
            record.name[0] = 0;
        }
    }
    // A named object that this process really created: its bytes become the
    // session's section, so the next process that uses the name gets them.
    bool PublishNamed(U handle, U protection)
    {
        auto *slot = app.FindKernelHandle(handle);
        if (!slot || slot->kind != 5 || slot->object >= 64)
            return true;
        auto &record = objects[slot->object];
        // Either flag says the record already holds a reference on the name. A
        // second CreateFileMappingA with the same name answers with this very
        // record, and its reference must not be counted twice.
        if (!record.named || record.section || record.session)
            return true;
        auto *entry = FindNamed(record.name);
        if (entry)
        {
            record.session = entry->session;
            if (!record.session)
            {
                record.section = entry->section;
                ++record.section->references;
            }
            ++entry->references;
            return true;
        }
        for (auto &free_entry : named)
            if (!free_entry.used)
            {
                const U size = record.data.size;
                if (!size) { Error(8); return false; }
                // The bytes of a named object live in a session block. The
                // block itself is created when the first view asks for an
                // address, because only then is the address handed out.
                record.session = true;
                free_entry.used = true;
                free_entry.session = true;
                strcpy(free_entry.name, record.name);
                free_entry.size = size;
                free_entry.protection = protection;
                free_entry.references = 1;
                return true;
            }
        Error(8);
        return false;
    }
    void FreeObject(Object &object)
    {
        if (auto *record = Find(object))
        {
            if (record->file_handle && app.files.Owns(record->file_handle)) app.files.Close(record->file_handle);
            free(record->snapshot);
            ReleaseSection(*record);
            *record = {};
        }
    }
    Object **AllocateTable(U count)
    {
        auto **table = new Object *[count] {};
        if (!table)
            Error(8);
        return table;
    }
    void FreeTable(Object **table) { delete[] table; }
    U CreateHandle(Object &object, U flags)
    {
        const U handle = app.NewKernelHandle(5, object.id - 1);
        if (!handle)
            return ~0u;
        auto *entry = app.FindKernelHandle(handle);
        entry->inherit = (flags & 0x80000000u) != 0;
        // The original OpenFileMappingA does not retain desiredAccess.
        entry->access = 0xf001f;
        return handle;
    }
    Object *ResolveMapping(U handle, U, U)
    {
        active = nullptr;
        const auto *entry = app.FindKernelHandle(handle);
        if (!entry || entry->kind != 5 || entry->object >= 64 || !objects[entry->object].used)
        {
            Error(6);
            return nullptr;
        }
        active = &objects[entry->object].data;
        return active; // Lookup only; a successful view takes its own reference.
    }
    void AddReference(Object &object) { app.ReferenceMapping(object.id - 1); }
    void ReleaseReference(Object &object) { app.ReleaseMappingReference(object.id - 1); }
    View *AllocateView()
    {
        if (view_count == 512)
        {
            Error(8);
            return nullptr;
        }
        auto *view = new View;
        if (!view)
        {
            Error(8);
            return nullptr;
        }
        ++view_count;
        return view;
    }
    void FreeView(View &view)
    {
        delete &view;
        --view_count;
    }
    U Reserve(U page, U count, U flags)
    {
        // Pager 1 commits right after (0x10); a SEC_RESERVE section, pager
        // 0xff, reserves only (0) and commits through VirtualAlloc.
        if (!active || !((active->pager >= 1 && active->pager <= 4 && flags == 0x10) || (active->pager == 255 && flags == 0)))
            return ~0u;
        // A named object gets its address from the session, so every process
        // maps its view at the same place. This is the shared-memory behavior that the
        // shared bytes alone could not give.
        if (auto *record = Find(*active); record && record->session)
        {
            const U base = app.ReserveSessionView(record->name, active->size);
            return base ? base : ~0u;
        }
        // Mapping identity belongs to the original object; Space owns native
        // address allocation and page metadata. It is a separate owner class.
        const U protection = active->protection & 4 ? 0x60000 : 0x40000;
        return app.ReserveMappingPages(page, count, protection);
    }
    U Commit(U page, U count, U pager, U backing, U flags)
    {
        if (!active || pager < 1 || pager > 4 || active->pager != pager || (flags != 0x60040000 && flags != 0x60060000) ||
            page < (active->base >> 12))
            return 0;
        if (!Win98VmmMapping::StaticAllowed(0x10, flags) || !Win98VmmMapping::PagerIncrement(flags))
            return 0;
        const U offset = page - (active->base >> 12);
        if (active->index >= state.capacity || state.slots[active->index] != active ||
            backing != (active->index << 18) + offset)
            return 0;
        // The two high bits select mapping/pager behavior in the original.
        // Here that identity was checked above and ownership is carried by Space.
        // Repeated commits use the object's canonical pages and preserve bytes.
        auto *session_record = Find(*active);
        const bool session = session_record && session_record->session;
        // A commit inside a session block reaches every process that holds it,
        // so a view of the same object has the same committed pages on both
        // sides.
        const U result = session ? app.CommitSessionView(page, count)
                                 : app.CommitMappingPages(page, count, flags & 0x60000);
        if (result)
            if (auto *record = Find(*active))
            {
                const U reach = ((offset + count) << 12) < active->size ? ((offset + count) << 12) : active->size;
                if (reach > record->committed) record->committed = reach;
            }
        if (result && session)
        {
            // A file behind the object fills its block once, in the process
            // that read the file. Everything else already lives in the block.
            auto &record = *session_record;
            if (record.snapshot)
            {
                record.section_loaded = true;
                // Pages that were filled before keep what the guest wrote.
                U start = offset << 12;
                const U end = (offset + count) << 12;
                const bool joins = start <= record.snapshot_loaded;
                if (joins && start < record.snapshot_loaded) start = record.snapshot_loaded;
                if (start < end && start < record.snapshot_size)
                {
                    const U bytes = record.snapshot_size - start < end - start ? record.snapshot_size - start : end - start;
                    app.cpu->Write(active->base + start, record.snapshot + start, bytes);
                }
                if (joins && end > record.snapshot_loaded) record.snapshot_loaded = end;
            }
            return result;
        }
        if (result)
            if (auto *record = Find(*active); record && (record->snapshot || record->section))
            {
                if (record->section && record->section_loaded)
                    return result;
                record->section_loaded = record->section != nullptr;
                const unsigned char *source = record->section ? record->section->bytes : record->snapshot;
                const U total = record->section ? record->section->size : record->snapshot_size;
                const U start = offset << 12;
                if (start < total)
                {
                    const U bytes = total - start < (count << 12) ? total - start : (count << 12);
                    // The pages may be read-only for the guest; the fill is the pager's own write.
                    app.cpu->Write(page << 12, source + start, bytes);
                }
            }
        return result;
    }
    // The size of the view that starts at `address`, zero when none does.
    U ViewBytes(U address)
    {
        for (auto *view = state.views; view; view = view->next)
            if (view->address == address && view->object)
                return view->object->size;
        return 0;
    }
    // VirtualAlloc(MEM_COMMIT) on pages of a view: the pages of its object
    // are committed, in the session block for a named object. Answers the
    // first page, or zero when the range is not inside one view.
    U CommitInView(U address, U bytes)
    {
        for (auto *view = state.views; view; view = view->next)
        {
            // Only the pages of a SEC_RESERVE section (pager 0xff) commit
            // this way; the view of any other section is committed already
            // and keeps its protection.
            if (!view->object || !view->object->base || view->object->pager != 255) continue;
            Object &o = *view->object;
            const unsigned long long begin = o.base, end = begin + ((o.size + 4095ull) & ~4095ull);
            if (address < begin || (unsigned long long)address + bytes > end) continue;
            const U first = address >> 12, count = ((address + bytes - 1) >> 12) - first + 1;
            auto *record = Find(o);
            const bool session = record && record->session;
            const U result = session ? app.CommitSessionView(first, count)
                                     : app.CommitMappingPages(first, count, o.protection & 4 ? 0x60000 : 0x40000);
            if (!result) return 0;
            if (record)
            {
                const U reach = ((first - (o.base >> 12) + count) << 12) < o.size ? ((first - (o.base >> 12) + count) << 12) : o.size;
                if (reach > record->committed) record->committed = reach;
            }
            return first << 12;
        }
        return 0;
    }
    void ReleasePages(U base, U flags)
    {
        // The last handle of a writable file mapping closing is the last
        // chance to reach the file, and Destroy releases the pages before it
        // frees the object, so the write back happens here while the bytes
        // still exist.
        bool session = false;
        for (auto &record : objects)
            if (record.used && record.data.base == base)
            {
                WriteFileBack(record, record.data);
                session = session || record.session;
            }
        // The pages of a session block belong to the session. This process
        // only gives up its reference; the block lives until the last process
        // of the session lets go.
        if (session)
        {
            if (auto *block = Application::FindSessionBlock(base)) app.ReleaseSessionBlock(*block);
            return;
        }
        if (flags != 0x10 || !app.ReleaseMappingPages(base))
            app.Fail("Mapping backing release failed");
    }
    // Snapshot-backed file pagers retain distinct identities for view access
    // checks. DOS/VxD file services below remain outside this native provider.
    U PagerRead() { return 2; }
    U PagerWrite() { return 3; }
    U PagerCopy() { return 4; }
    File *ResolveFile(U, U, U)
    {
        Error(50);
        return nullptr;
    }
    U DuplicateBackingName(U)
    {
        Error(50);
        return 0;
    }
    void FreeBackingName(U) { app.Fail("File mapping backing names are not ported"); }
    U FileSize(U)
    {
        Error(50);
        return ~0u;
    }
    U TranslateFile(U &, U &)
    {
        Error(50);
        return 50;
    }
    U FileOwner() { return 0; }
    void RetainFileOwner(U) { app.Fail("File mapping owners are not ported"); }
    U SetFilePointer(U, U)
    {
        Error(50);
        return ~0u;
    }
    bool SetEndOfFile(U)
    {
        Error(50);
        return false;
    }
    void CloseFile(Object &, U) { app.Fail("File mapping cleanup is not ported"); }
    void UnmapPages(U page, U count)
    {
        // The native snapshot was written back by UnmapViewOfFile. Other
        // views of the object share this canonical reservation; release it
        // only when the final object/view reference disappears.
        for(const auto &record:objects)if(record.used&&record.snapshot&&record.data.base){
            const U first=record.data.base>>12, pages=(record.data.size+4095)>>12;
            if(page>=first&&page-first<=pages&&count<=pages-(page-first))return;
        }
        app.Fail("File view unmap is outside its backing reservation");
    }
};
// Another process's mapping object duplicated into this process: both
// records share one section, so the block one process writes is the block
// the other reads (SHLWAPI's SHAllocShared/SHLockShared).
unsigned Application::DuplicateMappingFrom(Application &source, unsigned object)
{
    if (!source.mapping_system || object >= 64 || !source.mapping_system->objects[object].used) return 0;
    auto &record = source.mapping_system->objects[object];
    const unsigned size = record.data.size;
    if (!size) return 0;
    if (!record.section)
    {
        auto *section = new MappingSystem::Record::Section;
        section->bytes = static_cast<unsigned char *>(malloc(size));
        if (!section->bytes) { delete section; return 0; }
        memset(section->bytes, 0, size);
        section->size = size;
        section->references = 1;
        if (record.snapshot) memcpy(section->bytes, record.snapshot, record.snapshot_size < size ? record.snapshot_size : size);
        record.section = section;
    }
    // The source's current bytes are the section's; a view it still holds
    // may have been written since the last unmap.
    if (record.data.base) source.Read(record.data.base, record.section->bytes, size);
    if (!mapping_system)
    {
        mapping_system = new MappingSystem(*this);
        if (!mapping_system) return 0;
    }
    auto &e = *mapping_system;
    e.create_protection = 4;
    const unsigned handle = Win98Mapping::Create(e, e.state, ~0u, 0, 4, 0, size, 0);
    if (!handle) return 0;
    if (auto *entry = FindKernelHandle(handle); entry && entry->kind == 5 && entry->object < 64)
    {
        auto &mine = e.objects[entry->object];
        MappingSystem::ReleaseSection(mine);
        mine.section = record.section;
        ++mine.section->references;
    }
    if (log) { char line[160]; snprintf(line, sizeof line, "Mapping shared with process %u: size=%u", source.ProcessId(), size); log(line); }
    return handle;
}
Application::MappingSystem::Named Application::MappingSystem::named[64];
Application::MappingSystem *Application::MappingSystem::systems = nullptr;
bool Application::MappingNameTaken(const char *name)
{
    return name && *name && MappingSystem::FindNamed(name) != nullptr;
}
void Application::ReferenceMapping(unsigned index)
{
    if (!mapping_system || index >= 64 || !mapping_system->objects[index].used)
    {
        Fail("Invalid native mapping reference");
        return;
    }
    ++mapping_system->objects[index].references;
}
void Application::ReleaseMappingReference(unsigned index)
{
    if (!mapping_system || index >= 64 || !mapping_system->objects[index].used ||
        !mapping_system->objects[index].references)
    {
        Fail("Invalid native mapping release");
        return;
    }
    auto &record = mapping_system->objects[index];
    if (!--record.references)
        Win98Mapping::Destroy(*mapping_system, mapping_system->state, record.data);
}
void Application::FreeMappings()
{
    delete mapping_system;
    mapping_system = nullptr;
}
unsigned Application::MappingApi(ApiName name, const unsigned *a)
{
    if (!mapping_system)
    {
        mapping_system = new MappingSystem(*this);
        if (!mapping_system)
        {
            last_error = 8;
            return 0;
        }
    }
    auto &e = *mapping_system;
    switch (name.id)
    {
    API_CASE("CreateFileMappingA")
    {
        if (a[1])
        {
            unsigned security[3];
            if (!Read(a[1], security, sizeof security))
                return 0;
        }
        if (a[5])
            ReadString(a[5]);
        if (state == Failed)
            return 0;
        // A file mapping becomes an anonymous mapping filled from a snapshot
        // of the file (SHELL32's icon extractor maps executables this way).
        // PAGE_READWRITE additionally keeps its own duplicated file handle and
        // writes the changed bytes back at FlushViewOfFile, UnmapViewOfFile and
        // process exit, and grows the file to the mapping size the way
        // KERNEL32 does. Win98 has one shared page cache behind the file; the
        // native volume copies at those boundaries instead.
        if (a[0] != ~0u && files.Owns(a[0]))
        {
            // The page protection is the low byte. SEC_COMMIT is what a file
            // mapping is anyway and SEC_NOCACHE changes nothing here; the
            // original Create (src/win98_mapping.h) tests single bits too.
            // Need for Speed II SE maps its data with PAGE_READONLY |
            // SEC_COMMIT.
            const unsigned protection = a[2] & ~(0x08000000u | 0x10000000u);
            if (protection != 2 && protection != 4 && protection != 8) { last_error = 87; return 0; }
            const bool writable = protection == 4;
            const unsigned required = writable ? 3 : 1;
            if ((files.MappingAccess(a[0]) & required) != required)
            { last_error = 87; return 0; }
            unsigned size = 0;
            if (files.Size(a[0], size)) { last_error = 6; return 0; }
            const unsigned wanted = a[4] ? a[4] : size;
            if (!wanted || a[3]) { last_error = !wanted ? 1006 : 8; return 0; }
            unsigned file = 0;
            if (writable && files.Duplicate(a[0], files, 0, true, false, file)) { last_error = 6; return 0; }
            // KERNEL32 extends the file to the mapping size before it hands
            // out an object, so a later view sees storage and not a hole.
            if (writable && wanted > size)
            {
                unsigned grown = 0;
                if (files.Seek(file, wanted, 0, grown) || files.Truncate(file))
                { files.Close(file); last_error = 5; return 0; }
            }
            auto *snapshot = static_cast<unsigned char *>(malloc(wanted));
            if (!snapshot) { if (file) files.Close(file); last_error = 8; return 0; }
            memset(snapshot, 0, wanted);
            unsigned position = 0, done = 0;
            files.Seek(a[0], 0, 1, position);
            unsigned start = 0;
            files.Seek(a[0], 0, 0, start);
            files.Read(a[0], snapshot, size < wanted ? size : wanted, done);
            unsigned restored = 0;
            files.Seek(a[0], position, 0, restored);
            e.create_protection = a[2];
            const unsigned handle = Win98Mapping::Create(e, e.state, ~0u, a[1], a[2], 0, wanted, a[5]);
            if (!handle) { free(snapshot); if (file) files.Close(file); return 0; }
            if (auto *entry = FindKernelHandle(handle); entry && entry->kind == 5 && entry->object < 64)
            {
                auto &record = e.objects[entry->object];
                if (record.file_handle && files.Owns(record.file_handle)) files.Close(record.file_handle);
                free(record.snapshot);
                record.snapshot = snapshot;
                record.snapshot_size = done;
                // Anonymous backing is an implementation detail. File views
                // must retain the original pager's copy-on-write semantics.
                record.data.pager = protection == 8 ? e.PagerCopy() :
                    writable ? e.PagerWrite() : e.PagerRead();
                record.file_handle = file;
                record.file_bytes = wanted;
                record.file_writable = writable;
            }
            else { free(snapshot); if (file) files.Close(file); }
            if (a[5] && !e.PublishNamed(handle, a[2])) { const unsigned close[1] = {handle}; ThreadApi("CloseHandle", close); return 0; }
            return handle;
        }
        e.create_protection = a[2];
        const unsigned handle = Win98Mapping::Create(e, e.state, a[0], a[1], a[2], a[3], a[4], a[5]);
        if (handle && a[5] && !e.PublishNamed(handle, a[2]))
        { const unsigned close[1] = {handle}; ThreadApi("CloseHandle", close); return 0; }
        return handle;
    }
    API_CASE("OpenFileMappingA")
    {
        if (a[2])
            ReadString(a[2]);
        return state == Failed ? 0 : Win98Mapping::Open(e, a[0], a[1], a[2]);
    }
    API_CASE("MapViewOfFile") API_CASE("MapViewOfFileEx")
        return Win98Mapping::Map(e, e.state, a[0], a[1], a[2], a[3], a[4],
                                 API_IS(name,"MapViewOfFileEx") ? a[5] : 0);
    API_CASE("FlushViewOfFile")
    {
        for(auto *view=e.state.views;view;view=view->next)if(view->object){
            auto &object=*view->object;
            const auto end=(unsigned long long)object.base+object.size;
            if(a[0]>=object.base&&(unsigned long long)a[0]+a[1]<=end){e.WriteBack(object);return 1;}
        }
        last_error=87;return 0;
    }
    API_CASE("UnmapViewOfFile")
    {
        for (auto *view = e.state.views; view; view = view->next)
            if (view->address == a[0] && view->object) { e.WriteBack(*view->object); break; }
        // A view the resource functions read as a file image is gone with it;
        // the next view at this address is another file.
        ForgetBorrowedDatafile(a[0]);
        return Win98Mapping::Unmap(e, e.state, a[0]);
    }
    default:
        break;
    }
    Fail(name);
    return 0;
}
// VirtualAlloc(MEM_COMMIT) inside a view of a section.
unsigned Application::MappedViewBytes(unsigned address)
{
    return mapping_system ? mapping_system->ViewBytes(address) : 0;
}
unsigned Application::MappedViewCommit(unsigned address, unsigned bytes)
{
    if (!mapping_system || !bytes) return 0;
    return mapping_system->CommitInView(address, bytes);
}
} // namespace X86
