// SPDX-License-Identifier: GPL-2.0-or-later
// The registry of the machine comes from the hives of its drive C:.
// Win98 reads C:\WINDOWS\SYSTEM.DAT and USER.DAT when it starts. The host is
// built without the registry image of tools/import_registry.py
// (NEO98_NO_REGISTRY_IMAGE, tools/native_app.py), so the program holds nothing
// of an installation: it reads the two hives, and NETAMD.INF and NETCLI.INF
// for the keys of the LAN card, through the guest's file system from the
// drive it mounted, and the runtime decodes them (src/registry_hives.h). The
// change journal goes on top as before. docs/BUILDING.md.
#pragma once
#include "legacy_data.h"
#include "../src/files.h"
#include "../src/registry.h"
#include "../src/win98_version_info.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace HostRegistry
{
inline bool ReadGuestFile(Files::Session &session, const char *path, std::vector<unsigned char> &bytes)
{
    unsigned handle = 0, status = 0, size = 0, done = 0;
    bytes.clear();
    if (session.Open(path, 1, 1, 3, 0, handle, status))
        return false;
    bool okay = !session.Size(handle, size) && size < 64u * 1024 * 1024;
    if (okay)
    {
        bytes.resize(size);
        okay = !size || (!session.Read(handle, bytes.data(), size, done) && done == size);
    }
    session.Close(handle);
    if (!okay)
        bytes.clear();
    return okay;
}

// Call once after the drives are mounted and the journal is attached, before
// anything else uses the registry. Builds the store and reports what it
// started from; false when the hives are missing or do not decode.
inline bool Start(FILE *output = stdout)
{
    std::vector<unsigned char> system, user, netamd, netcli;
    Registry::Hives hives;
    {
        Files::Session session(Files::System());
        const bool found = ReadGuestFile(session, "C:\\WINDOWS\\SYSTEM.DAT", system) &&
                           ReadGuestFile(session, "C:\\WINDOWS\\USER.DAT", user);
        if (found)
        {
            hives.system = system.data();
            hives.system_size = unsigned(system.size());
            hives.user = user.data();
            hives.user_size = unsigned(user.size());
            if (ReadGuestFile(session, "C:\\WINDOWS\\INF\\NETAMD.INF", netamd) &&
                ReadGuestFile(session, "C:\\WINDOWS\\INF\\NETCLI.INF", netcli))
            {
                hives.netamd = netamd.data();
                hives.netamd_size = unsigned(netamd.size());
                hives.netcli = netcli.data();
                hives.netcli_size = unsigned(netcli.size());
            }
            Registry::StartFrom(&hives);
        }
    }
    const auto &store = Registry::System();
    Registry::StartFrom(nullptr);
    const unsigned error = Registry::SystemInitializationError();
    static const char *const sources[] = {"nothing", "image", "hives C:\\WINDOWS\\SYSTEM.DAT C:\\WINDOWS\\USER.DAT"};
    fprintf(output, "NEO98 HOST registry starts from %s keys=%u values=%u lan=%u error=%u\n",
           sources[store.start.source < 3 ? store.start.source : 0], store.start.keys, store.start.values,
           store.start.network ? 1u : 0u, error);
    fflush(output);
    return store.start.source == 2 && !error;
}

// setup_win98.py ends QEMU from its NEO98Finish RunOnce command. Older
// exported hives still say HWSetup="1" although.neo98-setup.json certifies
// the completed import. Original RUNONCE 402205..402276 reads this flag;
// 402667..40268E refuses an ordinary installer's RunOnce while it is set.
// Finish that import once. A later guest setup may set HWSetup again and
// must keep it, so the migration has its own persisted completion marker.
inline bool FinishImportedSetup(Registry::Store &store, const char *root)
{
    if (!root || !*root || store.import_error) return false;
    const std::string path = std::string(root) + "/.neo98-setup.json";
    FILE *marker = fopen(LegacyData::ReadPath(path).c_str(), "rb");
    if (!marker) return false;
    fclose(marker);
    unsigned migration = 0, setup = 0;
    bool created = false;
    const Registry::Value *value = nullptr;
    const std::string old_key = "Software\\" + LegacyData::Brand() + "\\ImportedSetup";
    if (!store.Open(store.roots[2], old_key.c_str(), false, migration, created) &&
        !store.Query(migration, "Finished", value)) return false;
    static const char key[] = "Software\\NEO98\\ImportedSetup";
    if (!store.Open(store.roots[2], key, false, migration, created) &&
        !store.Query(migration, "Finished", value)) return false;
    if (store.Open(store.roots[2], "Software\\Microsoft\\Windows\\CurrentVersion\\Setup",
                   false, setup, created)) return false;
    const unsigned found = store.Query(setup, "HWSetup", value);
    if (found != 0 && found != 2) return false;
    if (!found) {
        // Only the exact flag left by our exported installation is repaired.
        if (value->type != 1 || (value->size != 1 && value->size != 2) ||
            value->data[0] != '1' || (value->size == 2 && value->data[1])) return false;
        if (store.DeleteValue(setup, "HWSetup")) return false;
    }
    if (store.Open(store.roots[2], key, true, migration, created)) return false;
    const unsigned finished = 1;
    return !store.Set(migration, "Finished", 4, &finished, sizeof finished);
}

// A folder populated with the redistributable's files can still have the
// registry version of setup's 6.1 installation. Read the installed runtime,
// after WININIT has replaced pending files, before dxdiag or an application
// queries it. A partial update must not advertise a newer DirectX runtime.
inline bool SyncDirectXVersion()
{
    if (Registry::SystemInitializationError()) return false;
    struct Resources final : Win98VersionInfo::Resources
    {
        Files::Session files{Files::System()};
        std::vector<unsigned char> bytes;
        const unsigned char *Image(const char *path, unsigned &size) override
        {
            if (!ReadGuestFile(files, path, bytes)) { size = 0; return nullptr; }
            size = unsigned(bytes.size());
            return bytes.data();
        }
        bool Version(const char *path, unsigned &high, unsigned &low)
        {
            unsigned offset = 0;
            if (!Win98VersionInfo::InfoSize(*this, path, offset)) return false;
            unsigned char raw[0x5c] = {};
            Win98VersionInfo::BufferMemory block(raw, sizeof raw);
            if (!Win98VersionInfo::Info(*this, block, path, offset, sizeof raw,
                                       Win98VersionInfo::BufferMemory::Base, true)) return false;
            const unsigned fixed = Win98VersionInfo::Word(raw, 4) > 0x1f ? 0x14 : 0x28;
            high = Win98VersionInfo::Long(raw, fixed + 8);
            low = Win98VersionInfo::Long(raw, fixed + 12);
            return true;
        }
    } resources;
    unsigned high = 0, low = 0, other_high = 0, other_low = 0;
    if (!resources.Version("C:\\WINDOWS\\SYSTEM\\DSOUND.DLL", high, low) ||
        !resources.Version("C:\\WINDOWS\\SYSTEM\\DDRAW.DLL", other_high, other_low) ||
        high != other_high || low != other_low || high != 0x00040009 || low != 0x00000388 ||
        !resources.Version("C:\\WINDOWS\\SYSTEM\\D3D9.DLL", other_high, other_low) ||
        high != other_high || low != other_low) return false;
    auto &store = Registry::System();
    unsigned key = 0;
    bool created = false;
    if (store.Open(store.roots[2], "Software\\Microsoft\\DirectX", false, key, created)) return false;
    const Registry::Value *value = nullptr;
    // Only repair the stale value of the vanilla Win98 installation. Leave
    // another installer's version alone, including later service releases.
    if (store.Query(key, "Version", value) || !value || value->type != 1 || value->size < 12 ||
        value->size > 14 || memcmp(value->data, "4.06.03.0518", 12)) return false;
    static const char version[] = "4.09.00.0904";
    return !store.Set(key, "Version", 1, version, sizeof version);
}
} // namespace HostRegistry

