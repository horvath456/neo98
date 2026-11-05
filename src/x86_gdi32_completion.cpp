// SPDX-License-Identifier: GPL-2.0-or-later
// The GDI32 exports that were still missing from the export contract
// after the import census of 23 September 2026.
#include "x86_app.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
namespace
{
// GDI32: the monitor profiles an old display driver names by a
// single byte in REG_BINARY "ICMProfile". Index 0 stays empty.
const char *const LegacyProfiles[] = {
    nullptr,
    "mnB22G15.icm", "mnB22G18.icm", "mnB22G21.icm", "mnEBUG15.icm", "mnEBUG18.icm", "mnEBUG21.icm",
    "mnP22G15.icm", "mnP22G18.icm", "mnP22G21.icm", "Diamond Compatible 9300K G2.2.icm",
    "Hitachi Compatible 9300K G2.2.icm", "NEC Compatible 9300K G2.2.icm",
    "Trinitron Compatible 9300K G2.2.icm"};
// The device key GDI derives from the display's device node through
// CONFIGMG (functions 8Dh and 85h, request 102h): the software key of the
// display adapter below HKEY_LOCAL_MACHINE. The display of this port is the
// adapter the installation carries, Display\0000.
constexpr const char DisplayDeviceKey[] = "System\\CurrentControlSet\\Services\\Class\\Display\\0000";
} // namespace

