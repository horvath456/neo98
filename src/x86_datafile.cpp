// SPDX-License-Identifier: GPL-2.0-or-later
// LOAD_LIBRARY_AS_DATAFILE modules. The steps follow the original KERNEL32
// body : find the file, open it for reading, require an MZ
// signature, map it read-only in one piece, require a PE signature and a
// resource directory, and answer the view address with bit 0 set. Nothing is
// relocated, no import is bound and no DllMain runs. FreeLibrary
// recognizes the odd handle and unmaps the view again.
#include "x86_app.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
namespace
{
const char *Leaf(const char *name)
{
    const char *result = name;
    for (const char *p = name; *p; ++p)
        if (*p == '\\' || *p == '/')
            result = p + 1;
    return result;
}
} // namespace
// The system arena keeps its loader permissions. In the private address
// range Win98 permits VirtualProtect even on IMAGE_SCN_MEM_SHARED sections
// (native repertoire_pe probe). The section flag does not by itself
// make the protection immutable. Quake II also changes private code pages.
bool Application::ImageRightsFixed(unsigned address, unsigned size) const
{
    const unsigned long long end = (unsigned long long)address + size;
    for (unsigned i = 0; i < module_watermark; ++i)
    {
        const auto &m = modules[i];
        if (!m.pe || !m.handle)
            continue;
        const unsigned long long base = m.pe->Base(), image_end = base + m.pe->SizeOfImage();
        if (address >= image_end || base >= end)
            continue;
        if (base >= 0x80000000u)
            return true;
    }
    return false;
}
unsigned Application::ResourceModule::Address(unsigned rva) const
{
    if (!pe) return 0;
    if (!datafile) return pe->Base() + rva;
    const unsigned offset = pe->FileOffset(rva);
    return offset == ~0u ? 0 : view + offset;
}
Application::DatafileModule *Application::DatafileAt(unsigned handle)
{
    if (!(handle & 1)) return nullptr;
    for (auto &d : datafiles)
        if (d.handle && d.handle == handle)
        {
            // A borrowed view may have been unmapped since.
            if (d.borrowed && MappedViewBytes(d.base) != d.size)
            {
                delete d.pe;
                delete[] d.file;
                d = {};
                return nullptr;
            }
            return &d;
        }
    return nullptr;
}
void Application::ForgetBorrowedDatafile(unsigned base)
{
    for (auto &d : datafiles)
        if (d.handle && d.borrowed && d.base == base)
        {
            for (auto &resource : resource_handles)
                if (resource.id && resource.module == d.handle) resource = {};
            delete d.pe;
            delete[] d.file;
            d = {};
        }
}
// The resource functions of KERNEL32 take any module handle with bit 0 set
// as the address of a PE file mapped in one piece (Windows 98 clears the bit
// to read the file layout); they do not ask whether
// LoadLibraryEx made it. DSS.DLL of IE 5.01 checks its own signature in
// DllMain that way: OpenFile, CreateFileMapping, MapViewOfFile, and then
// FindResourceA(view | 1, "VerifyThis", RT_RCDATA). Such a view is
// read once into a record of its own; it stays the program's.
Application::DatafileModule *Application::AdoptViewAsDatafile(unsigned handle)
{
    const unsigned base = handle & ~1u, size = MappedViewBytes(base);
    if (!size || size < 64) return nullptr;
    DatafileModule *slot = nullptr;
    for (auto &d : datafiles)
        if (!d.handle) { slot = &d; break; }
    if (!slot) return nullptr;
    auto *data = new unsigned char[size];
    if (!data) return nullptr;
    if (!Read(base, data, size))
    {
        delete[] data;
        return nullptr;
    }
    auto *pe = new Pe32::Image;
    if (!pe || !pe->Parse(data, size) || !pe->HasResources())
    {
        delete pe;
        delete[] data;
        return nullptr;
    }
    *slot = {};
    slot->handle = handle;
    slot->base = base;
    slot->size = size;
    slot->pe = pe;
    slot->file = data;
    slot->borrowed = true;
    return slot;
}
bool Application::ResolveResourceModule(unsigned handle, ResourceModule &out)
{
    out = {};
    if (handle & 1)
    {
        auto *view = DatafileAt(handle);
        if (!view) view = AdoptViewAsDatafile(handle);
        if (!view) { last_error = 6; return false; }
        out = {view->pe, view->base, handle, true};
        return true;
    }
    const unsigned index = handle ? ModuleIndex(handle, true) : 0;
    if (index >= ModuleCapacity) { last_error = 6; return false; }
    if (!modules[index].pe) { last_error = 1812; return false; }
    out = {modules[index].pe, 0, index, false};
    return true;
}
unsigned Application::LoadDatafileModule(const char *input)
{
    // The original searches the same way an ordinary load does and gives up
    // with the search error of the lower layer;.
    if (!input || !input[0]) { last_error = 87; return 0; }
    char name[260];
    if (strlen(input) > 255) { last_error = 206; return 0; }
    strcpy(name, input);
    char path[260] = {};
    const bool bare = Leaf(name) == name && !strchr(name, ':');
    unsigned result = 126;
    if (!bare)
        result = files.FullPath(name, path);
    else
    {
        char searched[600];
        SearchModuleFile(name, path, searched, sizeof searched);
        result = path[0] ? 0 : 2;
    }
    unsigned handle = 0, status = 0;
    // CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0).
    if (!result)
        result = files.Open(path, 1, 1, 3, 0, handle, status);
    if (result) { last_error = result == 3 ? 2u : result; return 0; }
    unsigned size = 0, done = 0;
    result = files.Size(handle, size);
    if (!result && size < 2) result = 11;
    unsigned char *data = !result ? new unsigned char[size] : nullptr;
    if (!result && !data) result = 8;
    if (!result) result = files.Read(handle, data, size, done);
    files.Close(handle);
    if (result || done != size)
    {
        delete[] data;
        last_error = result ? result : 11;
        return 0;
    }
    // the first two bytes must read MZ, before the mapping exists.
    if (data[0] != 'M' || data[1] != 'Z')
    {
        delete[] data;
        last_error = 11;
        return 0;
    }
    DatafileModule *slot = nullptr;
    for (auto &d : datafiles)
        if (!d.handle) { slot = &d; break; }
    if (!slot)
    {
        delete[] data;
        last_error = 8;
        return 0;
    }
    auto *pe = new Pe32::Image;
    // Windows 98 accepts every PE32 image, program or library alike, and
    // then insists on a resource directory; without one the original
    // unmaps the view again and reports ERROR_BAD_FORMAT.
    if (!pe || !pe->Parse(data, size) || !pe->HasResources())
    {
        delete pe;
        delete[] data;
        last_error = 11;
        return 0;
    }
    const unsigned pages = (size + 4095) >> 12;
    const unsigned base = ReserveMappingPages(0x80060000, pages, 0x40000);
    if (base == ~0u || !CommitMappingPages(base >> 12, pages, 0x40000))
    {
        if (base != ~0u) ReleaseMappingPages(base);
        delete pe;
        delete[] data;
        last_error = 8;
        return 0;
    }
    // The view is read-only for the guest, so the fill is the loader's write,
    // the same exception the image loader uses for an import table.
    ++loader_writes;
    const bool stored = Store(base, data, size);
    --loader_writes;
    if (!stored)
    {
        ReleaseMappingPages(base);
        delete pe;
        delete[] data;
        last_error = 8;
        return 0;
    }
    slot->handle = base | 1;
    slot->base = base;
    slot->size = size;
    slot->pe = pe;
    slot->file = data;
    snprintf(slot->path, sizeof slot->path, "%s", path[0] ? path : name);
    return slot->handle;
}
bool Application::FreeDatafileModule(unsigned handle)
{
    auto *view = DatafileAt(handle);
    if (!view) return false;
    // Every resource handle of this view dies with it, the way the addresses
    // it handed out stop being readable.
    for (auto &resource : resource_handles)
        if (resource.id && resource.module == handle) resource = {};
    if (!view->borrowed)
        ReleaseMappingPages(view->base);
    delete view->pe;
    delete[] view->file;
    *view = {};
    return true;
}
} // namespace X86