// The system font the runtime starts with is the FNT resource of
// C:\WINDOWS\FONTS\VGAFIX.FON (Fixedsys). The command line of the development
// names a file with those bytes (build/generated/notepad-fixedsys.fnt); the
// app names the font file of the drive by its guest path, and the host takes
// the resource out of it, so the bundle carries no font of Win98.
namespace HostSystemFont
{
inline bool GuestPath(const char *text) { return text && text[0] && text[1] == ':' && text[2] == '\\'; }
// The first RT_FONT resource of an NE font file, cut to the size its header
// declares (dfSize), as tools/unicorn_assets.py extracts it.
inline bool FirstFont(const std::vector<unsigned char> &fon, std::vector<unsigned char> &font)
{
    const auto word = [&](size_t at) -> unsigned { return at + 2 <= fon.size() ? fon[at] | fon[at + 1] << 8 : 0u; };
    const auto dword = [&](size_t at) -> unsigned { return word(at) | word(at + 2) << 16; };
    if (fon.size() < 64 || fon[0] != 'M' || fon[1] != 'Z')
        return false;
    const size_t ne = dword(60);
    if (word(ne) != 0x454e)
        return false;
    size_t at = ne + word(ne + 36);
    const unsigned shift = word(at);
    at += 2;
    if (shift > 16)
        return false;
    while (at + 8 <= fon.size() && word(at))
    {
        const unsigned kind = word(at), count = word(at + 2);
        at += 8;
        for (unsigned i = 0; i < count; ++i, at += 12)
        {
            if (at + 12 > fon.size())
                return false;
            const size_t offset = size_t(word(at)) << shift, length = size_t(word(at + 2)) << shift;
            if (kind != 0x8008)
                continue;
            if (length < 118 || offset + length > fon.size())
                return false;
            const unsigned declared = dword(offset + 2);
            if (declared < 118 || declared > length)
                return false;
            font.assign(fon.begin() + long(offset), fon.begin() + long(offset + declared));
            return true;
        }
    }
    return false;
}
inline bool Load(const char *guest_path, std::vector<unsigned char> &font)
{
    Files::Session session(Files::System());
    std::vector<unsigned char> fon;
    return HostRegistry::ReadGuestFile(session, guest_path, fon) && FirstFont(fon, font);
}
} // namespace HostSystemFont