unsigned Application::Gdi32CompletionApi(ApiName name, const unsigned *a)
{
    switch (name.id)
    {
    // is exported twice, as SetBrushOrgEx and as FixBrushOrgEx.
    API_CASE("FixBrushOrgEx")
        return GdiApi("SetBrushOrgEx", a);
    // push 2 and fall into the body of GetClipRgn, which is
    // GetRandomRgn with iNum 1. So GetMetaRgn is GetRandomRgn with
    // iNum 2, the meta region, and answers the way that routine does.
    API_CASE("GetMetaRgn")
    {
        const unsigned query[3] = {a[0], a[1], 2};
        return GdiApi("GetRandomRgn", query);
    }
    // CL 7Bh with (owner, 1) to GDI. KERNEL32 calls it for
    // every owner of a process that ends. It deletes the DCs whose
    // owner word is the argument, then every other object with that owner
    // through LocalFirst and LocalNext, and answers what its last call, a GlobalFree, left in
    // AX: zero. Owner zero means "never stamped" in this port and is not a
    // task, so it deletes nothing.
    API_CASE("ByeByeGDI")
        Gdi::DeleteObjectsOfOwner(a[0] & 0xffff);
        return 0;
    // GlobalLock, CreateDIBPatternBrushPt, GlobalUnlock, the unlock
    // even when the lock failed.
    API_CASE("CreateDIBPatternBrush")
    {
        const unsigned pointer = Api("GlobalLock", a);
        const unsigned pt[2] = {pointer, a[1]};
        const unsigned brush = Gdi32CompletionApi("CreateDIBPatternBrushPt", pt);
        Api("GlobalUnlock", a);
        return brush;
    }
    // the size of the packed DIB, then CL 33h to GDI, which
    // copies it (src/gdi_dib_brush.inc). No SetLastError on any path.
    API_CASE("CreateDIBPatternBrushPt")
    {
        const unsigned dib = a[0], usage = a[1];
        if (usage > 1 || !dib)
            return 0;
        unsigned char head[40];
        if (!Read(dib, head, 12))
            return 0;
        const auto word = [&](unsigned at) { return unsigned(head[at] | head[at + 1] << 8); };
        const auto dword = [&](unsigned at) { return word(at) | word(at + 2) << 16; };
        unsigned size = 0;
        if (dword(0) == 40)
        {
            if (!Read(dib, head, 40))
                return 0;
            if (word(12) != 1)
                return 0;
            const unsigned width = dword(4), height = dword(8), depth = word(14), compression = dword(16);
            // A negative (top-down) height is refused here; the negation
            // further down in the original is never reached.
            if ((width & 0xffff0000u) || (height & 0xffff0000u))
                return 0;
            size = 40;
            if (depth <= 8)
            {
                const unsigned used = dword(32), full = 1u << depth;
                size += (used && used <= full ? used : full) * (usage ? 2 : 4);
            }
            if (compression == 0 || compression == 3)
            {
                size += (((depth * width + 31) >> 3) & ~3u) * height;
                if (compression == 3)
                    size += 12;
            }
            else
            {
                if (!dword(20))
                    return 0;
                size += dword(20);
            }
        }
        else if (dword(0) == 12)
        {
            if (word(8) != 1)
                return 0;
            const unsigned width = word(4), height = word(6), depth = word(10);
            size = 12 + (depth <= 8 ? (usage ? 2u : 3u) << depth : 0) +
                   (((depth * width + 31) >> 3) & ~3u) * height;
        }
        else
            return 0;
        if (size > 64u << 20)
            return 0;
        auto *bytes = new unsigned char[size];
        const bool ok = Read(dib, bytes, size);
        const unsigned brush = ok ? Gdi::CreateDibPatternBrush(bytes, size, usage) : 0;
        delete[] bytes;
        return brush ? OwnGdi(brush, 1) : 0;
    }
    // for the DIB styles: 5 (BS_DIBPATTERN) and 8 (BS_DIBPATTERN8X8)
    // take lbHatch as a global handle, 6 (BS_DIBPATTERNPT) as a pointer; the
    // usage is the low word of lbColor. A fault reading the LOGBRUSH is the
    // zero of its SEH frame.
    API_CASE("CreateBrushIndirect")
    {
        unsigned logical[3] = {};
        if (!a[0] || !Read(a[0], logical, sizeof logical))
            return 0;
        const unsigned call[2] = {logical[2], logical[1] & 0xffff};
        return Gdi32CompletionApi(logical[0] == 6 ? "CreateDIBPatternBrushPt" : "CreateDIBPatternBrush", call);
    }
    // the DC signature (4F4Dh to 4F50h), then the two words at
    // DC+ACh, widened with their sign. TRUE; a null SIZE only skips the copy.
    API_CASE("GetAspectRatioFilterEx")
    {
        int x = 0, y = 0;
        if (!Gdi::AspectFilter(a[0], nullptr, x, y))
            return 0;
        if (a[1])
        {
            const int size[2] = {int(short(x)), int(short(y))};
            if (!Store(a[1], size, sizeof size))
                return 0;
        }
        return 1;
    }
    // Windows 98 checks the DC signature (answer -1), then.
    API_CASE("EnumICMProfilesA")
        return EnumIcmProfiles(a[0], a[1], a[2]);

    API_CASE("UpdateICMRegKeyA")
        return UpdateIcmRegKey(a[1], a[2], a[3]);
    default:
        break;
    }
    Fail(name);
    return 0;
}

