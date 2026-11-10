// SPDX-License-Identifier: GPL-2.0-or-later
// Native providers for the implemented Win98 KERNEL32 path family.
#include "x86_app.h"
#include "win98_path_family.h"
#include "win98_search_path.h"
#include "win98_short_alias.h"
#include "win98_nls.h"
#ifdef NEO98_RUNTIME_CONTENT
#include "windows_runtime_data.h"
#else
#include "../build/generated/nls_tables.h"
#endif
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <new>
namespace X86
{
struct Application::KernelPathEnvironment
{
    using U = unsigned;
    static constexpr U Prefix = 0xf2000000, Slots = 32, Maximum = 1024;
    struct Buffer { unsigned char *bytes = nullptr; U size = 0; } buffers[Slots];
    Application &app;
    Win98PathFamily::State core{};
    U locks[2]{}, drive_directory = 0;
    bool search = false;
    explicit KernelPathEnvironment(Application &a) : app(a) {}
    bool Initialize()
    {
        if (!(core.path = Allocate(Maximum, 8)) ||
            !(core.current_scratch = Allocate(Maximum, 8)) ||
            !(core.set_scratch = Allocate(Maximum, 8)) ||
            !(core.temp_scratch = Allocate(Maximum, 8)) ||
            !(core.task_directory = Allocate(Maximum, 8)) ||
            !(drive_directory = Allocate(Maximum, 8)) ||
            !(core.current = Text(app.files.current))) return false;
        core.task_flags = 0x10;
        core.task_drive = core.drive_cache = 2;
        return true;
    }
    ~KernelPathEnvironment() { for (auto &b : buffers) free(b.bytes); }
    static bool Private(U p) { return (p & 0xff000000) == Prefix; }
    void Invalid(const char *why) { app.Fail(why); }
    unsigned char *Access(U p)
    {
        U index = (p >> 16) & 255, offset = p & 65535;
        if (index >= Slots || !buffers[index].bytes || offset >= buffers[index].size)
        { Invalid("Kernel path storage access exceeds its native allocation"); return nullptr; }
        return buffers[index].bytes + offset;
    }
    U Read8(U p)
    {
        if (Private(p)) { const auto *b = Access(p); return b ? *b : 0; }
        unsigned char value = 0; app.Read(p, &value, 1); return value;
    }
    void Write8(U p, U v)
    {
        if (Private(p)) { if (auto *b = Access(p)) *b = v; }
        else { unsigned char value = v; app.Store(p, &value, 1); }
    }
    void Write32(U p, U v) { for (U i = 0; i < 4; ++i) Write8(p + i, v >> (i * 8)); }
    U LastError() { return app.last_error; }
    void Error(U v) { app.last_error = v; }
    U Allocate(U bytes, U flags)
    {
        if (bytes > Maximum || (flags & ~8u)) { Error(8); return 0; }
        // The original short-path caller may write its NUL at offset 260.
        U capacity = bytes == 260 ? 261 : bytes;
        for (U i = 0; i < Slots; ++i) if (!buffers[i].bytes)
        {
            auto *data = static_cast<unsigned char *>(malloc(capacity));
            if (!data) { Error(8); return 0; }
            if (flags & 8) memset(data, 0, capacity);
            buffers[i] = {data, capacity}; return Prefix | (i << 16);
        }
        Error(8); return 0;
    }
    U AllocateZero(U bytes) { return Allocate(bytes, 8); }
    U Free(U p)
    {
        if (!Private(p) || (p & 65535) || ((p >> 16) & 255) >= Slots)
        { Invalid("Kernel path free has no native owner"); return 0; }
        auto &b = buffers[(p >> 16) & 255];
        if (!b.bytes) { Invalid("Kernel path storage was freed twice"); return 0; }
        free(b.bytes); b = {}; return 1;
    }
    U Text(const char *text)
    {
        U n = strlen(text), p = Allocate(n + 1, 8);
        if (p) for (U i = 0; i <= n; ++i) Write8(p + i, static_cast<unsigned char>(text[i]));
        return p;
    }
    U Duplicate(U p)
    {
        U n = Win98PathFamily::Length(*this, p), out = Allocate(n + 1, 0);
        if (out) Win98PathFamily::Copy(*this, out, p);
        return out;
    }
    bool String(U p, char (&out)[1024])
    {
        for (U i = 0; i < sizeof out; ++i) { out[i] = Read8(p + i); if (!out[i]) return app.Status() != Failed; }
        Error(206); return false;
    }
    bool Absolute(U p, char (&out)[260])
    {
        char text[1024]; if (!String(p, text)) return false;
        U error = app.files.FullPath(text, out); if (error) Error(error); return !error;
    }
    U Attributes(U p)
    {
        char path[260]; if (!Absolute(p, path)) return ~0u;
        U node, error = app.files.store.Lookup(path, node);
        if (error) { Error(error); return ~0u; }
        return app.files.store.Get(node)->attributes;
    }
    static U Fold(U c) { return Win98ShortAlias::NameFold(c); }
    // DOS keeps the current directory of every drive but the current one in a
    // "=X:" environment variable, and the original path core asks for it by
    // that name. Here that directory lives in Files::Session.
    //
    U Environment(const char *name)
    {
        if (name && name[0] == '=' && name[1] && name[2] == ':' && !name[3] && drive_directory)
        {
            char text[260];
            app.files.Directory(name[1], text);
            U node = 0;
            if (app.files.store.Lookup(text, node))
                return 0;
            for (U i = 0; i <= strlen(text); ++i)
                Write8(drive_directory + i, (unsigned char)text[i]);
            return drive_directory;
        }
        return app.FindProcessEnvironment(name);
    }
    void Lock(U id)
    {
        if (id >= 2) { Invalid("Unknown kernel path lock"); return; }
        app.KernelPathLock(true, id);
        ++locks[id];
    }
    void Unlock(U id)
    {
        if (id >= 2 || !locks[id]) { Invalid("Unbalanced kernel path lock"); return; }
        --locks[id];
        app.KernelPathLock(false, id);
    }
    bool FileApiMode(U direction) { return Win98PathFamily::FileApiMode(core, direction); }
    void ClearFileApiState(U) { Win98PathFamily::ClearMode(core); }
    U ConvertInput(U, U) { Invalid("OEM file API conversion is not configured"); return 0; }
    U FreeInput(U, U) { Invalid("OEM file API conversion is not configured"); return 0; }
    void ConvertBytes(U, U, U, U) { Invalid("OEM file API conversion is not configured"); }
    void ConvertString(U to, U from, U direction)
    {
        // Native conversion provider: compose the already verified SBCS NLS
        // table lookups. This is not an implementation of CharToOem/OemToChar itself.
        if (!app.InitializeNls()) return;
        static const Win98Nls::Table tables[] = {
            {OriginalCodePage1252, sizeof OriginalCodePage1252},
            {OriginalCodePage437, sizeof OriginalCodePage437},
            {OriginalCodePage850, sizeof OriginalCodePage850}};
        auto table = [&](U page) -> const Win98Nls::Table * {
            U i = page == 1252 ? 0 : page == 437 ? 1 : page == 850 ? 2 : 3;
            return i < 3 && tables[i].Valid() ? &tables[i] : nullptr;
        };
        const auto *ansi = table(app.system_locale.ansi), *oem = table(app.system_locale.oem);
        if (!ansi || !oem) { Invalid("Path conversion needs a configured SBCS code page"); return; }
        const auto *source = direction ? ansi : oem, *target = direction ? oem : ansi;
        for (U n = 0; n < Maximum; ++n)
        {
            U c = Read8(from + n);
            Write8(to + n, target->Narrow(source->Wide(c)));
            if (!c) return;
        }
        Invalid("DOS task path exceeds native storage");
    }
    U DosCurrentDrive() { Invalid("DOS task directory execution is not configured"); return 0; }
    U DosCurrentDirectory(U, U) { Invalid("DOS task directory execution is not configured"); return 50; }
    void DosSelectDrive(U) { Invalid("DOS task directory execution is not configured"); }
    void DosSetDirectory(U) { Invalid("DOS task directory execution is not configured"); }
    void DosClock(U &cx, U &dx)
    {
        // ARM clock projection: elapsed time of day, not a claim of a DOS RTC.
        auto ticks = Application::Now() / 10000;
        U hundredths = ticks % 100, seconds = (ticks / 100) % 60;
        U minutes = (ticks / 6000) % 60, hours = (ticks / 360000) % 24;
        cx = (hours << 8) | minutes; dx = (seconds << 8) | hundredths;
    }
    U CreateFile(U p, U access, U share, U security, U disposition, U attributes, U templ)
    {
        if (access != 0x40000000 || share || security || disposition != 1 || attributes != 0x80 || templ)
        { Invalid("Unexpected temporary-file creation contract"); return ~0u; }
        char path[1024]; if (!String(p, path)) return ~0u;
        U handle, status, error = app.files.Open(path, 2, 0, 1, 0x80, handle, status);
        if (error) { Error(error); return ~0u; }
        return handle;
    }
    U CloseFile(U handle) { U error = app.files.Close(handle); if (error) Error(error); return !error; }
    static bool Match(const char *pattern, const char *name)
    {
        if (!strcmp(pattern, "*.*")) pattern = "*";
        const char *star = nullptr, *retry = name;
        while (*name)
        {
            if (*pattern == '?' || Fold(static_cast<unsigned char>(*pattern)) == Fold(static_cast<unsigned char>(*name))) ++pattern, ++name;
            else if (*pattern == '*') star = pattern++, retry = name;
            else if (star) pattern = star + 1, name = ++retry;
            else return false;
        }
        while (*pattern == '*') ++pattern;
        return !*pattern;
    }
    U FindFirst(U p, Win98ShortPath::Names &names)
    {
        if (search) { Error(4); return ~0u; }
        char path[260]; if (!Absolute(p, path)) return ~0u;
        char *part = strrchr(path, '\\');
        if (!part || !part[1]) { Error(2); return ~0u; }
        char pattern[256]; strcpy(pattern, part + 1);
        if (part == path + 2) part[1] = 0; else *part = 0;
        U parent, error = app.files.store.Lookup(path, parent);
        if (error) { Error(error); return ~0u; }
        for (U i = 0, id; !app.files.store.Enumerate(parent, i++, id);)
        {
            const auto *node = app.files.store.Get(id);
            // The pattern also matches the 8.3 alias, as a FAT directory
            // search does; the alias is what GetShortPathNameA picks.
            if (!Match(pattern, node->name) && !(*node->alias && Match(pattern, node->alias))) continue;
            names = {}; memcpy(names.long_name, node->name, strlen(node->name) + 1);
            if (*node->alias) memcpy(names.short_name, node->alias, strlen(node->alias) + 1);
            search = true; Error(0); return 1;
        }
        Error(2); return ~0u;
    }
    void FindClose(U handle) { if (handle != 1 || !search) Error(6); else search = false; }
};

void Application::FreeKernelPaths()
{
    if (kernel_paths) { kernel_paths->~KernelPathEnvironment(); free(kernel_paths); kernel_paths = nullptr; }
}
Win98PathFamily::State *Application::ProcessFileState()
{
    if (!kernel_paths)
    {
        void *storage = malloc(sizeof(KernelPathEnvironment));
        if (!storage) { last_error = 8; return 0; }
        auto *candidate = new (storage) KernelPathEnvironment(*this);
        if (!candidate->Initialize())
        {
            candidate->~KernelPathEnvironment(); free(storage);
            last_error = 8; return 0;
        }
        kernel_paths = candidate;
    }
    return &kernel_paths->core;
}
unsigned Application::KernelPathApi(ApiName name, const unsigned *a)
{
    using namespace Win98PathFamily;
    if(!ProcessFileState()) return 0;
    auto &e = *kernel_paths;
    if (API_IS(name,"SetFileApisToANSI")){e.core.process_flags &= ~0x40u; return 0;}
    if (API_IS(name,"SetFileApisToOEM")){e.core.process_flags |= 0x40u; return 0;}
    if (API_IS(name,"AreFileApisANSI")) return Win98PathFamily::AreFileApisANSI(e.core);
    // Serialize the persistent provider table and every path-family operation.
    // Core path/task entries take the same native recursive mutexes as needed.
    struct Guard
    {
        KernelPathEnvironment &e;
        explicit Guard(KernelPathEnvironment &env) : e(env) { e.Lock(0); }
        ~Guard() { e.Unlock(0); }
    } guard(e);
    e.core.thread_flags=threads[current_thread].file_api_flags;
    struct ModeSave {unsigned &task;Win98PathFamily::State &s;~ModeSave(){task=s.thread_flags;}} mode{threads[current_thread].file_api_flags,e.core};
    if (Status() == Failed || Status() == Exited) return 0;
    // Used public pointers must never access the private native token table.
    auto guest = [&](U pointer) { if (KernelPathEnvironment::Private(pointer)) { Fail("Guest supplied a private kernel path token"); return false; } return true; };
    if (API_IS(name,"GetFullPathNameA"))
    { if (!guest(a[0]) || (a[1] && (!guest(a[2]) || !guest(a[3])))) return 0; }
    else if (API_IS(name,"GetTempFileNameA"))
    {
        if (!a[0] || !a[1] || !a[3]) { last_error=87; return 0; }
        if (!guest(a[0]) || !guest(a[1]) || !guest(a[3])) return 0;
    }
    else if (API_IS(name,"GetShortPathNameA") || API_IS(name,"GetLongPathNameA"))
    {
        if (!guest(a[0]) || (a[2] && !guest(a[1]))) return 0;
        // / probe the string and writable output before the
        // path worker. Their SEH handler Windows 98 returns zero/error 87.
        if(!a[0]){last_error=87;return 0;}
        for(unsigned i=0;i<260;++i){
            if(!virtual_memory.Access(a[0]+i,1,false)){last_error=87;return 0;}
            unsigned char byte=0;Read(a[0]+i,&byte,1);if(!byte)break;
        }
        if(a[1]&&a[2]&&!virtual_memory.Access(a[1],a[2],true)){last_error=87;return 0;}
    }
    else if (API_IS(name,"SetCurrentDirectoryA"))
    { if (!guest(a[0])) return 0; }
    else if (a[0] && !guest(a[1])) return 0;
    U result = 0;
    if (API_IS(name,"GetFullPathNameA")) result = FullPath(e, e.core, a[0], a[1], a[2], a[3]);
    else if (API_IS(name,"GetCurrentDirectoryA")) result = CurrentDirectory(e, e.core, a[0], a[1]);
    else if (API_IS(name,"SetCurrentDirectoryA"))
    {
        result = SetCurrentDirectory(e, e.core, a[0]);
        if (result && Status() != Failed)
        {
            char path[1024];
            if (!e.String(e.core.current, path) || strlen(path) >= sizeof files.current)
            { Fail("Current directory exceeds native session capacity"); result = 0; }
            else files.Adopt(path);
        }
    }
    else if (API_IS(name,"GetTempPathA")) result = TempPath(e, e.core, a[0], a[1]);
    else if (API_IS(name,"GetTempFileNameA")) result = TempFileName(e, e.core, a[0], a[1], a[2], a[3]);
    else if (API_IS(name,"GetShortPathNameA") || API_IS(name,"GetLongPathNameA"))
        result = Win98ShortPath::Get(e, a[0], a[1], a[2], API_IS(name,"GetLongPathNameA"));
    else Fail("Unknown kernel path API");
    return Status() == Failed ? 0 : result;
}

namespace {
void AppendPath(char *out, unsigned capacity, const char *directory, const char *name)
{
    unsigned n = 0;
    if (directory)
        for (; directory[n] && n + 1 < capacity; ++n) out[n] = directory[n];
    if (n && out[n - 1] != '\\' && out[n - 1] != '/' && out[n - 1] != ':' && n + 1 < capacity)
        out[n++] = '\\';
    for (unsigned i = 0; name[i] && n + 1 < capacity; ++i) out[n++] = name[i];
    out[n] = 0;
}
} // namespace

// KERNEL32 ordinal 681 and its body. The step order comes
// from win98_search_path.h; the filesystem, the directory strings and the
// environment are native providers.
unsigned Application::SearchPathApi(const unsigned *a)
{
    const char *extension = a[2] ? ReadString(a[2]) : nullptr;
    if (!a[1]) { last_error = 87; return 0; }
    const char *requested = ReadString(a[1]);
    if (log && trace_calls) { char line[600]; snprintf(line, sizeof line, "SearchPath requested=%s extension=%s", requested, extension ? extension : ""); log(line); }
    if (!Win98SearchPath::ExtensionAccepted(extension)) { last_error = 87; return 0; }  //
    char name[260] = {};
    AppendPath(name, sizeof name, nullptr, requested);
    if (extension && *extension && Win98SearchPath::NeedsExtension(name))
    {
        const unsigned n = (unsigned)strlen(name);
        for (unsigned i = 0; extension[i] && n + i + 1 < sizeof name; ++i) name[n + i] = extension[i];
    }
    auto found = [&](const char *candidate, char (&resolved)[260]) {
        if (!*candidate) return false;
        unsigned node = 0;
        if (files.FullPath(candidate, resolved)) return false;
        if (Files::System().Lookup(resolved, node)) return false;
        const auto *record = Files::System().Get(node);
        // Win98 Windows 98 accepts a successful DOS 7143h attribute query,
        // including directories. InstallShield uses this to locate Disk1.
        return record != nullptr;
    };
    char resolved[260] = {};
    bool hit = false;
    char candidate[260] = {};
    if (Win98SearchPath::Qualified(name))                                 //
    {
        AppendPath(candidate, sizeof candidate, nullptr, name);
        hit = found(candidate, resolved);
    }
    else if (a[0])
    {
        // An explicit lpPath replaces the table with its own semicolon list.
        const char *list = ReadString(a[0]);
        for (const char *p = list; *p && !hit;)
        {
            char directory[260]; unsigned n = 0;
            while (*p && *p != ';' && n + 1 < sizeof directory) directory[n++] = *p++;
            directory[n] = 0;
            while (*p == ';') ++p;
            if (!n) continue;
            AppendPath(candidate, sizeof candidate, directory, name);
            hit = found(candidate, resolved);
        }
    }
    else
    {
        char application[260] = {};
        if (modules[0].path[0])
        {
            unsigned cut = 0;
            for (unsigned i = 0; modules[0].path[i] && i + 1 < sizeof application; ++i)
            {
                application[i] = modules[0].path[i];
                if (application[i] == '\\' || application[i] == '/') cut = i;
            }
            application[cut] = 0;
        }
        const char *path_variable = nullptr;
        const unsigned address = FindProcessEnvironment("PATH");
        if (address) path_variable = ReadString(address);
        const Win98SearchPath::Step *order = Win98SearchPath::Order(Win98SearchPath::Mode);
        for (unsigned step = 0; step < Win98SearchPath::Steps && !hit; ++step)
        {
            switch (order[step])
            {
            case Win98SearchPath::ApplicationDirectory:
                if (*application) { AppendPath(candidate, sizeof candidate, application, name); hit = found(candidate, resolved); }
                break;
            case Win98SearchPath::CurrentDirectory:
                AppendPath(candidate, sizeof candidate, files.current, name); hit = found(candidate, resolved);
                break;
            case Win98SearchPath::SystemDirectory:
                AppendPath(candidate, sizeof candidate, "C:\\WINDOWS\\SYSTEM", name); hit = found(candidate, resolved);
                break;
            case Win98SearchPath::WindowsDirectory:
                AppendPath(candidate, sizeof candidate, "C:\\WINDOWS", name); hit = found(candidate, resolved);
                break;
            case Win98SearchPath::PathVariable:
                for (const char *p = path_variable; p && *p && !hit;)
                {
                    char directory[260]; unsigned n = 0;
                    while (*p && *p != ';' && n + 1 < sizeof directory) directory[n++] = *p++;
                    directory[n] = 0;
                    while (*p == ';') ++p;
                    if (!n) continue;
                    AppendPath(candidate, sizeof candidate, directory, name);
                    hit = found(candidate, resolved);
                }
                break;
            }
        }
    }
    if (!hit) { last_error = 2; return 0; }                  // ERROR_FILE_NOT_FOUND
    const unsigned length = (unsigned)strlen(resolved);
    if (!Win98SearchPath::Fits(length + 1, a[3]))                          //
        return length + 1;
    if (!a[4]) return length + 1;
    Store(a[4], resolved, length + 1);
    if (a[5])                                                 // the file-part scanner
    {
        unsigned cut = 0;
        for (unsigned i = 0; i < length; ++i)
            if (resolved[i] == '\\' || resolved[i] == '/') cut = i + 1;
        Write32(a[5], a[4] + cut);
    }
    return length;
}
} // namespace X86
