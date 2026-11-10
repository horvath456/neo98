// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_serialui_pages.h"
#include "dll_routes.h"
// New NEO98 module graph, file loading and DLL lifecycle. Export search and
// ANSI filename copying use the implemented Win98 algorithms.
#include "registry.h"
#include "win98_exports.h"
#include "win98_library_ex.h"
#include "win98_ne_image.h"
#include "win98_win16_module.h"
#include "x86_app.h"
#include "arm64_cpu.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
namespace
{
constexpr unsigned Missing = Application::ModuleCapacity;
const char *Leaf(const char *name)
{
    const char *result = name;
    for (const char *p = name; *p; ++p)
        if (*p == '\\' || *p == '/')
            result = p + 1;
    return result;
}
bool Same(const char *a, const char *b)
{
    while (*a || *b)
    {
        unsigned x = static_cast<unsigned char>(*a++), y = static_cast<unsigned char>(*b++);
        if (x >= 'a' && x <= 'z')
            x -= 32;
        if (y >= 'a' && y <= 'z')
            y -= 32;
        if (x != y)
            return false;
    }
    return true;
}
bool Name(const char *input, char (&out)[260])
{
    const unsigned n = strlen(input);
    if (!n || n > 255)
        return false;
    memcpy(out, input, n + 1);
    if (!strchr(Leaf(out), '.'))
        strcat(out, ".DLL");
    else if (out[n - 1] == '.')
        out[n - 1] = 0;
    return true;
}
} // namespace
// A stack-owned search context belongs to the loader task. No application or
// module-layout fields are added. It disappears before initialization callbacks.
struct Application::LibraryExEnvironment {
 Application&app;Win98LibraryEx::State state;char duplicate[260]{};
 LibraryExEnvironment*previous;unsigned thread;
 static LibraryExEnvironment*&Head(){static LibraryExEnvironment*head=nullptr;return head;}
 explicit LibraryExEnvironment(Application&a):app(a),previous(Head()),thread(a.ThreadId()){Head()=this;}
 ~LibraryExEnvironment(){auto**p=&Head();while(*p&&*p!=this)p=&(*p)->previous;if(*p)*p=previous;}
 static const char*Altered(Application&a){for(auto*p=Head();p;p=p->previous)if(&p->app==&a&&p->thread==a.ThreadId())return p->state.altered?p->duplicate:nullptr;return nullptr;}
 unsigned Read8(unsigned p){unsigned char b=0;app.Read(p,&b,1);return b;}
 void Error(unsigned e){app.last_error=e;}
 bool ConvertMode(){return false;} // Existing file API provider is ANSI.
 unsigned Convert(unsigned){Error(50);return 0;}
 bool CompatibilityMode(){return false;} // No Win16 path-compatibility task flag.
 bool Compatibility(unsigned&,unsigned&,bool){Error(50);return false;}
 void FreeConverted(unsigned){}void ClearConversionFlags(){}
 unsigned Duplicate(unsigned p){const char*name=app.ReadString(p);if(app.state==Failed)return 0;if(strlen(name)>=sizeof duplicate){Error(206);return 0;}strcpy(duplicate,name);return 0xf8000000;}
 void Free(unsigned){duplicate[0]=0;}
 // the datafile branch maps the file and never initializes it.
 unsigned Datafile(unsigned p){const char*name=app.ReadString(p);if(app.state==Failed)return 0;return app.LoadDatafileModule(name);}
 unsigned Normal(unsigned p){
  const unsigned index=app.LoadModule(app.ReadString(p));
  // Windows 98 clears the task search string before DllMain executes.
  if(index!=Missing)Win98LibraryEx::BeforeInitialize(*this,state);
  unsigned result=0;
  if(index!=Missing){++app.modules[index].references;if(app.InitializeModule(index))result=app.modules[index].handle;else --app.modules[index].references;}
  if(!result){unsigned saved=app.last_error;app.CollectModules();app.last_error=saved;}
  return result;
 }
};
// KnownDLLs of this installation, as SYSTEM.DAT stores them under
// System\CurrentControlSet\Control\SessionManager\KnownDLLs. Win98 resolves a
// bare name that appears there in the system directory only, so a copy beside
// the program cannot displace a core DLL. The list is data, never a constant:
// it comes from the imported hive.
bool Application::KnownModule(const char *leaf)
{
    auto &registry = Registry::System();
    unsigned key = Registry::Missing;
    bool created = false;
    if (registry.Open(registry.roots[2], "System\\CurrentControlSet\\Control\\SessionManager\\KnownDLLs",
                      false, key, created))
        return false;
    const Registry::Value *value = nullptr;
    for (unsigned i = 0; !registry.EnumValue(key, i, value); ++i)
        if (value && value->data && value->size &&
            Same(reinterpret_cast<const char *>(value->data), leaf))
            return true;
    return false;
}
// The Win98 module search, in order. `name` already carries its extension.
// Returns the number of directories tried and fills `searched` for the
// MISSING DLL line.
unsigned Application::SearchModuleFile(const char *name, char (&path)[260], char *searched,
                                       unsigned searched_size)
{
    char directory[260];
    const char *altered = LibraryExEnvironment::Altered(*this);
    const char *source = Win98LibraryEx::SearchSource<const char *>(altered, modules[0].path);
    memcpy(directory, source, strlen(source) + 1);
    unsigned end = Leaf(directory) - directory;
    // Keep "C:\\" whole, drop the separator of every deeper directory, so the
    // MISSING DLL line reads the way a person writes a path.
    if (end > 3)
        --end;
    directory[end] = 0;
    const char *locations[16] = {};
    unsigned count = 0;
    if (KnownModule(Leaf(name)))
        locations[count++] = "C:\\WINDOWS\\SYSTEM";
    else
    {
        locations[count++] = directory;
        locations[count++] = files.current;
        locations[count++] = "C:\\WINDOWS\\SYSTEM";
        locations[count++] = "C:\\WINDOWS";
    }
    // PATH last, as Win98 does. The value is the process environment's.
    char list[1024] = {};
    if (count > 1)
    {
        const unsigned value = FindProcessEnvironment("PATH");
        const char *text = value ? ReadString(value) : nullptr;
        if (text && strlen(text) < sizeof list)
            strcpy(list, text);
    }
    char entries[12][260];
    unsigned parsed = 0;
    for (const char *p = list; *p && count < 16 && parsed < 12;)
    {
        char *entry = entries[parsed];
        unsigned n = 0;
        while (*p && *p != ';')
        {
            if (n < 259)
                entry[n++] = *p;
            ++p;
        }
        if (*p)
            ++p;
        entry[n] = 0;
        while (n && (entry[n - 1] == '\\' || entry[n - 1] == ' '))
            entry[--n] = 0;
        if (!n)
            continue;
        ++parsed;
        locations[count++] = entry;
    }
    if (searched_size)
        searched[0] = 0;
    unsigned tried = 0;
    for (unsigned i = 0; i < count; ++i)
    {
        const char *location = locations[i];
        if (!location || !*location)
            continue;
        char candidate[520], normalized[260];
        snprintf(candidate, sizeof candidate, "%s\\%s", location, name);
        if (searched_size)
        {
            const unsigned used = strlen(searched);
            if (used + strlen(location) + 2 < searched_size)
                snprintf(searched + used, searched_size - used, "%s%s", used ? ";" : "", location);
        }
        ++tried;
        if (files.FullPath(candidate, normalized))
            continue;
        unsigned node = 0;
        if (files.store.Lookup(normalized, node))
            continue;
        const auto *record = files.store.Get(node);
        if (!record || (record->attributes & 16))
            continue;
        strcpy(path, normalized);
        return tried;
    }
    path[0] = 0;
    return tried;
}
// A DLL the program image needs is not there. KERNEL32 formats
// string four with the name the import table carries, ".DLL" appended when it
// has no extension, and sets ERROR_DLL_NOT_FOUND. Only the start of
// a process shows it; a LoadLibrary that misses keeps its error.
// The first miss wins, as the pending text of the original is set once per
// failed binding.
void Application::RecordMissingImport(const char *name)
{
    if (!binding_process_image || missing_module[0])
        return;
    snprintf(missing_module, sizeof missing_module, "%s", name);
    missing_module_error = 1157;
    load_failure = true;
}
// An import names an export the DLL does not have. The binder
// passes the names of both modules (module table entry +10h) and
// the import to the error formatter. It loads string 8, "The %1 file is\nlinked to
// missing export %2:%3.", or string 9 with %3!d! when the import is an
// ordinal (the high twelve bits of the name are clear), formats it with
// FormatMessage (flags 2400h, the bare line feed stays), turns carriage
// returns into blanks and sets ERROR_GEN_FAILURE (31). The start of the
// process shows the text like a missing DLL and the creator's
// CreateProcess fails with 31, which SHELL32 reports as "A device attached
// to the system is not functioning.".
void Application::RecordMissingExport(const char *importer, const char *exporter, const char *name, unsigned ordinal)
{
    if (!binding_process_image || missing_module[0])
        return;
    auto upper_leaf = [](const char *path, char (&out)[96]) {
        const char *leaf = path;
        for (const char *p = path; *p; ++p)
            if (*p == '\\' || *p == '/' || *p == ':') leaf = p + 1;
        unsigned n = 0;
        for (; leaf[n] && n < sizeof out - 1; ++n)
            out[n] = leaf[n] >= 'a' && leaf[n] <= 'z' ? char(leaf[n] - 32) : leaf[n];
        out[n] = 0;
    };
    char first[96], second[96];
    upper_leaf(importer, first);
    upper_leaf(exporter, second);
    if (name)
        snprintf(missing_text, sizeof missing_text, "The %s file is\nlinked to missing export %s:%s.", first, second, name);
    else
        snprintf(missing_text, sizeof missing_text, "The %s file is\nlinked to missing export %s:%u.", first, second, ordinal);
    for (char *p = missing_text; *p; ++p)
        if (*p == '\r') *p = ' ';
    snprintf(missing_module, sizeof missing_module, "%s", second);
    missing_module_error = 31;
}
bool Application::LockLoader()
{
    while (loader_owner && loader_owner != ThreadId() && state != Failed && state != Exited)
    {
        threads[current_thread].block = 4;
        Suspend(Idle);
    }
    if (state == Failed || state == Exited)
        return false;
    loader_owner = ThreadId();
    ++loader_depth;
    return true;
}
void Application::UnlockLoader()
{
    if (loader_depth && !--loader_depth)
        loader_owner = 0;
}
unsigned Application::ModuleIndex(unsigned handle, bool allow_unloading)
{
    for (unsigned i = 0; i < module_watermark; ++i)
        if (modules[i].kind && (allow_unloading || !modules[i].unloading) && modules[i].handle == handle)
            return i;
    return Missing;
}
unsigned Application::FindModule(const char *input)
{
    if (!input)
        return 0;
    char name[260], path[260];
    if (!Name(input, name))
        return Missing;
    // Every glide2x.dll is the one native driver, wherever the program names
    // it, so a path finds it by its leaf.
    const bool glide = Same(Leaf(name), GlideModule()) && NativeModule(Leaf(name));
    const bool absolute = (Leaf(name) != name || strchr(name, ':')) && !glide;
    if (absolute && files.FullPath(name, path))
        return Missing;
    if (glide)
    {
        const char *leaf = Leaf(name);
        memmove(name, leaf, strlen(leaf) + 1);
    }
    for (unsigned i = 0; i < module_watermark; ++i)
        if (modules[i].kind && !modules[i].unloading &&
            Same(absolute ? path : name, absolute ? modules[i].path : Leaf(modules[i].path)))
            return i;
    return Missing;
}
// The DLLs built for ARM64 that stand in for a system file whose original is
// x86 code. Windows on ARM keeps both forms of such a
// file, the ARM64 one in System32 and the x86 one in SysWOW64, and its loader
// picks by the kind of the process. Here the x86 original lies in
// WINDOWS\SYSTEM and the ARM64 form belongs to the host, like the native
// providers, which have no file in the tree either.
namespace
{
Application::Arm64SystemModule arm64_system_modules[8];
unsigned arm64_system_module_count = 0;
} // namespace
bool Application::AddArm64SystemModule(const char *name, const unsigned char *bytes, unsigned size)
{
    if (!name || !bytes || !size) return false;
    for (unsigned i = 0; i < arm64_system_module_count; ++i)
        if (Same(arm64_system_modules[i].name, name))
        {
            arm64_system_modules[i] = {name, bytes, size};
            return true;
        }
    if (arm64_system_module_count == sizeof arm64_system_modules / sizeof arm64_system_modules[0]) return false;
    arm64_system_modules[arm64_system_module_count++] = {name, bytes, size};
    return true;
}
const Application::Arm64SystemModule *Application::FindArm64SystemModule(const char *leaf)
{
    for (unsigned i = 0; i < arm64_system_module_count; ++i)
        if (Same(arm64_system_modules[i].name, leaf)) return &arm64_system_modules[i];
    return nullptr;
}
unsigned Application::LoadModule(const char *input, unsigned parent)
{
    char name[260];
    if (!input || !Name(input, name))
    {
        last_error = 126;
        return Missing;
    }
    unsigned index = FindModule(input);
    if (index != Missing)
    {
        if (parent < ModuleCapacity && parent != index)
            modules[parent].dependencies.Add(index);
        return index;
    }
    for (index = 1; index < ModuleCapacity && modules[index].kind; ++index)
    {
    }
    if (index == ModuleCapacity)
    {
        last_error = 8;
        return Missing;
    }
    if (index >= module_watermark) module_watermark = index + 1;
    auto &m = modules[index];
    char path[260] = {};
    const bool bare = Leaf(name) == name && !strchr(name, ':');
    bool provider = NativeModule(name);
    if (!bare && !files.FullPath(name, path))
    {
        char system[260];
        snprintf(system, sizeof system, "C:\\WINDOWS\\SYSTEM\\%s", Leaf(name));
        provider = Same(path, system) && NativeModule(Leaf(name));
        // A glide2x.dll a program brings along drives a real card through
        // its VxD and cannot run here; the name alone picks the native
        // driver.
        if (Same(Leaf(name), GlideModule())) provider = NativeModule(Leaf(name));
    }
    if (provider)
    {
        m.kind = 2;
        // The native COMDLG32 keeps a fixed handle because the implemented
        // common dialogs name it as their instance; it lies in the service
        // reservation, above the native window-procedure tokens at 71000000
        // and below the handles the other providers take.
        m.handle = Same(Leaf(name), "COMDLG32.DLL") ? 0x71ff0000 : 0x72000000 + index * 65536;
        // KERNEL32 keeps the base it has under Win98. Unpackers and protectors
        // take the return address a process starts with, expect it above
        // 77E00000h, walk down in 64-KiB steps to the MZ header and read the
        // export directory themselves (ASPack in Collapse.exe). With a
        // start address in the gate region they gave up and returned into an
        // empty stack.
        if (Same(Leaf(name), "KERNEL32.DLL") && AvailableImageAddress(Win98KernelBase, 0x10000))
            m.handle = Win98KernelBase;
        snprintf(m.path, sizeof m.path, "C:\\WINDOWS\\SYSTEM\\%s", Leaf(name));
        m.bound = true;
        CreateModuleHeader(index);
        // Preserve the Win98 core DLL dependency graph even when their code
        // is provided natively. Loading USER32 also makes GDI32 and ADVAPI32
        // visible to GetModuleHandle, module enumeration and debuggers.
        const char *leaf=Leaf(name);
        if(Same(leaf,"USER32.DLL")||Same(leaf,"GDI32.DLL")||Same(leaf,"ADVAPI32.DLL")){
            if(LoadModule("KERNEL32.DLL",index)==Missing)return Missing;
            if(Same(leaf,"USER32.DLL")&&LoadModule("GDI32.DLL",index)==Missing)return Missing;
            if(!Same(leaf,"ADVAPI32.DLL")&&LoadModule("ADVAPI32.DLL",index)==Missing)return Missing;
        }
    }
    else
    {
        unsigned handle = 0, status = 0;
        unsigned result = 126;
        // An ARM64 process gets the ARM64 form of a system file by its name,
        // or by its path in the system directory.
        const Arm64SystemModule *builtin = nullptr;
        if (Arm64Process())
        {
            char system[260];
            snprintf(system, sizeof system, "C:\\WINDOWS\\SYSTEM\\%s", Leaf(name));
            if (bare || (!files.FullPath(name, path) && Same(path, system))) builtin = FindArm64SystemModule(Leaf(name));
            if (builtin)
            {
                memcpy(path, system, strlen(system) + 1);
                result = 0;
            }
        }
        if (builtin)
        {
            // The image is at hand; there is no file to open.
        }
        else if (!bare)
        {
            result = files.FullPath(name, path);
            if (!result)
                result = files.Open(path, 1, 1, 3, 0, handle, status);
        }
        else
        {
            char searched[600];
            SearchModuleFile(name, path, searched, sizeof searched);
            if (!path[0])
            {
                if (log)
                {
                    char text[1000];
                    snprintf(text, sizeof text, "MISSING DLL %s needed by %s, searched: %s", name,
                             parent < ModuleCapacity && modules[parent].kind ? modules[parent].path : modules[0].path,
                             searched);
                    log(text);
                }
                last_error = 126;
                RecordMissingImport(name);
                return Missing;
            }
            result = files.Open(path, 1, 1, 3, 0, handle, status);
        }
        if (result)
        {
            if (log && (result == 2 || result == 3))
            {
                char text[1000];
                snprintf(text, sizeof text, "MISSING DLL %s needed by %s, searched: %s", name,
                         parent < ModuleCapacity && modules[parent].kind ? modules[parent].path : modules[0].path,
                         path[0] ? path : name);
                log(text);
            }
            last_error = result == 2 || result == 3 ? 126 : result;
            if (result == 2 || result == 3)
                RecordMissingImport(name);
            return Missing;
        }
        unsigned size = 0, done = 0;
        unsigned char *data = nullptr;
        if (builtin)
        {
            size = done = builtin->size;
            data = new unsigned char[size];
            if (data) memcpy(data, builtin->bytes, size);
            else result = 8;
        }
        else
        {
            result = files.Size(handle, size);
            if (!result && !size)
                result = 193;
            data = !result ? new unsigned char[size] : nullptr;
            if (!result && !data)
                result = 8;
            if (!result)
                result = files.Read(handle, data, size, done);
            files.Close(handle);
        }
        if (result || done != size)
        {
            delete[] data;
            last_error = result ? result : 193;
            return Missing;
        }
        // An NE file is a 16-bit module. The Win16 loader of PLAN.md step 9
        // owns it: a system module of the 16-bit side never gets here, because
        // its imports resolve to native gates, so what arrives is a 16-bit DLL
        // a program ships with, and it runs as the original code it is.
        if (Win98Ne::IsImage(data, size))
        {
            unsigned error = 0;
            const unsigned handle = Win16LoadLibrary(path, data, size, error);
            delete[] data;
            if (!handle)
            {
                last_error = error ? error : 193;
                return Missing;
            }
            m.kind = 5;
            m.handle = handle;
            m.bound = true;
            strcpy(m.path, path);
            if (parent < ModuleCapacity && parent != index)
                modules[parent].dependencies.Add(index);
            return index;
        }
        auto *pe = new Pe32::Image;
        if (!pe || !pe->Parse(data, size) || !pe->IsDll())
        {
            unsigned format_error=193;
            // Win98 reads the complete fixed PE32 header before validating
            // its layout. A short read returns GEN_FAILURE; a full invalid
            // header returns BAD_FORMAT (loader_layout, including 311/312).
            if(pe&&pe->Error()&&size>=64&&data[0]=='M'&&data[1]=='Z'){
                unsigned nt=0;memcpy(&nt,data+60,4);
                if(nt<=size&&size-nt>=24&&data[nt]=='P'&&data[nt+1]=='E'&&
                   !data[nt+2]&&!data[nt+3]&&data[nt+4]==0x4c&&data[nt+5]==1)
                    format_error=size-nt<248?31:11;
            }
            delete pe;
            delete[] data;
            last_error = format_error;
            return Missing;
        }
        // A module is code for the processor of its process. An ARM64 process
        // runs no x86 code, so a DLL that is original x86 code by the DLL
        // rule is not a module it can load, and an x86 process cannot run a
        // DLL built for ARM64. Windows answers ERROR_BAD_EXE_FORMAT.
        // docs/ARM64-APPS.md.
        if (HasProcessor() && pe->Arm64() != Arm64Process())
        {
            if (log)
            {
                char text[400];
                snprintf(text, sizeof text, pe->Arm64() ? "x86 process cannot load the ARM64 module %s"
                                                        : "ARM64 process cannot load the x86 module %s", name);
                log(text);
            }
            delete pe;
            delete[] data;
            last_error = 193;
            return Missing;
        }
        // The first x86 module of a process that has run native code only,
        // the DOS process, brings its processor (src/x86_dos_vm.cpp).
        if (!HasProcessor() && !AttachProcessor())
        {
            delete pe;
            delete[] data;
            last_error = 8;
            return Missing;
        }
        if (pe->HasTls() && log)
        {
            // Static TLS slots exist only for the modules the process image
            // brings in. A module that arrives later keeps the index its file
            // carries, so its thread objects land in another module's block.
            // Win98 loads it anyway.
            char message[320];
            snprintf(message, sizeof message,
                     "Module %s loaded after process start keeps its file TLS index", path);
            log(message);
        }
        unsigned base = pe->PreferredBase();
        // Sections of the shared arena that this process holds without their
        // image may lie where this image wants to be. They go back first, so
        // the address counts as free; the ones of this image come back below.
        ReleaseSessionSectionsIn(base, pe->SizeOfImage());
        // KERNEL32 refuses a private image's preferred
        // base below 4 MiB and takes the relocation path. The system VM can
        // occupy those pages even though VirtualQuery reports MEM_FREE.
        // BWCC32 prefers 00010000 and overlaps KERNEL's data.
        // The preferred base of an ARM64 DLL lies above 4 GiB: it is always moved.
        if (!pe->HasPreferredBase() || base < 0x00400000 || !AvailableImageAddress(base, pe->SizeOfImage()))
        {
            // A module the session already moved keeps that address here too,
            // so a pointer into it means the same thing in every process.
            base = 0;
            if (pe->CanRelocate())
            {
                // Where the session already put this module, if it fits here.
                base = SessionModuleBase(Leaf(path));
                if (base && !AvailableImageAddress(base, pe->SizeOfImage()))
                    base = 0;
                if (!base)
                    for (unsigned candidate = 0x30000000; candidate < 0x60000000; candidate += 65536)
                        if (AvailableImageAddress(candidate, pe->SizeOfImage()))
                        {
                            base = candidate;
                            RecordSessionModuleBase(Leaf(path), base, pe->SizeOfImage());
                            break;
                        }
            }
        }
        if (!base)
        {
            delete pe;
            delete[] data;
            last_error = 487;
            return Missing;
        }
        m.kind = 3;
        m.handle = base;
        m.file = data;
        m.pe = pe;
        strcpy(m.path, path);
        ReleaseSessionSectionsIn(base, pe->SizeOfImage());
        if (!pe->Load(*this, base))
        {
            // Mapping succeeded as one whole image, or failed without mapping.
            if (!AvailableImageAddress(base, pe->SizeOfImage()))
                Unmap(base, pe->SizeOfImage());
            delete m.pe;
            delete[] m.file;
            m = {};
            last_error = 193;
            return Missing;
        }
        const unsigned saved = binding_module;
        binding_module = index;
        ++loader_writes;
        const bool bound = pe->BindImports(*this, Resolve, this);
        --loader_writes;
        binding_module = saved;
        if (!bound)
        {
            last_error = last_error ? last_error : 127;
            return Missing;
        }
        m.bound = true;
        // The sections with IMAGE_SCN_MEM_SHARED belong to the session, not to
        // this process. The image is loaded and bound at this point, so what
        // goes into the session is a finished section.
        ShareImageSections(index);
        if (log)
        {
            char detail[350];
            snprintf(detail, sizeof detail, "DLL mapped %s base=%08x preferred=%08x bytes=%u", m.path, base,
                     pe->PreferredBase(), pe->SizeOfImage());
            log(detail);
        }
    }
    if (parent < ModuleCapacity && parent != index)
        modules[parent].dependencies.Add(index);
    return index;
}
bool Application::Resolve(void *context, const char *dll, const char *name, unsigned ordinal,
                          unsigned *address)
{
    auto &app = *static_cast<Application *>(context);
    const unsigned binding = app.binding_module;
    const char *importer = binding < ModuleCapacity && app.modules[binding].kind ? app.modules[binding].path
                                                                     : app.modules[0].path;
    const unsigned module = app.LoadModule(dll, binding);
    if (module == Missing)
    {
        if (app.log)
        {
            char text[700];
            snprintf(text, sizeof text, "DLL dependency load failed %s error=%u", dll, app.last_error);
            app.log(text);
        }
        return false;
    }
    if (app.Arm64Process())
    {
        // The import table of an ARM64 program gets only what the signature
        // table describes; a name of Win64 that leads to another export here
        // (GetWindowLongPtrA) gets an entry of its own with its signature.
        const char *provider = nullptr;
        *address = 0;
        // A DLL of the program itself, ARM64 code like it: the export is the
        // address of the function.
        if (app.modules[module].kind == 3 && app.modules[module].pe && app.modules[module].pe->Arm64())
            *address = app.ModuleProcedure(module, name, ordinal, 0, true);
        else if (name && (*address = Arm64::CoreGate(*app.cpu, dll, name)) != 0)
        {
            // An export the processor of the process answers itself.
        }
        else if (name && Arm64::Described(dll, name, provider))
        {
            const unsigned gate = app.ModuleProcedure(module, provider, 0, 0, true);
            *address = gate && strcmp(provider, name) ? Arm64::AliasGate(*app.cpu, dll, name, gate) : gate;
        }
        else if (app.log)
        {
            char text[400];
            snprintf(text, sizeof text, "ARM64 import %s!%s has no signature in sdk/api", dll, name ? name : "(ordinal)");
            app.log(text);
        }
    }
    else
        *address = app.ModuleProcedure(module, name, ordinal, 0, true);
    if (!*address)
    {
        app.last_error = 127;
        // Every unresolved export by name, so a run names the gap instead of
        // only the DLL that carries it.
        if (app.log)
        {
            char text[700], symbol[160];
            if (name)
                snprintf(symbol, sizeof symbol, "%s", name);
            else
                snprintf(symbol, sizeof symbol, "#%u", ordinal);
            snprintf(text, sizeof text, "MISSING IMPORT %s!%s needed by %s", dll, symbol, importer);
            app.log(text);
        }
        app.RecordMissingExport(importer, app.modules[module].path, name, ordinal);
        app.last_error = 127;
    }
    return *address != 0;
}
unsigned Application::ModuleProcedure(unsigned index, const char *name, unsigned ordinal, unsigned recursion,
                                      bool importing)
{
    if (index >= ModuleCapacity || !modules[index].kind || modules[index].unloading || recursion >= 16)
        return 0;
    auto &m = modules[index];
    if (m.kind == 2)
    {
        unsigned address = 0;
        return NativeProcedure(Leaf(m.path), name, ordinal, &address, importing) ? address : 0;
    }
    Pe32::Image::Symbol symbol;
    if (!m.pe || !m.pe->FindExport(name, ordinal, symbol))
        return 0;
    if (symbol.address)
        return symbol.address;
    char *dot = strchr(symbol.forwarder, '.');
    if (!dot || dot == symbol.forwarder || !dot[1])
        return 0;
    *dot++ = 0;
    unsigned target = FindModule(symbol.forwarder);
    // Runtime GetProcAddress follows Win98: a forwarder must name a loaded DLL.
    // Import binding may load that dependency as part of the image graph.
    if (target == Missing && importing)
        target = LoadModule(symbol.forwarder, index);
    if (target == Missing)
        return 0;
    if (target != index)
        m.dependencies.Add(target);
    if (*dot == '#')
    {
        unsigned value = 0;
        if (!*++dot)
            return 0;
        for (; *dot; ++dot)
        {
            if (*dot < '0' || *dot > '9' || value > 6553)
                return 0;
            value = value * 10 + *dot - '0';
            if (value > 65535)
                return 0;
        }
        return ModuleProcedure(target, nullptr, value, recursion + 1, importing);
    }
    return ModuleProcedure(target, dot, 0, recursion + 1, importing);
}
bool Application::InitializeModule(unsigned index)
{
    auto &m = modules[index];
    if (m.attached || m.initializing)
        return true;
    if (!m.bound || m.unloading)
    {
        last_error = 1114;
        return false;
    }
    m.initializing = true;
    for (unsigned i = 1; i < module_watermark; ++i)
        if (m.dependencies.Has(i) && !InitializeModule(i))
        {
            m.initializing = false;
            return false;
        }
    unsigned success = 1;
    if (m.kind == 2 && Same(Leaf(m.path), "SHLWAPI.DLL"))
        success = AttachShellStopwatch();
    // COMDLG32 takes its private TLS index in its own DLL_PROCESS_ATTACH
    // (original ), so only a process that loads it loses a public slot.
    if (m.kind == 2 && Same(Leaf(m.path), "COMDLG32.DLL"))
        success = InitializeCommonDialog();
    if (m.kind == 2 && Same(Leaf(m.path), "COMCTL32.DLL"))
    {
        const unsigned args[] = {m.handle};
        success = CommonInitApi("DLL_PROCESS_ATTACH", args);
    }
    if (m.kind == 3 && m.pe->Entry())
    {
        const unsigned args[] = {m.handle, 1, modules[0].attached ? 0u : 1u};
        // No DLL_PROCESS_DETACH after a refused DLL_PROCESS_ATTACH. KERNEL32
        // calls the entry point, which turns a FALSE return
        // into code 3; its caller Windows 98 answers that by
        // clearing bit 8 of the module flag word at +14h and returns. The
        // detach walk Windows 98 calls with reason 0 only for a module
        // whose bit 8 is still set, so a module that refused never
        // sees the second call. An MSVC built DLL relies on that: its
        // DllMainCRTStartup has already torn the CRT down, and a detach would
        // re-enter the freed critical sections.
        success = Call(m.pe->Entry(), args, 3);
    }
    m.initializing = false;
    if (!success || state == Failed || state == Exited)
    {
        // The loader's own message names no module, and a process that stops
        // in DLL_PROCESS_ATTACH is the most common single crash class of the
        // test matrix, so say which module refused.
        if (log && !success && state != Failed && state != Exited)
        {
            char text[320];
            snprintf(text, sizeof text, "DLL init refused module=%s", m.path);
            log(text);
        }
        last_error = 1114;
        return false;
    }
    m.attached = true;
    m.order = ++module_order;
    return true;
}
void Application::DeleteModule(unsigned index)
{
    auto &m = modules[index];
    if (m.kind == 2 && Same(Leaf(m.path), "SHLWAPI.DLL"))
        FreeShellStopwatch();
    for (auto &resource : resource_handles)
        if (resource.id && resource.module == index) resource = {};
    ReleaseModuleHeader(index);
    if (m.kind == 3)
    {
        if (cpu && m.pe)
        {
            DropSessionBlocksIn(m.handle, m.pe->SizeOfImage());
            Unmap(m.handle, m.pe->SizeOfImage());
        }
        delete m.pe;
        delete[] m.file;
    }
    m = {};
    for (unsigned i = 0; i < module_watermark; ++i)
        modules[i].dependencies.Remove(index);
    while (module_watermark > 1 && !modules[module_watermark - 1].kind) --module_watermark;
}
void Application::CollectModules()
{
    // Process detach keeps every image mapped until all entry points return.
    // A FreeLibrary from DllMain must not unmap that very callback (MSHTML
    // does this while releasing its dynamically loaded dependencies).
    if (collecting_modules || modules_terminated)
        return;
    collecting_modules = true;
    for (;;)
    {
        ModuleSet reachable;
        reachable.Add(0);
        for (unsigned i = 1; i < module_watermark; ++i)
            if (modules[i].references || modules[i].initializing)
                reachable.Add(i);
        for (unsigned pass = 0; pass < module_watermark; ++pass)
        {
            ModuleSet before = reachable;
            for (unsigned i = 0; i < module_watermark; ++i)
                if (reachable.Has(i))
                    reachable.Merge(modules[i].dependencies);
            if (reachable == before)
                break;
        }
        unsigned chosen = Missing;
        for (unsigned i = 1; i < module_watermark; ++i)
            if (modules[i].kind && !reachable.Has(i) &&
                (chosen == Missing || modules[i].order > modules[chosen].order))
                chosen = i;
        if (chosen == Missing)
            break;
        auto &m = modules[chosen];
        m.unloading = true;
        if (m.kind == 3 && m.attached && m.pe->Entry() && state != Failed && state != Exited)
        {
            const unsigned args[] = {m.handle, 0, 0};
            Call(m.pe->Entry(), args, 3);
        }
        // KERNEL32 tells USER about each module it frees, after the detach
        // call: the classes the process registered for it go with it.
        if (user_initialized && state != Failed && state != Exited)
            User::DestroyLibraryClasses(m.handle);
        DeleteModule(chosen);
    }
    collecting_modules = false;
}
unsigned Application::ModuleOrder(unsigned (&indices)[ModuleCapacity], unsigned (&orders)[ModuleCapacity], bool thread, bool reverse)
{
    unsigned count = 0;
    for (unsigned i = 1; i < module_watermark; ++i)
    {
        const auto &m = modules[i];
        if (m.kind != 3 || !m.attached || m.unloading || !m.pe->Entry() || (thread && m.no_thread_calls))
            continue;
        unsigned position = count++;
        while (position && (reverse ? orders[position - 1] < m.order : orders[position - 1] > m.order))
        {
            indices[position] = indices[position - 1];
            orders[position] = orders[position - 1];
            --position;
        }
        indices[position] = i;
        orders[position] = m.order;
    }
    return count;
}
void Application::NotifyModules(unsigned reason)
{
    // Snapshot only live modules, so work stays bounded after many reloads.
    // A callback can unload/reload a slot; its new occupant must not receive
    // a notification that was pending for the old module.
    unsigned indices[ModuleCapacity], orders[ModuleCapacity];
    const unsigned count = ModuleOrder(indices, orders, true, reason == 3);
    for (unsigned i = 0; i < count && state != Failed && state != Exited; ++i)
    {
        auto &m = modules[indices[i]];
        if (m.kind == 3 && m.attached && !m.unloading && !m.no_thread_calls && m.order == orders[i])
        {
            const unsigned args[] = {m.handle, reason, 0};
            Call(m.pe->Entry(), args, 3);
        }
    }
}
void Application::TerminateModules()
{
    if (modules_terminated || !LockLoader())
        return;
    modules_terminated = true;
    for (unsigned i = 0; i < ThreadCapacity; ++i)
        if (i != current_thread)
            threads[i].done = true;
    unsigned indices[ModuleCapacity], orders[ModuleCapacity];
    const unsigned count = ModuleOrder(indices, orders, false, true);
    for (unsigned i = 0; i < count && state != Failed && state != Exited; ++i)
    {
        auto &m = modules[indices[i]];
        if (m.kind == 3 && m.attached && !m.unloading && m.order == orders[i])
        {
            m.attached = false;
            const unsigned args[] = {m.handle, 0, 1};
            Call(m.pe->Entry(), args, 3);
        }
    }
    UnlockLoader();
}
// The module a null handle names: the exe MODREF at PDB+98h (GetModuleHandleA, GetModuleFileNameA ). A Win16 process has no Win32
// image, and KERNEL32 makes its PDB with MTE index 0 (PDB+2Ah) and
// the MODREF of that index at +98h, which is KERNEL32
// itself, the first module the MTE table got. So a 32-bit DLL that
// LoadLibraryEx32W brings into a 16-bit task sees KERNEL32 there.
unsigned Application::NullModuleIndex()
{
    if (modules[0].kind != 4)
        return 0;
    for (unsigned i = 1; i < module_watermark; ++i)
        if (modules[i].kind && !modules[i].unloading && Same(Leaf(modules[i].path), "KERNEL32.DLL"))
            return i;
    return 0;
}
unsigned Application::ModuleApi(ApiName api, const unsigned *a)
{
    if (!LockLoader())
        return 0;
    unsigned result = 0;
    // FreeLibrary Windows 98 tests bit 0 first and unmaps a datafile view
    // without touching the module graph.
    if (API_IS(api,"FreeLibrary") && (a[0] & 1))
    {
        const bool freed = FreeDatafileModule(a[0]);
        if (!freed)
            last_error = 6;
        UnlockLoader();
        return freed;
    }
    if (API_IS(api,"GetModuleHandleA"))
    {
        unsigned index = a[0] ? FindModule(ReadString(a[0])) : NullModuleIndex();
        if (index != Missing)
            result = modules[index].handle;
        else
            last_error = 126;
    }
    else if (API_IS(api,"GetModuleFileNameA"))
    {
        if (a[2])
        {
            const char zero = 0;
            Store(a[1], &zero, 1);
        }
        const unsigned index = a[0] ? ModuleIndex(a[0]) : NullModuleIndex();
        if (index == Missing)
            last_error = 87;
        else
        {
            char text[260];
            result = Win98Exports::FileName(text, modules[index].path, strlen(modules[index].path), a[2]);
            if (a[2])
                Store(a[1], text, result + 1);
        }
    }
    else if (API_IS(api,"GetProcAddress"))
    {
        const unsigned index = ModuleIndex(a[0]);
        if (index == Missing)
            last_error = 6;
        else
        {
            const char *name = a[1] < 65536 ? nullptr : ReadString(a[1]);
            const bool guest_module = modules[index].kind == 3 && modules[index].pe && modules[index].pe->Arm64();
            if (Arm64Process() && name && !guest_module)
            {
                const char *dll = Leaf(modules[index].path);
                const char *provider = nullptr;
                result = Arm64::CoreGate(*cpu, dll, name);
                if (!result && Arm64::Described(dll, name, provider))
                {
                    const unsigned gate = ModuleProcedure(index, provider, 0);
                    result = gate && strcmp(provider, name) ? Arm64::AliasGate(*cpu, dll, name, gate) : gate;
                }
            }
            else
                result = ModuleProcedure(index, name, a[1]);
            if(log && trace_calls && api_calls>=trace_from) {
                char text[512];
                if(a[1]<65536)
                    snprintf(text,sizeof text,"DYNAMIC_IMPORT module=%s ordinal=%u result=%08x",modules[index].path,a[1],result);
                else
                    snprintf(text,sizeof text,"DYNAMIC_IMPORT module=%s name=%s result=%08x",modules[index].path,ReadString(a[1]),result);
                log(text);
            }
            // Measuring mode: every name a program asks for at run time, with
            // the answer. A packed loader asks for hundreds and ends the
            // process on the first null.
            if (export_census && log)
            {
                char text[512];
                if (a[1] < 65536)
                    snprintf(text, sizeof text, "PROBE_CENSUS_LOOKUP module=%s ordinal=%u result=%08x",
                             Leaf(modules[index].path), a[1], result);
                else
                    snprintf(text, sizeof text, "PROBE_CENSUS_LOOKUP module=%s name=%s result=%08x",
                             Leaf(modules[index].path), ReadString(a[1]), result);
                log(text);
            }
            if (!result)
                last_error = 127;
        }
    }
    else if (API_IS(api,"LoadLibraryExA"))
    {
        LibraryExEnvironment environment(*this);
        result=Win98LibraryEx::Load(environment,environment.state,a[0],a[1],a[2]);
    }
    else if (API_IS(api,"LoadLibraryA"))
    {
        const unsigned index = LoadModule(a[0] ? ReadString(a[0]) : nullptr);
        if (index != Missing)
        {
            ++modules[index].references;
            if (InitializeModule(index))
                result = modules[index].handle;
            else
                --modules[index].references;
        }
        if (!result)
        {
            unsigned saved = last_error;
            CollectModules();
            last_error = saved;
        }
    }
    else
    {
        const unsigned index = ModuleIndex(a[0]);
        if (index == Missing || !index)
            last_error = 6;
        else if (API_IS(api,"DisableThreadLibraryCalls"))
        {
            modules[index].no_thread_calls = true;
            result = 1;
        }
        else
        {
            // Static dependencies retain the module even if FreeLibrary is
            // mistakenly called on a handle obtained with GetModuleHandle.
            if (modules[index].references)
                --modules[index].references;
            CollectModules();
            result = 1;
        }
    }
    UnlockLoader();
    return result;
}