// The profiles of a display DC come from the value "ICMProfile" of
// the display's device key; a printer DC would ask MSCMS with its DEVMODE, and
// no DC of this port has a DEVMODE yet. Each name goes to the callback with
// the colour directory in front, until the callback answers zero.
unsigned Application::EnumIcmProfiles(unsigned dc, unsigned procedure, unsigned parameter)
{
    if (!Gdi::ValidDC(dc))
        return ~0u;
    last_error = 0;
    if (!procedure || !virtual_memory.Access(procedure, 1, false))
    {
        last_error = 87;
        return ~0u;
    }
    const unsigned block = Allocate(0x300);
    if (!block)
    {
        Fail("EnumICMProfilesA: no guest memory for its buffers");
        return ~0u;
    }
    const unsigned text = block, key_slot = block + 0x110, type_slot = block + 0x114,
                   size_slot = block + 0x118, path = block + 0x180;
    Store(text, "mscms.dll", 10);
    const unsigned library = Api("LoadLibraryA", &text);
    if (!library)
    {
        Free(block);
        return ~0u;
    }
    unsigned result = ~0u, error = 8, data = 0;
    Store(text, DisplayDeviceKey, sizeof DisplayDeviceKey);
    const unsigned open[3] = {0x80000002u, text, key_slot};
    Write32(key_slot, 0);
    error = Api("RegOpenKeyA", open);
    if (!error)
    {
        Store(text, "ICMProfile", 11);
        Write32(size_slot, 0);
        const unsigned probe[6] = {Read32(key_slot), text, 0, type_slot, 0, size_slot};
        Api("RegQueryValueExA", probe);
        unsigned size = Read32(size_slot);
        if (!size)
            error = 0;
        else
        {
            if (size == 1)
                size = 0x105;
            Write32(size_slot, size);
            const unsigned alloc[2] = {0x42, size};
            const unsigned handle = Api("GlobalAlloc", alloc);
            data = handle ? Api("GlobalLock", &handle) : 0;
            if (!data)
                error = 0;
            else
            {
                const unsigned query[6] = {Read32(key_slot), text, 0, type_slot, data, size_slot};
                error = Api("RegQueryValueExA", query);
                if (!error)
                {
                    const unsigned type = Read32(type_slot);
                    unsigned char first = 0;
                    Read(data, &first, 1);
                    if (type == 3 && Read32(size_slot) == 1 && first && first < 14)
                    {
                        const char *legacy = LegacyProfiles[first];
                        Store(data, legacy, (unsigned)strlen(legacy) + 1);
                        const unsigned char terminator = 0;
                        Store(data + (unsigned)strlen(legacy) + 1, &terminator, 1);
                    }
                    if (type == 1)
                    {
                        // the base name of the path.
                        const char *name = ReadString(data);
                        const char *base = name;
                        for (const char *p = name; *p; ++p)
                            if (*p == '\\' || *p == ':')
                                base = p + 1;
                        char copy[260];
                        strncpy(copy, base, sizeof copy - 1);
                        copy[sizeof copy - 1] = 0;
                        const unsigned length = (unsigned)strlen(copy);
                        Store(data, copy, length + 1);
                        const unsigned char terminator = 0;
                        Store(data + length + 1, &terminator, 1);
                    }
                    // The directory Windows 98 read from Setup\ICMPath, and a
                    // backslash.
                    char directory[260];
                    IcmDirectory(directory);
                    const unsigned prefix = (unsigned)strlen(directory);
                    for (unsigned at = data;;)
                    {
                        const char *entry = ReadString(at);
                        if (!*entry)
                            break;
                        char full[0x200];
                        snprintf(full, sizeof full, "%s%s", directory, entry);
                        const unsigned entry_length = (unsigned)strlen(entry);
                        Store(path, full, prefix + entry_length + 1);
                        const unsigned call[2] = {path, parameter};
                        result = Call(procedure, call, 2);
                        at += entry_length + 1;
                        if (!result)
                            break;
                    }
                    error = 0;
                }
            }
        }
    }
    Api("FreeLibrary", &library);
    if (data)
    {
        const unsigned handle = Api("GlobalHandle", &data);
        Api("GlobalUnlock", &handle);
        const unsigned again = Api("GlobalHandle", &data);
        Api("GlobalFree", &again);
    }
    if (Read32(key_slot))
    {
        const unsigned key = Read32(key_slot);
        Api("RegCloseKey", &key);
    }
    if (error)
        last_error = error;
    Free(block);
    return result;
}
// The colour directory with its backslash. GDI32 starts with
// "c:\windows\system\color" and replaces it with ICMPath of
// HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\SETUP when that is there.
void Application::IcmDirectory(char (&out)[260])
{
    snprintf(out, sizeof out, "c:\\windows\\system\\color");
    unsigned handle = 0, node = 0;
    bool created = false;
    if (!registry.Open(0x80000002u, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\SETUP", false, handle, created, ""))
    {
        const Registry::Value *value = nullptr;
        if (!registry.Key(handle, node) && !registry.store.Query(node, "ICMPath", value) && value->size &&
            value->size < sizeof out)
        {
            memcpy(out, value->data, value->size);
            out[value->size - 1 < sizeof out ? value->size : sizeof out - 1] = 0;
        }
        registry.Close(handle);
    }
    const unsigned length = (unsigned)strlen(out);
    if (length + 1 < sizeof out)
    {
        out[length] = '\\';
        out[length + 1] = 0;
    }
}
// Every command goes to MSCMS, which runs as the original; without
// MSCMS the answer is zero with the loader's error.
unsigned Application::UpdateIcmRegKey(unsigned identifier, unsigned file, unsigned command)
{
    if (!file && command != 6)
    {
        last_error = 87;
        return 0;
    }
    const unsigned block = Allocate(0x400);
    if (!block)
    {
        Fail("UpdateICMRegKeyA: no guest memory for its buffers");
        return 0;
    }
    Store(block, "mscms.dll", 10);
    const unsigned library = Api("LoadLibraryA", &block);
    if (!library)
    {
        Free(block);
        return 0;
    }
    unsigned result = 0, error = 8;
    const auto procedure = [&](const char *entry) {
        Store(block, entry, (unsigned)strlen(entry) + 1);
        const unsigned query[2] = {library, block};
        return Api("GetProcAddress", query);
    };
    const auto swapped = [&](unsigned at) {
        const unsigned v = Read32(at);
        return (v >> 24) | ((v >> 8) & 0xff00) | ((v << 8) & 0xff0000) | (v << 24);
    };
    switch (command)
    {
    case 1: // ICM_ADDPROFILE
    case 2: // ICM_DELETEPROFILE
    {
        const unsigned entry = procedure(command == 1 ? "InstallColorProfileA" : "UninstallColorProfileA");
        if (entry)
        {
            const unsigned call[3] = {0, file, 0};
            result = Call(entry, call, command == 1 ? 2 : 3);
            error = 0;
        }
        break;
    }
    case 3: // ICM_QUERYPROFILE: the base name among all profiles MSCMS knows.
    {
        const unsigned entry = procedure("EnumColorProfilesA");
        if (!entry)
            break;
        const char *name = ReadString(file);
        const char *base = name;
        for (const char *p = name; *p; ++p)
            if (*p == '\\' || *p == ':')
                base = p + 1;
        char wanted[260];
        strncpy(wanted, base, sizeof wanted - 1);
        wanted[sizeof wanted - 1] = 0;
        error = 0;
        if (!*wanted)
            break;
        const unsigned type = block + 0x100, size_slot = block + 0x160;
        unsigned char zero[0x54] = {};
        Store(type, zero, sizeof zero);
        Write32(type, 0x54);
        Write32(type + 4, 0x200);
        Write32(size_slot, 0);
        const unsigned probe[5] = {0, type, 0, size_slot, 0};
        Call(entry, probe, 5);
        const unsigned size = Read32(size_slot);
        if (!size)
            break;
        const unsigned alloc[2] = {0x42, size};
        const unsigned handle = Api("GlobalAlloc", alloc);
        const unsigned list = handle ? Api("GlobalLock", &handle) : 0;
        if (!list)
        {
            error = 8;
            break;
        }
        const unsigned fetch[5] = {0, type, list, size_slot, 0};
        if (Call(entry, fetch, 5))
            for (unsigned at = list; *ReadString(at); at += (unsigned)strlen(ReadString(at)) + 1)
            {
                char left[260], right[260];
                snprintf(left, sizeof left, "%s", ReadString(at));
                snprintf(right, sizeof right, "%s", wanted);
                bool same = true;
                for (unsigned i = 0; same; ++i)
                {
                    unsigned x = (unsigned char)left[i], y = (unsigned char)right[i];
                    if (x >= 'a' && x <= 'z') x -= 32;
                    if (y >= 'a' && y <= 'z') y -= 32;
                    same = x == y;
                    if (!x || !y) break;
                }
                if (same)
                {
                    result = 1;
                    break;
                }
            }
        Api("GlobalUnlock", &handle);
        Api("GlobalFree", &handle);
        break;
    }
    case 4: // ICM_SETDEFAULTPROFILE: TRUE, MSCMS loaded and nothing done.
        result = 1;
        error = 0;
        break;
    case 5: // ICM_REGISTERICMATCHER
    case 6: // ICM_UNREGISTERICMATCHER
    {
        if (!identifier || !virtual_memory.Access(identifier, 4, false))
        {
            error = 87;
            break;
        }
        const unsigned entry = procedure(command == 5 ? "RegisterCMMA" : "UnregisterCMMA");
        if (entry)
        {
            const unsigned call[3] = {0, swapped(identifier), file};
            result = Call(entry, call, command == 5 ? 3 : 2);
            error = 0;
        }
        break;
    }
    case 7: // ICM_QUERYMATCH: `file` is a DEVMODE.
    {
        const unsigned entry = procedure("EnumColorProfilesA");
        if (!entry)
            break;
        error = 0;
        const unsigned type = block + 0x100, size_slot = block + 0x160, fields = Read32(file + 0x28);
        unsigned char zero[0x54] = {};
        Store(type, zero, sizeof zero);
        Write32(type, 0x54);
        Write32(type + 4, 0x200);
        unsigned mask = 1;
        Write32(type + 0xc, file);
        if (fields & 0x2000000) { mask |= 2; Write32(type + 0x10, Read32(file + 0x84)); }
        if (fields & 0x4000000) { mask |= 4; Write32(type + 0x14, Read32(file + 0x88)); }
        if (fields & 0x400)
        {
            mask |= 8;
            short x = 0, y = 0;
            Read(file + 0x3a, &x, 2);
            y = x;
            if (fields & 0x2000) Read(file + 0x40, &y, 2);
            Write32(type + 0x18, unsigned(int(x)));
            Write32(type + 0x1c, unsigned(int(y)));
        }
        // the DEVMODE's own fields first; when MSCMS knows no
        // profile for them and the DEVMODE names a manufacturer and a model
        // (+8Ch, +90h, byte-swapped ), again with those two in
        // place of the device name. A match is a profile MSCMS hands back.
        Write32(type + 8, mask);
        Write32(size_slot, 0);
        const unsigned probe[5] = {0, type, 0, size_slot, 0};
        Call(entry, probe, 5);
        if (!Read32(size_slot) && Read32(file + 0x8c) && Read32(file + 0x90))
        {
            Write32(type + 8, (mask | 0x1800) & ~1u);
            Write32(type + 0x3c, swapped(file + 0x8c));
            Write32(type + 0x40, swapped(file + 0x90));
            Call(entry, probe, 5);
        }
        const unsigned size = Read32(size_slot);
        if (size)
        {
            const unsigned alloc[2] = {0x42, size};
            const unsigned handle = Api("GlobalAlloc", alloc);
            const unsigned list = handle ? Api("GlobalLock", &handle) : 0;
            if (list)
            {
                const unsigned fetch[5] = {0, type, list, size_slot, 0};
                result = Call(entry, fetch, 5) ? 1 : 0;
                Api("GlobalUnlock", &handle);
                Api("GlobalFree", &handle);
            }
        }
        break;
    }
    default:
        error = 87;
        break;
    }
    Api("FreeLibrary", &library);
    if (!result && error)
        last_error = error;
    Free(block);
    return result;
}
} // namespace X86
