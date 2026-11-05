// SPDX-License-Identifier: GPL-2.0-or-later
// Mock VCOMM, registry and loader around src/win98_comm.h for the original
// comparison in tools/test_kernel_comm_original.py. The Python side drives the
// original KERNEL32 code with hooks that implement the same mocks and records
// the same trace, so the two traces, results and memory images must match.
#include "../src/win98_comm.h"
#include <string.h>
extern "C"
{
struct Record { unsigned kind, v[7]; };
struct Trace { Record *records; unsigned capacity, count; };
struct Scenario
{
    unsigned handle, object, overlapped, port_event, event_handle, event_object, service_result, wait_mask;
    unsigned window_base, window_size, mirror_base, mirror_size;
};
struct RegEntry { const char *path, *name, *data; unsigned size; };
struct ConfigScenario { unsigned module_type, module, procedure, driver_result, thunk_result; };
}
namespace
{
void Add(Trace &t, unsigned kind, unsigned a = 0, unsigned b = 0, unsigned c = 0, unsigned d = 0, unsigned e = 0, unsigned f = 0, unsigned g = 0)
{
    if (t.count < t.capacity) t.records[t.count] = Record{kind, {a, b, c, d, e, f, g}};
    ++t.count;
}
unsigned Hash(const char *s)
{
    unsigned h = 2166136261u;
    for (; *s; ++s) h = (h ^ (unsigned char)*s) * 16777619u;
    return h;
}
unsigned Lower(unsigned c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
bool SameText(const char *a, const char *b)
{
    for (;; ++a, ++b)
    {
        if (Lower((unsigned char)*a) != Lower((unsigned char)*b)) return false;
        if (!*a) return true;
    }
}
bool Prefix(const char *path, const char *prefix)
{
    unsigned i = 0;
    for (; prefix[i]; ++i) if (Lower((unsigned char)path[i]) != Lower((unsigned char)prefix[i])) return false;
    return path[i] == '\\';
}
struct Mock
{
    const Scenario &s; unsigned char *memory; Trace &t; unsigned error;
    bool Access(unsigned p, unsigned n, bool) { return p >= s.window_base && n <= s.window_size && p - s.window_base <= s.window_size - n; }
    unsigned Object(unsigned h) { return s.handle && h == s.handle ? s.object : 0; }
    void SetError(unsigned e) { error = e; Add(t, 2, e); }
    unsigned char *At(unsigned p) { return memory + (p - s.mirror_base); }
    unsigned Read32(unsigned p) { unsigned v; memcpy(&v, At(p), 4); return v; }
    void Write32(unsigned p, unsigned v) { memcpy(At(p), &v, 4); }
    bool Service(unsigned service, const unsigned *a, unsigned count)
    {
        Add(t, 1, service, count, a[0], count > 1 ? a[1] : 0, count > 2 ? a[2] : 0, count > 3 ? a[3] : 0);
        if (service == Win98Comm::ServiceLastError || service == Win98Comm::ServiceResetEvent || service == Win98Comm::ServiceSetEvent) return true;
        if (!s.service_result) return false;
        if (service == Win98Comm::ServiceWaitEvent) Write32(a[1], s.wait_mask);
        return true;
    }
    bool Lookup(unsigned h, unsigned &object, bool &overlapped, unsigned &event)
    {
        if (!s.handle || h != s.handle) return false;
        object = s.object; overlapped = s.overlapped != 0; event = s.port_event; return true;
    }
    unsigned EventObject(unsigned h) { return s.event_handle && h == s.event_handle ? s.event_object : 0; }
    void Wait(unsigned e) { Add(t, 3, e); }
};
struct Registry
{
    const RegEntry *entries; unsigned count; const ConfigScenario &c; Trace &t; unsigned error;
    char keys[64][300]; unsigned next = 0;
    bool Exists(const char *path)
    {
        for (unsigned i = 0; i < count; ++i) if (SameText(entries[i].path, path) || Prefix(entries[i].path, path)) return true;
        return false;
    }
    const char *KeyPath(unsigned key) { return key == 0x80000002u ? "" : key >= 0x10 && key - 0x10 < next ? keys[key - 0x10] : nullptr; }
    bool OpenKey(unsigned parent, const char *path, unsigned &key)
    {
        const char *base = KeyPath(parent);
        char full[300];
        bool ok = base != nullptr;
        if (ok)
        {
            if (*base) { Win98Comm::Copy(full, base, sizeof full); unsigned n = Win98Comm::Length(full); full[n] = '\\'; Win98Comm::Copy(full + n + 1, path, sizeof full - n - 1); }
            else Win98Comm::Copy(full, path, sizeof full);
            ok = Exists(full) && next < 64;
        }
        if (ok) { Win98Comm::Copy(keys[next], full, 300); key = 0x10 + next++; }
        Add(t, 5, parent, Hash(path), ok, ok ? key : 0);
        return ok;
    }
    bool EnumKey(unsigned key, unsigned index, char *out, unsigned capacity)
    {
        const char *base = KeyPath(key);
        char seen[64][100]; unsigned found = 0; bool ok = false;
        if (base)
            for (unsigned i = 0; i < count && !ok; ++i)
            {
                const char *p = entries[i].path;
                if (*base) { if (!Prefix(p, base)) continue; p += Win98Comm::Length(base) + 1; }
                char name[100]; unsigned n = 0;
                for (; p[n] && p[n] != '\\' && n < 99; ++n) name[n] = p[n];
                name[n] = 0;
                if (p[n] && p[n] != '\\') continue; // component longer than 99 characters is not enumerable here
                bool dup = false;
                for (unsigned k = 0; k < found; ++k) if (SameText(seen[k], name)) dup = true;
                if (dup) continue;
                if (found < 64) Win98Comm::Copy(seen[found], name, 100);
                if (found++ == index && n + 1 <= capacity) { Win98Comm::Copy(out, name, capacity); ok = true; }
            }
        Add(t, 6, key, index, ok);
        return ok;
    }
    bool QueryString(unsigned key, const char *name, char *out, unsigned capacity)
    {
        const char *base = KeyPath(key); bool ok = false;
        if (base)
            for (unsigned i = 0; i < count && !ok; ++i)
                if (SameText(entries[i].path, base) && SameText(entries[i].name, name) && entries[i].size + 1 <= capacity)
                {
                    memcpy(out, entries[i].data, entries[i].size); out[entries[i].size] = 0; ok = true;
                }
        Add(t, 7, key, Hash(name), ok);
        return ok;
    }
    void CloseKey(unsigned key) { Add(t, 8, key); }
    unsigned ModuleType(const char *path) { Add(t, 10, Hash(path)); return c.module_type; }
    unsigned LoadLibrary(const char *path) { Add(t, 11, Hash(path)); return c.module; }
    unsigned ProcAddress(unsigned module, const char *entry) { Add(t, 12, module, Hash(entry)); return c.procedure; }
    unsigned CallDriver(unsigned procedure, unsigned x, unsigned y, unsigned z) { Add(t, 13, procedure, x, y, z); return c.driver_result; }
    unsigned Thunk16(unsigned mode, const char *path, const char *entry, unsigned x, unsigned y, unsigned z, unsigned w)
    {
        Add(t, 14, mode, Hash(path), Hash(entry), x, y, z, w); return c.thunk_result;
    }
    void FreeLibrary(unsigned module) { Add(t, 15, module); }
    void SetError(unsigned e) { error = e; Add(t, 2, e); }
};
} // namespace
extern "C" unsigned kernel_comm_thin(unsigned id, const unsigned *args, const Scenario *s, unsigned char *memory, unsigned *error,
                                     Record *records, unsigned capacity, unsigned *count)
{
    Trace t{records, capacity, 0};
    Mock m{*s, memory, t, 0x76543210};
    const unsigned r = id < Win98Comm::WaitCommEvent ? Win98Comm::Thin(m, id, args)
                     : id == Win98Comm::WaitCommEvent ? Win98Comm::Wait(m, args) : Win98Comm::Config(m, id, args);
    *error = m.error; *count = t.count;
    return r;
}
extern "C" unsigned kernel_comm_config(unsigned id, const char *name, const unsigned *args, const ConfigScenario *c,
                                       const RegEntry *entries, unsigned entry_count, unsigned *error, Record *records,
                                       unsigned capacity, unsigned *count)
{
    Trace t{records, capacity, 0};
    Registry r{entries, entry_count, *c, t, 0x76543210, {}};
    const unsigned result = Win98Comm::Dispatch(r, id, name, args);
    *error = r.error; *count = t.count;
    return result;
}
extern "C" unsigned kernel_comm_hash(const char *s) { return Hash(s); }
