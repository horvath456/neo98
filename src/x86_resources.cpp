// SPDX-License-Identifier: GPL-2.0-or-later
// Native PE resource service. Resource bytes remain in the owning mapped module.
#include "x86_app.h"
#include <string.h>
namespace X86
{
bool Application::ResourceName(unsigned value, Pe32::Image::ResourceKey &key, unsigned short (&wide)[260])
{
    key = {};
    if (value < 65536)
    {
        key.id = value;
        return true;
    }
    const char *name = ReadString(value);
    if (state == Failed)
        return false;
    if (name[0] == '#')
    {
        unsigned id = 0, i = 1;
        for (; name[i] >= '0' && name[i] <= '9'; ++i)
        {
            id = id * 10 + name[i] - '0';
            if (id > 65535)
            {
                last_error = 87;
                return false;
            }
        }
        if (i == 1 || name[i])
        {
            last_error = 87;
            return false;
        }
        key.id = id;
        return true;
    }
    const unsigned n = strlen(name);
    if (n >= 260)
    {
        last_error = 206;
        return false;
    }
    for (unsigned i = 0; i < n; ++i)
        wide[i] = AnsiWide((unsigned char)name[i]);
    key.name = wide;
    key.length = n;
    return state != Failed;
}
// The Unicode form of ResourceName: an integer below 65536, "#123", or a
// UTF-16 string read from the guest.
bool Application::ResourceNameW(unsigned value, Pe32::Image::ResourceKey &key, unsigned short *wide)
{
    key = {};
    if (value < 65536) { key.id = value; return true; }
    unsigned n = 0;
    for (; n < 260; ++n)
    {
        if (!virtual_memory.Access(value + n * 2, 2, false)) { last_error = 87; return false; }
        unsigned short c = 0;
        Read(value + n * 2, &c, 2);
        if (!c) break;
        wide[n] = c;
    }
    if (n >= 260) { last_error = 206; return false; }
    if (n && wide[0] == '#')
    {
        if (n == 1) { last_error = 87; return false; }
        unsigned id = 0;
        for (unsigned i = 1; i < n; ++i)
        {
            if (wide[i] < '0' || wide[i] > '9' || id > (65535u - (wide[i] - '0')) / 10) { last_error = 87; return false; }
            id = id * 10 + wide[i] - '0';
        }
        key.id = id;
        return true;
    }
    key.name = wide;
    key.length = n;
    return true;
}
unsigned Application::ResourceApi(ApiName name, const unsigned *a)
{
    // Original Win98 KERNEL32 leaves these operations unconditional.
    if (API_IS(name,"LockResource"))
        return a[0]; //
    if (API_IS(name,"FreeResource"))
        return 0; //
    if (!LockLoader())
        return 0;
    struct Unlock
    {
        Application &app;
        ~Unlock() { app.UnlockLoader(); }
    } unlock{*this};
    // A handle with bit 0 is a LOAD_LIBRARY_AS_DATAFILE view; its resource
    // addresses are file offsets, not image addresses.
    ResourceModule module;
    if (!ResolveResourceModule(a[0], module))
        return 0;
    const auto *pe = module.pe;
    const bool enum_types = API_IS(name,"EnumResourceTypesA") || API_IS(name,"EnumResourceTypesW");
    const bool enum_names = API_IS(name,"EnumResourceNamesA") || API_IS(name,"EnumResourceNamesW");
    if (enum_types || enum_names) {
        const bool wide = name[strlen(name) - 1] == 'W';
        Pe32::Image::ResourceKey type;
        unsigned short type_name[260];
        if (enum_names && !(wide ? ResourceNameW(a[1], type, type_name) : ResourceName(a[1], type, type_name))) return 0;
        const unsigned callback = a[enum_names ? 2 : 1], parameter = a[enum_names ? 3 : 2];
        if (!callback) { last_error = 87; return 0; }
        for (unsigned i = 0; state != Failed && state != Exited; ++i) {
            // Callbacks run on the same fiber and may call FreeLibrary under
            // the recursive loader lock. CollectModules can delete
            // modules.pe, so resolve it again through the module handle
            // before each iteration.
            ResourceModule live;
            if (!ResolveResourceModule(a[0], live)) { last_error = 6; return 0; }
            pe = live.pe;
            Pe32::Image::ResourceLabel label;
            unsigned error = 0;

            if (!pe->ResourceLabelAt(enum_names ? &type : nullptr, i, label, &error)) {
                if (error) last_error = error;
                return !error;
            }
            unsigned value = label.id, scratch = 0;
            if (label.text) {
                // Each callback owns temporary writable text. A nested enumeration
                // gets independent storage; callbacks may also load resources.
                unsigned bytes = (label.length + 1) * 2;
                scratch = Allocate(bytes * 2);
                if (!scratch) { last_error = 8; return 0; }
                Store(scratch, label.text, label.length * 2);
                const unsigned short zero = 0;
                Store(scratch + label.length * 2, &zero, 2);
                value = scratch;
                if (!wide) {
                    const unsigned convert[] = {0, 0, scratch, label.length + 1, scratch + bytes, bytes, 0, 0};
                    if (!NlsApi(811, convert)) { Free(scratch); return 0; }
                    value += bytes;
                }
            }
            const unsigned types[] = {a[0], value, parameter};
            const unsigned names[] = {a[0], a[1], value, parameter};
            const unsigned result = Call(callback, enum_names ? names : types, enum_names ? 4 : 3);
            if (scratch) Free(scratch);
            // Original KERNEL32/FC8A returns FALSE on callback stop.
            if (!result) return 0;
        }
        return 0;
    }
    if (API_IS(name,"EnumResourceLanguagesA") || API_IS(name,"EnumResourceLanguagesW"))
    {
        // (hModule, type, name, ENUMRESLANGPROC, lParam). COMCTL32's property
        // sheets ask which languages a dialog template carries before choosing
        // one. The callback receives the caller's own type and name values.
        const bool wide = name[strlen(name) - 1] == 'W';
        Pe32::Image::ResourceKey type, id;
        unsigned short type_name[260], id_name[260];
        if (wide ? !ResourceNameW(a[1], type, type_name) || !ResourceNameW(a[2], id, id_name)
                 : !ResourceName(a[1], type, type_name) || !ResourceName(a[2], id, id_name))
            return 0;
        if (!a[3]) { last_error = 87; return 0; }
        unsigned languages[64], error = 0;
        const unsigned count = pe->ResourceLanguages(type, id, languages, 64, &error);
        if (!count) { last_error = error ? error : 1814; return 0; }
        for (unsigned i = 0; i < count && state != Failed && state != Exited; ++i)
        {
            const unsigned args[] = {a[0], a[1], a[2], languages[i] & 65535, a[4]};
            if (!Call(a[3], args, 5)) break;
        }
        return 1;
    }
    if (API_IS(name,"FindResourceA") || API_IS(name,"FindResourceExA") ||
        API_IS(name,"FindResourceW") || API_IS(name,"FindResourceExW"))
    {
        const bool extended = !strncmp(name, "FindResourceEx", 14);
        const bool wide = name[strlen(name) - 1] == 'W';
        Pe32::Image::ResourceKey type, id;
        unsigned short type_name[260], id_name[260];
        if (!(wide ? ResourceNameW(a[extended ? 1 : 2], type, type_name) : ResourceName(a[extended ? 1 : 2], type, type_name)) ||
            !(wide ? ResourceNameW(a[extended ? 2 : 1], id, id_name) : ResourceName(a[extended ? 2 : 1], id, id_name)))
            return 0;
        const unsigned language = extended ? a[3] & 65535 : 0;
        Pe32::Image::ResourceEntry data;
        unsigned error = 0;
        bool found = pe->FindResource(type, id, language, data, &error);
        // Native fallback policy for LANG_NEUTRAL: English, then first available language.
        if (!found && !language && error == 1815)
            found = pe->FindResource(type, id, 0x409, data, &error) ||
                    pe->FindResource(type, id, ~0u, data, &error);
        if (!found)
        {
            last_error = error;
            return 0;
        }
        const unsigned address = module.Address(data.data);
        if (!address)
        {
            last_error = 1812;
            return 0;
        }
        for (const auto &resource : resource_handles)
            if (resource.id && resource.module == module.key && resource.entry == data.entry)
                return resource.id;
        if (resource_serial == 0x1000000)
        {
            last_error = 8;
            return 0;
        }
        for (auto &resource : resource_handles)
            if (!resource.id)
            {
                resource = {0x69000000 | resource_serial++, module.key, data.entry,
                            address,                        data.size,  false};
                return resource.id;
            }
        last_error = 8;
        return 0;
    }
    for (auto &resource : resource_handles)
        if (resource.id == a[1] && resource.id && resource.module == module.key)
        {
            if (API_IS(name,"SizeofResource"))
                return resource.size;
            resource.loaded = true;
            return resource.address;
        }
    last_error = 6;
    return 0;
}
} // namespace X86