namespace{
// The Win16 module catalogue of PLAN.md step 8, one row per Route::Native16
// entry of src/dll_routes.h. The row order is the slot order of
// Application::native_win16_modules and of CatalogArgumentWords; the file name
// is the installed NE module the implementation stands for, and `names` are
// the export names ordinal 36 resolves, starting at `first`. A module whose
// thunk targets are internal routines has no names; its catalogue ordinals are
// reachable through the token table only.
struct CatalogModule{const char*path;unsigned first,last;const char*const*names;unsigned name_count;};
const char*const serialui_names[]={"WEP","ENUMPROPPAGES","DRVCOMMCONFIGDIALOG","DRVSETDEFAULTCOMMCONFIG","DRVGETDEFAULTCOMMCONFIG","FEFIFOFUM"};
const char*const ver_names[]={"GETFILEVERSIONINFOSIZE","GETFILEVERSIONINFO","VERFINDFILE","VERINSTALLFILE","VERLANGUAGENAME","VERQUERYVALUE"};
// DCIMAN.DLL ordinals 1 to 40 as the NE entry table carries them; the three
// gaps are its internal thunk targets and keep the catalogue ordinals 76 to 79.
const char*const dciman_names[]={"DCIOPENPROVIDER","DCICLOSEPROVIDER","DCICREATEPRIMARY","DCICREATEOFFSCREEN",
 "DCICREATEOVERLAY","DCIENUM","DCISENDCOMMAND","DCISETSRCDESTCLIP","WEP","WINWATCHOPEN","WINWATCHCLOSE",
 "WINWATCHDIDSTATUSCHANGE","WINWATCHNOTIFY","WINWATCHGETCLIPLIST","DLLENTRYPOINT","DCITHK_THUNKDATA16",
 "","","","GETWINDOWREGIONDATA","GETDCREGIONDATA","","","","","","","","",
 "DCIDESTROY","DCIENDACCESS","DCIBEGINACCESS","DCIDRAW","DCISETCLIPLIST","DCISETDESTINATION",
 "","","","","DCICREATEPRIMARY32"};
const char*const dskapi_names[]={"DLLDRV_GETDRIVERVERSION","___EXPORTEDSTUB","DLLDRV_GETCAPABILITIES",
 "DLLDRV_GETDRIVEMAP","DLLDRV_GETORIGDRIVE","DLLDRV_GETAUTOMOUNT","DLLDRV_SETAUTOMOUNT","DLLDRV_SWITCHDRIVE",
 "DLLDRV_UNMOUNT","","DLLDRV_GETDRIVERANGE","DLLDRV_NUMDISKUNITS","DLLDRV_NUMFRAGMENTS","DLLDRV_ISDRIVEPROT",
 "DLLDRV_REMOUNT","DLLDRV_SETCOMPRESSIONALGO","DLLDRV_SETCOMPRESSIONTHRESH","DLLDRIVE_GETVOLFORMAT",
 "DLLGETCLUSTERSIZE","DLLGETVOLNAME","DLLCOMPRESSIONTYPE","DLLLOCKDRIVE","DLLUNLOCKDRIVE","DLLGETDRIVEECR",
 "DLLGETDRIVEACR","DLLGETFIRSTCLUSTER","DLLGETDRIVECOMPFREE","DLLGETDRIVEPHYSFREE","DLLGETDRIVECOMPUSED",
 "DLLGETDRIVEPHYSUSED","DLLGETDRIVECOMPTOTAL","DLLGETDRIVEPHYSTOTAL","DLLGETDRIVEOVERHEAD",
 "DLLDRV_GETCOMPRESSIONALGO"};
const char*const user_names[]={"POSTMESSAGE32"};
const char*const pifmgr_names[]={"WEP","OPENPROPERTIES","GETPROPERTIES","SETPROPERTIES","EDITPROPERTIES",
 "FLUSHPROPERTIES","ENUMPROPERTIES","ASSOCIATEPROPERTIES","CLOSEPROPERTIES","LOADPROPERTYLIB","ENUMPROPERTYLIBS",
 "FREEPROPERTYLIB","ADDPROPERTYSHEET","REMOVEPROPERTYSHEET","LOADPROPERTYSHEETS","ENUMPROPERTYSHEETS",
 "FREEPROPERTYSHEETS","PIFPROPGETPAGES","DLLCANUNLOADNOW","CREATESTARTUPPROPERTIES","DELETESTARTUPPROPERTIES",
 "INITPIFREGENTRIES","PROCESSSTARTUPPROPERTIES"};
const CatalogModule catalog[]={
 {"C:\\WINDOWS\\SYSTEM\\SERIALUI.DLL",1,6,serialui_names,6},
 {"C:\\WINDOWS\\SYSTEM\\VER.DLL",6,11,ver_names,6},
 // DDRAW16.DLL exports only its two thunk data blocks, DLLENTRYPOINT and WEP;
 // all 65 targets of THK3216_THUNKDATA16 are internal, so the catalogue
 // ordinal is the index byte plus one and there are no names.
 {"C:\\WINDOWS\\SYSTEM\\DDRAW16.DLL",1,65,nullptr,0},
 {"C:\\WINDOWS\\SYSTEM\\DCIMAN.DLL",1,79,dciman_names,40},
 {"C:\\WINDOWS\\SYSTEM\\DSKAPI16.DLL",4,37,dskapi_names,34},
 // DMUSIC16.DLL exports WEP, the two MMSYSTEM callbacks, the timer callback,
 // DLLENTRYPOINT, DRIVERPROC and its thunk data block; none of its nine thunk
 // targets is a named entry point, so the catalogue ordinal is the index byte
 // plus one and there are no names.
 {"C:\\WINDOWS\\SYSTEM\\DMUSIC16.DLL",1,9,nullptr,0},
 // PIFMGR.DLL: the shell's page collector (SHELL32 ) loads it by
 // name and asks for PIFPROPGETPAGES (ordinal 18), which it then calls
 // through SHELL.DLL's flat thunk index 10; the export and the pages are
 // src/x86_pif_pages.cpp. The property services behind ordinals
 // 2, 3, 4 and 9 are reached through the SHELL forwarders.
 {"C:\\WINDOWS\\SYSTEM\\PIFMGR.DLL",1,23,pifmgr_names,23},
 // USER.EXE: MSAFD.DLL asks it for POSTMESSAGE32 (ordinal 825)
 // and hands the address to WSOCK2.VXD, which posts the WSAAsyncSelect
 // messages through it. The native WSOCK2 posts through USER itself and only
 // needs to know that the address was handed over. USER is
 // always loaded under Win98, so LoadLibrary16("USER") finds it.
 {"C:\\WINDOWS\\SYSTEM\\USER.EXE",825,825,user_names,1},
 // TSP3216S.DLL: TAPI's bridge to 16-bit telephony providers, reached through
 // TSP3216L.TSP's TAPIFTHK block; the catalogue ordinals are the NE ordinals
 // (src/x86_tsp3216s.cpp).
 {"C:\\WINDOWS\\SYSTEM\\TSP3216S.DLL",1,113,nullptr,0},
 // INET16.DLL: ICFG95's component check, index 8 of WIZTHK_THUNKDATA16, an
 // internal routine, so ordinal 9 (src/x86_inet16.cpp).
 {"C:\\WINDOWS\\SYSTEM\\INET16.DLL",9,9,nullptr,0},
};
}
// KERNEL32 ordinals 35, 36 and 37. Native module records expose implemented
// Win16 services through segmented dispatch identities; their NE code is never
// run. Any other NE file is what KRNL386's LoadLibrary would load, so it is
// placed in this process as original 16-bit code the way a printer driver is
// (src/x86_win16_hosted.cpp). SHELL32 loads a 16-bit Control Panel applet
// this way and calls its CPlApplet through SHELL.DLL.
unsigned Application::Win16ModuleApi(unsigned ordinal, const unsigned *a)
{
 ++win16_module_requests;
 if(ordinal!=Win98Win16Module::LoadLibrary16&&Win16HostedModule(a[0]&65535)){
  const unsigned handle=a[0]&65535;
  if(ordinal==Win98Win16Module::FreeLibrary16){Win16FreeLibrary(handle);return 0;}
  if(ordinal==Win98Win16Module::GetProcAddress16){
   // KRNL386 GetProcAddress: a selector of zero is an ordinal, else a name.
   unsigned number=a[1];
   if(number>>16)number=Win16HostedOrdinal(handle,ReadString(number));
   return number?Win16HostedProcedure(handle,number):0;
  }
  return 0;
 }
 // The modules of the system a 16-bit program reaches through gates
 // (DllRoutes::Win16System) are always loaded under Win98. A 32-bit program
 // that asks for one by its module name gets a handle of its own, and for an
 // export the far address of the gate, which it calls through QT_Thunk like
 // any 16-bit routine: MSISYS.OCX of MSINFO32 takes eighteen entries of
 // KERNEL, four of USER, one of GDI and five of TOOLHELP this way.
 // The original KRNL386.EXE and GDI.EXE are never placed as 16-bit code.
 // (USER and VER have rows of the catalogue below and keep them.)
 static const char*const system16[]={"KERNEL","GDI","TOOLHELP"};
 constexpr unsigned System16Handle=0xf007;
 const auto system_gate=[&](const char*module,unsigned number)->unsigned{
  if(!Win16HostedReady())return 0;
  const char*label=nullptr;
  if(number>>16){label=ReadString(number);number=0;}
  char copy[96]="";if(label)snprintf(copy,sizeof copy,"%s",label);
  if(Same(module,"KERNEL")){
   const unsigned paragraph=Win16RealModeParagraph(number,label?copy:nullptr);
   if(paragraph!=~0u)return Win16RealModeSelector(paragraph)<<16;
  }
  if(label&&!Win16SystemOrdinal(module,copy))return 0;
  const unsigned gate=Win16Gate(module,label?copy:nullptr,number);
  return gate?Win16GatePointer(gate):0;
 };
 if(ordinal!=Win98Win16Module::LoadLibrary16){
  const unsigned handle=a[0]&65535;
  if(handle>=System16Handle&&!((handle-System16Handle)&7)&&(handle-System16Handle)/8<sizeof system16/sizeof*system16){
   if(ordinal==Win98Win16Module::GetProcAddress16)return system_gate(system16[(handle-System16Handle)/8],a[1]);
   return 0;
  }
 }
 if(ordinal==Win98Win16Module::LoadLibrary16){
  {char input[64];snprintf(input,sizeof input,"%s",ReadString(a[0]));
   // "TOOLHELP.DLL" names the module as "TOOLHELP" does; a path names a file.
   char*dot=strrchr(input,'.');
   if(dot&&!strpbrk(input,"\\:/")&&(Same(dot,".DLL")||Same(dot,".EXE")))*dot=0;
   for(unsigned i=0;i<sizeof system16/sizeof*system16;++i)if(Same(input,system16[i]))return System16Handle+i*8;}
  char name[260];if(!Name(ReadString(a[0]),name))return 2;
  // A module name of the system, such as "USER", names the loaded module and
  // its file carries.EXE; Name would have added.DLL.
  {const char*input=ReadString(a[0]);if(!strchr(input,'.')&&DllRoutes::Win16System(input)&&strlen(input)<250){strcpy(name,input);strcat(name,".EXE");}}
  const char*bases[]={"","C:\\WINDOWS\\SYSTEM\\","C:\\WINDOWS\\"};
  unsigned native=0;bool exists=false;char hosted[260]={};
  for(const char*base:bases){
   char candidate[520],absolute[260];snprintf(candidate,sizeof candidate,"%s%s",base,name);
   if(!files.FullPath(candidate,absolute)){
    // Installed originals are references for these native providers. Their
    // presence must not turn a supported native module into a bad NE image.
    for(const auto&row:catalog)if(Same(absolute,row.path)){native=unsigned(&row-catalog)+1;break;}
    if(native)break;
    unsigned id=0;
    if(!files.store.Lookup(absolute,id)){
     exists=true;
     // An NE file is a 16-bit module; KRNL386 answers 21 only for a PE file.
     unsigned char head[0x40];unsigned handle=0,status=0,done=0;
     if(!files.Open(absolute,1,1,3,0,handle,status)){
      unsigned ne=0;unsigned char magic[2]={};
      if(!files.Read(handle,head,sizeof head,done)&&done==sizeof head&&head[0]=='M'&&head[1]=='Z'){
       ne=head[0x3c]|head[0x3d]<<8|head[0x3e]<<16|unsigned(head[0x3f])<<24;
       unsigned position=0;
       if(!files.Seek(handle,ne,0,position)&&!files.Read(handle,magic,2,done)&&done==2&&magic[0]=='N'&&magic[1]=='E')
        snprintf(hosted,sizeof hosted,"%s",absolute);
      }
      files.Close(handle);
     }
     break;
    }
    char installed[260];snprintf(installed,sizeof installed,"C:\\WINDOWS\\SYSTEM\\%s",Leaf(name));
    if(Same(absolute,installed)&&NativeModule(Leaf(name))){exists=true;break;}
   }
   if(!*base&&(strchr(name,'\\')||strchr(name,':')))break;
  }
  if(!native&&exists&&hosted[0])return Win16HostedLoad(hosted);
  if(!native)return exists?21:2;
  // Native catalogue entry backed by the locally verified NE module. Its
  // descriptor contains dispatch identities, never the original x86 code.
  auto&m=native_win16_modules[native-1];
  if(m.handle){++m.references;return m.handle;}
  if(next_module_alias>65535)return 8;
  unsigned memory=Allocate(65536);if(!memory)return 8;
  unsigned segment=SegmentMap(memory);if(!segment){Free(memory);return 8;}
  m={next_module_alias,1,memory,segment};next_module_alias+=8;
  if (native == 9) { const unsigned zero[8] = {}; Store(memory, zero, sizeof zero); }
  return m.handle;
 }
 NativeWin16Module*m=nullptr;
 for(auto&entry:native_win16_modules)if(entry.handle&&(a[0]&65535)==entry.handle){m=&entry;break;}
 if(!m)return 0;
 if(ordinal==Win98Win16Module::FreeLibrary16){
  if(!--m->references){selectors.Unmap(m->segment);Free(m->memory);*m=NativeWin16Module{};}
  return 0;
 }
 if(ordinal==Win98Win16Module::GetProcAddress16){
  // Ordinal 36 takes either an ordinal number or a name; the catalogue row of
  // this slot says which ordinals it carries and which names resolve.
  const auto&row=catalog[unsigned(m-native_win16_modules)];
  unsigned number=a[1];
  if(number>>16){
   const char*name=ReadString(number);number=0;
   for(unsigned i=0;i<row.name_count;++i)if(row.names[i]&&*row.names[i]&&Same(name,row.names[i])){number=row.first+i;break;}
  }
  if(number>=row.first&&number<=row.last)return m->segment+0x100+number*16;
  // Another export of USER: its gate.
  return Same(row.path,"C:\\WINDOWS\\SYSTEM\\USER.EXE")&&a[1]?system_gate("USER",a[1]):0;
 }
 return 0;
}
// The value ordinal 37 answers with, for a caller that already holds the
// handle: the flat thunk connector fills the token table of a Route::Native16
// block with these identities (src/x86_flat_thunk.cpp).
unsigned Application::Win16CatalogTarget(unsigned handle,unsigned ordinal){
 for(auto&entry:native_win16_modules)if(entry.handle&&entry.handle==(handle&65535))return entry.segment+0x100+ordinal*16;
 return 0;
}
// Argument words one catalogue routine takes off the stack, counted from the
// generated 32-bit stub that pushes them, and ~0u for an ordinal this port does
// not carry. SERIALUI ordinal 7 is its private callback, which has no
// export and is reached through the token offset 0x2ae only. VER.DLL:
// GetFileVersionInfoSize 4, GetFileVersionInfo 8, VerFindFile 15,
// VerInstallFile 15, VerLanguageName 4, VerQueryValue 8.
unsigned Application::CatalogArgumentWords(unsigned slot,unsigned ordinal){
 static const unsigned serialui[]={0,1,6,5,6,6,3,4},version[]={4,8,15,15,4,8};
 switch(slot){
 case 0: return ordinal>=1&&ordinal<=7?serialui[ordinal]:~0u;
 case 1: return ordinal>=6&&ordinal<=11?version[ordinal-6]:~0u;
 case 2: return DirectDraw16Words(ordinal);
 case 3: return DciMan16Words(ordinal);
 case 4: return DiskApi16Words(ordinal);
 case 5: return DMusic16Words(ordinal);
 case 6: return ordinal==18?6:~0u;
 case 8: return Tsp3216Words(ordinal);
 case 9: return Inet16Words(ordinal);
 default: return ~0u;
 }
}
bool Application::NativeWin16Dispatch(unsigned token,unsigned stack,unsigned available,unsigned&result,unsigned&words){
 NativeWin16Module*m=nullptr;unsigned ordinal=0,slot=0;
 for(auto&entry:native_win16_modules)if(entry.handle&&(token>>16)==(entry.segment>>16)){
  slot=unsigned(&entry-native_win16_modules);
  unsigned offset=token&65535;ordinal=offset>=0x110&&!(offset&15)?(offset-0x100)/16:0;
  if(!slot&&offset==0x2ae)ordinal=7;
  const unsigned count=CatalogArgumentWords(slot,ordinal);
  if(count!=~0u){m=&entry;words=count;}
  break;
 }
 if(!m)return false;
 if(available<words*2||!virtual_memory.Access(stack,words*2,false)){Fail("Native Win16 export argument block is truncated");return false;}
 ++m->references;unsigned handle=m->handle;
 // No Win16 mutex since PLAN.md step 8. The implemented 16-bit routines are
 // reentrant native code; only the transition counter is kept.
 if(state==Failed||state==Exited){Win16ModuleApi(Win98Win16Module::FreeLibrary16,&handle);return false;}
 const unsigned previous_count=ThunkLockCount();ThunkLockCount(previous_count+1);
 auto pointer=[&](unsigned offset){return selectors.Linear(Read32(stack+offset));};
 auto word=[&](unsigned offset){unsigned short v=0;Read(stack+offset,&v,2);return unsigned(v);};
 if(slot==1)result=VersionInfo16(ordinal,stack);
 else if(slot==2)result=DirectDraw16(ordinal,stack);
 else if(slot==3)result=DciMan16(ordinal,stack);
 else if(slot==4)result=DiskApi16(ordinal,stack);
 else if(slot==5)result=DMusic16(ordinal,stack);
 else if(slot==8)result=Tsp3216(ordinal,stack);
 else if(slot==9)result=Inet16(ordinal,stack);
 else if(slot==6){
  // PIFPROPGETPAGES is called through SHELL.DLL, flat thunk index
  // 10, which src/x86_flat_thunk.cpp answers before any 16-bit call; a
  // direct far call to the identity has no caller in the tree.
  if(log)log("PIFMGR ordinal 18 reached outside the SHELL thunk");
  result=0;
 }
 else if(ordinal==1)result=1; // SERIALUI WEP returns 1.
 else if(ordinal==2){unsigned args[]={pointer(8),Read32(stack+4),Read32(stack)};result=CommApi("SerialUiEnum16",args);}
 else if(ordinal==7){
  // SERIALUI, its private property-page collector callback.
  struct Memory{
   Application&app;
   unsigned Read16(unsigned p){unsigned short v=0;app.Read(p,&v,2);return v;}
   unsigned Read32(unsigned p){return app.Read32(p);}
   void Write16(unsigned p,unsigned value){unsigned short v=value;app.Store(p,&v,2);}
   void Write32(unsigned p,unsigned v){app.Write32(p,v);}
   unsigned Linear(unsigned p){return app.selectors.Linear(p);}
  }memory{*this};
  result=Win98SerialUi::CollectPage(memory,pointer(0),Read32(stack+4));
 }
 else if(ordinal==3){unsigned args[]={pointer(6),word(4),pointer(0)};result=CommApi("SerialUiDialog16",args);}
 else if(ordinal==4){unsigned args[]={pointer(8),pointer(4),Read32(stack)};result=CommApi("SerialUiSet16",args);}
 else if(ordinal==5){unsigned args[]={pointer(8),pointer(4),pointer(0)};result=CommApi("SerialUiGet16",args);}
 else{unsigned args[]={pointer(0),word(4)};result=CommApi("SerialUiFifo16",args);}
 ThunkLockCount(previous_count);
 Win16ModuleApi(Win98Win16Module::FreeLibrary16,&handle);return state!=Failed;
}
} // namespace X86
