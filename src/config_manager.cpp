// SPDX-License-Identifier: GPL-2.0-or-later
// HKEY_DYN_DATA\Config Manager\Enum, the devnodes the configuration manager
// publishes.
// Under Win98 CONFIGMG, part of VMM32, builds the device tree at start and
// keeps one key per live devnode under HKEY_DYN_DATA\Config Manager\Enum,
// named after the devnode, with the value HardWareKey pointing into
// HKLM\Enum. The printer spooler depends on it: the local monitor of
// SPOOLSS lists the ports it can print to by walking these keys and
// keeping the devnodes whose HKLM\Enum key has Class "Ports" and a PortName.
// A port that is only named in WIN.INI [ports] is never listed.
// Every device instance of the installed hive is a devnode here: the machine
// Win98 was installed on is the machine this port presents (the serial port
// COM1 of src/x86_kernel_comm.inc and the parallel port LPT1 of
// src/x86_kernel_lpt.cpp among them).
// The 16-bit API of CONFIGMG over the same devnodes is
// src/win16_config_manager.cpp.
#include "neo98_hardware.h"
#include "host_devices.h"
#include "registry.h"
#include <stdio.h>
#include <string.h>
namespace Registry
{
namespace
{
bool Same(const char *a, const char *b)
{
    for (;; ++a, ++b)
    {
        unsigned x = (unsigned char)*a, y = (unsigned char)*b;
        if (x >= 'a' && x <= 'z') x -= 32;
        if (y >= 'a' && y <= 'z') y -= 32;
        if (x != y) return false;
        if (!x) return true;
    }
}
bool Text(const Store &store, U key, const char *name, char *out, U capacity)
{
    const Value *value = nullptr;
    if (store.Query(key, name, value) || !value || !value->size || value->size >= capacity)
        return false;
    memcpy(out, value->data, value->size);
    out[value->size] = 0;
    return true;
}
} // namespace

// The instance keys three levels below HKLM\Enum (enumerator, device,
// instance) in stored order, without the reserved HTREE\RESERVED\0 of the
// hive. Every one is a devnode of the configuration manager; the handle of
// the i-th is C29A0000h + 1000h + i * 80h, the name of its key under
// HKEY_DYN_DATA\Config Manager\Enum (src/win16_config_manager.cpp).
U DevnodeHandle(const Store &store, U key)
{
    auto &node = store.nodes[key];
    if (!node.devnode_handle && store.next_devnode < 65536)
        node.devnode_handle = 0xc29a1000u + store.next_devnode++ * 0x80u;
    return node.devnode_handle;
}

U Devnodes(const Store &store, U enumeration, U *keys, U capacity)
{
    U count = 0, enumerator = 0;
    for (U i = 0; !store.EnumKey(enumeration, i, enumerator); ++i)
    {
        if (Same(store.nodes[enumerator].name, "HTREE"))
            continue;
        U device = 0;
        for (U j = 0; !store.EnumKey(enumerator, j, device); ++j)
        {
            U instance = 0;
            for (U k = 0; !store.EnumKey(device, k, instance); ++k)
                if (count < capacity)
                    keys[count++] = instance;
        }
    }
    // Assign in initial enumeration order, then retain those identities.
    for (U i = 0; i < count; ++i) DevnodeHandle(store, keys[i]);
    return count;
}

// "Disable in this hardware profile" of the Device Manager: CSConfigFlags
// of the device under HKLM\Config\<profile>\Enum, bit 0
// (CSCONFIGFLAG_DISABLED). Profile 0 is the current one, CurrentConfig of
// IDConfigDB. Win98 writes four bytes of REG_BINARY.
static bool ProfileKey(const Store &store, U profile, const char *hardware, char *path, U capacity)
{
    char number[8] = "0001";
    if (profile)
        snprintf(number, sizeof number, "%04u", profile % 10000);
    else
    {
        U key = 0;
        bool created = false;
        char text[8];
        if (!const_cast<Store &>(store).Open(store.roots[2], "System\\CurrentControlSet\\control\\IDConfigDB", false,
                                             key, created) &&
            Text(store, key, "CurrentConfig", text, sizeof text) && strlen(text) == 4)
            memcpy(number, text, 5);
    }
    return snprintf(path, capacity, "Config\\%s\\Enum\\%s", number, hardware) < (int)capacity;
}
bool ProfileFlags(const Store &store, U profile, const char *hardware, U &flags)
{
    flags = 0;
    char path[320];
    U key = 0;
    bool created = false;
    if (!ProfileKey(store, profile, hardware, path, sizeof path) ||
        const_cast<Store &>(store).Open(store.roots[2], path, false, key, created))
        return false;
    const Value *value = nullptr;
    if (store.Query(key, "CSConfigFlags", value) || !value || value->size < 1)
        return false;
    for (U i = 0; i < 4 && i < value->size; ++i)
        flags |= U(((const unsigned char *)value->data)[i]) << (i * 8);
    return true;
}
bool SetProfileFlags(Store &store, U profile, const char *hardware, U flags)
{
    char path[320];
    U key = 0;
    bool created = false;
    if (!ProfileKey(store, profile, hardware, path, sizeof path) || store.Open(store.roots[2], path, true, key, created))
        return false;
    const unsigned char bytes[4] = {(unsigned char)flags, (unsigned char)(flags >> 8), (unsigned char)(flags >> 16),
                                    (unsigned char)(flags >> 24)};
    if (store.Set(key, "CSConfigFlags", 3, bytes, sizeof bytes))
        return false;
    PublishDevnodes(store);
    return true;
}
bool DevnodeDisabled(const Store &store, const char *hardware)
{
    U flags = 0;
    return ProfileFlags(store, 0, hardware, flags) && (flags & 1);
}

bool PublishHardwareDiagnostic(Store &store, U instance)
{
    U enumeration = 0, diagnostic = 0;
    bool created = false;
    if (store.Open(store.roots[2], "Enum", false, enumeration, created) ||
        store.Open(store.roots[2], "HwDiag\\Enum", true, diagnostic, created)) return false;
    U devices[DevnodeCapacity];
    const U count = Devnodes(store, enumeration, devices, DevnodeCapacity);
    for (U i = 0; i < count; ++i)
    {
        const U source = devices[i], model = store.nodes[source].parent, bus = store.nodes[model].parent;
        char hardware[260];
        snprintf(hardware, sizeof hardware, "%s\\%s\\%s", store.nodes[bus].name, store.nodes[model].name,
                 store.nodes[source].name);
        U destination = 0, history = 0, snapshot = 0;
        if (store.Open(diagnostic, hardware, true, destination, created)) return false;
        // EYEDOG reads the Enum device's values, then HwDiag\\Inst<n> for
        // its status and allocated CM resource descriptors. HWINFOD's
        // per-resource value is a series of DWORD length + descriptor.
        const Value *value = nullptr;
        for (U index = 0; !store.EnumValue(source, index, value); ++index)
            if (store.Set(destination, value->name, value->type, value->data, value->size)) return false;
        char snapshot_name[32];
        snprintf(snapshot_name, sizeof snapshot_name, "Inst%u", instance);
        if (store.Open(destination, "HwDiag", true, history, created) ||
            store.Open(history, snapshot_name, true, snapshot, created)) return false;
        const U disabled = DevnodeDisabled(store, hardware);
        const U status = DevnodeStatus(disabled, Same(store.nodes[bus].name, "ROOT"));
        const U problem = disabled ? DevnodeProblemDisabled : 0;
        if (store.Set(history, "LastInstance", 4, &instance, 4) ||
            store.Set(history, "EnumInstance", 4, &instance, 4) ||
            store.Set(snapshot, "Status", 4, &status, 4) || store.Set(snapshot, "Problem", 4, &problem, 4)) return false;
        const Neo98Hardware::Resources *resources = nullptr;
        for (const auto &entry : Neo98Hardware::DeviceResources)
            if (Same(entry.device, hardware)) { resources = &entry; break; }
        static const char *const configurations[] = {"Alloc\\LogConf0", "Boot\\LogConf0"};
        for (const char *configuration : configurations)
        {
            U key = 0;
            if (store.Open(snapshot, configuration, true, key, created)) return false;
            static constexpr char kinds[] = {'P', 'M', 'Q', 'D'};
            for (const char kind : kinds)
            {
                unsigned char bytes[256] = {};
                U used = 0;
                if (resources) for (const auto &one : resources->list)
                {
                    if (one.kind != kind) continue;
                    auto word = [&](U at, U v) { bytes[used + at] = v; bytes[used + at + 1] = v >> 8; };
                    auto dword = [&](U at, U v) { word(at, v); word(at + 2, v >> 16); };
                    const U length = kind == 'P' ? 24 : kind == 'M' ? 36 : kind == 'Q' ? 8 : 4;
                    if (used + length + 4 > sizeof bytes) return false;
                    dword(0, length);
                    if (kind == 'P') { word(4, 1); word(6, 12); word(8, one.first); word(10, one.last); bytes[used + 15] = 0xff; }
                    else if (kind == 'M') { word(4, 1); word(6, 20); dword(8, one.first); dword(12, one.last); }
                    else if (kind == 'Q') { word(6, one.first); word(8, 1u << one.first); }
                    else { bytes[used + 4] = one.first > 3; bytes[used + 5] = one.first; word(6, 1u << one.first); }
                    used += length + 4;
                }
                const char *name = kind == 'P' ? "IOResource" : kind == 'M' ? "MemResource" : kind == 'Q' ? "IRQResource" : "DMAResource";
                if (store.Set(key, name, 3, bytes, used)) return false;
            }
        }
    }
    return true;
}

// The camera of the host as a device (Neo98Hardware::CameraName): present
// while the host has a camera, gone when it has none, as a USB camera comes
// and goes under Win98. These keys are made anew at every start and never
// reach the journal of the user's registry.
static void PublishCamera(Store &store)
{
    const bool present = HostDevices::HasCamera();
    Unrecorded quiet;
    bool created = false;
    U node = 0;
    char key[200];
    snprintf(key, sizeof key, "System\\CurrentControlSet\\Services\\Class\\%s", Neo98Hardware::CameraDriver);
    char resource[200];
    snprintf(resource, sizeof resource, "%s\\msvideo\\%s", Neo98Hardware::MediaResources, Neo98Hardware::CameraResource);
    if (!present)
    {
        if (!store.Open(store.roots[2], Neo98Hardware::CameraDevice, false, node, created))
        {
            store.DeleteKey(store.roots[2], Neo98Hardware::CameraDevice);
            store.DeleteKey(store.roots[2], key);
            store.DeleteKey(store.roots[2], resource);
        }
        return;
    }
    auto text = [&](U at, const char *name, const char *value) { store.Set(at, name, 1, value, U(strlen(value)) + 1); };
    if (!store.Open(store.roots[2], Neo98Hardware::CameraNode, true, node, created))
    {
        text(node, "Class", "MEDIA");
        text(node, "ClassGUID", "{4d36e96c-e325-11ce-bfc1-08002be10318}");
        text(node, "Driver", Neo98Hardware::CameraDriver);
        text(node, "DeviceDesc", Neo98Hardware::CameraName);
        text(node, "HardwareID", Neo98Hardware::CameraId);
        text(node, "Mfg", Neo98Hardware::Maker);
        const unsigned char flags[4] = {};
        store.Set(node, "ConfigFlags", 3, flags, sizeof flags);
    }
    if (!store.Open(store.roots[2], key, true, node, created))
    {
        text(node, "DriverDesc", Neo98Hardware::CameraName);
        text(node, "ProviderName", Neo98Hardware::Maker);
        text(node, "DriverDate", Neo98Hardware::DriverDate);
        text(node, "MatchingDeviceId", Neo98Hardware::CameraId);
        text(node, "DevLoader", Neo98Hardware::SoundLoader);
        U drivers = 0;
        if (!store.Open(node, "Drivers", true, drivers, created))
        {
            text(drivers, "SubClasses", "msvideo");
            text(drivers, "MIGRATED", "0");
            U file = 0;
            char path[64];
            snprintf(path, sizeof path, "msvideo\\%s", Neo98Hardware::CameraDriverFile);
            if (!store.Open(drivers, path, true, file, created))
            {
                text(file, "Description", Neo98Hardware::CameraName);
                text(file, "Driver", Neo98Hardware::CameraDriverFile);
            }
        }
    }
    if (!store.Open(store.roots[2], resource, true, node, created))
    {
        text(node, "Active", "1");
        text(node, "Description", Neo98Hardware::CameraName);
        text(node, "FriendlyName", Neo98Hardware::CameraName);
        text(node, "Driver", Neo98Hardware::CameraDriverFile);
    }
}

void PublishDevnodes(Store &store)
{
    PublishCamera(store);
    if(HostDevices::HasBattery())
    {
        bool created=false;U node=0;
        if(!store.Open(store.roots[2],"Enum\\Root\\NEO98BAT\\0000",true,node,created))
        {
            auto text=[&](const char *key,const char *value){store.Set(node,key,1,value,U(strlen(value))+1);};
            text("DeviceDesc","NEO98 Host Battery");text("HardwareID","NEO98BAT");text("Class","Battery");text("ClassGUID",HostDevices::BatteryGuid);
        }
        if(!store.Open(store.roots[2],"System\\CurrentControlSet\\Control\\Class\\{72631e54-78a4-11d0-bcf7-00aa00b7b32a}",true,node,created))
            store.Set(node,"",1,"Batteries",10);
        char path[256];
        snprintf(path,sizeof path,"System\\CurrentControlSet\\Control\\DeviceClasses\\%s\\##.#%s",HostDevices::BatteryGuid,HostDevices::BatteryInterface);
        if(!store.Open(store.roots[2],path,true,node,created))
            store.Set(node,"DeviceInstance",1,HostDevices::BatteryId,sizeof HostDevices::BatteryId);
        const auto base=unsigned(strlen(path));snprintf(path+base,sizeof path-base,"\\#\\Control");
        if(!store.Open(store.roots[2],path,true,node,created)){const unsigned linked=1;store.Set(node,"Linked",4,&linked,4);}

    }
    U enumeration = 0;
    bool created = false;
    if (store.Open(store.roots[2], "Enum", false, enumeration, created))
        return;
    U list = 0;
    if (store.Open(store.roots[6], "Config Manager\\Enum", true, list, created))
        return;
    U keys[DevnodeCapacity];
    const U count = Devnodes(store, enumeration, keys, DevnodeCapacity);
    for (U i = 0; i < count; ++i)
    {
        const U instance = keys[i], device = store.nodes[instance].parent, enumerator = store.nodes[device].parent;
        char hardware[260], name[16];
        snprintf(hardware, sizeof hardware, "%s\\%s\\%s", store.nodes[enumerator].name, store.nodes[device].name,
                 store.nodes[instance].name);
        // CONFIGMG names the key after the devnode's address.
        snprintf(name, sizeof name, "%08X", DevnodeHandle(store, instance));
        U node = 0;
        if (store.Open(list, name, true, node, created))
            continue;
        store.Set(node, "HardWareKey", 1, hardware, U(strlen(hardware)) + 1);
        // The parts of the sound card name its devnode: the Multimedia
        // control panel lists a key of Control\\MediaResources only with it.
        if (Same(hardware, Neo98Hardware::SoundNode + 5))
        {
            static const char *const parts[] = {"wave", "midi", "mixer"};
            for (const char *part : parts)
            {
                char path[200];
                snprintf(path, sizeof path, "%s\\%s\\%s", Neo98Hardware::MediaResources, part,
                         Neo98Hardware::SoundResource);
                U key = 0;
                const U handle = DevnodeHandle(store, instance);
                const unsigned char devnode[4] = {(unsigned char)handle, (unsigned char)(handle >> 8),
                                                  (unsigned char)(handle >> 16), (unsigned char)(handle >> 24)};
                if (!store.Open(store.roots[2], path, false, key, created))
                    store.Set(key, "DevNode", 3, devnode, sizeof devnode);
            }
        }
        if (Same(hardware, Neo98Hardware::CameraNode + 5))
        {
            char path[200];
            snprintf(path, sizeof path, "%s\\msvideo\\%s", Neo98Hardware::MediaResources, Neo98Hardware::CameraResource);
            U key = 0;
            const U handle = DevnodeHandle(store, instance);
            const unsigned char devnode[4] = {(unsigned char)handle, (unsigned char)(handle >> 8),
                                              (unsigned char)(handle >> 16), (unsigned char)(handle >> 24)};
            Unrecorded quiet;
            if (!store.Open(store.roots[2], path, false, key, created))
                store.Set(key, "DevNode", 3, devnode, sizeof devnode);
        }
        // CM_PROB_DISABLED (16h) for a device disabled in this profile; the
        // status as CM_Get_DevNode_Status answers it.
        const bool disabled = DevnodeDisabled(store, hardware);
        const unsigned char problem[4] = {(unsigned char)(disabled ? DevnodeProblemDisabled : 0)};
        store.Set(node, "Problem", 3, problem, sizeof problem);
        const U flags = DevnodeStatus(disabled, Same(store.nodes[enumerator].name, "ROOT"));
        const unsigned char status[4] = {(unsigned char)flags, (unsigned char)(flags >> 8), (unsigned char)(flags >> 16),
                                         (unsigned char)(flags >> 24)};
        store.Set(node, "Status", 3, status, sizeof status);
    }
    // Remove only published devnodes that no longer exist. An insertion or
    // deletion elsewhere in the registry never renumbers a live handle.
    for (U i = 0, node = 0; !store.EnumKey(list, i, node); )
    {
        unsigned handle = 0; char tail = 0;
        const char *name = store.nodes[node].name;
        if (strlen(name) != 8 || sscanf(name, "%8x%c", &handle, &tail) != 1 ||
            handle < 0xc29a1000u || handle >= 0xc29a1000u + 65536u * 0x80u) { ++i; continue; }
        bool live = false;
        for (U k = 0; k < count; ++k)
            if (DevnodeHandle(store, keys[k]) == handle) { live = true; break; }
        if (live || store.DeleteKey(list, name)) ++i;
    }
}
} // namespace Registry
