// SPDX-License-Identifier: GPL-2.0-or-later
// Win16 VER provider. Marshal version-query arguments and results between the
// flat thunk interface and src/win98_version_info.h.
#include "win98_szdd.h"
#include "win98_version_info.h"
#include "x86_app.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
// The catalogue ordinals of VER.DLL. `stack` addresses the last word the
// 32-bit stub pushed, so an argument pushed earlier lies above it.
unsigned Application::VersionInfo16(unsigned ordinal, unsigned stack, bool sixteen)
{
    namespace V = Win98VersionInfo;
    struct GuestMemory final : V::Memory
    {
        Application &app;
        explicit GuestMemory(Application &a) : app(a) {}
        unsigned Read8(unsigned address) override
        {
            unsigned char value = 0;
            app.Read(address, &value, 1);
            return value;
        }
        unsigned Read16(unsigned address) override
        {
            unsigned short value = 0;
            app.Read(address, &value, 2);
            return value;
        }
        unsigned Read32(unsigned address) override { return app.Read32(address); }
        void Write8(unsigned address, unsigned value) override
        {
            const unsigned char byte = (unsigned char)value;
            app.Store(address, &byte, 1);
        }
        void Write16(unsigned address, unsigned value) override
        {
            const unsigned short word = (unsigned short)value;
            app.Store(address, &word, 2);
        }
        void Write32(unsigned address, unsigned value) override { app.Write32(address, value); }
        void Copy(unsigned to, unsigned from, unsigned bytes) override
        {
            for (unsigned i = 0; i < bytes; ++i) Write8(to + i, Read8(from + i));
        }
        bool Readable(unsigned address, unsigned bytes) override
        {
            return app.virtual_memory.Access(address, bytes, false);
        }
        bool Writable(unsigned address, unsigned bytes) override
        {
            return app.virtual_memory.Access(address, bytes, true);
        }
    } memory(*this);

    // One guest scratch page for the strings and buffers the native KERNEL32
    // entries want to see in the process's own memory.
    struct Scratch
    {
        Application &app;
        unsigned address = 0;
        explicit Scratch(Application &a) : app(a) { address = a.Allocate(4096); }
        ~Scratch() { if (address) app.Free(address); }
        unsigned Put(const char *text, unsigned slot)
        {
            if (!address || !text) return 0;
            unsigned length = 0;
            while (text[length]) ++length;
            if (length >= 512) return 0;
            const unsigned at = address + slot * 512;
            app.Store(at, text, length + 1);
            return at;
        }
        unsigned Slot(unsigned slot) const { return address ? address + slot * 512 : 0; }
    } scratch(*this);

    struct GuestResources final : V::Resources
    {
        Application &app;
        unsigned char *expanded = nullptr;
        unsigned expanded_size = 0;
        char cached[260] = {};
        explicit GuestResources(Application &a) : app(a) {}
        ~GuestResources() override { delete[] expanded; }
        const unsigned char *Image(const char *path, unsigned &size) override
        {
            size = 0;
            if (!path || !*path) return nullptr;
            if (expanded && !strcmp(cached, path)) { size = expanded_size; return expanded; }
            char full[260] = {};
            unsigned node = 0;
            if (app.files.FullPath(path, full) || app.files.store.Lookup(full, node))
            {
                // LZOpenFile(OF_READ) searches a bare name the way OpenFile
                // does: after the current directory come the Windows and the
                // system directory. MSVIDEO hands the bare driver name of its
                // SYSTEM.INI line.
                if (strchr(path, '\\') || strchr(path, ':')) return nullptr;
                bool found = false;
                static const char *const folders[] = {"C:\\WINDOWS\\", "C:\\WINDOWS\\SYSTEM\\"};
                for (const char *folder : folders)
                {
                    char candidate[260];
                    snprintf(candidate, sizeof candidate, "%s%s", folder, path);
                    if (!app.files.FullPath(candidate, full) && !app.files.store.Lookup(full, node)) { found = true; break; }
                }
                if (!found) return nullptr;
            }
            const auto *record = app.files.store.Get(node);
            const unsigned char *bytes = record ? app.files.store.Bytes(node) : nullptr;
            if (!record || !bytes) return nullptr;
            if (!Win98Szdd::Header(bytes, record->size)) { size = record->size; return bytes; }
            // LZOpenFile hands an SZDD file over expanded, so every offset the
            // implementation computes is an offset in the expanded stream.
            const unsigned length = Win98Szdd::Size(bytes);
            auto *out = new unsigned char[length ? length : 1];
            if (!out || Win98Szdd::Decode(bytes, record->size, out, length)) { delete[] out; return nullptr; }
            delete[] expanded;
            expanded = out;
            expanded_size = length;
            unsigned i = 0;
            for (; path[i] && i + 1 < sizeof cached; ++i) cached[i] = path[i];
            cached[i] = 0;
            size = length;
            return expanded;
        }
    } resources(*this);

    struct GuestFiles final : V::FileSystem
    {
        Application &app;
        Scratch &scratch;
        GuestResources &resources;
        GuestFiles(Application &a, Scratch &s, GuestResources &r) : app(a), scratch(s), resources(r) {}
        unsigned Attributes(const char *path) override
        {
            const unsigned text = scratch.Put(path, 0);
            if (!text) return 0xffffffffu;
            const unsigned arguments[] = {text};
            return app.Api("GetFileAttributesA", arguments);
        }
        bool Exists(const char *path, char *full, unsigned size) override
        {
            const unsigned text = scratch.Put(path, 0);
            const unsigned out = scratch.Slot(1);
            if (!text || !out) return false;
            // OpenFile(OF_EXIST) searches the same list SearchPath walks.
            const unsigned arguments[] = {0, text, 0, 512, out, 0};
            if (!app.Api("SearchPathA", arguments)) return false;
            V::Copy(full, app.ReadString(out), size);
            return true;
        }
        bool Readable(const char *path) override { return Attributes(path) != 0xffffffffu; }
        int Copy(const char *from, const char *to, unsigned &error) override
        {
            error = 0;
            unsigned size = 0;
            const unsigned char *image = resources.Image(from, size);
            if (!image) return -1; // LZERROR_BADINHANDLE
            const unsigned name = scratch.Put(to, 0), buffer = scratch.Slot(1);
            if (!name || !buffer) return -5; // LZERROR_GLOBALLOC
            const unsigned create[] = {name, 0x40000000u, 0, 0, 2, 0x80, 0};
            const unsigned handle = app.Api("CreateFileA", create);
            if (handle == 0xffffffffu || !handle)
            {
                error = app.last_error;
                return 0; // the create was refused
            }
            int result = 1;
            for (unsigned at = 0; at < size; at += 512)
            {
                const unsigned step = size - at < 512 ? size - at : 512;
                app.Store(buffer, image + at, step);
                const unsigned written[] = {handle, buffer, step, scratch.Slot(2), 0};
                if (!app.Api("WriteFile", written) || app.Read32(scratch.Slot(2)) != step)
                {
                    result = -4; // LZERROR_WRITE
                    break;
                }
            }
            const unsigned close[] = {handle};
            app.Api("CloseHandle", close);
            return result;
        }
        bool Remove(const char *path, unsigned &error) override
        {
            const unsigned text = scratch.Put(path, 0);
            if (!text) return false;
            const unsigned arguments[] = {text};
            if (app.Api("DeleteFileA", arguments)) return true;
            error = app.last_error;
            return false;
        }
        unsigned Rename(const char *from, const char *to) override
        {
            const unsigned a = scratch.Put(from, 0), b = scratch.Put(to, 1);
            if (!a || !b) return 3;
            const unsigned arguments[] = {a, b};
            return app.Api("MoveFileA", arguments) ? 0u : app.last_error;
        }
        bool Loaded(const char *leaf, const char *full) override
        {
            const unsigned text = scratch.Put(leaf, 0), out = scratch.Slot(1);
            if (!text || !out) return false;
            const unsigned handle[] = {text};
            const unsigned module = app.Api("GetModuleHandleA", handle);
            if (module <= 1) return false;
            const unsigned name[] = {module, out, 260};
            if (!app.Api("GetModuleFileNameA", name)) return false;
            return V::SameText(app.ReadString(out), full);
        }
        void WindowsDirectory(char *out, unsigned size) override { Directory("GetWindowsDirectoryA", out, size); }
        void SystemDirectory(char *out, unsigned size) override { Directory("GetSystemDirectoryA", out, size); }
        void Directory(const char *api, char *out, unsigned size)
        {
            const unsigned buffer = scratch.Slot(1);
            if (out && size) out[0] = 0;
            if (!buffer) return;
            const unsigned arguments[] = {buffer, 260};
            if (app.Api(api, arguments)) V::Copy(out, app.ReadString(buffer), size);
        }
        // LoadVerInfo, without the 16-bit local heap: the block of one
        // file, read through the same two routines the original calls.
        unsigned VersionBlock(const char *path, unsigned char *out, unsigned size) override
        {
            unsigned handle = 0;
            const unsigned length = V::InfoSize(resources, path, handle);
            if (!length || length > size) return 0;
            V::BufferMemory block(out, size);
            if (!V::Info(resources, block, path, handle, length, V::BufferMemory::Base, false)) return 0;
            return length;
        }
        // The original clears VIFF_FORCEINSTALL only for a value whose
        // data is exactly 0x13e bytes long, which is what it asked for.
        bool CheckVerDll(const char *leaf) override
        {
            const unsigned path = scratch.Put(
                "System\\CurrentControlSet\\Control\\SessionManager\\CheckVerDLLs", 0);
            const unsigned name = scratch.Put(leaf, 1), key = scratch.Slot(2), bytes = scratch.Slot(3),
                           data = scratch.Slot(4);
            if (!path || !name || !key) return false;
            const unsigned open[] = {0x80000002u, path, 0, 0x20019, key};
            if (app.Api("RegOpenKeyExA", open)) return false;
            app.Write32(bytes, 0x13e);
            const unsigned query[] = {app.Read32(key), name, 0, 0, data, bytes};
            const bool found = !app.Api("RegQueryValueExA", query) && app.Read32(bytes) == 0x13e;
            const unsigned close[] = {app.Read32(key)};
            app.Api("RegCloseKey", close);
            return found;
        }
    } files(*this, scratch, resources);

    auto flat = [&](unsigned at) { return selectors.Linear(Read32(stack + at)); };
    auto word = [&](unsigned at) {
        unsigned short value = 0;
        Read(stack + at, &value, 2);
        return unsigned(value);
    };
    auto text = [&](unsigned at) { return ReadString(flat(at)); };
    switch (ordinal)
    {
    case 2: // GETFILERESOURCESIZE, only for 16-bit callers
    {
        // (lpszFileName, lpszResType, lpszResID, lpdwFileOffset). SETUPX
        // asks GetProcAddress for it with the seven others and drops VER.DLL
        // altogether when one of them is missing.
        char path[260] = {};
        V::Copy(path, text(12), sizeof path);
        unsigned offset = 0;
        bool handed = false;
        const unsigned size =
            V::FileResourceSize(resources, path, Read32(stack + 8), Read32(stack + 4), offset, &handed);
        if (handed)
            if (const unsigned out = flat(0)) Write32(out, offset);
        return size;
    }
    case 3: // GETFILERESOURCE, only for 16-bit callers
    {
        // (lpszFileName, lpszResType, lpszResID, dwFileOffset, dwResLen,
        // lpvData). Answers BOOL in AX.
        char path[260] = {};
        V::Copy(path, text(20), sizeof path);
        return V::FileResource(resources, memory, path, Read32(stack + 16), Read32(stack + 12),
                               Read32(stack + 8), Read32(stack + 4), flat(0));
    }
    case 20: // GETFILEVERSIONINFORAW, : without the conversion
    {
        char path[260] = {};
        V::Copy(path, text(12), sizeof path);
        const unsigned handle = Read32(stack + 8), bytes = Read32(stack + 4), buffer = flat(0);
        return V::Info(resources, memory, path, handle, bytes, buffer, true) ? 1u : 0u;
    }
    case 6: // GETFILEVERSIONINFOSIZE, VERSION.DLL, thunk index 5
    {
        char path[260] = {};
        V::Copy(path, text(4), sizeof path);
        unsigned handle = 0;
        const unsigned size = V::InfoSize(resources, path, handle);
        if (const unsigned out = flat(0)) Write32(out, handle);
        return size;
    }
    case 7: // GETFILEVERSIONINFO, index 4
    {
        char path[260] = {};
        V::Copy(path, text(12), sizeof path);
        const unsigned handle = Read32(stack + 8), bytes = Read32(stack + 4), buffer = flat(0);
        return V::Info(resources, memory, path, handle, bytes, buffer, false) ? 1u : 0u;
    }
    case 8: // VERFINDFILE, index 3
    {
        char file[260] = {}, app_dir[260] = {}, current[260] = {}, destination[260] = {};
        V::Copy(file, text(24), sizeof file);
        V::Copy(app_dir, text(16), sizeof app_dir);
        V::FindRequest request;
        request.flags = word(28);
        request.file = file;
        request.app_dir = flat(16) ? app_dir : nullptr;
        request.current_dir = current;
        request.current_length = flat(8) ? memory.Read16(flat(8)) : 0;
        request.destination_dir = flat(4) ? destination : nullptr;
        request.destination_length = flat(0) ? memory.Read16(flat(0)) : 0;
        if (request.current_length > sizeof current) request.current_length = sizeof current;
        if (request.destination_length > sizeof destination)
            request.destination_length = sizeof destination;
        unsigned current_out = 0, destination_out = 0;
        const unsigned result = V::FindFile(files, version_find, request, current_out, destination_out);
        if (flat(12) && !(result & 4)) Store(flat(12), current, V::Length(current) + 1);
        if (flat(8)) memory.Write16(flat(8), current_out);
        if (request.destination_dir && !(result & 4))
            Store(flat(4), destination, V::Length(destination) + 1);
        if (request.destination_dir && flat(0)) memory.Write16(flat(0), destination_out);
        return result;
    }
    case 9: // VERINSTALLFILE, index 2
    {
        char source[260] = {}, destination[260] = {}, source_dir[260] = {}, destination_dir[260] = {},
             current_dir[260] = {}, temp[260] = {};
        V::Copy(source, text(24), sizeof source);
        V::Copy(destination, text(20), sizeof destination);
        V::Copy(source_dir, text(16), sizeof source_dir);
        V::Copy(destination_dir, text(12), sizeof destination_dir);
        V::Copy(current_dir, text(8), sizeof current_dir);
        V::Copy(temp, text(4), sizeof temp);
        V::InstallRequest request;
        request.flags = word(28);
        request.source = source;
        request.destination = destination;
        request.source_dir = flat(16) ? source_dir : nullptr;
        request.destination_dir = flat(12) ? destination_dir : nullptr;
        request.current_dir = flat(8) ? current_dir : nullptr;
        request.temp = temp;
        request.temp_length = flat(0) ? memory.Read16(flat(0)) : 0;
        if (request.temp_length > sizeof temp) request.temp_length = sizeof temp;
        unsigned length = 0;
        const unsigned result = V::InstallFile(files, request, length);
        if ((result & 1) && flat(4)) Store(flat(4), temp, V::Length(temp) + 1);
        if ((result & 0x40001) && flat(0)) memory.Write16(flat(0), length);
        return result;
    }
    case 10: // VERLANGUAGENAME, index 1
        return V::LanguageName(memory, word(6), selectors.Linear(Read32(stack + 2)), word(0));
    default: // 11 VERQUERYVALUE, index 0
    {
        const unsigned block = flat(12), buffer = flat(4), length = flat(0);
        char sub[520] = {};
        V::Copy(sub, ReadString(flat(8)), sizeof sub);
        unsigned value = 0, bytes = 0;
        if (!V::QueryValue(memory, block, sub, value, bytes)) return 0;
        // The 32-bit stub at takes the low word of what the 16-bit
        // routine wrote and adds the flat block address, so that word has to
        // be the value's offset inside the block.
        // A 16-bit caller gets the far pointer itself: the block's selector
        // and its offset plus the distance.
        const unsigned far_block = Read32(stack + 12);
        const unsigned offset = (sixteen ? far_block : 0) + (value - block);
        if (buffer) Write32(buffer, (far_block & 0xffff0000u) | (offset & 0xffffu));
        if (length) memory.Write16(length, bytes);
        return 1;
    }
    }
}
} // namespace X86
