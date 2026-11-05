// SPDX-License-Identifier: GPL-2.0-or-later
// The TrueType driver of Win98 as original x86 code. See truetype_machine.h.
#include "truetype_machine.h"
#include "x86_cpu.h"
#include "jit_memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace TrueTypeMachine
{
namespace
{
// The layout of the machine. GDI32.DLL keeps its own, so every
// address in the comments of this port is the address that runs here.
constexpr U ImageBase = 0xbff20000;
// Thread block and GDT fill one 16 KiB page of the host: under FEXCore a
// host page with an unmapped guest page in it is closed, and every access to
// fs:[0] would go through the fault handler.
#if defined(__linux__) && defined(__x86_64__)
// Linux reserves the pages below mmap_min_addr. These are private driver
// addresses; neither the GDI32 image nor a font refers to their old location.
constexpr U Tib = 0x30000, Gdt = 0x31000;
#else
constexpr U Tib = 0x4000, Gdt = 0x5000;
#endif
constexpr U Traps = 0x10000, TrapBytes = 0x10000, Return = Traps + TrapBytes - 16;
constexpr U StackBase = 0x100000, StackBytes = 0x100000;
constexpr U HeapBase = 0x01000000, HeapLimit = 0x11000000, HeapStep = 0x100000;
constexpr U ViewBase = 0x20000000, ViewLimit = 0x60000000;
constexpr U Classes = 28;       // 16 bytes.. 1 GiB

struct Shared
{
    unsigned char *data = nullptr;
    U size = 0, references = 0;
};
struct Handle
{
    U kind = 0;                 // 0 free, 1 file, 2 mapping
    Shared *bytes = nullptr;
    U position = 0, view = 0, view_bytes = 0;
};
constexpr U HandleCount = 64, HandleBase = 0x100;

struct Import
{
    char name[40];
    U arguments;                // stdcall argument count; ~0u: not provided
};

struct State
{
    X86::Cpu *cpu = nullptr;
    bool started = false, tried = false, stopped = false;
    char failure[200] = {};
    Import imports[160] = {};
    U import_count = 0;
    U heap_top = HeapBase, heap_mapped = HeapBase;
    U free_lists[Classes] = {};
    U view_top = ViewBase;
    Handle handles[HandleCount] = {};
    U last_error = 0, locale = 0x409;
    Reader reader = nullptr;
    Query query = nullptr;
} machine;

void Fail(const char *text)
{
    if (!machine.stopped)
        snprintf(machine.failure, sizeof machine.failure, "TrueType machine: %s", text);
    machine.stopped = true;
    if (machine.cpu)
        machine.cpu->Stop();
}

bool Map(U address, U bytes)
{
    return machine.cpu->Map(address, bytes, X86::Prot::All) == X86::CpuError::Ok;
}
bool Get(U address, void *out, U bytes)
{
    return !bytes || machine.cpu->Read(address, out, bytes) == X86::CpuError::Ok;
}
bool Put(U address, const void *in, U bytes)
{
    return !bytes || machine.cpu->Write(address, in, bytes) == X86::CpuError::Ok;
}
U Dword(U address)
{
    U value = 0;
    Get(address, &value, 4);
    return value;
}
void SetDword(U address, U value) { Put(address, &value, 4); }
U Register(int which)
{
    U value = 0;
    machine.cpu->Get(which, value);
    return value;
}
void SetRegister(int which, U value) { machine.cpu->Set(which, value); }
bool CString(U address, char *out, U capacity)
{
    for (U i = 0; i < capacity; ++i)
    {
        if (!Get(address + i, out + i, 1))
            return false;
        if (!out[i])
            return true;
    }
    out[capacity - 1] = 0;
    return false;
}

// ---- heap -------------------------------------------------------------
// The heap of HeapCreate, which every allocation of the driver
// goes through. A block carries its class and
// the requested size in an eight-byte header; freed blocks go back to the
// list of their class. Memory is handed out zeroed.
U ClassOf(U bytes)
{
    U c = 0;
    while (c < Classes - 1 && (16u << c) < bytes + 8)
        ++c;
    return c;
}
U HeapAllocate(U bytes)
{
    if (bytes > 0x4000000)
        return 0;
    const U c = ClassOf(bytes), block = 16u << c;
    U at = machine.free_lists[c];
    if (at)
        machine.free_lists[c] = Dword(at);
    else
    {
        if (HeapLimit - machine.heap_top < block)
            return 0;
        at = machine.heap_top;
        machine.heap_top += block;
        while (machine.heap_mapped < machine.heap_top)
        {
            if (!Map(machine.heap_mapped, HeapStep))
                return 0;
            machine.heap_mapped += HeapStep;
        }
    }
    static unsigned char zero[4096];
    for (U done = 0; done < block; done += sizeof zero)
        Put(at + done, zero, block - done < sizeof zero ? block - done : sizeof zero);
    SetDword(at, c);
    SetDword(at + 4, bytes);
    return at + 8;
}
bool HeapBlock(U address, U &c, U &bytes)
{
    if (address < HeapBase + 8 || address >= machine.heap_top || (address & 7))
        return false;
    c = Dword(address - 8);
    bytes = Dword(address - 4);
    return c < Classes;
}
bool HeapFree(U address)
{
    U c = 0, bytes = 0;
    if (!HeapBlock(address, c, bytes))
        return false;
    SetDword(address - 8, machine.free_lists[c]);
    machine.free_lists[c] = address - 8;
    return true;
}

// ---- handles and files --------------------------------------------------
Handle *HandleAt(U value)
{
    if (value < HandleBase || (value - HandleBase) % 4 || (value - HandleBase) / 4 >= HandleCount)
        return nullptr;
    Handle &h = machine.handles[(value - HandleBase) / 4];
    return h.kind ? &h : nullptr;
}
U NewHandle(U kind, Shared *bytes)
{
    for (U i = 0; i < HandleCount; ++i)
        if (!machine.handles[i].kind)
        {
            machine.handles[i] = {};
            machine.handles[i].kind = kind;
            machine.handles[i].bytes = bytes;
            ++bytes->references;
            return HandleBase + i * 4;
        }
    return 0;
}
void Drop(Shared *bytes)
{
    if (bytes && !--bytes->references)
    {
        free(bytes->data);
        delete bytes;
    }
}
bool ReadFile(const char *path, Shared *&out)
{
    unsigned char *data = nullptr;
    U size = 0;
    if (!machine.reader || !machine.reader(path, data, size))
        return false;
    out = new Shared;
    out->data = data;
    out->size = size;
    return true;
}

// ---- imports ------------------------------------------------------------
// The KERNEL32 and ADVAPI32 functions the driver code calls, with their
// stdcall argument counts. Anything else stops the machine with its name.
struct Provided
{
    const char *name;
    U arguments;
};
const Provided provided[] = {
    {"HeapCreate", 3}, {"HeapAlloc", 3}, {"HeapFree", 3}, {"HeapReAlloc", 4}, {"HeapSize", 3},
    {"RegOpenKeyA", 3}, {"RegCreateKeyExA", 9}, {"RegQueryValueExA", 6}, {"RegCloseKey", 1},
    {"GetSystemDefaultLCID", 0}, {"GetOEMCP", 0}, {"lstrlenA", 1}, {"lstrcpyA", 2}, {"lstrcatA", 2},
    {"lstrcmpiA", 2}, {"RtlMoveMemory", 3}, {"RtlZeroMemory", 2}, {"GetWindowsDirectoryA", 2},
    {"GetSystemDirectoryA", 2}, {"SearchPathA", 6}, {"CreateFileA", 7}, {"ReadFile", 5},
    {"GetFileSize", 2}, {"CreateFileMappingA", 6}, {"ConvertToGlobalHandle", 1},
    {"MapViewOfFile", 5}, {"UnmapViewOfFile", 1}, {"CloseHandle", 1}, {"GetLastError", 0},
    {"SetLastError", 1}, {"MulDiv", 3}, {"GetCurrentProcessId", 0}};

U Argument(U esp, U index) { return Dword(esp + 4 + index * 4); }

char Upper(char c) { return c >= 'a' && c <= 'z' ? char(c - 32) : c; }
bool SameText(const char *a, const char *b)
{
    while (*a && Upper(*a) == Upper(*b))
        ++a, ++b;
    return Upper(*a) == Upper(*b);
}

// The registry the driver reads: the fonts folder of the Fonts-Folder key and
// the three Gasp lists. A key handle of this machine is an
// index into a small table of full key paths; the provider answers values by
// path and name.
constexpr U KeyCount = 8, KeyBase = 0x20000;
struct Key
{
    bool used = false;
    char path[260] = {};
} keys[KeyCount];

const char *RootName(U hkey)
{
    switch (hkey)
    {
    case 0x80000000u: return "HKEY_CLASSES_ROOT";
    case 0x80000001u: return "HKEY_CURRENT_USER";
    case 0x80000002u: return "HKEY_LOCAL_MACHINE";
    case 0x80000003u: return "HKEY_USERS";
    case 0x80000005u: return "HKEY_CURRENT_CONFIG";
    default: return nullptr;
    }
}
U RegistryCall(const char *name, U esp)
{
    auto is = [name](const char *other) { return !strcmp(name, other); };
    if (is("RegOpenKeyA") || is("RegCreateKeyExA"))
    {
        const bool create = is("RegCreateKeyExA");
        char path[260] = {}, full[520] = {};
        const U hkey = Argument(esp, 0);
        if (!CString(Argument(esp, 1), path, sizeof path))
            return 87;
        const char *root = RootName(hkey);
        if (root)
            snprintf(full, sizeof full, "%s\\%s", root, path);
        else
        {
            const U index = hkey - KeyBase;
            if (index >= KeyCount || !keys[index].used)
                return 6;
            snprintf(full, sizeof full, "%s\\%s", keys[index].path, path);
        }
        for (U i = 0; i < KeyCount; ++i)
            if (!keys[i].used)
            {
                keys[i].used = true;
                snprintf(keys[i].path, sizeof keys[i].path, "%s", full);
                SetDword(Argument(esp, create ? 7 : 2), KeyBase + i);
                // RegCreateKeyExA only reads; the key it names
                // exists in the hive of the installation.
                if (create && Argument(esp, 8))
                    SetDword(Argument(esp, 8), 2);   // REG_OPENED_EXISTING_KEY
                return 0;
            }
        return 8;
    }
    if (is("RegCloseKey"))
    {
        const U index = Argument(esp, 0) - KeyBase;
        if (index < KeyCount)
            keys[index] = {};
        return 0;
    }
    // RegQueryValueExA(key, name, reserved, type, data, size)
    const U index = Argument(esp, 0) - KeyBase;
    if (index >= KeyCount || !keys[index].used || !machine.query)
        return 6;
    char value_name[128] = {};
    if (Argument(esp, 1) && !CString(Argument(esp, 1), value_name, sizeof value_name))
        return 87;
    unsigned char buffer[1024];
    U size = sizeof buffer;
    if (!machine.query(keys[index].path, value_name, buffer, size) || size > sizeof buffer)
        return 2;
    if (Argument(esp, 3))
        SetDword(Argument(esp, 3), 1);   // REG_SZ, the type of every value read here
    const U size_at = Argument(esp, 5), data = Argument(esp, 4);
    if (size_at)
    {
        const U room = Dword(size_at);
        SetDword(size_at, size);
        if (data && room < size)
            return 234;
    }
    if (data)
        Put(data, buffer, size);
    return 0;
}

U Serve(const char *name, U esp)
{
    auto is = [name](const char *other) { return !strcmp(name, other); };
    if (is("HeapCreate"))
        return 0x4eaf0000;                     // the one heap of the driver
    if (is("HeapAlloc"))
        return HeapAllocate(Argument(esp, 2));
    if (is("HeapFree"))
        return HeapFree(Argument(esp, 2));
    if (is("HeapSize"))
    {
        U c = 0, bytes = 0;
        return HeapBlock(Argument(esp, 2), c, bytes) ? bytes : ~0u;
    }
    if (is("HeapReAlloc"))
    {
        U c = 0, bytes = 0;
        const U old = Argument(esp, 2), want = Argument(esp, 3);
        if (!HeapBlock(old, c, bytes))
            return 0;
        const U fresh = HeapAllocate(want);
        if (!fresh)
            return 0;
        static unsigned char copy[4096];
        const U n = bytes < want ? bytes : want;
        for (U done = 0; done < n; done += sizeof copy)
        {
            const U chunk = n - done < sizeof copy ? n - done : sizeof copy;
            Get(old + done, copy, chunk);
            Put(fresh + done, copy, chunk);
        }
        HeapFree(old);
        return fresh;
    }
    if (is("RegOpenKeyA") || is("RegCreateKeyExA") || is("RegQueryValueExA") || is("RegCloseKey"))
        return RegistryCall(name, esp);
    if (is("GetSystemDefaultLCID"))
        return machine.locale;
    if (is("GetOEMCP"))
        return 437;
    if (is("GetCurrentProcessId"))
        return 1;
    if (is("lstrlenA"))
    {
        char text[1024];
        CString(Argument(esp, 0), text, sizeof text);
        return U(strlen(text));
    }
    if (is("lstrcpyA") || is("lstrcatA"))
    {
        char a[520] = {}, b[520] = {};
        if (is("lstrcatA"))
            CString(Argument(esp, 0), a, sizeof a);
        CString(Argument(esp, 1), b, sizeof b);
        strncat(a, b, sizeof a - strlen(a) - 1);
        Put(Argument(esp, 0), a, U(strlen(a)) + 1);
        return Argument(esp, 0);
    }
    if (is("lstrcmpiA"))
    {
        char a[520] = {}, b[520] = {};
        CString(Argument(esp, 0), a, sizeof a);
        CString(Argument(esp, 1), b, sizeof b);
        return SameText(a, b) ? 0 : 1;
    }
    if (is("RtlMoveMemory"))
    {
        const U to = Argument(esp, 0), from = Argument(esp, 1), n = Argument(esp, 2);
        unsigned char *buffer = static_cast<unsigned char *>(malloc(n ? n : 1));
        if (!buffer || !Get(from, buffer, n) || !Put(to, buffer, n))
            Fail("RtlMoveMemory outside the machine's memory");
        free(buffer);
        return 0;
    }
    if (is("RtlZeroMemory"))
    {
        static unsigned char zero[4096];
        const U to = Argument(esp, 0), n = Argument(esp, 1);
        for (U done = 0; done < n; done += sizeof zero)
            Put(to + done, zero, n - done < sizeof zero ? n - done : sizeof zero);
        return 0;
    }
    if (is("GetWindowsDirectoryA") || is("GetSystemDirectoryA"))
    {
        const char *path = is("GetWindowsDirectoryA") ? "C:\\WINDOWS" : "C:\\WINDOWS\\SYSTEM";
        const U n = U(strlen(path));
        if (Argument(esp, 1) <= n)
            return n + 1;
        Put(Argument(esp, 0), path, n + 1);
        return n;
    }
    if (is("MulDiv"))
    {
        // Win32 MulDiv: the 64-bit product, rounded half away from zero, -1
        // for a zero divisor or a result outside 32 bits.
        const long long a = int(Argument(esp, 0)), b = int(Argument(esp, 1)), c = int(Argument(esp, 2));
        if (!c)
            return ~0u;
        const long long p = a * b, magnitude = ((p < 0 ? -p : p) + (c < 0 ? -c : c) / 2) / (c < 0 ? -c : c);
        if (magnitude > 0x7fffffffLL)
            return ~0u;
        return U(int((p < 0) != (c < 0) ? -magnitude : magnitude));
    }
    if (is("GetLastError"))
        return machine.last_error;
    if (is("SetLastError"))
    {
        machine.last_error = Argument(esp, 0);
        return 0;
    }
    if (is("SearchPathA"))
    {
        // SearchPathA(path, name, extension, size, buffer, part) :
        // the fonts directory first, then the default path with the name as
        // given (a full path in practice).
        char directory[260] = {}, file[260] = {}, full[520] = {};
        if (Argument(esp, 0))
            CString(Argument(esp, 0), directory, sizeof directory);
        CString(Argument(esp, 1), file, sizeof file);
        const bool qualified = strchr(file, ':') || file[0] == '\\';
        if (qualified || !directory[0])
            snprintf(full, sizeof full, "%s", file);
        else
            snprintf(full, sizeof full, "%s%s%s", directory,
                     directory[strlen(directory) - 1] == '\\' ? "" : "\\", file);
        Shared *bytes = nullptr;
        if (!ReadFile(full, bytes))
        {
            machine.last_error = 2;
            return 0;
        }
        free(bytes->data);
        delete bytes;
        const U n = U(strlen(full));
        if (Argument(esp, 3) <= n)
            return n + 1;
        Put(Argument(esp, 4), full, n + 1);
        if (Argument(esp, 5))
        {
            const char *leaf = strrchr(full, '\\');
            SetDword(Argument(esp, 5), Argument(esp, 4) + U(leaf ? leaf + 1 - full : 0));
        }
        return n;
    }
    if (is("CreateFileA"))
    {
        char path[260] = {};
        CString(Argument(esp, 0), path, sizeof path);
        Shared *bytes = nullptr;
        if (!ReadFile(path, bytes))
        {
            machine.last_error = 2;
            return ~0u;
        }
        const U handle = NewHandle(1, bytes);
        if (!handle)
        {
            free(bytes->data);
            delete bytes;
            machine.last_error = 4;
            return ~0u;
        }
        return handle;
    }
    if (is("ReadFile"))
    {
        Handle *h = HandleAt(Argument(esp, 0));
        if (!h || h->kind != 1)
            return 0;
        U n = Argument(esp, 2);
        if (n > h->bytes->size - h->position)
            n = h->bytes->size - h->position;
        Put(Argument(esp, 1), h->bytes->data + h->position, n);
        h->position += n;
        if (Argument(esp, 3))
            SetDword(Argument(esp, 3), n);
        return 1;
    }
    if (is("GetFileSize"))
    {
        Handle *h = HandleAt(Argument(esp, 0));
        return h && h->kind == 1 ? h->bytes->size : ~0u;
    }
    if (is("CreateFileMappingA"))
    {
        Handle *h = HandleAt(Argument(esp, 0));
        if (!h || h->kind != 1)
            return 0;
        return NewHandle(2, h->bytes);
    }
    if (is("ConvertToGlobalHandle"))
        return Argument(esp, 0);
    if (is("MapViewOfFile"))
    {
        Handle *h = HandleAt(Argument(esp, 0));
        if (!h || h->kind != 2)
            return 0;
        if (!h->view)
        {
            const U bytes = (h->bytes->size + 0xffff) & ~0xffffu;
            if (ViewLimit - machine.view_top < bytes || !Map(machine.view_top, bytes) ||
                !Put(machine.view_top, h->bytes->data, h->bytes->size))
                return 0;
            h->view = machine.view_top;
            h->view_bytes = bytes;
            machine.view_top += bytes;
        }
        return h->view;
    }
    if (is("UnmapViewOfFile"))
    {
        // unmaps the view right after mapping it and keeps using its
        // address through the file object. Win98 maps a global
        // mapping at one address in the shared arena for every process; the
        // view here stays until the mapping handle is closed.
        return 1;
    }
    if (is("CloseHandle"))
    {
        Handle *h = HandleAt(Argument(esp, 0));
        if (!h)
            return 0;
        if (h->view)
            machine.cpu->Unmap(h->view, h->view_bytes);
        Drop(h->bytes);
        *h = {};
        return 1;
    }
    Fail("import without provider");
    return 0;
}

void TrapHook(X86::Cpu &, unsigned address, void *)
{
    const U at = U(address);
    if (at == Return)
        return;
    const U index = (at - Traps) / 16;
    if (index >= machine.import_count || (at - Traps) % 16)
    {
        Fail("jump into the import area");
        return;
    }
    const Import &item = machine.imports[index];
    if (item.arguments == ~0u)
    {
        char text[120];
        snprintf(text, sizeof text, "import %s has no provider", item.name);
        Fail(text);
        return;
    }
    const U esp = Register(X86::Reg::Esp);
    SetRegister(X86::Reg::Eax, Serve(item.name, esp));
}

bool FaultHook(X86::Cpu &, unsigned, unsigned address, unsigned, void *)
{
    char text[120];
    snprintf(text, sizeof text, "memory fault at %08x, eip %08x", U(address), Register(X86::Reg::Eip));
    Fail(text);
    return false;
}

U Word(const unsigned char *p) { return p[0] | U(p[1]) << 8; }
U Long(const unsigned char *p) { return Word(p) | Word(p + 2) << 16; }

// Maps GDI32.DLL at its base and points every import at a trap. The image
// has relocations but keeps its base here, so nothing is relocated.
bool LoadImage()
{
    unsigned char *file = nullptr;
    U size = 0;
    if (!machine.reader || !machine.reader("C:\\WINDOWS\\SYSTEM\\GDI32.DLL", file, size))
    {
        snprintf(machine.failure, sizeof machine.failure, "TrueType machine: no GDI32.DLL in the Win98 tree");
        return false;
    }
    bool ok = size > 0x400;
    const U pe = ok ? Long(file + 0x3c) : 0;
    ok = ok && pe + 0xf8 < size && !memcmp(file + pe, "PE\0\0", 4) && Long(file + pe + 0x34) == ImageBase;
    const U sections = ok ? Word(file + pe + 6) : 0, optional = ok ? Word(file + pe + 0x14) : 0;
    const U image_size = ok ? (Long(file + pe + 0x50) + 0xfff) & ~0xfffu : 0;
    ok = ok && image_size && Map(ImageBase, image_size) && Put(ImageBase, file, Long(file + pe + 0x54));
    for (U i = 0; ok && i < sections; ++i)
    {
        const unsigned char *s = file + pe + 0x18 + optional + i * 40;
        if (s + 40 > file + size)
        {
            ok = false;
            break;
        }
        const U address = Long(s + 12), raw = Long(s + 16), offset = Long(s + 20);
        if (raw && (offset > size || raw > size - offset || address + raw > image_size))
            ok = false;
        else if (raw)
            ok = Put(ImageBase + address, file + offset, raw < Long(s + 8) || !Long(s + 8) ? raw : Long(s + 8));
    }
    // Imports: every name gets a sixteen-byte stub "ret n" (C2 nn 00) that the
    // trap hook serves before it runs; a name without provider gets "hlt".
    static unsigned char stubs[TrapBytes];
    memset(stubs, 0xf4, sizeof stubs);
    const U imports = ok ? Long(file + pe + 0x80) : 0;
    for (U d = imports; ok && imports; d += 20)
    {
        unsigned char descriptor[20];
        if (!Get(ImageBase + d, descriptor, 20))
        {
            ok = false;
            break;
        }
        if (!Long(descriptor + 12))
            break;
        const U lookup = Long(descriptor) ? Long(descriptor) : Long(descriptor + 16), iat = Long(descriptor + 16);
        for (U n = 0;; ++n)
        {
            const U entry = Dword(ImageBase + lookup + n * 4);
            if (!entry)
                break;
            if (machine.import_count == sizeof machine.imports / sizeof *machine.imports)
            {
                ok = false;
                break;
            }
            Import &item = machine.imports[machine.import_count];
            if (entry & 0x80000000u)
                snprintf(item.name, sizeof item.name, "#%u", entry & 0xffff);
            else
                CString(ImageBase + entry + 2, item.name, sizeof item.name);
            item.arguments = ~0u;
            for (const auto &p : provided)
                if (!strcmp(p.name, item.name))
                    item.arguments = p.arguments;
            const U stub = machine.import_count * 16;
            if (item.arguments != ~0u)
            {
                stubs[stub] = 0xc2;
                stubs[stub + 1] = (unsigned char)(item.arguments * 4);
                stubs[stub + 2] = 0;
            }
            SetDword(ImageBase + iat + n * 4, Traps + stub);
            ++machine.import_count;
        }
    }
    free(file);
    if (!ok)
    {
        snprintf(machine.failure, sizeof machine.failure, "TrueType machine: GDI32.DLL is not the Win98 image");
        return false;
    }
    return Put(Traps, stubs, sizeof stubs);
}
} // namespace

void SetReader(Reader reader) { machine.reader = reader; }
void SetQuery(Query query) { machine.query = query; }
void SetLocale(U locale) { machine.locale = locale; }
bool Started() { return machine.started; }
const char *Failure() { return machine.failure; }

bool Start()
{
    if (machine.started || machine.tried)
        return machine.started;
    machine.tried = true;
#if !defined(__APPLE__) && !defined(__linux__)
    // The Pi kernel backs every engine with one slot of its fixed JIT pool
    // (src/jit_memory.cpp); a host takes the translation buffer from mmap.
    if (!neo98_jit_free_slots())
    {
        snprintf(machine.failure, sizeof machine.failure, "TrueType machine: no JIT slot left");
        return false;
    }
#endif
    // The engine of the 32-bit processes: the driver is flat 32-bit code.
    // It runs inside a GDI call, that is inside the Run of a guest processor
    // on this thread; FexCpu nests the run of one processor in the syscall
    // handler of another (tools/host_fex_cpu.cpp, Run).
    if (X86::CreateCpu(X86::ProcessCpuBackend(), machine.cpu, "truetype-machine") != X86::CpuError::Ok)
    {
        snprintf(machine.failure, sizeof machine.failure, "TrueType machine: no x86 engine");
        machine.cpu = nullptr;
        return false;
    }
    if (!Map(Tib, 0x4000) || !Map(Traps, TrapBytes) || !Map(StackBase, StackBytes) || !LoadImage())
    {
        if (!machine.failure[0])
            snprintf(machine.failure, sizeof machine.failure, "TrueType machine: memory setup failed");
        return false;
    }
    // A flat code and data selector and FS on the thread block: the driver
    // entries install SEH frames through fs:[0].
    const unsigned long long descriptors[] = {
        0, 0x00cf9b000000ffffull, 0x00cf93000000ffffull,
        0xfffull | ((unsigned long long)(Tib & 0xffffff) << 16) | (0x93ull << 40) | (0x40ull << 48)};
    Put(Gdt, descriptors, sizeof descriptors);
    X86::DescriptorTable gdt;
    gdt.base = Gdt;
    gdt.limit = sizeof descriptors - 1;
    machine.cpu->SetWide(X86::Reg::Gdtr, &gdt);
    SetRegister(X86::Reg::Ds, 0x10);
    SetRegister(X86::Reg::Es, 0x10);
    SetRegister(X86::Reg::Ss, 0x10);
    SetRegister(X86::Reg::Fs, 0x18);
    SetDword(Tib, 0xffffffffu);
    SetDword(Tib + 4, StackBase + StackBytes);
    SetDword(Tib + 8, StackBase);
    if (machine.cpu->SetGateHook(Traps, Traps + TrapBytes - 1, TrapHook, nullptr) != X86::CpuError::Ok ||
        machine.cpu->SetFaultHook(FaultHook, nullptr) != X86::CpuError::Ok)
    {
        snprintf(machine.failure, sizeof machine.failure, "TrueType machine: hook setup failed");
        return false;
    }
    machine.started = true;
    // The Linux direct backend can serve these RET stubs through a local
    // mode transition. Other engines retain their ordinary gate contract.
    machine.cpu->SetReturnGateExecution(true);
    bool ok = false;
    Call(Initialize, nullptr, 0, ok);
    if (!ok)
        machine.started = false;
    return machine.started;
}

U Call(U entry, const U *arguments, U count, bool &ok)
{
    ok = false;
    if (!machine.started || machine.stopped)
        return 0;
    U esp = StackBase + StackBytes - 64 - count * 4;
    for (U i = 0; i < count; ++i)
        SetDword(esp + i * 4, arguments[i]);
    esp -= 4;
    SetDword(esp, Return);
    SetRegister(X86::Reg::Esp, esp);
    SetRegister(X86::Reg::Ebp, 0);
    const unsigned error = machine.cpu->Run(entry, Return);
    if (error != X86::CpuError::Ok && !machine.stopped)
    {
        char text[120];
        snprintf(text, sizeof text, "emulation error %u at %08x", U(error), Register(X86::Reg::Eip));
        Fail(text);
    }
    if (machine.stopped || Register(X86::Reg::Eip) != Return)
    {
        if (!machine.stopped)
            Fail("entry did not return");
        return 0;
    }
    ok = true;
    return Register(X86::Reg::Eax);
}

// Saved states (src/x86_state_machine.cpp): the engine of the machine, which
// a loaded state makes anew, and the hooks of this file on the new one.
X86::Cpu *&StateCpu() { return machine.cpu; }
bool StateBind()
{
    return machine.cpu && machine.cpu->SetGateHook(Traps, Traps + TrapBytes - 1, TrapHook, nullptr) == X86::CpuError::Ok &&
           machine.cpu->SetFaultHook(FaultHook, nullptr) == X86::CpuError::Ok;
}

U Allocate(U bytes) { return machine.started ? HeapAllocate(bytes) : 0; }
void Release(U address)
{
    if (machine.started)
        HeapFree(address);
}
bool Read(U address, void *out, U bytes) { return machine.started && Get(address, out, bytes); }
bool Write(U address, const void *in, U bytes) { return machine.started && Put(address, in, bytes); }
U Read32(U address) { return machine.started ? Dword(address) : 0; }
U String(const char *text)
{
    const U n = U(strlen(text)) + 1, at = Allocate(n);
    if (at)
        Put(at, text, n);
    return at;
}
} // namespace TrueTypeMachine
