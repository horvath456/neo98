// SPDX-License-Identifier: GPL-2.0-or-later
// Win32 ABI adapter to NEO98's shared native registry service.
#include "x86_app.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "win98_user_name.h"
namespace X86
{
// ADVAPI32. The decisions are in src/win98_user_name.h; the store and
// the handle table below are the same ones every other Reg* entry uses.
unsigned Application::UserNameApi(const unsigned *a)
{
    unsigned handle = 0, key = 0;
    bool created = false;
    if (registry.Open(0x80000002u, Win98UserName::Key(), false, handle, created, ""))
        return 0;                                       // LastError untouched
    unsigned result = registry.Key(handle, key);
    const Registry::Value *value = nullptr;
    if (!result)
        result = registry.store.Query(key, Win98UserName::Value(), value);
    if (!result)
    {
        // The wrapper probes the size word first, then the buffer
        // for exactly that many bytes. Its 87 reaches the caller as a failed
        // query, so it takes the same exit as a missing value.
        if (!a[1] || !virtual_memory.Access(a[1], 4, true))
            result = 87;
        else
        {
            const unsigned capacity = Read32(a[1]);
            Write32(a[1], value->size);
            if (!a[0] || capacity < value->size || !virtual_memory.Access(a[0], value->size, true))
                result = capacity < value->size ? 234u : 87u;
            else
                Store(a[0], value->data, value->size);
        }
    }
    registry.Close(handle);                              // on every path
    if (result)
    {
        last_error = Win98UserName::QueryFailure;        //
        return 0;
    }
    return 1;                                            // the ESI loaded
}
// NEO98_REG_TRACE=1 writes every call of a program to the registry into the
// log: the function, the key, the subkey or value it names and the result.
unsigned Application::RegistryApi(ApiName name, const unsigned *a)
{
    static const bool trace = getenv("NEO98_REG_TRACE") != nullptr;
    if (!trace || !log)
        return RegistryCall(name, a);
    char path[512] = "", named[160] = "";
    unsigned key = 0;
    if (!API_IS(name, "RegCloseKey") && !registry.Key(a[0], key))
        registry.store.Path(key, path, sizeof path);
    const bool by_name = !API_IS(name, "RegCloseKey") && !API_IS(name, "RegFlushKey") && !strstr(name, "Enum") &&
                         !strstr(name, "QueryInfo");
    if (by_name && a[1])
        snprintf(named, sizeof named, "%s", ReadString(a[1]));
    const unsigned result = RegistryCall(name, a);
    char text[800];
    if (strstr(name, "Enum"))
        snprintf(text, sizeof text, "REG %s %s #%u = %u", static_cast<const char *>(name), path, a[1], result);
    else
        snprintf(text, sizeof text, "REG %s %s | %s = %u", static_cast<const char *>(name), path, named, result);
    log(text);
    return result;
}
unsigned Application::RegistryCall(ApiName name, const unsigned *a)
{
    auto &store = registry.store;
    if (API_IS(name,"RegCloseKey"))
        return registry.Close(a[0]);
    if (API_IS(name,"RegOpenKeyA") || API_IS(name,"RegOpenKeyExA") || API_IS(name,"RegCreateKeyA") ||
        API_IS(name,"RegCreateKeyExA"))
    {
        bool create = strstr(name, "Create") != nullptr, extended = strstr(name, "Ex") != nullptr;
        if (extended && !create && a[2])
            return 87;
        // ADVAPI32 implements local RegCreateKeyExA
        // with RegOpenKeyA/RegCreateKeyA. Reserved, class, options, access
        // and security attributes are never read on the local path.
        unsigned out = extended ? (create ? a[7] : a[4]) : a[2];
        if (!out || !virtual_memory.Access(out, 4, true) ||
            (create && extended && a[8] && !virtual_memory.Access(a[8], 4, true)))
            return 87;
        if (!create && a[1] && ReadString(a[1])[0] == '\\')
            return 2;
        unsigned handle = 0;
        bool created = false;
        unsigned result;
        if (create && extended)
        {
            // The wrapper writes disposition before each primitive call,
            // including a failing create.
            if (a[8]) Write32(a[8], 2);
            result = registry.Open(a[0], ReadString(a[1]), false, handle, created, "");
            if (result)
            {
                if (a[8]) Write32(a[8], 1);
                result = registry.Open(a[0], ReadString(a[1]), true, handle, created, "");
            }
        }
        else
            result = registry.Open(a[0], ReadString(a[1]), create, handle, created, "");
        if (!result)
        {
            Write32(out, handle);
        }
        else if (log && trace_calls)
        {
            char detail[384];
            snprintf(detail, sizeof detail, "Registry open failed key=%08x path=%s result=%u", a[0],
                     a[1] ? ReadString(a[1]) : "", result);
            log(detail);
        }
        return result;
    }
    unsigned key = 0, result = registry.Key(a[0], key);
    if (result)
        return result;
    switch (name.id)
    {
    API_CASE("RegFlushKey")
    {
        // ADVAPI32 hands this to the hive writer. A key of a hive RegLoadKey
        // mounted is written to that hive's file (src/x86_registry_hives.cpp).
        // Everything else is the change journal of src/registry_persist.cpp.
        // Without a storage there is nothing to write and the call still
        // succeeds, as it does on a registry that is already consistent.
        unsigned mount = 0;
        if (HiveOfKey(key, mount))
            return HiveFlush(mount);
        return Registry::Attached() && !Registry::Flush() ? 5u : 0u;
    }
    API_CASE("RegDeleteKeyA")
        return a[1] ? store.DeleteKey(key, ReadString(a[1])) : 87u;
    API_CASE("RegDeleteValueA")
        return store.DeleteValue(key, ReadString(a[1]));
    API_CASE("RegSetValueExA")
    {
        // Windows 98 _RegSetValueEx measures REG_SZ itself, ignoring cbData,
        // and rejects strings of 0x4000 bytes or more. SETUPX can therefore
        // pass zero length. The native store retains the terminator.
        unsigned size = a[5];
        if (a[3] == 1 && !a[2])
        {
            if (!a[4])
                return 87;
            unsigned length = 0;
            unsigned char c = 1;
            while (length < 0x4000 && Read(a[4] + length, &c, 1) && c)
                ++length;
            if (c)
                return 87;
            size = length + 1;
        }
        if (a[2] || size > 65536 || (size && !a[4]))
            return 87;
        auto *bytes = size ? new unsigned char[size] : nullptr;
        if (size && !Read(a[4], bytes, size))
        {
            delete[] bytes;
            return 87;
        }
        result = store.Set(key, ReadString(a[1]), a[3], bytes, size);
        if (!result && log)
        {
            unsigned hash = 2166136261u;
            for (unsigned i = 0; i < size; ++i)
                hash = (hash ^ bytes[i]) * 16777619u;
            char detail[384];
            snprintf(detail, sizeof detail, "Registry write name=%s type=%u bytes=%u hash=%08x",
                     ReadString(a[1]), a[3], size, hash);
            log(detail);
        }
        delete[] bytes;
        return result;
    }
    API_CASE("RegSetValueA") API_CASE("RegQueryValueA")
    {
        bool set = API_IS(name,"RegSetValueA"), created = false;
        unsigned child = 0;
        result = store.Open(key, ReadString(a[1]), set, child, created);
        if (result)
            return result;
        if (set)
        {
            if (a[2] != 1 || !a[3])
                return 87;
            const char *text = ReadString(a[3]);
            return store.Set(child, "", 1, text, strlen(text) + 1);
        }
        if (!a[3])
            return a[2] ? 87u : 0u;
        const Registry::Value *value = nullptr;
        result = store.Query(child, "", value);
        if (result)
            return result;
        if (value->type != 1)
            return 13;
        unsigned capacity = Read32(a[3]);
        Write32(a[3], value->size);
        if (!a[2])
            return 0;
        if (capacity < value->size)
            return 234;
        Store(a[2], value->data, value->size);
        return 0;
    }
    API_CASE("RegQueryValueExA")
    {
        // ADVAPI32 forwards lpReserved to VMM without rejecting it.
        // EYEDOG.OCX passes the address of a zero DWORD even for scalar
        // status queries. The Win98 provider ignores this reserved argument.
        if (a[4] && !a[5])
            return 87;
        // The input capacity may alias the output type. Capture it before
        // clearing/writing the latter, as VMM's query worker does.
        const unsigned capacity = a[5] ? Read32(a[5]) : 0;
        if (a[3]) Write32(a[3], 0);
        const Registry::Value *value = nullptr;
        result = store.Query(key, ReadString(a[1]), value);
        if (result)
        {
            if (log && trace_calls)
            {
                char detail[384];
                snprintf(detail, sizeof detail, "Registry query failed key=%08x name=%s result=%u", a[0],
                         a[1] ? ReadString(a[1]) : "", result);
                log(detail);
            }
            return result;
        }
        if (a[3])
            Write32(a[3], value->type);
        if (a[5])
            Write32(a[5], value->size);
        if (!a[4])
            return 0;
        if (capacity < value->size)
            return 234;
        Store(a[4], value->data, value->size);
        return 0;
    }
    API_CASE("RegEnumKeyA") API_CASE("RegEnumKeyExA")
    {
        bool extended = API_IS(name,"RegEnumKeyExA");
        if (!a[2] || (extended && (!a[3] || a[4] || (a[5] && !a[6]))))
            return 87;
        unsigned child = 0;
        result = store.EnumKey(key, a[1], child);
        if (result)
            return result;
        auto &node = store.nodes[child];
        unsigned length = strlen(node.name), klass_length = strlen(node.klass),
                 capacity = extended ? Read32(a[3]) : a[3];
        if (capacity <= length)
            return 234;
        if (extended && a[5] && Read32(a[6]) <= klass_length)
            return 234;
        PutString(a[2], node.name, capacity);
        if (extended)
        {
            Write32(a[3], length);
            if (a[5])
                PutString(a[5], node.klass);
            if (a[6])
                Write32(a[6], klass_length);
            if (a[7])
            {
                Write32(a[7], 0);
                Write32(a[7] + 4, 0);
            }
        }
        return 0;
    }
    API_CASE("RegEnumValueA")
    {
        if (!a[2] || !a[3] || a[4] || (a[6] && !a[7]))
            return 87;
        const Registry::Value *value = nullptr;
        result = store.EnumValue(key, a[1], value);
        if (result)
            return result;
        unsigned length = strlen(value->name), capacity = Read32(a[3]),
                 data_capacity = a[7] ? Read32(a[7]) : 0;
        if (a[5])
            Write32(a[5], value->type);
        if (a[7])
            Write32(a[7], value->size);
        if (a[6])
        {
            if (data_capacity < value->size)
                return 234;
            Store(a[6], value->data, value->size);
        }
        if (capacity <= length)
        {
            if (capacity) PutString(a[2], value->name, capacity);
            Write32(a[3], capacity ? capacity - 1 : 0);
            return 234;
        }
        PutString(a[2], value->name, capacity);
        Write32(a[3], length);
        return 0;
    }
    API_CASE("RegQueryInfoKeyA")
    {
        if (a[3] || (a[1] && !a[2]))
            return 87;
        const auto &node = store.nodes[key];
        unsigned klass_length = strlen(node.klass), capacity = a[2] ? Read32(a[2]) : 0;
        unsigned children = 0, max_name = 0, max_class = 0, values = 0, max_value_name = 0, max_data = 0;
        for (unsigned i = node.child; i != Registry::Missing; i = store.nodes[i].sibling)
        {
            const auto &n = store.nodes[i];
            if (!n.used || n.deleted)
                continue;
            ++children;
            unsigned length = strlen(n.name);
            if (length > max_name)
                max_name = length;
            length = strlen(n.klass);
            if (length > max_class)
                max_class = length;
        }
        for (unsigned i = node.values; i != Registry::Missing; i = store.values[i].next)
        {
            const auto &v = store.values[i];
            if (!v.used)
                continue;
            ++values;
            unsigned length = strlen(v.name);
            if (length > max_value_name)
                max_value_name = length;
            if (v.size > max_data)
                max_data = v.size;
        }
        unsigned info[] = {children, max_name, max_class, values, max_value_name, max_data, 0};
        for (unsigned i = 0; i < 7; ++i)
            if (a[4 + i])
                Write32(a[4 + i], info[i]);
        if (a[11])
        {
            Write32(a[11], 0);
            Write32(a[11] + 4, 0);
        }
        if (a[2])
            Write32(a[2], klass_length);
        if (a[1])
        {
            if (capacity <= klass_length)
                return 234;
            PutString(a[1], node.klass);
        }
        return 0;
    }
    default:
        break;
    }
    Fail("Unsupported registry API");
    return 120;
}
} // namespace X86
