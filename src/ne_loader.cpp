// SPDX-License-Identifier: GPL-2.0-or-later
// The Win16 loader of PLAN.md step 9, first part: it reads an NE image,
// places its segments behind real descriptors of a real LDT, relocates them,
// builds the task database and the automatic data segment, and runs the
// 16-bit startup code until it asks USER for something.
// The 16-bit side is a second entrance to the same native services, not an
// operating system underneath. KERNEL, USER and GDI are our native code, so
// every import of a 16-bit module that names one of those resolves to a gate
// in the gate region, and the gate arrives here with the Pascal frame of the
// caller. Only programs and the 16-bit DLLs a program ships with run as
// original 16-bit code.
// The address space is the flat one of the process. A descriptor is a window
// on it: the base is the flat address the block already has, so a 16-bit
// pointer and a 32-bit pointer name the same byte and nothing has to be
// copied between the two sides.
// The KRNL386 contracts are in win98_ne_task.h.
#include "dll_routes.h"
#include "jit_memory.h"
#include "win98_local_heap.h"
#include "win98_ne_image.h"
#include "win98_ne_task.h"
#include "win16_machine.h"
#include "win98_win16_user.h"
#include "x86_app.h"
#include "system_vm.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
namespace Win16U = Win98Win16User;
namespace
{
// The LDT lives beside the GDT of the process, in the descriptor page the
// process already reserves. Entry four of the GDT points at it.
constexpr unsigned LdtBase = Application::Win16LdtBase, LdtBytes = Application::Win16LdtBytes, LdtSelector = 0x20;
constexpr unsigned GdtBase = Application::SystemPageBase;
// The page MakeProcInstance builds its eight-byte thunks in. It is its own
// page of the scratch region the level change already uses, because the thunks
// are code and the flat arena is not executable.
constexpr unsigned ProcedureThunkBase = 0x0fe01000, ProcedureThunkBytes = 4096;
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
        unsigned x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'a' && x <= 'z') x -= 32;
        if (y >= 'a' && y <= 'z') y -= 32;
        if (x != y) return false;
    }
    return true;
}
void CopyText(char *out, const char *in, unsigned capacity)
{
    unsigned n = 0;
    if (in)
        while (n + 1 < capacity && in[n])
        {
            out[n] = in[n];
            ++n;
        }
    out[n] = 0;
}
// The module name a 16-bit import names, without its extension, so that the
// module reference "KERNEL" and the file KRNL386.EXE meet.
void BareName(const char *input, char (&out)[16])
{
    const char *leaf = Leaf(input);
    unsigned n = 0;
    for (; leaf[n] && leaf[n] != '.' && n + 1 < sizeof out; ++n)
        out[n] = leaf[n] >= 'a' && leaf[n] <= 'z' ? leaf[n] - 32 : leaf[n];
    out[n] = 0;
}
} // namespace

Application::Win16Owner::~Win16Owner()
{
    if (!state)
        return;
    for (const auto &table : state->accelerators)
        if (table.native) Application::DestroyCreatedAcceleratorTable(table.native);
    delete[] state->globals;
    state->globals = nullptr;
    state->global_capacity = 0;
    for (unsigned i = 0; i < state->module_count; ++i)
    {
        delete[] state->modules[i].file;
        delete[] state->modules[i].selector;
        delete[] state->modules[i].memory;
    }
    for (auto *file : state->system_files)
        delete[] file;
    delete state;
    state = nullptr;
}

// ---------------------------------------------------------------- descriptors

// Writes one descriptor of the LDT. The table entry already carries base,
// limit and type, so this only copies the eight bytes into guest memory.
bool Application::Win16Descriptor(unsigned selector)
{
    unsigned char bytes[8] = {};
    if (!selectors.Descriptor(selector, bytes))
        return false;
    const unsigned index = selector >> 3;
    if (index * 8 + 8 > LdtBytes)
        return false;
    return Store(LdtBase + index * 8, bytes, sizeof bytes);
}
// A descriptor for a block of the process address space. `limit` is the last
// addressable byte, as the descriptor counts it.
unsigned Application::Win16Segment(unsigned base, unsigned limit, bool code, bool big)
{
    const unsigned selector = selectors.CreateSegment(base, limit, code, big);
    if (!selector || !Win16Descriptor(selector))
        return 0;
    return selector;
}
unsigned Application::Win16Linear(unsigned selector, unsigned offset) const
{
    const auto *entry = selectors.At(selector);
    return entry ? entry->base + (offset & 65535) : 0;
}
unsigned Application::Win16Limit(unsigned selector) const
{
    const auto *entry = selectors.At(selector);
    return entry ? entry->limit : 0;
}
void Application::Win16DescribeAddress(char *out, unsigned capacity, unsigned selector, unsigned offset)
{
    if (!out || !capacity)
        return;
    out[0] = 0;
    if (!win16.state)
        return;
    const char *module_name = nullptr;
    unsigned segment_number = 0;
    for (unsigned i = 0; i < win16.state->module_count && !module_name; ++i)
    {
        const auto &module = win16.state->modules[i];
        for (unsigned segment = 1; segment <= module.segments; ++segment)
            if (module.selector[segment] == selector)
            {
                module_name = module.name[0] ? module.name : Leaf(module.path);
                segment_number = segment;
                break;
            }
    }
    const char *gate_module = nullptr, *gate_name = nullptr;
    unsigned gate_ordinal = 0;
    for (unsigned window = 0; window < 16 && !gate_module; ++window)
        if (win16.state->gate_selector[window] == selector)
        {
            const unsigned gate = GateBase + (window << 16) + (offset & 0xffff);
            if (gate >= GateBase && gate < GateBase + import_count * 16 && !((gate - GateBase) & 15))
            {
                const auto &item = imports[(gate - GateBase) / 16];
                gate_module = item.dll;
                gate_name = item.name;
                gate_ordinal = item.id;
            }
        }
    unsigned char bytes[12] = {};
    unsigned byte_count = 0;
    const auto *entry = selectors.At(selector);
    if (entry && offset <= entry->limit)
    {
        byte_count = entry->limit - offset + 1;
        if (byte_count > sizeof bytes)
            byte_count = sizeof bytes;
        // This runs while Check is describing a CPU failure. A diagnostic
        // read must neither replace that failure nor re-enter Check.
        if (cpu->Read(entry->base + offset, bytes, byte_count) != CpuError::Ok)
            byte_count = 0;
    }
    int used = snprintf(out, capacity, ", at %04x:%04x", selector & 0xffff, offset & 0xffff);
    if (used < 0 || (unsigned)used >= capacity)
        return;
    if (module_name)
        used += snprintf(out + used, capacity - used, " in %s segment %u", module_name, segment_number);
    else if (gate_module)
        used += snprintf(out + used, capacity - used, " at gate %s.%u %s", gate_module, gate_ordinal,
                         gate_name ? gate_name : "");
    else
        used += snprintf(out + used, capacity - used, " in unowned selector");
    if (used < 0 || (unsigned)used >= capacity || !byte_count)
        return;
    used += snprintf(out + used, capacity - used, " bytes");
    for (unsigned i = 0; i < byte_count && used >= 0 && (unsigned)used < capacity; ++i)
        used += snprintf(out + used, capacity - used, " %02x", bytes[i]);
}
unsigned Application::Win16Word(unsigned linear)
{
    unsigned short value = 0;
    Read(linear, &value, 2);
    return value;
}
void Application::Win16Word(unsigned linear, unsigned value)
{
    unsigned short word = (unsigned short)value;
    Store(linear, &word, 2);
}

// ---------------------------------------------------------------------- gates

// The flat address a 16-bit import resolves to. Every gate is an import
// record of the gate region, so the existing code hook catches the call and
// Dispatch finds the record again. The record keeps the module name, the
// export name and the ordinal.
// The ordinal behind a name of one of our modules. A relocation of the linker
// names an ordinal, but GetProcAddress names a string, and a module of ours is
// not a file whose name table could be walked. The names here are the ones the
// resident and non-resident name tables of the matching file of the tree
// carry, with the ordinal beside them; only entries this port serves are
// listed, because a name that resolves to an ordinal with nothing behind it
// would only move the stop.
unsigned Application::Win16SystemOrdinal(const char *module, const char *name)
{
    // Every ordinal the Win16*Call routines serve, with its name from the
    // resident or non-resident name table of the original module
    // (MMSYSTEM.DLL, KEYBOARD.DRV, COMMCTRL.DLL, COMMDLG.DLL, SHELL.DLL,
    // WIN87EM.DLL, VER.DLL, TOOLHELP.DLL of the tree; ).
    struct Entry { const char *module, *name; unsigned ordinal; };
    static const Entry table[] = {
        {"DCIMAN", "DCIOpenProvider", 1},       {"DCIMAN", "DCICloseProvider", 2},
        {"DCIMAN", "DCICreatePrimary", 3},      {"DCIMAN", "DCICreateOffscreen", 4},
        {"DCIMAN", "DCICreateOverlay", 5},      {"DCIMAN", "DCIEnum", 6},
        {"MMSYSTEM", "sndPlaySound", 2},        {"MMSYSTEM", "joyGetNumDevs", 101},
        {"MMSYSTEM", "midiOutGetNumDevs", 201}, {"MMSYSTEM", "midiInGetNumDevs", 301},
        {"MMSYSTEM", "auxGetNumDevs", 350},     {"MMSYSTEM", "waveOutGetNumDevs", 401},
        {"MMSYSTEM", "waveOutGetDevCaps", 402}, {"MMSYSTEM", "waveOutOpen", 404},
        {"MMSYSTEM", "waveOutClose", 405},      {"MMSYSTEM", "waveOutPrepareHeader", 406},
        {"MMSYSTEM", "waveOutUnprepareHeader", 407}, {"MMSYSTEM", "waveOutWrite", 408},
        {"MMSYSTEM", "waveOutReset", 411},      {"MMSYSTEM", "waveOutGetPosition", 412},
        {"MMSYSTEM", "timeGetTime", 607},
        {"MMSYSTEM", "waveInGetNumDevs", 501},  {"MMSYSTEM", "mciSendCommand", 701},
        {"MMSYSTEM", "mciSendString", 702},     {"MMSYSTEM", "mciGetErrorString", 706},
        {"COMMDLG", "GetOpenFileName", 1},      {"COMMDLG", "GetSaveFileName", 2},
        {"COMMDLG", "ChooseColor", 5},          {"COMMDLG", "ChooseFont", 15},
        {"COMMDLG", "PrintDlg", 20},            {"COMMDLG", "CommDlgExtendedError", 26},
        {"COMMDLG", "GetFileTitle", 27},        {"COMMCTRL", "InitCommonControls", 17},
        {"COMMCTRL", "CreateToolbarEx", 20},    {"KEYBOARD", "AnsiToOem", 5},
        {"KEYBOARD", "OemToAnsi", 6},           {"KEYBOARD", "MapVirtualKey", 131},
        {"KEYBOARD", "GetKeyNameText", 133},    {"KEYBOARD", "AnsiToOemBuff", 134},
        {"KEYBOARD", "OemToAnsiBuff", 135},     {"COMMCTRL", "GetEffectiveClientRect", 4},
        {"COMMCTRL", "CreateStatusWindow", 6},    {"SHELL", "DragAcceptFiles", 9},
        {"SHELL", "ShellExecute", 20},          {"SHELL", "ExtractIcon", 34},  {"SHELL", "ExtractIconExA", 40},
        {"SHELL", "RegisterShellHook", 102},    {"SHELL", "DriveType", 262},
        {"VER", "GetFileResourceSize", 2},    {"VER", "GetFileResource", 3},
        {"VER", "GetFileVersionInfoSize", 6}, {"VER", "GetFileVersionInfo", 7},
        {"VER", "VerFindFile", 8},              {"VER", "VerInstallFile", 9},
        {"VER", "VerLanguageName", 10},         {"VER", "VerQueryValue", 11},
        {"VER", "GetFileVersionInfoRaw", 20},
        {"TOOLHELP", "NotifyRegister", 73},     {"TOOLHELP", "NotifyUnregister", 74},
        {"TOOLHELP", "InterruptRegister", 75},  {"TOOLHELP", "InterruptUnregister", 76},
        {"WIN87EM", "__FPMATH", 1},             {"KERNEL", "GetVersionEx", 149},
        {"USER", "BroadcastSystemMessage", 604},
        {"COMMCTRL", "PropertySheet", 159},     {"COMMCTRL", "CreatePropertySheetPage", 160},
        {"COMMCTRL", "DestroyPropertySheetPage", 161},
        {"COMMCTRL", "DPA_Create", 328},        {"COMMCTRL", "DPA_Destroy", 329},
        {"COMMCTRL", "DPA_Grow", 330},          {"COMMCTRL", "DPA_Clone", 331},
        {"COMMCTRL", "DPA_GetPtr", 332},        {"COMMCTRL", "DPA_GetPtrIndex", 333},
        {"COMMCTRL", "DPA_InsertPtr", 334},     {"COMMCTRL", "DPA_SetPtr", 335},
        {"COMMCTRL", "DPA_DeletePtr", 336},     {"COMMCTRL", "DPA_DeleteAllPtrs", 337},
        {"COMMCTRL", "DPA_Sort", 338},          {"COMMCTRL", "DPA_Search", 339},
    };
    // Named KRNL386 entry points served by these gates. Exclude __AHSHIFT,
    // __AHINCR and __WINFLAGS because they are constants, not callable
    // exports.
    struct Named { const char *name; unsigned ordinal; };
    static const Named kernel[] = {
        {"FATALEXIT", 1}, {"GETVERSION", 3}, {"LOCALINIT", 4}, {"LOCALALLOC", 5},
        {"LOCALREALLOC", 6}, {"LOCALFREE", 7}, {"LOCALLOCK", 8}, {"LOCALUNLOCK", 9},
        {"LOCALSIZE", 10}, {"LOCALHANDLE", 11}, {"LOCALFLAGS", 12}, {"LOCALCOMPACT", 13},
        {"GLOBALALLOC", 15}, {"GLOBALREALLOC", 16}, {"GLOBALFREE", 17}, {"GLOBALLOCK", 18},
        {"GLOBALUNLOCK", 19}, {"GLOBALSIZE", 20}, {"GLOBALHANDLE", 21}, {"GLOBALFLAGS", 22},
        {"GLOBALHANDLENORIP", 159},
        {"LOCKSEGMENT", 23}, {"UNLOCKSEGMENT", 24}, {"GLOBALCOMPACT", 25}, {"YIELD", 29},
        {"WAITEVENT", 30}, {"GETCURRENTTASK", 36}, {"GETCURRENTPDB", 37}, {"GETMODULEHANDLE", 47},
        {"GETMODULEUSAGE", 48}, {"GETMODULEFILENAME", 49}, {"GETPROCADDRESS", 50},
        {"MAKEPROCINSTANCE", 51}, {"FREEPROCINSTANCE", 52}, {"GETINSTANCEDATA", 54}, {"CATCH", 55},
        {"THROW", 56}, {"GETPROFILEINT", 57}, {"GETPROFILESTRING", 58}, {"WRITEPROFILESTRING", 59},
        {"FINDRESOURCE", 60}, {"LOADRESOURCE", 61}, {"LOCKRESOURCE", 62}, {"FREERESOURCE", 63},
        {"ACCESSRESOURCE", 64}, {"SIZEOFRESOURCE", 65}, {"INITATOMTABLE", 68}, {"FINDATOM", 69},
        {"ADDATOM", 70}, {"DELETEATOM", 71}, {"GETATOMNAME", 72}, {"GETATOMHANDLE", 73},
        {"OPENFILE", 74}, {"_LCLOSE", 81}, {"_LREAD", 82}, {"_LCREAT", 83}, {"_LLSEEK", 84},
        {"_LOPEN", 85}, {"_LWRITE", 86}, {"LSTRCPY", 88}, {"LSTRCAT", 89}, {"LSTRLEN", 90},
        {"INITTASK", 91}, {"LOADLIBRARY", 95}, {"FREELIBRARY", 96}, {"DOS3CALL", 102},
        {"SETERRORMODE", 107}, {"OUTPUTDEBUGSTRING", 115}, {"LOCALSHRINK", 121},
        {"GETPRIVATEPROFILEINT", 127}, {"GETPRIVATEPROFILESTRING", 128},
        {"WRITEPRIVATEPROFILESTRING", 129}, {"FILECDR", 130}, {"GETDOSENVIRONMENT", 131},
        {"GETWINFLAGS", 132}, {"GETWINDOWSDIRECTORY", 134}, {"GETSYSTEMDIRECTORY", 135},
        {"GETDRIVETYPE", 136}, {"FATALAPPEXIT", 137}, {"SETLASTERROR", 147}, {"GETLASTERROR", 148},
        {"GETNUMTASKS", 152}, {"WINEXEC", 166}, {"GETFREESPACE", 169}, {"ALLOCDSTOCSALIAS", 171},
        {"ALLOCSELECTOR", 175}, {"FREESELECTOR", 176}, {"GETSELECTORBASE", 186},
        {"SETSELECTORBASE", 187}, {"GETSELECTORLIMIT", 188}, {"GLOBALFIX", 197},
        {"GLOBALUNFIX", 198}, {"SETHANDLECOUNT", 199}, {"REGENUMKEY", 216}, {"REGOPENKEY", 217},
        {"REGCREATEKEY", 218}, {"REGDELETEKEY", 219}, {"REGCLOSEKEY", 220}, {"REGSETVALUE", 221},
        {"REGDELETEVALUE", 222}, {"REGQUERYVALUE", 224}, {"REGQUERYVALUEEX", 225},
        {"REGSETVALUEEX", 226}, {"REGFLUSHKEY", 227}, {"HMEMCPY", 348}, {"_HREAD", 349},
        {"_HWRITE", 350}, {"LSTRCPYN", 353}, {"LOADLIBRARYEX32W", 513}, {"FREELIBRARY32W", 514},
        {"GETPROCADDRESS32W", 515}, {"CALLPROC32W", 517}, {"GETCURRENTDIRECTORY", 411},
        {"SETCURRENTDIRECTORY", 412}, {"ISPEFORMAT", 431}, {"ISDBCSLEADBYTE", 207}
    };
    if (!name || !*name)
        return 0;
    for (const auto &entry : table)
        if (Same(entry.module, module) && Same(entry.name, name))
            return entry.ordinal;
    if (Same(module, "KERNEL"))
        for (const auto &entry : kernel)
            if (Same(entry.name, name))
                return entry.ordinal;
    // USER and GDI tables provide export names and argument-word counts.
    // LoadLibrary/GetProcAddress can therefore resolve names used by Visual
    // Basic Declare statements and other callers. Compare names
    // case-insensitively because these native modules have no separate
    // on-disk name table.
    Win16U::U count = 0;
    const Win16U::Ordinal *entries = nullptr;
    if (Same(module, "USER"))
        entries = Win16U::UserOrdinals(count);
    else if (Same(module, "GDI"))
        entries = Win16U::GdiOrdinals(count);
    for (Win16U::U i = 0; entries && i < count; ++i)
        if (Same(entries[i].name, name))
            return entries[i].ordinal;
    return 0;
}
unsigned Application::Win16Gate(const char *module, const char *name, unsigned ordinal)
{
    auto &machine = *win16.state;
    if (!ordinal)
        ordinal = Win16SystemOrdinal(module, name);
    for (unsigned i = 0; i < machine.gate_count; ++i)
    {
        const auto &item = imports[(machine.gates[i] - GateBase) / 16];
        if (item.id == ordinal && Same(item.dll, module))
            return machine.gates[i];
    }
    if (machine.gate_count >= 512)
        return 0;
    char label[128];
    if (name && *name)
        snprintf(label, sizeof label, "%s", name);
    else
        snprintf(label, sizeof label, "#%u", ordinal);
    const unsigned gate = NewImport(module, label, FlatThunkGate);
    if (!gate)
        return 0;
    imports[(gate - GateBase) / 16].id = ordinal;
    machine.gates[machine.gate_count++] = gate;
    return gate;
}
bool Application::Win16OwnsGate(unsigned gate) const
{
    if (!win16.state)
        return false;
    for (unsigned i = 0; i < win16.state->gate_count; ++i)
        if (win16.state->gates[i] == gate)
            return true;
    return false;
}
// The 16-bit far pointer of a gate. The gate region is one megabyte, so it
// needs one 64-KiB code descriptor per window; a window is made when a gate
// in it is first asked for.
unsigned Application::Win16GatePointer(unsigned gate)
{
    auto &machine = *win16.state;
    const unsigned offset = gate - GateBase, window = offset >> 16;
    if (window >= 16)
        return 0;
    if (!machine.gate_selector[window])
    {
        machine.gate_selector[window] = Win16Segment(GateBase + (window << 16), 65535, true, false);
        if (!machine.gate_selector[window])
            return 0;
    }
    return machine.gate_selector[window] << 16 | (offset & 65535);
}

// ------------------------------------------------------------- module loading

Application::Win16Module *Application::Win16Find(const char *name)
{
    if (!win16.state)
        return nullptr;
    char bare[16];
    BareName(name, bare);
    for (unsigned i = 0; i < win16.state->module_count; ++i)
        if (Same(win16.state->modules[i].name, bare))
            return &win16.state->modules[i];
    return nullptr;
}
// The module whose segment table holds a code selector, with the segment's
// number, for the error box (src/x86_fault_box.inc): KRNL386 names the place
// of a fault by the file of the module and the logical segment.
bool Application::Win16CodeModule(unsigned selector, char *leaf, unsigned capacity, unsigned &segment)
{
    if (!win16.state || !capacity)
        return false;
    selector &= 0xfffc;
    for (unsigned i = 0; i < win16.state->module_count; ++i)
    {
        const auto &module = win16.state->modules[i];
        for (unsigned n = 1; module.selector && n <= module.segments; ++n)
        {
            if (!module.selector[n] || (module.selector[n] & 0xfffc) != selector)
                continue;
            const char *name = module.path;
            for (const char *p = module.path; *p; ++p)
                if (*p == '\\' || *p == '/' || *p == ':')
                    name = p + 1;
            CopyText(leaf, *name ? name : module.name, capacity);
            segment = n;
            return true;
        }
    }
    return false;
}
// Reads one file of the mounted tree. The caller owns the bytes.
unsigned Application::Win16ReadFile(const char *path, unsigned char *&bytes, unsigned &length)
{
    unsigned handle = 0, status = 0;
    unsigned result = files.Open(path, 1, 1, 3, 0, handle, status);
    if (result)
        return result == 3 ? Win98Ne::FileNotFound : result;
    unsigned size = 0, done = 0;
    result = files.Size(handle, size);
    if (!result && !size)
        result = Win98Ne::BadFormat;
    unsigned char *data = !result ? new unsigned char[size] : nullptr;
    if (!result && !data)
        result = 8;
    if (!result)
        result = files.Read(handle, data, size, done);
    files.Close(handle);
    if (result || done != size)
    {
        delete[] data;
        return result ? result : Win98Ne::BadFormat;
    }
    bytes = data;
    length = size;
    return Win98Ne::Ok;
}
// Loads an NE image that is already in memory. `library` says which of the
// two error codes a mismatch of the image kind produces: KRNL386 answers 20
// when a program is asked to be a library and the other way round.
Application::Win16Module *Application::Win16Place(const char *path, unsigned char *bytes, unsigned length,
                                                  bool library, unsigned &error)
{
    auto &machine = *win16.state;
    // Win16FreeLibrary closes the gap a freed library leaves, so the next
    // module goes behind the last one.
    const unsigned slot = machine.module_count;
    if (slot >= Win16Machine::ModuleCapacity)
    {
        error = 8;
        delete[] bytes;
        return nullptr;
    }
    auto &module = machine.modules[slot];
    if (!module.image.Parse(bytes, length, error))
    {
        delete[] bytes;
        return nullptr;
    }
    if (module.image.IsLibrary() != library)
    {
        error = Win98Ne::BadLibrary;
        delete[] bytes;
        return nullptr;
    }
    // A self-loading module carries its own loader in segment one and expects
    // the kernel to call it for every segment. That is a second loader with a
    // different contract; this one refuses the module instead of pretending.
    if (module.image.SelfLoading())
    {
        error = Win98Ne::BadFormat;
        delete[] bytes;
        return nullptr;
    }
    module.file = bytes;
    module.library = library;
    snprintf(module.path, sizeof module.path, "%s", path);
    BareName(path, module.name);
    char own[16];
    if (module.image.ModuleOwnName(own, sizeof own))
        memcpy(module.name, own, sizeof own);
    module.references = 1;
    if (slot == machine.module_count)
        ++machine.module_count;
    if (!Win16ModuleDatabase(module) || !Win16LoadSegments(module, error))
    {
        error = error ? error : Win98Ne::BadFormat;
        // The slot stays taken (libraries it imported may lie behind it), but
        // it no longer answers to its name or handle: a later LoadLibrary of
        // the same file has to fail the same way instead of finding half a
        // module.
        module.name[0] = 0;
        module.path[0] = 0;
        module.handle = 0;
        module.library = false;
        return nullptr;
    }
    Win16PatchEntries(module);
    return &module;
}
// FreeLibrary, KRNL386. GetExePtr turns the handle or the
// instance into the module and answers nothing for a stranger, and then the
// body does nothing either. counts the usage down; at zero DelModule
// calls the exit procedure with WEP_FREE_DLL
// and gives the segments, the module database and its resources back. The
// program's own module is not a library and stays.
// The same body serves a Win16 task and a 32-bit process that hosts 16-bit
// libraries (printer drivers, MCI drivers; src/x86_win16_hosted.cpp); only
// the call of the exit procedure differs, because a hosted process has no
// task stack and enters 16-bit code through Win16HostedCall. After the exit
// procedure USER forgets the classes of the module, as SignalProc32 does for
// USIG_DLL_UNLOAD_WIN16 (USER, the class walk ). A later
// LoadLibrary places the file anew with fresh data segments, which is what a
// driver such as MCIAVI expects when OpenDriver loads it a second time.
// The libraries this one imported keep their references; KRNL386 lowers
// those as well, and this port does not follow that chain yet.
bool Application::Win16FreeLibrary(unsigned word)
{
    if (!win16.state)
        return true;
    auto &machine = *win16.state;
    unsigned index = machine.module_count;
    for (unsigned i = 0; i < machine.module_count; ++i)
        if (word && (machine.modules[i].handle == word || machine.modules[i].instance == word))
        {
            index = i;
            break;
        }
    if (index == machine.module_count || !machine.modules[index].library)
        return true;
    auto &module = machine.modules[index];
    if (module.references > 1)
    {
        --module.references;
        return true;
    }
    if (machine.hosted)
    {
        Win16HostedExit(module);
        if (state == Failed)
            return false;
    }
    else if (!Win16CallExit(module))
        return false;
    // DelModule reports the module (ptrace 65h, NFY_DELMODULE, TOOLHELP) and each segment it gives back (ptrace 52h, NFY_FREESEG).
    Win16ToolhelpNotify(6, module.handle);
    for (unsigned number = 1; number <= module.segments && state != Failed; ++number)
        if (module.selector[number])
            Win16ToolhelpNotify(2, module.selector[number]);
    if (state == Failed)
        return false;
    // WNDCLASS carries the instance; the handle covers a caller that gave
    // the module database instead.
    User::DestroyModuleClasses(module.instance);
    User::DestroyModuleClasses(module.handle);
    if (log)
    {
        char text[320];
        snprintf(text, sizeof text, "WIN16 freed library %s handle=%04x", module.path, module.handle);
        log(text);
    }
    for (auto &table : machine.accelerators)
        if (table.native && table.module == module.handle)
        {
            DestroyCreatedAcceleratorTable(table.native);
            table = Win16Machine::Accelerator{};
        }
    for (unsigned i = 0; i < machine.resource_count; ++i)
    {
        auto &entry = machine.resources[i];
        if (entry.module == index)
        {
            if (entry.memory)
                Free(entry.memory);
            if (entry.selector)
                selectors.Unmap(entry.selector << 16);
            entry = Win16Machine::Resource{};
            entry.module = ~0u;
        }
        else if (entry.module != ~0u && entry.module > index)
            --entry.module;
    }
    for (unsigned number = 1; number <= module.segments; ++number)
        if (module.selector[number])
        {
            // The engine has translated the code of the segment; the next
            // module the arena puts at this address must not run it.
            if (module.memory[number])
                cpu->InvalidateCode(module.memory[number], Win16Limit(module.selector[number]) + 1);
            // The segment is a block of the global table (Win16RegisterSegment),
            // whose release also gives the memory back.
            if (!Win16GlobalFree(module.selector[number]) && module.memory[number])
                Free(module.memory[number]);
            selectors.Unmap(module.selector[number] << 16);
        }
    if (module.handle)
        selectors.Unmap(module.handle << 16);
    if (module.database)
        Free(module.database);
    delete[] module.file;
    delete[] module.selector;
    delete[] module.memory;
    for (unsigned i = index; i + 1 < machine.module_count; ++i)
        machine.modules[i] = machine.modules[i + 1];
    machine.modules[--machine.module_count] = Win16Module{};
    return true;
}
// The module database a 16-bit program sees through GetModuleHandle: a block
// whose first word is "NE", because GetExePtr tests exactly that.
// The NE header and the tables behind it are the bytes of the file, so a
// program that walks its own segment or resource table finds the real thing.
bool Application::Win16ModuleDatabase(Win16Module &module)
{
    const unsigned bytes = module.image.Bytes() - module.image.Base() > 65536
                               ? 65536u
                               : module.image.Bytes() - module.image.Base();
    const unsigned block = Allocate(bytes);
    if (!block)
        return false;
    if (!Store(block, module.image.Data() + module.image.Base(), bytes))
        return false;
    module.database = block;
    module.handle = Win16Segment(block, bytes - 1, false, false);
    return module.handle != 0;
}
// Patch exported Win16 library entry prologues to select the library's data
// segment. Support both linker prologue forms. Without this, libraries access
// the caller's data, corrupting table lookups and heap operations.
void Application::Win16PatchEntries(Win16Module &module)
{
    if (!(module.image.ProgramFlags() & Win98Ne::SingleData) || !module.instance)
        return;
    const unsigned count = module.image.EntryCount();
    for (unsigned ordinal = 1; ordinal <= count; ++ordinal)
    {
        Win98Ne::Entry entry;
        if (!module.image.EntryAt(ordinal, entry))
            continue;
        // Bit 0 of the entry flags is "exported"; only an exported entry
        // carries the prologue.
        if (!(entry.flags & 1) || !entry.segment || entry.segment > module.segments)
            continue;
        const unsigned memory = module.memory[entry.segment];
        if (!memory)
            continue;
        unsigned char code[3] = {};
        if (!Read(memory + entry.offset, code, sizeof code))
            continue;
        const bool move = code[0] == 0x8c && code[1] == 0xd8 && code[2] == 0x90;
        const bool push = code[0] == 0x1e && code[1] == 0x58 && code[2] == 0x90;
        if (!move && !push)
            continue; // not the prologue the linker writes
        const unsigned char patch[3] = {0xb8, (unsigned char)(module.instance & 0xff),
                                        (unsigned char)(module.instance >> 8)};
        Store(memory + entry.offset, patch, sizeof patch);
    }
}
bool Application::Win16LoadSegments(Win16Module &module, unsigned &error)
{
    const unsigned count = module.image.Segments();
    // The segment number of a relocation and of an entry is a word, so a
    // module has at most 65.535 segments. The tables are made for the count
    // this module has; there is no ceiling of the port above it.
    if (count > 65535)
    {
        error = Win98Ne::BadFormat;
        return false;
    }
    module.selector = new unsigned[count + 1]();
    module.memory = new unsigned[count + 1]();
    module.segments = count;
    const unsigned automatic = module.image.AutoDataSegment();
    for (unsigned number = 1; number <= count; ++number)
    {
        Win98Ne::Segment segment;
        if (!module.image.SegmentAt(number, segment))
        {
            error = Win98Ne::BadFormat;
            return false;
        }
        // A segment with flag 2000h is 32-bit code or data inside a 16-bit
        // module: KRNL386 gives its descriptor the default operand size of 32
        // bits (the D bit), and nothing else changes. MSVIDEO.DLL has one,
        // segment 7 with the 32-bit loops of DrawDib and the codecs.
        unsigned bytes = segment.allocation;
        // The automatic data segment carries the local heap and the stack of
        // the instance behind the linker's data, as KRNL386 lays it out and
        // as LocalInit then finds it: the heap ends at the last
        // byte of the segment.
        unsigned placed = bytes;
        if (number == automatic)
        {
            bytes += module.image.StackSize() + module.image.HeapSize();
            if (bytes > 65536)
                bytes = 65536;
            module.instance_bytes = bytes;
            // KRNL386 grows this segment with GlobalReAlloc when the local
            // heap runs out (the default notification ). The whole
            // 64 KiB is placed here instead, so the heap grows inside a block
            // that never moves and no 16-bit pointer changes.
            placed = 65536;
        }
        else
            placed = bytes;
        const unsigned memory = Allocate(placed);
        if (!memory)
        {
            error = 8;
            return false;
        }
        // Everything beyond the file bytes is the uninitialized part and has
        // to read as zero.
        unsigned char zero[1024] = {};
        for (unsigned at = segment.file_bytes; at < placed; at += sizeof zero)
            Store(memory + at, zero, placed - at < sizeof zero ? placed - at : sizeof zero);
        if (segment.file_bytes &&
            !Store(memory, module.image.Data() + segment.file_offset, segment.file_bytes))
        {
            error = Win98Ne::BadFormat;
            return false;
        }
        module.memory[number] = memory;
        // A 16-bit code segment lies in the same arena as the data segments,
        // so the pages it covers need the execute right straight away.
        if (segment.Code()) MarkGuestCode(memory, placed);
        module.selector[number] = Win16Segment(memory, placed - 1, segment.Code(), segment.Use32());
        if (!module.selector[number])
        {
            error = 8;
            return false;
        }
        Win16RegisterSegment(module.selector[number], memory, placed);
        if (number == automatic)
            module.instance = module.selector[number];
    }
    for (unsigned number = 1; number <= count; ++number)
        if (!Win16Relocate(module, number, error))
        {
            if (log)
            {
                char text[200];
                snprintf(text, sizeof text, "WIN16 relocation of %s segment %u failed, error %u", module.name,
                         number, error);
                log(text);
            }
            return false;
        }
    // KRNL386 reports each loaded segment to the debugger hook (ptrace 50h),
    // which TOOLHELP turns into NFY_LOADSEG.
    for (unsigned number = 1; number <= count; ++number)
        if (module.selector[number])
            Win16ToolhelpLoadSegment(module, number);
    return state != Failed;
}
// Every relocation kind the linker writes. A record addresses a chain of
// places in the segment unless it is additive, in which case it patches one
// place and the word at the source stays what it was.
bool Application::Win16Relocate(Win16Module &module, unsigned number, unsigned &error)
{
    Win98Ne::Segment segment;
    if (!module.image.SegmentAt(number, segment))
    {
        error = Win98Ne::BadFormat;
        return false;
    }
    const unsigned count = module.image.RelocationCount(segment);
    const unsigned memory = module.memory[number];
    for (unsigned i = 0; i < count; ++i)
    {
        Win98Ne::Relocation record;
        module.image.RelocationAt(segment, i, record);
        unsigned selector = 0, offset = 0;
        switch (record.Class())
        {
        case Win98Ne::TargetInternal:
        {
            unsigned target = record.target, value = record.value;
            if (target == 255)
            {
                // A moveable entry: the entry table says where it really is.
                Win98Ne::Entry entry;
                if (!module.image.EntryAt(value, entry))
                {
                    error = Win98Ne::BadFormat;
                    return false;
                }
                target = entry.segment;
                value = entry.offset;
            }
            if (!target || target > module.segments)
            {
                error = Win98Ne::BadFormat;
                return false;
            }
            selector = module.selector[target];
            offset = value;
            break;
        }
        case Win98Ne::TargetImportOrdinal:
        case Win98Ne::TargetImportName:
        {
            char module_name[16] = {}, symbol[128] = {};
            module.image.ModuleName(record.target, module_name, sizeof module_name);
            unsigned ordinal = record.value;
            if (record.Class() == Win98Ne::TargetImportName)
            {
                module.image.ImportedName(record.value, symbol, sizeof symbol);
                ordinal = 0;
            }
            unsigned constant = 0;
            unsigned constant_ordinal = ordinal;
            if (!constant_ordinal)
                constant_ordinal = Same(symbol, "__AHSHIFT")    ? 113u
                                   : Same(symbol, "__AHINCR")  ? 114u
                                   : Same(symbol, "__WINFLAGS") ? 178u
                                                               : 0u;
            if (Same(module_name, "KERNEL") && constant_ordinal &&
                Win16KernelConstant(constant_ordinal, constant))
            {
                selector = constant;
                offset = constant;
                break;
            }
            if (Same(module_name, "KERNEL") && Win16RealModeParagraph(ordinal ? ordinal : 0, symbol) != ~0u)
            {
                selector = Win16RealModeSelector(Win16RealModeParagraph(ordinal, symbol));
                if (!selector)
                {
                    error = Win98Ne::OutOfMemory;
                    return false;
                }
                // An absolute entry of KRNL386: an offset fixup (kind 5, how
                // SYSDETMG's linker wrote them) gets the selector as well.
                offset = selector;
                break;
            }
            const unsigned far_pointer = Win16Import(module_name, symbol, ordinal, error);
            if (!far_pointer)
                return false;
            selector = far_pointer >> 16;
            offset = far_pointer & 65535;
            break;
        }
        case Win98Ne::TargetOsFixup:
            // The floating point emulator fixups. Win98 patches the call the
            // linker wrote into the real coprocessor instruction when a
            // coprocessor is present, and the guest CPU always has one, so
            // the record stays as the linker left it.
            continue;
        default:
            error = Win98Ne::BadFormat;
            return false;
        }
        unsigned at = record.source, steps = 0;
        for (;;)
        {
            if (at + 1 >= segment.allocation || ++steps > 65536)
            {
                if (log)
                {
                    char text[200];
                    snprintf(text, sizeof text, "WIN16 relocation chain leaves %s segment %u at %04x (record %u)",
                             module.name, number, at, i);
                    log(text);
                }
                error = Win98Ne::BadFormat;
                return false;
            }
            const unsigned next = Win16Word(memory + at);
            const bool additive = record.Additive();
            switch (record.kind)
            {
            case Win98Ne::RelocationLowByte:
            {
                unsigned char byte = 0;
                Read(memory + at, &byte, 1);
                byte = (unsigned char)(offset + (additive ? byte : 0));
                Store(memory + at, &byte, 1);
                break;
            }
            case Win98Ne::RelocationSelector:
                Win16Word(memory + at, selector + (additive ? next : 0));
                break;
            case Win98Ne::RelocationFarAddress:
                Win16Word(memory + at, offset + (additive ? next : 0));
                Win16Word(memory + at + 2, selector);
                break;
            case Win98Ne::RelocationOffset:
                Win16Word(memory + at, offset + (additive ? next : 0));
                break;
            default:
                error = Win98Ne::BadFormat;
                return false;
            }
            if (additive || next == 65535)
                break;
            at = next;
        }
    }
    return true;
}
// The ordinal of an export name. KRNL386 searches the resident name table
// first and the non-resident one after it; the comparison is case sensitive
// on the upper-case spelling the linker wrote.
unsigned Application::Win16Ordinal(Win16Module &module, const char *symbol)
{
    if (!symbol || !*symbol)
        return 0;
    char name[128];
    unsigned ordinal = 0;
    for (unsigned i = 1; module.image.ResidentExport(i, name, sizeof name, ordinal); ++i)
        if (Same(name, symbol))
            return ordinal;
    for (unsigned i = 0; module.image.NonResidentExport(i, name, sizeof name, ordinal); ++i)
        if (Same(name, symbol))
            return ordinal;
    return 0;
}
// The three KERNEL entries that are not code. KRNL386 exports them as absolute
// entries, so the linker writes their value straight into the instruction and
// the program never calls them. A gate pointer would be patched in instead and
// every huge pointer walk would step somewhere else. __AHSHIFT is the shift
// from a 64 KiB step to a selector step and __AHINCR the step itself, which in
// protected mode is one descriptor, and __WINFLAGS is the answer GetWinFlags
// gives at 1:0-. The value is written into both halves, because the kind of the
// record decides which half the linker reads.
bool Application::Win16KernelConstant(unsigned ordinal, unsigned &value)
{
    switch (ordinal)
    {
    case 113: value = 3; return true; // __AHSHIFT
    case 114: value = 8; return true; // __AHINCR
    // Build __WINFLAGS from the virtual CPU and system capabilities: 386/486
    // class, x87, protected and enhanced modes, CPUID, and the configured
    // paging-file report. SYSDM uses these flags for its processor and memory
    // displays. Neo98 reports 2C29h for this configuration.
    case 178: // __WINFLAGS, the same resident word as GetWinFlags.
        if (!EnsureSystemVmMemory()) return false;
        value = 0;
        memcpy(&value, system_vm_memory + SystemVm::KernelBase + SystemVm::WinFlagsOffset, 2);
        return true;
    default: return false;
    }
}
// The selectors KRNL386 exports for the first megabyte of the virtual
// machine: __0000H (183) to __F000H (194), __ROMBIOS (173). Each is a data
// selector with the paragraph as its base and a limit of FFFFh. SYSDETMG
// reads and hooks the interrupt table through __0000H (the INT 12h
// vector during detection) and reads the BIOS data area and the ROMs
// through __0040H, __C000H, __E000H and __F000H; MODEM.CPL and COMM.DRV read
// the port addresses. The answer is the paragraph, ~0u for any
// other import.
unsigned Application::Win16RealModeParagraph(unsigned ordinal, const char *symbol)
{
    static const struct { unsigned ordinal; const char *name; unsigned paragraph; } table[] = {
        {183, "__0000H", 0x0000}, {193, "__0040H", 0x0040}, {174, "__A000H", 0xa000},
        {181, "__B000H", 0xb000}, {182, "__B800H", 0xb800}, {195, "__C000H", 0xc000},
        {179, "__D000H", 0xd000}, {190, "__E000H", 0xe000}, {194, "__F000H", 0xf000},
        {173, "__ROMBIOS", 0xf000},
    };
    for (const auto &entry : table)
        if (ordinal ? ordinal == entry.ordinal : (symbol && Same(symbol, entry.name)))
            return entry.paragraph;
    return ~0u;
}
// The machine's first megabyte as 16-bit code sees it through those
// selectors. The high view shares its backing with the flat system-VM
// windows and GlobalDOSAlloc. Its null page is only exposed through Win16,
// preserving the existing Win32 null-page exception. It carries the virtual
// PC data used by hardware detection: the
// BIOS data area with the two serial ports of src/x86_kernel_comm.inc
// (3F8h, 2F8h) and LPT1 (378h), the equipment word and 640 KiB of base
// memory, the model byte FCh (AT) and a BIOS date, empty option
// ROM space (FFh) from C000h to EFFFh and the resident KERNEL tables.
// The guest address of the first megabyte (Win16RealModeSelector), made on
// first use.
unsigned Application::Win16RealModeImage()
{
    auto &machine = *win16.state;
    if (!machine.real_mode_image && !Win16RealModeSelector(0))
        return 0;
    return machine.real_mode_image;
}
// SETUPX runs three small routines of its own in real mode : it
// copies the routine behind a 22h byte stub into a GlobalDosAlloc block,
// points the INT 12h vector at the stub (through __0000H, with CLI around)
// and raises INT 12h with AX:BX:CX:DX = 5043h:4D4Eh:4E4Dh:4350h. The stub
// checks those words and calls the routine far; any other INT 12h goes on to
// the old vector. There is no virtual 8086 mode here, so the routine is
// recognised by its bytes and its answer given in AX, the one register it
// changes (each ends with POPA and returns ES or AX):
// - segment 30, SXISMSDOS7RUNNING : INT 2Fh 1611h, AX 0 when the
// shell is MS-DOS 7, as on every Win98;
// - segment 29 : INT 2Fh AD80h, KEYB.COM; none is loaded, FFFFh;
// - segment 33: INT 21h EA01h, the NetWare shell, which copies the server's
// connection entry to its byte 61h on; there is none, so the routine
// clears byte 61h and answers 0 in AX and DX (the Finish of the Add Printer
// wizard asks).
// Segment 31, absolute sectors by INT 25h/26h or INT 21h 7305h, has no
// answer here and stops the program.
bool Application::Win16SetupxRealModeCall()
{
    if ((Register(Reg::Eax) & 0xffff) != 0x5043 || (Register(Reg::Ebx) & 0xffff) != 0x4d4e ||
        (Register(Reg::Ecx) & 0xffff) != 0x4e4d || (Register(Reg::Edx) & 0xffff) != 0x4350)
        return false;
    const unsigned table = win16.state ? Win16RealModeImage() : 0;
    if (!table)
        return false;
    const unsigned vector = Read32(table + 0x12 * 4);
    const unsigned stub = (vector >> 16) * 16 + (vector & 0xffff);
    static const unsigned char head[] = {0x3d, 0x43, 0x50, 0x75, 0x18, 0x81, 0xfb, 0x4e, 0x4d};
    unsigned char bytes[sizeof head] = {};
    if (!Read(stub, bytes, sizeof bytes) || memcmp(bytes, head, sizeof head))
        return false;
    const unsigned target = Read32(stub + 0x18);
    const unsigned code = (target >> 16) * 16 + (target & 0xffff);
    static const unsigned char dos7[] = {0x06, 0x1e, 0x60, 0xb8, 0x11, 0x16, 0xcd, 0x2f,
                                         0x8e, 0xc0, 0x61, 0x8c, 0xc0, 0x1f, 0x07, 0xcb};
    static const unsigned char keyb[] = {0x06, 0x1e, 0x60, 0x8c, 0xc8, 0x8e, 0xd8, 0xb8, 0x80, 0xad, 0xcd, 0x2f};
    static const unsigned char netware[] = {0x06, 0x1e, 0x60, 0x8c, 0xc8, 0x8e, 0xc0, 0xbf,
                                            0x62, 0x00, 0xb8, 0x01, 0xea, 0x33, 0xdb, 0xcd};
    unsigned char routine[16] = {};
    if (!Read(code, routine, sizeof routine))
        return false;
    unsigned answer;
    if (!memcmp(routine, dos7, sizeof dos7))
        answer = 0;
    else if (!memcmp(routine, keyb, sizeof keyb))
        answer = 0xffff;
    else if (!memcmp(routine, netware, sizeof netware))
    {
        const unsigned char none = 0;
        Store(code + 0x61, &none, 1);
        Register(Reg::Edx, Register(Reg::Edx) & 0xffff0000u);
        answer = 0;
    }
    else
    {
        char text[200], hex[3 * sizeof routine + 1] = {};
        for (unsigned i = 0; i < sizeof routine; ++i)
            snprintf(hex + 3 * i, 4, "%02x ", routine[i]);
        snprintf(text, sizeof text, "SETUPX real-mode routine at %04x:%04x is not ported (bytes %s)", target >> 16,
                 target & 0xffff, hex);
        Fail(text);
        return true;
    }
    Register(Reg::Eax, (Register(Reg::Eax) & 0xffff0000u) | answer);
    return true;
}
// DOS's country tables, as Win98's DOS has them built in without COUNTRY= in
// CONFIG.SYS: country 001 with code page 437, the same entry that
// C:\WINDOWS\COMMAND\COUNTRY.SYS carries, read from there once. The
// file: the header "\xFFCOUNTRY", the entry table at the doubleword +13h (a
// word count, then records of a word length, country, code page, two words
// and the doubleword offset of the information table), the information table
// (a word count, then records of a word length, the identifier and a
// doubleword offset), each table behind an eight byte name and a word size.
// The collating table (6) and the upper case table (2) go into the first
// megabyte where DOS keeps its data (0600h and 0710h), the country
// information (1) stays here for INT 21h 6501h with the far address of the
// case map routine in it. That routine is a RETF; DPMI 0301h
// runs its work natively (Win16DosCaseMap). False when the file or the entry
// is missing.
bool Application::Win16DosCountry()
{
    auto &machine = *win16.state;
    if (machine.country_loaded)
        return true;
    const unsigned image = Win16RealModeImage();
    if (!image)
        return false;
    unsigned handle = 0, status = 0, size = 0, done = 0;
    if (files.Open("C:\\WINDOWS\\COMMAND\\COUNTRY.SYS", 1, 3, 3, 0x80, handle, status))
        return false;
    unsigned char *data = nullptr;
    bool ok = !files.Size(handle, size) && size >= 0x17 && size <= (1u << 20);
    if (ok) { data = new unsigned char[size]; ok = data && !files.Read(handle, data, size, done) && done == size; }
    files.Close(handle);
    auto word = [&](unsigned at) { return at + 2 <= size ? unsigned(data[at] | data[at + 1] << 8) : 0u; };
    auto dword = [&](unsigned at) { return at + 4 <= size ? word(at) | word(at + 2) << 16 : 0u; };
    unsigned found[8] = {};
    if (ok && !memcmp(data, "\xff" "COUNTRY", 8))
    {
        const unsigned entries = dword(0x13);
        const unsigned count = word(entries);
        for (unsigned i = 0, at = entries + 2; i < count && at + 14 <= size; ++i, at += word(at) + 2)
        {
            if (word(at + 2) != 1 || word(at + 4) != 437)
                continue;
            const unsigned info = dword(at + 10);
            for (unsigned j = 0, n = word(info); j < n && info + 2 + j * 8 + 8 <= size; ++j)
            {
                const unsigned id = word(info + 2 + j * 8 + 2), where = dword(info + 2 + j * 8 + 4);
                if (id < 8 && where + 10 <= size && data[where] == 0xff)
                    found[id] = where + 8;
            }
            break;
        }
    }
    ok = ok && found[1] && found[2] && found[6] && word(found[1]) == 0x26 && word(found[2]) == 128 &&
         word(found[6]) == 256 && found[1] + 2 + 0x26 <= size && found[2] + 2 + 128 <= size &&
         found[6] + 2 + 256 <= size;
    if (ok)
    {
        Store(image + DosCollateAddress, data + found[6], 2 + 256);
        Store(image + DosUpperAddress, data + found[2], 2 + 128);
        const unsigned char retf = 0xcb;
        Store(image + DosCaseMapAddress, &retf, 1);
        memcpy(machine.country_info, data + found[1] + 2, 0x26);
        // The case map routine's far address at +16h of the information.
        machine.country_info[0x16] = (unsigned char)(DosCaseMapAddress & 0xff);
        machine.country_info[0x17] = (unsigned char)((DosCaseMapAddress - 0x600) >> 8);
        machine.country_info[0x18] = 0x60;
        machine.country_info[0x19] = 0x00;
        machine.country_loaded = true;
    }
    delete[] data;
    return ok;
}
// The case map routine of the country information: AL of 80h and above to
// upper case through the country table, below that unchanged.
bool Application::Win16DosCaseMap(unsigned segment, unsigned offset, unsigned &ax)
{
    if (((segment << 4) + offset) != DosCaseMapAddress || !Win16DosCountry())
        return false;
    const unsigned al = ax & 0xff;
    if (al >= 0x80)
    {
        const unsigned upper = Win16Word(Win16RealModeImage() + DosUpperAddress + 2 + (al - 0x80)) & 0xff;
        ax = (ax & 0xff00) | upper;
    }
    return true;
}
unsigned Application::Win16RealModeSelector(unsigned paragraph)
{
    auto &machine = *win16.state;
    const unsigned index = paragraph >> 6; // 0000h, 0040h,..., F000h
    if (index >= sizeof machine.real_mode_selectors / sizeof machine.real_mode_selectors[0])
        return 0;
    if (machine.real_mode_selectors[index])
        return machine.real_mode_selectors[index];
    if (!machine.real_mode_image)
    {
        const unsigned allocation = Allocate(0x120000);
        const unsigned image = allocation ? (allocation + 0xffff) & ~0xffffu : 0;
        if (!image)
            return 0;
        if (!EnsureSystemVmMemory() || !Check(cpu->Unmap(image, 0x110000)) ||
            !Check(cpu->MapHost(image, 0x110000, Prot::All, system_vm_memory)))
            return 0;
        machine.real_mode_image = image;
    }
    // The VGA aperture is the same device through the physical selector and
    // a flat Win32 pointer. Other BIOS/DOS selectors retain their image.
    const unsigned physical = paragraph * 16;
    const bool video = physical >= 0xa0000 && physical < 0xc0000;
    if (video && !EnsureVga()) return 0;
    const unsigned selector = Win16Segment(video ? physical : machine.real_mode_image + physical, 0xffff, false, false);
    machine.real_mode_selectors[index] = selector;
    if (log)
    {
        char text[120];
        snprintf(text, sizeof text, "WIN16 real mode selector %04x for paragraph %04x at %08x", selector, paragraph,
                 video ? physical : machine.real_mode_image + physical);
        log(text);
    }
    return selector;
}
// The search order of LoadLibrary in KRNL386, for a module named without a
// path: the directory of the program, the current directory, the Windows
// directory and the system directory. The first entry is the one that matters
// here, because a 16-bit program of the collection ships its own DLLs next to
// its executable; without it every one of them stopped with error 2.
unsigned Application::Win16FindLibrary(const char *module_name, char (&full)[260],
                                       unsigned char *&bytes, unsigned &length)
{
    auto &machine = *win16.state;
    char program[260] = {};
    if (machine.module_count)
    {
        CopyText(program, machine.modules[0].path, sizeof program);
        unsigned cut = 0;
        for (unsigned i = 0; program[i]; ++i)
            if (program[i] == '\\' || program[i] == '/')
                cut = i;
        program[cut] = 0;
    }
    const char *roots[4] = {program[0] ? program : nullptr, ".", "C:\\WINDOWS\\SYSTEM", "C:\\WINDOWS"};
    unsigned result = Win98Ne::FileNotFound;
    for (const char *root : roots)
    {
        if (!root)
            continue;
        // A name without an extension gets ".DLL" (KRNL386 LoadModule);
        // "SYSDM.CPL", which MODEM.CPL loads, keeps its own.
        const char *extension = strchr(module_name, '.') ? "" : ".DLL";
        char path[300];
        if (root[0] == '.' && !root[1])
            snprintf(path, sizeof path, "%s%s", module_name, extension);
        else
            snprintf(path, sizeof path, "%s\\%s%s", root, module_name, extension);
        bytes = nullptr;
        length = 0;
        result = files.FullPath(path, full) ? Win98Ne::FileNotFound : Win16ReadFile(full, bytes, length);
        if (!result)
            return 0;
    }
    return result;
}
// One import of a 16-bit module. A module whose 16-bit side is ours answers
// with a gate; anything else is a shipped 16-bit DLL and is loaded as the
// original code it is.
unsigned Application::Win16Import(const char *module_name, const char *symbol, unsigned ordinal,
                                  unsigned &error)
{
    if (!*module_name)
    {
        error = Win98Ne::BadFormat;
        return 0;
    }
    if (DllRoutes::Win16System(module_name))
    {
        if (Same(module_name, "KERNEL"))
        {
            const unsigned paragraph = Win16RealModeParagraph(ordinal, symbol);
            if (paragraph != ~0u) return Win16RealModeSelector(paragraph) << 16;
        }
        const unsigned gate = Win16Gate(module_name, symbol, ordinal);
        if (!gate)
        {
            error = 8;
            return 0;
        }
        const unsigned pointer = Win16GatePointer(gate);
        if (!pointer)
            error = 8;
        return pointer;
    }
    Win16Module *module = Win16Find(module_name);
    if (!module)
    {
        char full[260];
        unsigned char *bytes = nullptr;
        unsigned length = 0;
        const unsigned result = Win16FindLibrary(module_name, full, bytes, length);
        if (result)
        {
            error = result;
            // KRNL386 shows nothing for a library it cannot find: its only
            // "Windows cannot find" box (string 4) belongs to
            // OpenFile with OF_PROMPT, and LoadModule never passes that flag.
            // The start fails with the loader's error, 2, and the loader keeps
            // the name, which KERNEL32 ordinal 91 (PK16FNF) hands to SHELL32.
            // SHELL32 asks for it after CreateProcess failed with 2,
            // 3, 123 or 161 on an NE file and says "Cannot find %1. Windows
            // needs this file to run %2." (string 1941h).
            snprintf(win16_not_found, sizeof win16_not_found, "%s.DLL", module_name);
            if (log)
            {
                char text[320];
                snprintf(text, sizeof text, "MISSING WIN16 MODULE %s", win16_not_found);
                log(text);
            }
            return 0;
        }
        module = Win16Place(full, bytes, length, true, error);
        if (!module)
            return 0;
    }
    unsigned number = ordinal;
    if (!number && *symbol)
        number = Win16Ordinal(*module, symbol);
    Win98Ne::Entry entry;
    if (!number || !module->image.EntryAt(number, entry) || !entry.segment ||
        entry.segment > module->segments || !module->selector[entry.segment])
    {
        error = Win98Ne::BadLibrary;
        if (log)
        {
            char text[320];
            snprintf(text, sizeof text, "MISSING WIN16 IMPORT %s!%s#%u", module_name, symbol, ordinal);
            log(text);
        }
        return 0;
    }
    return module->selector[entry.segment] << 16 | (entry.offset & 65535);
}

// Report a missing startup library using its import-table name, adding.DLL
// when absent. Windows 98 PE startup reports ERROR_DLL_NOT_FOUND through a
// foreground, topmost warning dialog; the parent receives the child exit code
// and SHELL32 does not show a second message for 1157. The 16-bit path uses
// ERROR_MOD_NOT_FOUND and MB_ICONHAND.
char Application::win16_not_found[128] = {};
void Application::ReportMissingModule()
{
    if (!missing_module[0])
        return;
    char name[sizeof missing_module];
    memcpy(name, missing_module, sizeof name);
    name[sizeof name - 1] = 0;
    missing_module[0] = 0;
    const unsigned error = missing_module_error ? missing_module_error : 126;
    missing_module_error = 0;
    char text[sizeof missing_text + 64];
    if (missing_text[0])
        snprintf(text, sizeof text, "%s", missing_text);
    else
        snprintf(text, sizeof text, "A required .DLL file, %s, was not found.", name);
    missing_text[0] = 0;
    if (log)
    {
        char line[sizeof text + 80];
        snprintf(line, sizeof line,
                 "MISSING MODULE MESSAGE caption=[Error Starting Program] text=[%s]", text);
        log(line);
    }
    MessageBox(0, text, "Error Starting Program", error != 126 ? 0x00050030u : 0x00000010u);
    last_error = error;
}

// ------------------------------------------------------------------ the start

// Everything of Start that does not depend on a PE image: the CPU, the flat
// arena, the descriptor tables and the hooks.
bool Application::Win16Machinery()
{
    if (!ConfigureMemory())
        return false;
    const auto backend = ProcessCpuBackend() == CpuBackend::Native ? CpuBackend::Native : CpuBackend::Unicorn;
    if (!Check(CreateCpu(backend, cpu, "win16-process")))
        return false;
    if (!InitializeCpuDescription() || !InitializeFpu())
        return false;
    if (!Map(0x0fe00000, 0x200000, 3) || !Map(GateBase, GateRegionBytes, 3) ||
        !Map(LdtBase, LdtBytes, 3) || !Map(GdtBase, Application::SystemPageBytes, 3) || !MapProcessHeap())
        return false;
    // The shared area is mapped in every context of Win98, the Win16 tasks
    // included, and a Win16 task reaches it as soon as a Win32 DLL it thunks
    // to creates a HEAP_SHARED heap: DIALER's TAPI32 and RPCRT4 do, and
    // TAPISRV grows that heap afterwards. Without the entry in the session
    // list a commit of another process never reached this one.
    JoinSession();
    unsigned char trap[65536];
    memset(trap, 0xf4, sizeof trap);
    for (unsigned offset = 0; offset < GateRegionBytes; offset += sizeof trap)
        Store(GateBase + offset, trap, sizeof trap);
    Protect(GateBase, GateRegionBytes, 5);
    const unsigned long long teb = Application::MainTeb;
    const unsigned long long descriptors[] = {
        0, 0x00cf9a000000ffffull, 0x00cf92000000ffffull,
        0xfffull | ((teb & 0xffffff) << 16) | (0x93ull << 40) | (0x40ull << 48) | ((teb >> 24) << 56),
        // Entry four is the LDT of this process. Type 2 is an LDT segment.
        (unsigned long long)(LdtBytes - 1) | ((unsigned long long)(LdtBase & 0xffffff) << 16) |
            (0x82ull << 40) | ((unsigned long long)(LdtBase >> 24) << 56)};
    Store(GdtBase, descriptors, sizeof descriptors);
    DescriptorTable gdt;
    gdt.base = GdtBase;
    gdt.limit = (3 + 16) * 8 - 1;
    Check(cpu->SetWide(Reg::Gdtr, &gdt));
    Register(Reg::Cr0, 1);
    Register(Reg::Cs, 8);
    const int segments[] = {Reg::Ds, Reg::Es, Reg::Ss, Reg::Gs};
    for (int reg : segments)
        Register(reg, 16);
    Register(Reg::Fs, 24);
    DescriptorTable ldt;
    ldt.base = LdtBase;
    ldt.limit = LdtBytes - 1;
    ldt.selector = LdtSelector;
    ldt.flags = 0x82;
    Check(cpu->SetWide(Reg::Ldtr, &ldt));
    Write32(teb, 0xffffffff);
    Write32(teb + 4, 0x10000000);
    Write32(teb + 8, 0x0fe00000);
    Write32(teb + 0x18, teb);
    Write32(teb + 0x1c, 0xffff0001);
    Write32(teb + 0x2c, teb + 0x88);
    threads[0].id = AllocateThreadIdentity();
    if (!threads[0].id)
    {
        Fail("Global thread identity capacity exhausted");
        return false;
    }
    Write32(teb + 0x20, process_id);
    Write32(teb + 0x24, threads[0].id);
    threads[0].app = this;
    threads[0].keyboard_layout = keyboard_layout_state.entries[0].layout;
    User::SetKeyboardLanguage(keyboard_layout_state.entries[0].layout);
    InitializeThreadControl(0);
    Write32(teb + 0x30, Application::ProcessPage);
    Check(cpu->SetGateHook(GateBase, GateBase + GateRegionBytes - 1, Gate, this));
    Check(cpu->SetFaultHook(MemoryFaultHook, this));
    AddSliceHook();
    // A 16-bit program reaches DOS through INT 21h as well as through the
    // KERNEL export DOS3Call; the startup code of the Microsoft C runtime
    // picks one of the two from a flag in its own code segment. Both arrive
    // at the same place here.
    Check(cpu->SetInterruptHook(Win16Interrupt, this));
    // The input/output privilege level is three, and interrupts are enabled.
    // CLI and STI are privileged by IOPL, not by the code selector: a program
    // on privilege three whose IOPL is lower faults on both. Win16 code runs
    // on privilege three and uses them anyway. USER.EXE has CLI and
    // STI around its queue, KRNL386.EXE has fifteen STI, and the
    // Visual-Basic-1 runtime reaches STI at VBRUN100 on the way into
    // its message dispatcher. Windows 98 gives a task privilege level three
    // with IOPL three for exactly this reason; with IOPL zero every one of
    // those instructions is a general protection fault.
    Register(Reg::Eflags, 0x3202);
    return state != Failed;
}
// A 16-bit program of the Win98 tree. Start hands the image over here when
// the file is an NE image.
bool Application::StartWin16(const void *exe, unsigned length, const unsigned char *font,
                             unsigned font_length, const char *name, const char *command)
{
    CopyText(program_name, name, sizeof program_name);
    state = Running;
    font_bytes = font;
    font_size = font_length;
    if (unsigned registry_error = Registry::SystemInitializationError())
    {
        last_error = registry_error;
        Fail("Installed registry configuration could not be loaded");
        return false;
    }
    Files::SetClock(&Application::FileClock);
    win16.state = new Win16Machine;
    if (!win16.state)
    {
        Fail("Win16 machine allocation failed");
        return false;
    }
    if (!Win16Machinery())
    {
        if (state != Failed)
            Fail("Win16 process setup failed");
        return false;
    }
    // A 16-bit task has the same process environment as a 32-bit one, because
    // it is a process here. Without it WinExec found no block to hand a child
    // and every launch failed with ERROR_NOACCESS. It goes after the machinery
    // because the block is a VirtualAlloc in the address space.
    if (!InitializeProcessEnvironment())
    {
        Fail("Win16 process environment could not be built");
        return false;
    }
    // The shared arena of the session is mapped in the process of a Win16
    // program too: WINSPOOL under RUNDLL.EXE reads the message buffer the
    // spooler left in its HEAP_SHARED heap.
    JoinSession();
    auto *bytes = new unsigned char[length];
    memcpy(bytes, exe, length);
    char executable_path[260];
    if (strchr(name, '\\') || strchr(name, '/') || strchr(name, ':'))
    {
        if (files.FullPath(name, executable_path))
        {
            delete[] bytes;
            Fail("Invalid executable path");
            return false;
        }
    }
    else
        snprintf(executable_path, sizeof executable_path, "C:\\WINDOWS\\%s", name);
    unsigned error = 0;
    Win16Module *module = Win16Place(executable_path, bytes, length, false, error);
    if (!module)
    {
        char text[360];
        snprintf(text, sizeof text, "Win16 program %s refused by the NE loader, error %u", executable_path,
                 error);
        last_error = error;
        // A library that is not there is an ordinary load error, shown to the
        // user by the shell and no crash.
        if (error == Win98Ne::FileNotFound)
            load_failure = true;
        Fail(text);
        return false;
    }
    modules[0].kind = 4;
    modules[0].handle = module->handle;
    modules[0].bound = true;
    CopyText(modules[0].path, executable_path, sizeof modules[0].path);
    command_line = String(command ? command : name);
    if (!Win16Task(*module, command))
        return false;
#ifdef NEO98_RUNTIME_CONTENT
    // USER creates its first MS Sans Serif font before the stock objects.
    // Release hosts must have read the installed catalogue by then.
    LoadInstalledFonts();
#endif
    if (!User::Initialize())
    {
        Fail("Native USER initialization failed");
        return false;
    }
    if (!User::AttachProcess(process_id, this))
    {
        Fail("Desktop process capacity exhausted");
        return false;
    }
    user_initialized = true;
    User::BindTask(process_id, threads[0].id);
    if (!InitializeUserProviders(module->image.ExpectedVersion()))
        return false;
    // The stock objects and the two bitmap fonts of the desktop.
    if (!InstallStockFonts())
        return false;
    const unsigned colors[] = {0xffffff, 0xc0c0c0, 0x808080, 0x404040, 0};
    for (unsigned i = 0; i < 5; ++i)
        stock[i] = Gdi::CreateSolidBrush(colors[i]);
    stock[15] = Gdi::DefaultPalette();
    stock[5] = Gdi::CreateNullBrush();
    stock[6] = Gdi::CreatePen(0, 0, 0xffffff);
    stock[7] = Gdi::CreatePen(0, 0, 0);
    stock[8] = Gdi::CreatePen(5, 0, 0);
    LoadDesktopWallpaper();
    auto &thread = threads[0];
    thread.fiber = NewThreadFiber();
    if (!thread.fiber || !Check(cpu->NewContext(thread.context)) ||
        !Check(cpu->Save(thread.context)) ||
        !thread.fiber->Start([](void *p) { static_cast<Application *>(p)->Run(); }, this))
    {
        Fail("Execution fiber allocation failed");
        return false;
    }
    if (log)
    {
        char text[400];
        snprintf(text, sizeof text,
                 "WIN16 program %s segments=%u entry=%04x:%04x data=%04x stack=%u heap=%u", module->path,
                 module->segments,
                 module->image.EntrySegment() && module->image.EntrySegment() <= module->segments
                     ? module->selector[module->image.EntrySegment()]
                     : 0,
                 module->image.EntryOffset(), module->instance, module->image.StackSize(),
                 module->image.HeapSize());
        log(text);
    }
    return true;
}
// A Win16 task's PSP word at 2Ch is the environment selector.
// GetDOSEnvironment returns that selector with offset zero. The block
// contains NUL-terminated NAME=VALUE strings, a second NUL, a WORD of one,
// and the full program path. Microsoft and Borland startup code expects this
// layout.
bool Application::Win16Environment(Win16Module &module)
{
    auto &machine = *win16.state;
    const unsigned none[] = {0};
    const unsigned strings = EnvironmentApi("GetEnvironmentStrings", none);
    if (state == Failed)
        return false;
    // The 32-bit block of this process, read to its double zero. A 16-bit
    // environment is a segment, so 64 KiB is the ceiling the shape allows.
    unsigned char block[0x10000];
    unsigned at = 0;
    if (strings)
        while (at + 1 < sizeof block)
        {
            unsigned char byte = 0;
            Read(strings + at, &byte, 1);
            block[at++] = byte;
            if (!byte && at >= 2 && !block[at - 2])
                break;
        }
    // An environment without a single string is still two zero bytes.
    if (at < 2)
    {
        block[0] = 0;
        block[1] = 0;
        at = 2;
    }
    const unsigned path_length = (unsigned)strlen(module.path);
    if (at + 3 + path_length >= sizeof block)
    {
        Fail("Win16 environment block exceeds its segment");
        return false;
    }
    block[at++] = 1;
    block[at++] = 0;
    memcpy(block + at, module.path, path_length + 1);
    at += path_length + 1;
    const unsigned memory = Allocate(at);
    if (!memory)
    {
        Fail("Win16 environment allocation failed");
        return false;
    }
    Store(memory, block, at);
    const unsigned selector = Win16Segment(memory, at - 1, false, false);
    if (!selector)
    {
        Fail("Win16 environment descriptor allocation failed");
        return false;
    }
    machine.environment = memory;
    machine.environment_bytes = at;
    machine.environment_selector = selector;
    Win16Word(machine.command_prefix + Win98NeTask::ProgramPrefixEnvironment, selector);
    return true;
}
// The task data, the program segment prefix, the stack and the registers
// the 16-bit entry point expects. The register list is the one the startup
// code of every Win16 compiler reads: AX zero, BX the stack size, CX the heap
// size, SI the previous instance, DI this instance, BP zero, ES the program
// segment prefix, DS the automatic data segment.
// The words behind the program name of a Win32 command line: the first
// token, quoted or bare, is the program itself and never part of the Win16
// command tail; the separating blanks go with it.
static const char *Win16CommandTail(const char *command)
{
    if (!command)
        return "";
    const char *p = command;
    while (*p == ' ' || *p == '\t')
        ++p;
    if (*p == '"')
    {
        ++p;
        while (*p && *p != '"')
            ++p;
        if (*p)
            ++p;
    }
    else
        while (*p && *p != ' ' && *p != '\t')
            ++p;
    while (*p == ' ' || *p == '\t')
        ++p;
    return p;
}

bool Application::Win16Task(Win16Module &module, const char *command)
{
    auto &machine = *win16.state;
    const unsigned prefix_block = Allocate(Win98NeTask::ProgramPrefixBytes);
    const unsigned task_block = Allocate(Win98NeTask::TaskBytes);
    if (!prefix_block || !task_block)
    {
        Fail("Win16 task allocation failed");
        return false;
    }
    unsigned char empty[Win98NeTask::TaskBytes] = {};
    Store(prefix_block, empty, Win98NeTask::ProgramPrefixBytes);
    Store(task_block, empty, Win98NeTask::TaskBytes);
    machine.prefix = Win16Segment(prefix_block, Win98NeTask::ProgramPrefixBytes - 1, false, false);
    machine.task = Win16Segment(task_block, Win98NeTask::TaskBytes - 1, false, false);
    machine.task_thread = ThreadId();
    if (!machine.prefix || !machine.task)
    {
        Fail("Win16 task descriptor allocation failed");
        return false;
    }
    machine.command_prefix = prefix_block;
    // The command line of the program sits in the prefix as a counted string
    // ending in a carriage return, which is where InitTask points ES:BX.
    // What arrives here is the Win32 command line, program name first, as
    // CreateProcess and WinExec hand it over; KRNL386's LoadModule keeps only
    // the words behind the program name for the prefix. Shanghai II rejected
    // its own quoted path as an "invalid command line parameter".
    const char *tail = Win16CommandTail(command);
    unsigned tail_length = (unsigned)strlen(tail);
    if (tail_length > 126)
        tail_length = 126;
    unsigned char counted[128] = {};
    counted[0] = (unsigned char)tail_length;
    memcpy(counted + 1, tail, tail_length);
    counted[1 + tail_length] = 13;
    Store(prefix_block + Win98NeTask::ProgramPrefixCommand, counted, tail_length + 2);
    if (!Win16Environment(module))
        return false;
    machine.instance_handle = module.instance;
    Win16Word(task_block + Win98NeTask::TaskInstance, module.instance);
    Win16Word(task_block + Win98NeTask::TaskModule, module.handle);
    Win16Word(task_block + Win98NeTask::TaskPdb, machine.prefix);
    Win16Word(task_block + Win98NeTask::TaskSignature, Win98NeTask::TaskSignatureValue);
    // The stack. SS is the automatic data segment when the header says so,
    // and a stack pointer of zero means the top of that segment.
    const unsigned stack_segment = module.image.StackSegment();
    unsigned stack_selector = 0, stack_pointer = module.image.StackPointer();
    if (!stack_segment || stack_segment == module.image.AutoDataSegment())
    {
        stack_selector = module.instance;
        if (!stack_pointer)
        {
            Win98Ne::Segment data;
            module.image.SegmentAt(module.image.AutoDataSegment(), data);
            stack_pointer = data.allocation + module.image.StackSize();
            if (stack_pointer > Win16Limit(stack_selector) + 1)
                stack_pointer = Win16Limit(stack_selector) + 1;
        }
    }
    else
    {
        if (stack_segment > module.segments)
        {
            Fail("Win16 stack segment of the header is not a segment of the module");
            return false;
        }
        stack_selector = module.selector[stack_segment];
        if (!stack_pointer)
            stack_pointer = Win16Limit(stack_selector) + 1;
    }
    if (!stack_selector)
    {
        Fail("Win16 program has no stack segment");
        return false;
    }
    machine.stack_selector = stack_selector;
    const unsigned entry_segment = module.image.EntrySegment();
    if (!entry_segment || entry_segment > module.segments || !module.selector[entry_segment])
    {
        Fail("Win16 program has no entry point");
        return false;
    }
    Win16Word(task_block + Win98NeTask::TaskStackSegment, stack_selector);
    Win16Word(task_block + Win98NeTask::TaskStackPointer, stack_pointer);
    // 16-bit code runs at privilege level three, as it does under Win98, and
    // its descriptors say so. A stack selector may only be loaded at the
    // level it names, so the level has to change first, and the only way a
    // processor leaves level zero is a far return that names the new stack.
    // The three words of the entry contract are already in place when it
    // runs; the far return itself is four dwords on a flat scratch stack.
    Register(Reg::Ds, module.instance ? module.instance : machine.prefix);
    Register(Reg::Es, machine.prefix);
    Register(Reg::Eax, 0);
    Register(Reg::Ebx, module.image.StackSize());
    Register(Reg::Ecx, module.image.HeapSize());
    Register(Reg::Esi, 0);
    Register(Reg::Edi, module.instance);
    Register(Reg::Ebp, 0);
    if (!Win16EnterRingThree(module.selector[entry_segment], module.image.EntryOffset(), stack_selector,
                             stack_pointer & 0xfffe))
        return false;
    return true;
}
// The generic thunks of KRNL386 that run 32-bit code (LoadLibraryEx32W and
// FreeLibrary32W through DllMain, CallProc32W) run it on the flat stack of
// the thread, the way the thunk layer of Win98 switches from the 16-bit
// SS:SP of the task to the Win32 stack of its thread. Without the switch,
// DllMain of TAPI32 and the modules it pulls in ran with ESP as the 16-bit
// SP and wrote outside its allocated stack. Each nested
// thunk takes the next 64 KiB window below the one in use, down to the
// stack limit in the TIB. Nested windows of a fixed size are this port's
// simplification: Win98 remembers the flat ESP at every 32-to-16 switch.
bool Application::Win16FlatEnter(CpuContext *&saved, unsigned &previous_top)
{
    saved = nullptr;
    if (!Win16Process() || !(Register(Reg::Cs) & 4))
        return true;
    auto &thread = threads[current_thread];
    const unsigned top = thread.win16_flat_top ? thread.win16_flat_top : Read32(Teb() + 4);
    const unsigned limit = Read32(Teb() + 8) + 0x1000;
    if (top < limit + 0x10000 || thread.win16_flat_depth >= 16)
    {
        Fail("Win16 generic thunk ran out of flat stack");
        return false;
    }
    if (!Check(cpu->NewContext(saved)))
        return false;
    Check(cpu->Save(saved));
    previous_top = thread.win16_flat_top;
    thread.win16_flat_top = top - 0x10000;
    // Level three cannot load the flat level-zero selectors; the whole state
    // of the flat setup comes back instead, as the hosted calls do it.
    if (!win16_flat_template || !Check(cpu->Restore(win16_flat_template)))
    {
        cpu->FreeContext(saved);
        saved = nullptr;
        thread.win16_flat_top = previous_top;
        Fail("Win16 generic thunk has no flat context");
        return false;
    }
    const int segments[] = {Reg::Ds, Reg::Es, Reg::Ss, Reg::Gs};
    for (int reg : segments)
        Register(reg, 16);
    Register(Reg::Fs, 24);
    Register(Reg::Esp, (top - 16) & ~15u);
    thread.win16_flat_saved[thread.win16_flat_depth++] = saved;
    return true;
}
void Application::Win16FlatLeave(CpuContext *saved, unsigned previous_top)
{
    if (!saved)
        return;
    auto &thread = threads[current_thread];
    thread.win16_flat_top = previous_top;
    if (thread.win16_flat_depth && thread.win16_flat_saved[thread.win16_flat_depth - 1] == saved)
        thread.win16_flat_saved[--thread.win16_flat_depth] = nullptr;
    // The 16-bit state of the task comes back whole; the callers take the
    // result from the case, not from EAX.
    if (state != Failed && state != Exited)
        Check(cpu->Restore(saved));
    cpu->FreeContext(saved);
}

Application::Win16TaskStack::Win16TaskStack(Application &a) : app(a)
{
    auto &thread = app.threads[app.current_thread];
    if (!app.Win16Process() || (app.Register(Reg::Cs) & 4) || !thread.win16_flat_depth)
        return;
    if (!app.Check(app.cpu->NewContext(flat)))
    {
        flat = nullptr;
        return;
    }
    app.Check(app.cpu->Save(flat));
    app.Check(app.cpu->Restore(thread.win16_flat_saved[thread.win16_flat_depth - 1]));
}
Application::Win16TaskStack::~Win16TaskStack()
{
    if (!flat)
        return;
    if (app.state != Failed && app.state != Exited)
        app.Check(app.cpu->Restore(flat));
    app.cpu->FreeContext(flat);
}
// The far return that takes the processor from the flat setup at level zero
// into the segmented world of the task at level three.
bool Application::Win16EnterRingThree(unsigned code, unsigned ip, unsigned stack, unsigned sp)
{
    constexpr unsigned Stub = 0x0fe00000, Frame = 0x0fe00100;
    const unsigned char retf = 0xcb;
    if (!Store(Stub, &retf, 1) || !Protect(Stub, 4096, 7))
    {
        Fail("Win16 level change stub could not be placed");
        return false;
    }
    const unsigned frame[] = {ip, code, sp, stack};
    if (!Store(Frame, frame, sizeof frame))
    {
        Fail("Win16 level change frame could not be written");
        return false;
    }
    Register(Reg::Cs, 8);
    Register(Reg::Ss, 16);
    if (!win16_flat_template && Check(cpu->NewContext(win16_flat_template)))
        Check(cpu->Save(win16_flat_template));
    Register(Reg::Esp, Frame);
    Register(Reg::Eip, Stub);
    return true;
}

// GetNumTasks counts the tasks in the session. Both Win16 and Win32 task
// creation increment the count, and task teardown decrements it.
// A Win32 task carries bit 10h at TDB+16h and a 32-bit pointer at TDB+54h.
// A Win32 program therefore counts like a
// 16-bit one, and WINFILE, which takes a count of one to mean "I am the
// shell", does not ask whether to end the Windows session while a
// 32-bit shell runs.
// A program is a process here, 16-bit or 32-bit or ARM64, and the table of
// processes of USER holds every one of the session whoever started it, so the
// count is the living entries of that table. Windows 98 has a few tasks of its
// own on top (KERNEL32, MSGSRV32, MPREXE, mmtask); they are no processes here
// and are not counted.
unsigned Application::Win16TaskCount()
{
    unsigned count = 0;
    bool self = false;
    for (unsigned index = 0; index < User::ProcessContextCount(); ++index)
        if (auto *process = static_cast<Application *>(User::ProcessContextAt(index));
            process && process->state != Failed && process->state != Exited)
        {
            ++count;
            self |= process == this;
        }
    return count + (self ? 0u : 1u);
}
// ------------------------------------------------------------- the transition

// Called from FlatThunkTransition for a gate this loader owns.
bool Application::Win16Transition(Import &item)
{
    const unsigned gate = pending;
    auto &machine = *win16.state;
    const unsigned ss = Register(Reg::Ss), sp = Register(Reg::Esp) & 0xffff;
    Win16Frame frame{*this};
    frame.stack = Win16Linear(ss, sp);
    frame.ip = Win16Word(frame.stack);
    frame.cs = Win16Word(frame.stack + 2);
    ++thunk_transitions;
    // The shipped 16-bit DLLs get their initialization entry before the first
    // service the program asks for. KRNL386 calls LibMain inside LoadModule,
    // before the program's entry point runs; here the call needs the ring
    // three stack of the task, and the first gate is the first moment that
    // stack exists.
    if (!machine.libraries_entered)
    {
        machine.libraries_entered = true;
        if (!Win16EnterLibraries())
            return false;
    }
    if (log && (trace_calls || !item.calls))
    {
        // The entry line carries the first argument words as well, because a
        // gate that stops never reaches the WIN16TRACE line below and the
        // arguments are then the only thing that says what was asked for.
        char text[260];
        snprintf(text, sizeof text, "WIN16 %s.%u %s arg=%04x,%04x,%04x,%04x,%04x from=%04x:%04x", item.dll,
                 item.id, item.name, frame.Word(0), frame.Word(1), frame.Word(2), frame.Word(3), frame.Word(4),
                 frame.cs, frame.ip);
        log(text);
    }
    ++item.calls;
    unsigned words = 0, result = 0;
    // 16-bit gates return handles in AX. While a gate runs, GDI lookup also
    // accepts the low word, matching Win9x GDI16.
    struct Narrow { Narrow() { Gdi::NarrowHandles(true); } ~Narrow() { Gdi::NarrowHandles(false); } } narrow;
    ++win16_gate_depth;
    // Only the gate's own first Api call may wait for the lock of the
    // screen; a native loop further down never does (Application::Api).
    win16_screen_check = true;
    win16_gate_dll = item.dll;
    win16_gate_name = item.name;
    bool answered = true;
    if (threads[current_thread].host_resume)
    {
        // A thread out of a saved state that stood in this call: the call
        // is not made again, its frames are entered where they were
        // (Application::ResumeHostCall).
        words = Win16StateWords(item);
        result = ResumeHostCall();
    }
    else
    {
        // A call a saved state can take up again says so to its handler.
        if (!item.tail_gate) item.tail_gate = TailGate(item.dll, item.name, item.id, true) ? 2 : 1;
        threads[current_thread].tail = item.tail_gate == 2 ? TailFresh : TailNone;
        answered = Win16Call(item, frame, result, words);
        threads[current_thread].tail = TailNone;
    }
    win16_screen_check = false;
    --win16_gate_depth;
    if (!answered)
        return false;
    if (state == Failed || state == Exited)
        return false;
    // A gate that has to wait leaves the frame where it is. Service suspends
    // the thread and enters the gate again, exactly as it does for a 32-bit
    // import that waits.
    if (waiting)
        return false;
    if (log && trace_calls)
    {
        char text[260];
        snprintf(text, sizeof text, "WIN16TRACE %s.%u %s words=%u arg=%04x,%04x,%04x result=%08x", item.dll,
                 item.id, item.name, words, frame.Word(0), frame.Word(1), frame.Word(2), result);
        log(text);
    }
    if (machine.throw_pending)
    {
        Register(Reg::Eax, result & 0xffff);
        Register(Reg::Edx, result >> 16);
        // KERNEL.56 Throw does not go back to its own caller. It rebuilds the
        // frame of the Catch that made the buffer and leaves through that
        // frame, so the stack, the stack segment and the far address all come
        // from the buffer instead of from this call.
        machine.throw_pending = 0;
        Register(Reg::Ss, machine.throw_ss);
        Register(Reg::Esp, machine.throw_sp);
        Register(Reg::Cs, machine.throw_cs);
        Register(Reg::Eip, machine.throw_ip);
        pending = 0;
        return true;
    }
    Win16GateReturn(sp, frame.cs, frame.ip, words, result);
    (void)gate;
    return true;
}
// The return of a Pascal gate to its 16-bit caller: the answer in DX:AX, the
// arguments off the stack. Also of a gate a saved state took up again
// (Application::FinishGate).
void Application::Win16GateReturn(unsigned sp, unsigned cs, unsigned ip, unsigned words, unsigned result)
{
    Register(Reg::Eax, result & 0xffff);
    Register(Reg::Edx, result >> 16);
    // The exports of KRNL386, USER and GDI save the caller's DS in their
    // prolog and end with `pop ds`, and loading a selector reads its
    // descriptor again. A block the call moved therefore shows its new base
    // to the caller from the next instruction on: MSTCP grows the global
    // block of its own heap with GlobalReAlloc while DS names it (MSTCP) and then writes the new end mark through DS. The
    // gate reloads DS the same way; a selector the call freed is left alone.
    if (const unsigned ds = Register(Reg::Ds) & 0xffff; ds > 7 && selectors.At(ds))
        Register(Reg::Ds, ds);
    Register(Reg::Esp, (sp + 4 + words * 2) & 0xffff);
    Register(Reg::Cs, cs);
    Register(Reg::Eip, ip);
    pending = 0;
}
// Everything the 16-bit KERNEL answers for. `words` is the number of argument
// words the callee removes, so it is part of the contract of every ordinal.
bool Application::Win16Call(Import &item, const Win16Frame &frame, unsigned &result, unsigned &words)
{
    const unsigned ordinal = item.id;
    if (Same(item.dll, "NEO98TSP"))
    {
        words = Tsp3216Words(ordinal);
        result = Tsp3216(ordinal, frame.stack + 4);
        return state != Failed;
    }
    if (Same(item.dll, "KERNEL"))
        return Win16KernelCall(ordinal, frame, result, words);
    if (Same(item.dll, "USER"))
    {
        // The thunk gate a native window procedure was handed out as. A
        // 16-bit program that far-calls it instead of passing it back to
        // CallWindowProc arrives here with the same Pascal frame.
        if (ordinal >= Win16ThunkOrdinal && ordinal < Win16ThunkOrdinal + 256)
        {
            words = 5;
            const unsigned index = ordinal - Win16ThunkOrdinal;
            if (index >= win16.state->procedure_count || !win16.state->procedures[index].native)
            {
                Fail("Win16 window procedure thunk has no native target");
                return false;
            }
            unsigned message = frame.Word(3), wparam = frame.Word(2), lparam = frame.Long(0);
            const unsigned hwnd = frame.Word(4);
            Win16ControlTransfer transfer;
            if (!Win16ToThirtyTwo(hwnd, message, wparam, lparam, &transfer))
                return false;
            const unsigned a[] = {win16.state->procedures[index].native, hwnd, message, wparam, lparam};
            result = Win16ControlAnswer(transfer, Api("CallWindowProcA", a));
            return true;
        }
        return Win16UserCall(ordinal, frame, result, words);
    }
    if (Same(item.dll, "GDI"))
        return Win16GdiCall(ordinal, frame, result, words);
    if (Same(item.dll, "KEYBOARD"))
        return Win16KeyboardCall(ordinal, frame, result, words);
    if (Same(item.dll, "SHELL"))
        return Win16ShellCall(ordinal, frame, result, words);
    if (Same(item.dll, "SOUND"))
        return Win16SoundCall(ordinal, frame, result, words);
    if (Same(item.dll, "WIN87EM"))
        return Win16MathCall(ordinal, frame, result, words);
    if (Same(item.dll, "COMMDLG"))
        return Win16CommDlgCall(ordinal, frame, result, words);
    if (Same(item.dll, "MMSYSTEM"))
        return Win16MultimediaCall(ordinal, frame, result, words);
    if (Same(item.dll, "COMMCTRL"))
        return Win16CommonControlCall(ordinal, frame, result, words);
    if (Same(item.dll, "CONFIGMG"))
        return Win16ConfigManagerCall(result, words);
    if (Same(item.dll, "VER"))
        return Win16VersionCall(ordinal, frame, result, words);
    if (Same(item.dll, "TOOLHELP"))
        return Win16ToolhelpCall(ordinal, frame, result, words);
    if (Same(item.dll, "DCIMAN"))
    {
        // DCIMAN.DLL is ours (Route::Native16): the routines DCIMAN32's thunks
        // reach (src/x86_dciman16.cpp) take the same Pascal words a 16-bit
        // caller such as MSVIDEO's DrawDib pushes.
        words = DciMan16Words(ordinal);
        if (words == ~0u)
        {
            char text[160];
            snprintf(text, sizeof text, "Win16 DCIMAN ordinal %u is not ported yet", ordinal);
            Fail(text);
            return false;
        }
        result = DciMan16(ordinal, frame.stack + 4);
        return state != Failed;
    }
    char text[320];
    snprintf(text, sizeof text,
             "Win16 gate %s.%u (%s) has no native provider yet; SYSTEM, MOUSE and DISPLAY are imported "
             "only by USER.EXE in the tree",
             item.dll, ordinal, item.name);
    Fail(text);
    return false;
}

// ------------------------------------------------------------- KRNL386 gates

// Zero guest memory in chunks. Global blocks may exceed 64 KiB; do not
// allocate an equally large stack buffer.
void Application::Win16Clear(unsigned address, unsigned bytes)
{
    unsigned char zero[1024] = {};
    for (unsigned at = 0; at < bytes; at += sizeof zero)
        Store(address + at, zero, bytes - at < sizeof zero ? bytes - at : sizeof zero);
}
// The global heap on selectors. Rounding, the flag check and the handle shape
// follow KRNL386; see win98_ne_task.h for the places they come from.
unsigned Application::Win16GlobalAlloc(unsigned flags, unsigned size)
{
    auto &machine = *win16.state;
    // KRNL386 reports suspicious bits through warning E002, then continues
    // with the allocation. The warning is not an allocation failure.
    const unsigned rounded = Win98NeTask::RoundSize(size);
    if (rounded == Win98NeTask::SizeFailure)
        return 0;
    // GlobalAlloc(GMEM_MOVEABLE, 0) is legal and answers a handle without
    // memory; every other zero-length request is refused.
    if (!rounded && !(flags & Win98NeTask::Moveable))
        return 0;
    Win16Block *slot = Win16GlobalSlot();
    if (!slot)
        return 0;
    unsigned memory = 0, selector = 0;
    if (rounded)
    {
        memory = Allocate(rounded);
        if (!memory)
            return 0;
        if (flags & Win98NeTask::ZeroInit)
            Win16Clear(memory, rounded);
    }
    auto &block = *slot;
    block = Win16Block{};
    block.memory = memory;
    block.size = rounded;
    block.flags = flags;
    // GlobalAlloc(GMEM_MOVEABLE, 0) builds the descriptor of its block with
    // the access byte of the table and `and al, 7Fh`, so
    // the block is there without being present. That is the same state a
    // discarded block has, and GlobalFlags reports it as GMEM_DISCARDED.
    if (!rounded)
        block.flags |= Win98NeTask::Discarded;
    if (!Win16GlobalMap(block, memory, rounded))
    {
        block = Win16Block{};
        if (memory)
            Free(memory);
        return 0;
    }
    block.handle = Win98NeTask::HandleOf(block.selector, (flags & Win98NeTask::Moveable) != 0);
    return block.handle;
}
// Create or resize a global block's descriptor sequence. Each descriptor
// advances the base by 64 KiB and reduces the remaining limit by the same
// amount. Selectors differ by eight, as specified by __AHINCR.
// Shrinking releases surplus descriptors. Growing extends the sequence while
// retaining its first selector where possible, because callers use that
// selector as the block identity.
bool Application::Win16GlobalMap(Win16Block &block, unsigned memory, unsigned size)
{
    const unsigned tiles = Win98NeTask::TilesOf(size);
    if (!block.selector)
    {
        block.selector = tiles == 1 ? Win16Segment(memory, size ? size - 1 : 0, false, false)
                                    : selectors.CreateRun(tiles);
        if (!block.selector)
            return false;
    }
    else if (tiles > block.tiles)
    {
        if (!selectors.ExtendRun(block.selector, block.tiles, tiles))
        {
            const unsigned moved = selectors.CreateRun(tiles);
            if (!moved)
                return false;
            for (unsigned i = 0; i < block.tiles; ++i)
                selectors.Unmap((block.selector + i * 8) << 16);
            block.selector = moved;
        }
    }
    else
        for (unsigned i = tiles; i < block.tiles; ++i)
            selectors.Unmap((block.selector + i * 8) << 16);
    block.tiles = tiles;
    for (unsigned i = 0; i < tiles; ++i)
    {
        auto *entry = selectors.Find((block.selector + i * 8) << 16);
        if (!entry)
            return false;
        entry->base = memory + i * 65536;
        entry->limit = size ? size - 1 - i * 65536 : 0;
        entry->present = memory != 0;
        if (!Win16Descriptor(block.selector + i * 8))
            return false;
    }
    return true;
}
Application::Win16Block *Application::Win16GlobalBlock(unsigned handle)
{
    auto &machine = *win16.state;
    handle = Win98NeTask::NormalizeHandle(handle & 65535);
    for (unsigned i = 0; i < machine.global_capacity; ++i)
        if (machine.globals[i].handle && machine.globals[i].handle == handle)
            return &machine.globals[i];
    // A caller may hand in the selector instead of the handle. KRNL386 turns
    // one into the other with "or dl, 1" before it looks the block
    // up, so a moveable block answers for both shapes. TAPI.DLL does exactly
    // that : it locks its block, keeps the selector and asks
    // GlobalSize with it.
    // Segment-register loads may retain a different requested privilege
    // level. It selects the same LDT entry and therefore the same block.
    const unsigned selector = Win98NeTask::SelectorOf(handle);
    for (unsigned i = 0; i < machine.global_capacity; ++i)
        if (machine.globals[i].handle &&
            (machine.globals[i].selector & ~3u) == (selector & ~3u))
            return &machine.globals[i];
    return nullptr;
}
// Release a global-heap block. GlobalFree returns zero on success and the
// unrecognized handle otherwise; this helper reports whether it found the
// block.
bool Application::Win16GlobalFree(unsigned handle)
{
    auto *block = Win16GlobalBlock(handle);
    if (!block)
        return false;
    if (block->dos)
    {
        for (auto &dos : win16.state->dos_blocks)
            if (dos.base == block->memory)
                dos = Win16Machine::DosBlock{};
    }
    else if (block->memory)
        Free(block->memory);
    for (unsigned i = 0; i < block->tiles; ++i)
        selectors.Unmap((block->selector + i * 8) << 16);
    *block = Win16Block{};
    return true;
}
// A free slot of the global block table, growing it when every slot is taken.
// No caller holds a block across an allocation, so growing by reallocation is
// safe here.
Application::Win16Block *Application::Win16GlobalSlot()
{
    auto &machine = *win16.state;
    for (unsigned i = 0; i < machine.global_capacity; ++i)
        if (!machine.globals[i].handle)
            return &machine.globals[i];
    const unsigned capacity = machine.global_capacity ? machine.global_capacity * 2 : 256;
    auto *grown = new Win16Block[capacity];
    if (!grown)
        return nullptr;
    for (unsigned i = 0; i < machine.global_capacity; ++i)
        grown[i] = machine.globals[i];
    delete[] machine.globals;
    machine.globals = grown;
    const unsigned index = machine.global_capacity;
    machine.global_capacity = capacity;
    return &machine.globals[index];
}
// The global block KRNL386 makes for a segment of a module. The loader owns
// the memory; the block only lets GlobalHandle, GlobalSize, GlobalFlags and
// GlobalLock answer for the selector, the way they answer for a segment under
// Win98 (a fixed block, so the handle is the selector itself).
void Application::Win16RegisterSegment(unsigned selector, unsigned memory, unsigned size)
{
    Win16Block *slot = Win16GlobalSlot();
    if (!slot)
        return;
    slot->selector = selector;
    slot->memory = memory;
    slot->size = size;
    slot->flags = 0;
    slot->locks = 0;
    slot->segment = true;
    slot->handle = Win98NeTask::HandleOf(selector, false);
}
// LocalInit on the automatic data segment, through the ported local heap.
// The heap lives in guest memory, so the segment is staged here, the ported
// routine runs on the staging bytes and the result is written back.
bool Application::Win16LocalInit(unsigned selector, unsigned start, unsigned end)
{
    const unsigned base = Win16Linear(selector, 0), limit = Win16Limit(selector);
    if (!base)
        return false;
    struct Staging final : Win98LocalHeap::Environment
    {
        unsigned char bytes[65536] = {};
        bool failed = false;
        Staging() : Environment(bytes) {}
        unsigned Compact(unsigned, unsigned &block) override
        {
            block = 0;
            return 0;
        }
        unsigned Notify(unsigned, unsigned, unsigned) override { return 0; }
        void Error(unsigned) override { failed = true; }
    } staging;
    const unsigned bytes = limit + 1;
    if (!Read(base, staging.bytes, bytes))
        return false;
    if (!Win98LocalHeap::Initialize(staging, start, end) || staging.failed)
        return false;
    return Store(base, staging.bytes, bytes);
}
// GlobalReAlloc on the 16-bit global heap. A block of this port never moves,
// because its selector is its identity and a 16-bit program keeps the selector;
// growing therefore means a new allocation and a copy, and the descriptor of
// the old selector is pointed at the new bytes. Shrinking only shortens the
// limit. A request that does not fit answers zero, as KRNL386 does.
unsigned Application::Win16GlobalReAlloc(unsigned handle, unsigned size, unsigned flags)
{
    auto *block = Win16GlobalBlock(handle);
    if (!block)
        return 0;
    if (flags & Win98NeTask::Modify) // KRNL386; size is ignored.
    {
        block->flags = (block->flags & ~Win98NeTask::Discardable) |
                       (flags & Win98NeTask::Discardable);
        // The original can make a fixed handle movable.
        // Omitting MOVEABLE does not turn an existing movable block fixed.
        if (flags & Win98NeTask::Moveable)
        {
            block->flags |= Win98NeTask::Moveable;
            block->handle = Win98NeTask::HandleOf(block->selector, true);
        }
        return block->handle;
    }
    // A block of conventional memory cannot move (KRNL386 hands out its
    // real mode segment beside the selector).
    if (block->dos)
        return size <= block->size ? block->handle : 0;
    const unsigned rounded = Win98NeTask::RoundSize(size);
    if (rounded == Win98NeTask::SizeFailure)
        return 0;
    if (block->flags & Win98NeTask::Discarded)
    {
        // Reallocate a discarded block under the same handle. Apply the
        // requested flags with GMEM_MOVEABLE, clear the discarded state,
        // install the new selector and reset the lock count. The old contents
        // were freed when discarded.
        if (!rounded)
            return block->handle; // A zero-size request preserves the discarded handle.
        const unsigned memory = Allocate(rounded);
        if (!memory)
            return 0;
        if (flags & Win98NeTask::ZeroInit)
            Win16Clear(memory, rounded);
        block->memory = memory;
        block->size = rounded;
        block->locks = 0;
        block->flags = (block->flags & ~(Win98NeTask::Discarded | Win98NeTask::Discardable)) |
                       Win98NeTask::Moveable | (flags & Win98NeTask::Discardable);
        if (!Win16GlobalMap(*block, memory, rounded))
            return 0;
        block->handle = Win98NeTask::HandleOf(block->selector, true);
        return block->handle;
    }
    if (!rounded)
    {
        // KRNL386 rejects a locked block or a discard request
        // without GMEM_MOVEABLE; neither failure may release the old bytes.
        // gives the memory back and leaves the handle standing; the
        // descriptor of the block loses its presence bit, which is how the
        // block lookup recognises a discarded block afterwards.
        if (block->locks || !(flags & Win98NeTask::Moveable)) return 0;
        if (block->memory)
            Free(block->memory);
        block->memory = 0;
        block->size = 0;
        block->flags |= Win98NeTask::Discarded;
        if (!Win16GlobalMap(*block, 0, 0))
            return 0;
        return block->handle;
    }
    if (rounded == block->size) // nothing to do.
        return block->handle;
    if (rounded < block->size)
    {
        // Shrinking retains the bytes, reduces the limits and releases
        // descriptors no longer needed for blocks larger than 64 KiB.
        block->size = rounded;
        return Win16GlobalMap(*block, block->memory, rounded) ? block->handle : 0u;
    }
    // Grow without changing the block's selector identity. Locked blocks may
    // grow; their GlobalLock far pointers remain valid because the selector
    // is unchanged.
    const unsigned memory = Allocate(rounded);
    if (!memory)
        return 0;
    unsigned char page[1024] = {};
    for (unsigned at = 0; block->memory && at < block->size; at += sizeof page)
    {
        const unsigned part = block->size - at < sizeof page ? block->size - at : sizeof page;
        Read(block->memory + at, page, part);
        Store(memory + at, page, part);
    }
    // GMEM_ZEROINIT clears only the added bytes. Without it, the contents of
    // the added range are unspecified.
    if (flags & Win98NeTask::ZeroInit)
        Win16Clear(memory + block->size, rounded - block->size);
    if (block->memory)
        Free(block->memory);
    block->memory = memory;
    block->size = rounded;
    return Win16GlobalMap(*block, memory, rounded) ? block->handle : 0u;
}
// The handle of a module of our side. The range is above every selector the
// LDT hands out, so a handle of this range is never mistaken for a module
// database; the index names the entry of the table in dll_routes.h.
unsigned Application::Win16SystemModule(const char *name)
{
    auto &machine = *win16.state;
    for (unsigned i = 0; i < machine.system_module_count; ++i)
        if (Same(machine.system_modules[i], name))
            return Win16SystemHandle + i;
    if (machine.system_module_count >= 8)
        return 0;
    const unsigned index = machine.system_module_count++;
    CopyText(machine.system_modules[index], name, sizeof machine.system_modules[index]);
    return Win16SystemHandle + index;
}
const char *Application::Win16SystemModuleName(unsigned handle) const
{
    if (!win16.state || handle < Win16SystemHandle)
        return nullptr;
    const unsigned index = handle - Win16SystemHandle;
    return index < win16.state->system_module_count ? win16.state->system_modules[index] : nullptr;
}
// A natively provided 16-bit module has no loaded file. Report the
// corresponding Windows 98 system filename and extension.
bool Application::Win16SystemModulePath(unsigned handle, char *path, unsigned capacity) const
{
    const char *bare = Win16SystemModuleName(handle);
    if (!bare)
        return false;
    const char *file = Same(bare, "KERNEL") ? "KRNL386.EXE"
                       : Same(bare, "USER") ? "USER.EXE"
                       : Same(bare, "GDI")  ? "GDI.EXE"
                                            : nullptr;
    const bool driver = Same(bare, "KEYBOARD") || Same(bare, "SOUND") || Same(bare, "SYSTEM") ||
                        Same(bare, "MOUSE") || Same(bare, "DISPLAY");
    if (file)
        snprintf(path, capacity, "C:\\WINDOWS\\SYSTEM\\%s", file);
    else
        snprintf(path, capacity, "C:\\WINDOWS\\SYSTEM\\%s.%s", bare, driver ? "DRV" : "DLL");
    return true;
}
// The NE image of the tree file behind a module of our side, for its
// resources only: nothing of it is placed, relocated or entered. Read once
// per process; a file that is missing or no NE image answers null.
const Win98Ne::Image *Application::Win16SystemImage(unsigned handle)
{
    char path[260];
    if (!Win16SystemModulePath(handle, path, sizeof path))
        return nullptr;
    auto &machine = *win16.state;
    const unsigned index = handle - Win16SystemHandle;
    if (!machine.system_read[index])
    {
        machine.system_read[index] = true;
        char full[260];
        unsigned char *bytes = nullptr;
        unsigned length = 0, error = 0;
        if (!files.FullPath(path, full) && !Win16ReadFile(full, bytes, length))
        {
            if (machine.system_images[index].Parse(bytes, length, error))
                machine.system_files[index] = bytes;
            else
                delete[] bytes;
        }
    }
    return machine.system_images[index].Valid() ? &machine.system_images[index] : nullptr;
}
// LoadLibrary by name, the way a program asks for one at run time. The search
// order is the one of the loader, and the entry of the library runs at once,
// because the program expects its exports to answer from the next call on.
unsigned Application::Win16LoadNamedLibrary(const char *name)
{
    if (!win16.state)
        return 0;
    char bare[16];
    BareName(name, bare);
    // KRNL386 looks a loaded module up by the file it came from as well as
    // by its module name, so "KRNL386.EXE" finds KERNEL: SETUPX asks for it
    // by that name and takes exports of it with GetProcAddress.
    if (Same(bare, "KRNL386"))
        CopyText(bare, "KERNEL", sizeof bare);
    // A module whose 16-bit side is ours is not a file. It answers a handle of
    // the system range, and GetProcAddress on that handle makes a gate; loading
    // the original SHELL.DLL of the tree instead put a second, unreachable
    // implementation behind the name.
    if (DllRoutes::Win16System(bare))
        return Win16SystemModule(bare);
    if (Win16Module *found = Win16Find(bare))
    {
        ++found->references;
        return found->handle;
    }
    char full[260];
    unsigned char *bytes = nullptr;
    unsigned length = 0;
    // The file part of the name with its extension; a name with a path is
    // opened where it says (KRNL386 LoadModule), everything else is searched.
    const char *leaf = name;
    for (const char *p = name; *p; ++p)
        if (*p == '\\' || *p == '/' || *p == ':')
            leaf = p + 1;
    char file[64];
    CopyText(file, leaf, sizeof file);
    unsigned error = Win98Ne::FileNotFound;
    if (leaf != name && !files.FullPath(name, full))
        error = Win16ReadFile(full, bytes, length);
    if (error)
        error = Win16FindLibrary(file, full, bytes, length);
    if (error)
        return error < 32 ? error : 2u; // KRNL386 answers the error below 32
    Win16Module *module = Win16Place(full, bytes, length, true, error);
    if (!module)
        return error < 32 ? error : 11u;
    if (!Win16EnterLibrary(*module))
        return 0;
    return module->handle;
}
// FindResource, LoadResource, LockResource, FreeResource and SizeofResource on
// the NE resource table. A resource handle of this port is an index into the
// table of the task plus one; FindResource names the entry and LoadResource
// stages the bytes behind a descriptor of their own, which is what LockResource
// then hands out as a far pointer.
bool Application::Win16ResourceCall(unsigned ordinal, const Win16Frame &frame, unsigned &result,
                                    unsigned &words)
{
    auto &machine = *win16.state;
    words = ordinal == 60 ? 5u : ordinal == 61 || ordinal == 64 || ordinal == 65 ? 2u : 1u;
    result = ordinal == 64 ? 0xffffu : 0u;
    if (ordinal == 60)
    {
        Win16Module &module = *Win16ModuleOf(frame.Word(4));
        unsigned type = 0, id = 0;
        char type_label[256] = {}, label[256] = {};
        Win16ResourceName(frame, 0, type, type_label, sizeof type_label);
        Win16ResourceName(frame, 2, id, label, sizeof label);
        unsigned bytes = 0, offset = 0;
        // NE stores counted strings for both type and name. Self-extracting
        // archives use private types, e.g. ZIPRESOURCE, with AccessResource.
        if (!module.image.ResourceAt(type, id, label[0] ? label : nullptr, offset, bytes,
                                     type_label[0] ? type_label : nullptr))
        {
            last_error = 1813; // ERROR_RESOURCE_NAME_NOT_FOUND
            return true;
        }
        for (unsigned i = 0; i < machine.resource_count; ++i)
            if (machine.resources[i].offset == offset && machine.resources[i].bytes == bytes &&
                &machine.modules[machine.resources[i].module] == &module)
            {
                result = i + 1;
                return true;
            }
        if (machine.resource_count >= 64)
        {
            last_error = 8;
            return true;
        }
        auto &entry = machine.resources[machine.resource_count];
        entry = Win16Machine::Resource{};
        entry.type = type;
        entry.id = id;
        entry.offset = offset;
        entry.bytes = bytes;
        for (unsigned i = 0; i < machine.module_count; ++i)
            if (&machine.modules[i] == &module)
                entry.module = i;
        result = ++machine.resource_count;
        return true;
    }
    // LockResource and FreeResource take the global handle returned by
    // LoadResource. FindResource and SizeofResource take the resource entry
    // identifier. Try GlobalLock before resource-specific lookup.
    const unsigned given = frame.Word(0);
    unsigned index = given - 1;
    if (ordinal == 62 || ordinal == 63)
        for (unsigned i = 0; i < machine.resource_count; ++i)
            if (machine.resources[i].handle && machine.resources[i].handle == given)
            {
                index = i;
                break;
            }
    // A resource of a library FreeLibrary gave back names no module.
    if (index >= machine.resource_count || machine.resources[index].module >= machine.module_count)
        return true;
    auto &entry = machine.resources[index];
    if (ordinal == 64) // AccessResource: open the module at the resource's file offset.
    {
        unsigned handle = 0, status = 0, position = 0;
        unsigned error = files.Open(machine.modules[entry.module].path, 1, 1, 3, 0, handle, status);
        if (!error) {
            error = files.Seek(handle, entry.offset, 0, position);
            if (!error) result = Win16FileWord(handle);
            if (error || result == 0xffffu) files.Close(handle);
        }
        if (error) last_error = error;
        return true;
    }
    if (ordinal == 65)
    {
        result = entry.bytes;
        return true;
    }
    if (ordinal == 63)
    {
        result = 0; // the block stays; a resource of this port is not discarded
        return true;
    }
    if (!entry.handle)
    {
        // Store resource bytes in a global-heap block because Windows 98
        // LoadResource returns a global handle. Applications may lock it with
        // GlobalLock instead of LockResource.
        entry.handle = Win16GlobalAlloc(Win98NeTask::Moveable, entry.bytes ? entry.bytes : 1);
        auto *block = entry.handle ? Win16GlobalBlock(entry.handle) : nullptr;
        if (!block)
        {
            entry.handle = 0;
            last_error = 8;
            return true;
        }
        entry.memory = block->memory;
        entry.selector = block->selector;
        Store(entry.memory, machine.modules[entry.module].image.Data() + entry.offset, entry.bytes);
    }
    // LoadResource answers the global handle, LockResource the far pointer.
    result = ordinal == 61 ? entry.handle : entry.selector << 16;
    return true;
}
// The registry entries KRNL386 added for Win95. Segment three thunks them
// straight into the registry of the system, so their arguments have the Win32
// widths with a far pointer where Win32 has a flat one.
bool Application::Win16RegistryCall(unsigned ordinal, const Win16Frame &frame, unsigned &result,
                                    unsigned &words)
{
    words = ordinal == 223 ? 16u
            : ordinal == 220 || ordinal == 227 ? 2u
            : ordinal == 222 || ordinal == 219 ? 4u
            : ordinal == 217 || ordinal == 218 ? 6u
            : ordinal == 216 || ordinal == 224 ? 8u
            : ordinal == 221                   ? 10u
                                               : 12u;
    result = 0;
    switch (ordinal)
    {
    case 217:
    case 218:
    {
        const unsigned key = frame.Long(4), name = frame.Pointer(2), out = frame.Pointer(0);
        const unsigned scratch = Win16Scratch();
        if (!scratch)
            return false;
        const unsigned a[] = {key, name, scratch};
        result = Api(ordinal == 217 ? "RegOpenKeyA" : "RegCreateKeyA", a);
        if (!result && out)
            Write32(out, Read32(scratch));
        return true;
    }
    case 220:
    case 227:
    {
        const unsigned a[] = {frame.Long(0)};
        result = Api(ordinal == 220 ? "RegCloseKey" : "RegFlushKey", a);
        return true;
    }
    case 216: // RegEnumKey(key, index, name, size)
    {
        // KRNL386 ends in and reads the name at bp+0Ah:
        // the size is a DWORD like the other three, eight words in all. Seven
        // left two bytes of the caller's frame behind.
        const unsigned a[] = {frame.Long(6), frame.Long(4), frame.Pointer(2), frame.Long(0)};
        result = Api("RegEnumKeyA", a);
        return true;
    }
    case 219:
    {
        const unsigned a[] = {frame.Long(2), frame.Pointer(0)};
        result = Api("RegDeleteKeyA", a);
        return true;
    }
    case 221: // RegSetValue(key, subkey, type, data, size)
    {
        const unsigned a[] = {frame.Long(8), frame.Pointer(6), frame.Long(4), frame.Pointer(2),
                              frame.Long(0)};
        result = Api("RegSetValueA", a);
        return true;
    }
    case 222:
    {
        const unsigned a[] = {frame.Long(2), frame.Pointer(0)};
        result = Api("RegDeleteValueA", a);
        return true;
    }
    case 224: // RegQueryValue(key, subkey, buffer, size)
    {
        const unsigned key = frame.Long(6), name = frame.Pointer(4), buffer = frame.Pointer(2),
                       size = frame.Pointer(0);
        const unsigned scratch = Win16Scratch();
        if (!scratch)
            return false;
        if (size)
            Write32(scratch, Read32(size));
        const unsigned a[] = {key, name, buffer, size ? scratch : 0u};
        result = Api("RegQueryValueA", a);
        if (size)
            Write32(size, Read32(scratch));
        return true;
    }
    case 223: // RegEnumValue(key, index, name, name size, reserved, type, data, data size)
    {
        // Every argument is a DWORD or a far pointer, `retf 20h`. NETDI walks
        // the values of a key of Enum\Network with it.
        const unsigned key = frame.Long(14), index = frame.Long(12), name = frame.Pointer(10),
                       name_size = frame.Pointer(8), reserved = frame.Pointer(6), type = frame.Pointer(4),
                       data = frame.Pointer(2), size = frame.Pointer(0);
        const unsigned scratch = Win16Scratch();
        if (!scratch)
            return false;
        Write32(scratch, name_size ? Read32(name_size) : 0u);
        if (type)
            Write32(scratch + 4, Read32(type));
        if (size)
            Write32(scratch + 8, Read32(size));
        const unsigned a[] = {key, index, name, name_size ? scratch : 0u, reserved ? scratch + 16 : 0u,
                              type ? scratch + 4 : 0u, data, size ? scratch + 8 : 0u};
        result = Api("RegEnumValueA", a);
        if (name_size)
            Write32(name_size, Read32(scratch));
        if (type)
            Write32(type, Read32(scratch + 4));
        if (size)
            Write32(size, Read32(scratch + 8));
        return true;
    }
    case 225: // RegQueryValueEx(key, name, reserved, type, data, size)
    case 226: // RegSetValueEx(key, name, reserved, type, data, size)
    {
        const unsigned key = frame.Long(10), name = frame.Pointer(8), reserved = frame.Pointer(6),
                       type = frame.Pointer(4), data = frame.Pointer(2), size = frame.Pointer(0);
        const unsigned scratch = Win16Scratch();
        if (!scratch)
            return false;
        if (ordinal == 226)
        {
            const unsigned a[] = {key, name, 0, frame.Long(4), data, frame.Long(0)};
            result = Api("RegSetValueExA", a);
            return true;
        }
        if (type)
            Write32(scratch, Read32(type));
        if (size)
            Write32(scratch + 8, Read32(size));
        const unsigned a[] = {key, name, reserved ? scratch + 16 : 0u, type ? scratch : 0u, data,
                              size ? scratch + 8 : 0u};
        result = Api("RegQueryValueExA", a);
        if (type)
            Write32(type, Read32(scratch));
        if (size)
            Write32(size, Read32(scratch + 8));
        return true;
    }
    default:
        break;
    }
    return true;
}
bool Application::Win16KernelCall(unsigned ordinal, const Win16Frame &frame, unsigned &result,
                                  unsigned &words)
{
    auto &machine = *win16.state;
    auto &module = machine.modules[0];
    result = 0;
    switch (ordinal)
    {
    case 149: // GetVersionEx(info): KRNL386 is entry 38h/4 of its
              // thunk table to KERNEL32 and hands the far pointer over as a
              // flat one, `retf 4`. SETUPX asks for it by name after
              // LoadLibrary("KRNL386.EXE") and takes its presence as the
              // sign of Windows 95.
    {
        words = 2;
        const unsigned a[] = {frame.Pointer(0)};
        result = Api("GetVersionExA", a);
        return state != Failed;
    }
    case 4: // LocalInit(segment, start, end)
    {
        words = 3;
        const unsigned segment = frame.Word(2), start = frame.Word(1);
        unsigned end = frame.Word(0);
        const unsigned selector = segment ? segment : Register(Reg::Ds);
        unsigned first = start;
        if (!start)
        {
            // with no start the heap is put at the end of the
            // segment and the argument is read as its size. The end of an
            // automatic data segment is the size its header asks for, not
            // the 64 KiB placed behind the descriptor: LIBENTRY of MSVIDEO.DLL
            // asks for 80h bytes this way, and at the very top of the segment
            // that heap could never grow (DrawDibOpen wants 0A2Ch).
            auto &machine = *win16.state;
            unsigned size = Win16Limit(selector) + 1;
            for (unsigned i = 0; i < machine.module_count; ++i)
                if (machine.modules[i].instance == selector && machine.modules[i].instance_bytes)
                    size = machine.modules[i].instance_bytes;
            const unsigned last = size > 65536 ? 65535u : size - 1;
            first = last - end;
            end = last;
        }
        result = Win16LocalInit(selector, first, end) ? 1 : 0;
        return true;
    }
    case 1:   // FatalExit(code)
    case 137: // FatalAppExit(action, text)
    {
        // The shared body of KERNEL32 (x86_kernel_fibers_debug.inc): the
        // system error box with the text, unless SEM_NOGPFAULTERRORBOX
        // (KERNEL.107) is set, then ExitProcess of this task only. The gate
        // used to stop the whole run. FatalExit takes the same box
        // without a text; the debugger break in front of it has no debugger
        // to reach here, so it is left out.
        words = ordinal == 1 ? 1u : 3u;
        FatalAppExit(ordinal == 137 ? frame.Pointer(0) : 0);
        if (state == Failed)
            return false;
        result = 0;
        return true;
    }
    case 5: // LocalAlloc(flags, bytes)
    case 6: // LocalReAlloc(handle, bytes, flags)
    case 7: // LocalFree(handle)
    case 8: // LocalLock(handle)
    case 9: // LocalUnlock(handle)
        return Win16LocalCall(ordinal, frame, result, words);
    case 15: // GlobalAlloc(flags, size)
        words = 3;
        result = Win16GlobalAlloc(frame.Word(2), frame.Long(0));
        return true;
    case 17: // GlobalFree(handle)
        words = 1;
        result = Win16GlobalFree(frame.Word(0)) ? 0 : frame.Word(0);
        return true;
    case 18: // GlobalLock(handle): the selector of the block, offset zero.
    case 23: // LockSegment behaves the same for a segment handle.
    {
        words = 1;
        // Handle 0xFFFF means the caller's own data segment.
        const unsigned given = frame.Word(0);
        const unsigned handle = given == 0xffff ? Register(Reg::Ds) : given;
        auto *block = Win16GlobalBlock(handle);
        if (block)
        {
            // Check descriptor presence before incrementing the lock count.
            // Discarded blocks and empty GMEM_MOVEABLE blocks return null
            // without becoming locked.
            if (!block->memory)
            {
                result = 0;
                return true;
            }
            if (block->locks != 255)
                ++block->locks;
            result = block->selector << 16;
            return true;
        }
        // A present selector that is not a global block, such as a module
        // segment, locks to itself. Invalid or nonpresent descriptors return
        // a null far pointer instead.
        const unsigned selector = Win98NeTask::SelectorOf(handle);
        result = selectors.At(selector) ? selector << 16 : 0;
        return true;
    }
    // GlobalWire increments the block lock count and returns selector|1 with
    // offset zero. Native blocks do not move. GlobalUnWire decrements the
    // count and returns FFFFh when it reaches zero, otherwise zero.
    case 111:
    {
        words = 1;
        auto *block = Win16GlobalBlock(frame.Word(0));
        if (!block || !block->memory)
        {
            result = 0;
            return true;
        }
        if (block->locks != 255)
            ++block->locks;
        result = block->selector << 16;
        return true;
    }
    case 112:
    {
        words = 1;
        auto *block = Win16GlobalBlock(frame.Word(0));
        if (!block)
        {
            result = 0;
            return true;
        }
        if (block->locks)
            --block->locks;
        result = block->locks ? 0 : 0xffff;
        return true;
    }
    case 19: // GlobalUnlock(handle): the remaining lock count.
    case 24:
    {
        words = 1;
        auto *block = Win16GlobalBlock(frame.Word(0));
        if (block && block->locks)
            --block->locks;
        result = block ? block->locks : 0;
        return true;
    }
    case 20: // GlobalSize(handle)
    {
        words = 1;
        auto *block = Win16GlobalBlock(frame.Word(0));
        result = block ? block->size : 0;
        return true;
    }
    case 21:  // GlobalHandle(selector): DX:AX is handle:selector.
    case 159: // GlobalHandleNoRIP : the same without the debug RIP.
    {
        words = 1;
        auto *block = Win16GlobalBlock(frame.Word(0));
        result = block ? block->handle << 16 | block->selector : 0;
        return true;
    }
    case 22: // GlobalFlags(handle): lock count in the low byte, the flag byte
             // of the block in the high byte ( `xchg ch, cl`). The
             // lookup adds bit 40h for a block whose descriptor is
             // not present, and that is GMEM_DISCARDED.
    {
        words = 1;
        auto *block = Win16GlobalBlock(frame.Word(0));
        result = block ? (block->locks |
                          (block->flags & (Win98NeTask::Discardable | Win98NeTask::Discarded)))
                       : 0;
        return true;
    }
    case 29: // Yield
    {
        // The scheduler rule of the Win16 federation: a task gives the
        // processor up here, and the host runs whoever else is runnable. It is
        // the same wait a 32-bit Sleep(0) takes, so no lock of ours is held
        // over it and no other task has to ask for one.
        words = 0;
        // KRNL386 jumps to the yield of USER (the far pointer at
        // cs:12h) when the task has a message queue, and UserYield answers the
        // messages other tasks sent it before the task gives the processor
        // up. A 16-bit driver's worker thread that sends to the window of a
        // task spinning in Yield needs exactly that.
        ProcessSentMessages();
        const unsigned a[] = {0};
        Api("Sleep", a);
        result = 0;
        return true;
    }
    case 30: // WaitEvent(task): no event is pending for a task that has not
             // asked USER for anything yet.
        words = 1;
        result = 0;
        return true;
    case 36: // GetCurrentTask
    {
        words = 0;
        // A thread of mmThreadCreate is a task of its own (src/win16_mmthread.cpp).
        unsigned task = machine.task;
        if (machine.hosted)
            for (const auto &record : machine.hosted_threads)
                if (record.thread == ThreadId() && record.task)
                    task = record.task;
        result = task << 16 | task;
        return true;
    }
    case 37: // GetCurrentPDB
        words = 0;
        result = machine.prefix;
        return true;
    case 55: // Catch(buffer): the setjmp of the 16-bit side. Windows 98 writes
             // the far return address of its own frame at 0 and 2, its stack
             // pointer at 4, the caller's BP at 6, SI at 8, DI at 0Ah, DS at
             // 0Ch and SS at 10h, and answers zero.
    {
        words = 2;
        const unsigned buffer = frame.Pointer(0);
        result = 0;
        if (!buffer)
            return true;
        // Catch saves a stack position eight bytes below its entry. Throw
        // restores those eight bytes when resuming.
        const unsigned entry = Register(Reg::Esp) & 0xffff;
        Win16Word(buffer + 0, frame.ip);
        Win16Word(buffer + 2, frame.cs);
        Win16Word(buffer + 4, (entry - 8) & 0xffff);
        Win16Word(buffer + 6, Register(Reg::Ebp) & 0xffff);
        Win16Word(buffer + 8, Register(Reg::Esi) & 0xffff);
        Win16Word(buffer + 10, Register(Reg::Edi) & 0xffff);
        Win16Word(buffer + 12, Register(Reg::Ds));
        Win16Word(buffer + 16, Register(Reg::Ss));
        return true;
    }
    case 56: // Throw(buffer, value): the longjmp. Windows 98 loads SS and SP from
             // the buffer, puts BP, SI, DI and DS back, writes the saved far
             // address into the frame it just rebuilt and leaves through the
             // epilogue of Catch with the value in AX.
    {
        words = 3;
        const unsigned buffer = frame.Pointer(1), value = frame.Word(0);
        result = value;
        if (!buffer)
            return true;
        machine.throw_pending = 1;
        machine.throw_ip = Win16Word(buffer + 0);
        machine.throw_cs = Win16Word(buffer + 2);
        // The eight bytes the epilogue of Catch removes: three pops, the
        // leave and the `retf 4` of its own two argument words.
        machine.throw_sp = (Win16Word(buffer + 4) + 8) & 0xffff;
        machine.throw_ss = Win16Word(buffer + 16);
        Register(Reg::Ebp, Win16Word(buffer + 6));
        Register(Reg::Esi, Win16Word(buffer + 8));
        Register(Reg::Edi, Win16Word(buffer + 10));
        Register(Reg::Ds, Win16Word(buffer + 12));
        return true;
    }
    case 47: // GetModuleHandle(name)
    {
        words = 2;
        const unsigned pointer = frame.Pointer(0);
        const char *name = pointer ? ReadString(pointer) : nullptr;
        // A handle instead of a string: the high word is zero then.
        if (!frame.Word(1))
        {
            auto *block = Win16GlobalBlock(frame.Word(0));
            result = block ? block->handle : frame.Word(0);
            return true;
        }
        Win16Module *found = name ? Win16Find(name) : nullptr;
        if (found)
        {
            result = found->handle;
            return true;
        }
        // Native 16-bit system modules have no file-table entry. Once loaded,
        // GetModuleHandle must return the same system handle as LoadLibrary;
        // Visual Basic checks this after loading.
        result = 0;
        if (name)
        {
            char bare[16];
            BareName(name, bare);
            for (unsigned i = 0; i < machine.system_module_count; ++i)
                if (Same(machine.system_modules[i], bare))
                    result = Win16SystemHandle + i;
            // The modules Win98 loads while it starts are there before any
            // program asks: KRNL386, USER, GDI and the system drivers, and
            // MMSYSTEM, which USER loads for its sounds. MSVIDEO asks for
            // KERNEL by name to find its generic thunks.
            static const char *const resident[] = {"KERNEL", "USER",  "GDI",   "KEYBOARD",
                                                   "SYSTEM", "MOUSE", "DISPLAY", "MMSYSTEM"};
            if (!result)
                for (const char *module : resident)
                    if (Same(bare, module))
                        result = Win16SystemModule(module);
            // A 16-bit DLL of our side that a flat thunk of this process
            // connected to is loaded: ThunkConnect32 loads it under Win98.
            // COMCTL32 connects COMMCTRL that way, and SETUPX finds the
            // 16-bit PropertySheet with GetModuleHandle("COMMCTRL") and
            // GetProcAddress.
            for (unsigned i = 0; !result && i < thunk_connection_count; ++i)
            {
                char connected[16];
                BareName(thunk_connections[i].dll16, connected);
                if (thunk_connections[i].connected && !thunk_connections[i].hosted && Same(connected, bare) &&
                    DllRoutes::Win16System(bare))
                    result = Win16SystemModule(bare);
            }
        }
        return true;
    }
    case 48: // GetModuleUsage(module): module reference count.
             // The module database holds a reference count incremented by
             // LoadModule. Return one for a module loaded once and zero for
             // an unknown handle.
    {
        words = 1;
        const unsigned handle = frame.Word(0);
        result = 0;
        if (handle >= Win16SystemHandle)
        {
            // Native 16-bit providers remain available throughout program
            // execution and are never unloaded from a file.
            result = Win16SystemModuleName(handle) ? 1u : 0u;
            return true;
        }
        for (unsigned i = 0; i < machine.module_count; ++i)
            if (machine.modules[i].handle == handle)
                result = machine.modules[i].references;
        // An instance handle is also accepted; resolve it to its module
        // first.
        if (!result)
            for (unsigned i = 0; i < machine.module_count; ++i)
                if (machine.modules[i].instance == handle)
                    result = machine.modules[i].references;
        return true;
    }
    case 49: // GetModuleFileName(module, buffer, size)
    {
        words = 4;
        const unsigned handle = frame.Word(3), buffer = frame.Pointer(1), size = frame.Word(0);
        // Resolve module, instance or task handles to their module. An
        // unknown handle or zero buffer size returns zero without touching
        // the buffer or last error.
        const char *path = nullptr;
        for (unsigned i = 0; i < machine.module_count; ++i)
            if (machine.modules[i].handle == handle || machine.modules[i].instance == handle)
                path = machine.modules[i].path;
        if (!path && handle && handle == machine.task)
            path = module.path;
        char system[260];
        if (!path && Win16SystemModulePath(handle, system, sizeof system))
            path = system;
        // Unknown module handles leave the output buffer unchanged.
        result = path && buffer ? PutString(buffer, path, size) : 0;
        return true;
    }
    case 50: // GetProcAddress(module, name)
    {
        // is `retf 6`: the module handle is one word and the name a
        // far pointer, and Windows 98 reads the handle at bp+0Ah.
        words = 3;
        const unsigned handle = frame.Word(2), pointer = frame.Pointer(0);
        if (const char *system = Win16SystemModuleName(handle))
        {
            // An entry of our side: the address is a gate, the same one the
            // loader would have written into a relocation.
            const char *symbol = pointer && frame.Word(1) ? ReadString(pointer) : "";
            const unsigned ordinal_asked = frame.Word(1) ? 0u : frame.Word(0);
            // Unknown export names return a null pointer without changing
            // last error. Do not create an ordinal-zero gate that merely
            // postpones the failure until invocation.
            if (ordinal_asked == 0 && !Win16SystemOrdinal(system, symbol))
            {
                if (log)
                {
                    char text[320];
                    snprintf(text, sizeof text, "MISSING WIN16 EXPORT %s!%s", system, symbol);
                    log(text);
                }
                result = 0;
                return true;
            }
            unsigned error = 0;
            result = Win16Import(system, symbol, ordinal_asked, error);
            return state != Failed;
        }
        Win16Module *found = nullptr;
        for (unsigned i = 0; i < machine.module_count; ++i)
            if (machine.modules[i].handle == handle)
                found = &machine.modules[i];
        if (!found)
        {
            result = 0;
            return true;
        }
        unsigned number = frame.Word(1) ? 0 : frame.Word(0);
        if (pointer && frame.Word(1))
            number = Win16Ordinal(*found, ReadString(pointer));
        Win98Ne::Entry entry;
        result = number && found->image.EntryAt(number, entry) && found->selector[entry.segment]
                     ? found->selector[entry.segment] << 16 | entry.offset
                     : 0;
        return true;
    }
    // Atom gates adapt arguments to the shared implementation in
    // src/x86_atoms.cpp. InitAtomTable, DeleteAtom and GetAtomHandle take one
    // word; AddAtom and FindAtom take a far pointer; GetAtomName takes eight
    // bytes. A pointer with selector zero represents an integer atom whose
    // value is its offset.
    case 68: // InitAtomTable(size)
    {
        words = 1;
        const unsigned a[] = {frame.Word(0)};
        result = Api("InitAtomTable", a) & 0xffff;
        return true;
    }
    case 69: // FindAtom(name)
    case 70: // AddAtom(name)
    {
        words = 2;
        const unsigned a[] = {frame.Word(1) ? frame.Pointer(0) : frame.Word(0)};
        result = Api(ordinal == 70 ? "AddAtomA" : "FindAtomA", a) & 0xffff;
        return true;
    }
    case 71: // DeleteAtom(atom)
    {
        words = 1;
        const unsigned a[] = {frame.Word(0)};
        result = Api("DeleteAtom", a) & 0xffff;
        return true;
    }
    case 72: // GetAtomName(atom, buffer, size)
    {
        words = 4;
        const unsigned a[] = {frame.Word(3), frame.Pointer(1), frame.Word(0)};
        result = Api("GetAtomNameA", a) & 0xffff;
        return true;
    }
    case 73: // GetAtomHandle(atom): shift string atoms left by two bits.
             // Return zero for integer atoms. The native atom table is shared
             // rather than stored in the task's data segment, so this value
             // identifies its node rather than a pointer in DS.
        words = 1;
        result = frame.Word(0) >= 0xc000 ? (frame.Word(0) << 2) & 0xffff : 0u;
        return true;
    case 91: // InitTask
    {
        words = 0;
        // The pops of the return address are already done by
        // the transition, so the stack pointer of the caller is the one after
        // them; the three instance words follow from it and from the stack
        // size the entry contract left in BX.
        const unsigned instance = machine.instance_handle;
        const unsigned base = Win16Linear(instance, 0);
        const unsigned sp = (Register(Reg::Esp) & 0xffff) + 4;
        const unsigned stack_size = Register(Reg::Ebx) & 0xffff;
        Win16Word(base + Win98NeTask::InstanceStackMin, sp);
        Win16Word(base + Win98NeTask::InstanceStackBottom, sp);
        Win16Word(base + Win98NeTask::InstanceStackTop,
                  (sp - stack_size + Win98NeTask::InstanceStackMargin) & 0xffff);
        const unsigned heap = Register(Reg::Ecx) & 0xffff;
        if (heap)
        {
            // The end the header asks for, not the descriptor limit: the
            // segment carries its full 64 KiB so the heap can grow into it.
            unsigned size = Win16Limit(instance) + 1;
            for (unsigned i = 0; i < machine.module_count; ++i)
                if (machine.modules[i].instance == instance && machine.modules[i].instance_bytes)
                    size = machine.modules[i].instance_bytes;
            const unsigned last = size > 65536 ? 65535u : size - 1;
            if (!Win16LocalInit(instance, last - heap, last))
            {
                Fail("Win16 LocalInit on the automatic data segment failed");
                return false;
            }
        }
        // The register answers of InitTask: AX nonzero, CX the stack limit,
        // DX the show command, SI the previous instance, DI this instance and
        // ES:BX the command line in the program segment prefix.
        Register(Reg::Ecx, stack_size);
        Register(Reg::Esi, 0);
        Register(Reg::Edi, instance);
        // with a count of zero BX stays at 80h; otherwise BX is
        // the first character after the blanks and tabs, and the carriage
        // return that ends the tail becomes a zero, so ES:BX is the ASCIIZ
        // lpCmdLine of WinMain. RUNDLL.EXE takes its last word as a path, and
        // SETUPX could not open "shell.inf\r".
        {
            const unsigned tail = machine.command_prefix + Win98NeTask::ProgramPrefixCommand;
            unsigned char bytes[128] = {};
            Read(tail, bytes, sizeof bytes);
            unsigned first = 0;
            if (bytes[0])
            {
                unsigned at = 1;
                while (at < 128 && (bytes[at] == ' ' || bytes[at] == '\t'))
                    ++at;
                first = at;
                while (at < 128 && bytes[at] && bytes[at] != 13)
                    ++at;
                if (at < 128 && bytes[at] == 13)
                {
                    const unsigned char zero = 0;
                    Store(tail + at, &zero, 1);
                }
            }
            Register(Reg::Ebx, Win98NeTask::ProgramPrefixCommand + first);
        }
        Register(Reg::Es, machine.prefix);
        // DX is the show command, and the transition writes DX out of the high
        // half of the answer, so it travels there. Writing DX here instead was
        // lost again on the way out, and every program started hidden.
        result = 1 | ((startup_info[12] ? startup_info[12] : 1u) & 0xffff) << 16;
        return true;
    }
    case 51: // MakeProcInstance(procedure, instance)
    {
        // Three words: the far procedure and the instance. The call site of
        // WINVER pushes exactly those.
        words = 3;
        const unsigned procedure = frame.Long(1), instance = frame.Word(0);
        if (!procedure)
        {
            result = 0;
            return true;
        }
        // The thunk KRNL386 builds: the instance into AX and a far jump to the
        // procedure, eight bytes. A 16-bit procedure reached through it keeps
        // the data segment of the instance, because its prologue moves AX into
        // DS. The thunks of one task live in one block with one code
        // descriptor, allocated the first time one is asked for.
        if (!machine.thunk_block)
        {
            unsigned char empty[ProcedureThunkBytes] = {};
            if (!Store(ProcedureThunkBase, empty, sizeof empty) ||
                !Protect(ProcedureThunkBase, ProcedureThunkBytes, 7))
            {
                Fail("Win16 procedure thunk page could not be placed");
                return false;
            }
            machine.thunk_block = ProcedureThunkBase;
            machine.thunk_selector =
                Win16Segment(machine.thunk_block, ProcedureThunkBytes - 1, true, false);
            if (!machine.thunk_selector)
            {
                result = 0;
                return true;
            }
        }
        for (unsigned i = 0; i < machine.thunk_count; ++i)
            if (machine.thunks[i].uses && machine.thunks[i].procedure == procedure &&
                machine.thunks[i].instance == instance)
            {
                ++machine.thunks[i].uses;
                result = machine.thunk_selector << 16 | (i * 8);
                return true;
            }
        // A slot FreeProcInstance gave back is taken first.
        unsigned index = machine.thunk_count;
        for (unsigned i = 0; i < machine.thunk_count; ++i)
            if (!machine.thunks[i].uses)
            {
                index = i;
                break;
            }
        if (index == machine.thunk_count)
        {
            if (machine.thunk_count * 8 + 8 > ProcedureThunkBytes ||
                machine.thunk_count >= sizeof machine.thunks / sizeof machine.thunks[0])
            {
                result = 0;
                return true;
            }
            ++machine.thunk_count;
        }
        const unsigned char code[8] = {0xb8, (unsigned char)instance, (unsigned char)(instance >> 8),
                                       0xea, (unsigned char)procedure, (unsigned char)(procedure >> 8),
                                       (unsigned char)(procedure >> 16), (unsigned char)(procedure >> 24)};
        Store(machine.thunk_block + index * 8, code, sizeof code);
        // A slot FreeProcInstance gave back held the jump to another
        // procedure, and the engine keeps its translation of those eight
        // bytes until told: DEFRAG's error box ran the procedure of the
        // drive dialog before it.
        cpu->InvalidateCode(machine.thunk_block + index * 8, sizeof code);
        machine.thunks[index] = {procedure, instance, 1};
        result = machine.thunk_selector << 16 | (index * 8);
        return true;
    }
    case 52: // FreeProcInstance(thunk)
    {
        // KRNL386 gives the thunk back; the slot is free for the next
        // MakeProcInstance once every caller that shares it has freed it. A pointer that is no thunk of this task changes nothing.
        words = 2;
        result = 0;
        const unsigned thunk = frame.Long(0);
        if (machine.thunk_block && (thunk >> 16) == machine.thunk_selector && !((thunk & 0xffff) % 8))
        {
            const unsigned index = (thunk & 0xffff) / 8;
            if (index < machine.thunk_count && machine.thunks[index].uses && !--machine.thunks[index].uses)
                machine.thunks[index].procedure = machine.thunks[index].instance = 0;
        }
        return true;
    }
    case 101: // NoHookDosCall: DOS3Call past the hooks of INT 21h; nobody hooks it here.
    case 102: // DOS3Call: the file interrupt behind the 16-bit runtime.
    {
        // KRNL386 hands every register back as DOS left it. The gate exit
        // writes DX from the high word of the answer, so a function that
        // does not return DX must see the caller's DX there: the C runtime's
        // _dos_findfirst keeps the offset of its find_t in DX across the 2Fh
        // call and sets the transfer address with it afterwards.
        words = 0;
        const unsigned in_ax = Register(Reg::Eax) & 0xffff, in_bx = Register(Reg::Ebx) & 0xffff,
                       in_cx = Register(Reg::Ecx) & 0xffff, in_dx = Register(Reg::Edx) & 0xffff;
        const bool answered = Win16DosCall(result);
        result = (result & 0xffff) | (Register(Reg::Edx) & 0xffff) << 16;
        if (log && trace_calls)
        {
            char text[160];
            snprintf(text, sizeof text, "WIN16DOS ax=%04x bx=%04x cx=%04x dx=%04x -> ax=%04x carry=%u", in_ax, in_bx,
                     in_cx, in_dx, result & 0xffff, Register(Reg::Eflags) & 1);
            log(text);
        }
        return answered;
    }
    case 57:  // GetProfileInt(section, key, default)
    case 58:  // GetProfileString(section, key, default, buffer, size)
    case 127: // GetPrivateProfileInt(section, key, default, file)
    case 128: // GetPrivateProfileString(section, key, default, buffer, size, file)
    case 129: // WritePrivateProfileString(section, key, value, file)
    {
        const bool private_file = ordinal >= 127;
        words = ordinal == 57 ? 5u : ordinal == 58 ? 9u : ordinal == 127 ? 7u : ordinal == 128 ? 11u : 8u;
        auto pointer_at = [&](unsigned start) { return frame.Pointer(words - 2 - start); };
        if (ordinal == 57 || ordinal == 127)
        {
            const unsigned a[] = {pointer_at(0), pointer_at(2), frame.Word(private_file ? 2 : 0),
                                  private_file ? pointer_at(5) : 0};
            result = Api(private_file ? "GetPrivateProfileIntA" : "GetProfileIntA", a);
            return true;
        }
        if (ordinal == 129)
        {
            const unsigned a[] = {pointer_at(0), pointer_at(2), pointer_at(4), pointer_at(6)};
            result = Api("WritePrivateProfileStringA", a);
            return true;
        }
        const unsigned a[] = {pointer_at(0), pointer_at(2), pointer_at(4), pointer_at(6),
                              frame.Word(private_file ? 2 : 0), private_file ? pointer_at(9) : 0};
        result = Api(private_file ? "GetPrivateProfileStringA" : "GetProfileStringA", a);
        return true;
    }
    case 74: // OpenFile(name, buffer, style)
    case 81: // _lclose(handle)
    case 82: // _lread(handle, buffer, bytes)
    case 83: // _lcreat(name, attributes)
    case 84: // _llseek(handle, offset, origin)
    case 85: // _lopen(name, mode)
    case 86: // _lwrite(handle, buffer, bytes)
        return Win16FileCall(ordinal, frame, result, words);
    case 88: // lstrcpy(destination, source)
    case 89: // lstrcat(destination, source)
    {
        // The shared bodies copy as far as the text goes; the gate kept a
        // 1024-byte stack copy and cut longer texts silently. The
        // far pointer of the destination stays the gate's answer.
        words = 4;
        const unsigned target = frame.Pointer(2), source = frame.Pointer(0);
        if (!target)
        {
            result = 0;
            return true;
        }
        const unsigned a[] = {target, source};
        Api(ordinal == 88 ? "lstrcpyA" : "lstrcatA", a);
        result = frame.Long(2);
        return true;
    }
    case 90: // lstrlen(text)
    {
        words = 2;
        const unsigned at = frame.Pointer(0);
        result = at ? (unsigned)strlen(ReadString(at)) : 0;
        return true;
    }
    case 131: // GetDOSEnvironment: Windows 98 loads DS from the current program
              // segment prefix, reads the word at 2Ch into DX and clears AX,
              // so the answer is the environment selector with offset zero.
        words = 0;
        result = machine.environment_selector << 16;
        return true;
    case 134: // GetWindowsDirectory(buffer, size)
    case 135: // GetSystemDirectory(buffer, size)
    {
        // Both paths and the Win98 rule for a buffer that is too small, which
        // answers the needed length and writes nothing, belong to the shared
        // entry. Copying the literal here truncated instead.
        words = 3;
        const unsigned a[] = {frame.Pointer(1), frame.Word(0)};
        result = Api(ordinal == 134 ? "GetWindowsDirectoryA" : "GetSystemDirectoryA", a);
        return true;
    }
    case 152: // GetNumTasks
        words = 0;
        result = Win16TaskCount();
        return true;
    // Task last-error access: GetLastError takes no arguments and returns a
    // DWORD in DX:AX; SetLastError takes two words.
    case 148: // GetLastError
        words = 0;
        result = last_error;
        return true;
    case 147: // SetLastError(error)
        words = 2;
        last_error = frame.Long(0);
        result = 0;
        return true;
    case 411: // GetCurrentDirectory(length, buffer)
    case 412: // SetCurrentDirectory(path)
    {
        // These are the Win32-shaped entries KRNL386 added for Win95: they
        // thunk straight into KERNEL32, so the arguments have the Win32 widths and the
        // 16-bit caller pushes a DWORD where Win32 has one.
        words = ordinal == 411 ? 4u : 2u;
        if (ordinal == 412)
        {
            const unsigned path = frame.Pointer(0);
            const unsigned a[] = {path};
            result = path ? Api("SetCurrentDirectoryA", a) : 0;
            return true;
        }
        const unsigned buffer = frame.Pointer(0), length = frame.Long(2);
        const unsigned a[] = {length, buffer};
        result = Api("GetCurrentDirectoryA", a);
        return true;
    }
    case 166: // WinExec(command, show)
    {
        words = 3;
        const unsigned command = frame.Pointer(1);
        const unsigned a[] = {command, frame.Word(0)};
        result = command ? Api("WinExec", a) : 0;
        if (log && trace_calls)
        {
            char text[320];
            snprintf(text, sizeof text, "WIN16 WinExec %s show=%u result=%u error=%u",
                     command ? ReadString(command) : "", frame.Word(0), result, last_error);
            log(text);
        }
        return true;
    }
    // ------------------------------------------------- the breadth of part four
    case 11: // LocalHandle(pointer), KRNL386.
    case 12: // LocalFlags(handle), KRNL386.
        words = 1;
        result = Win16LocalOperation(ordinal == 11 ? Win16LocalHandle : Win16LocalFlags, frame.Word(0));
        return true;
    case 10: // LocalSize(handle)
        words = 1;
        result = Win16LocalOperation(Win16LocalSizeOf, frame.Word(0));
        return true;
    case 13: // LocalCompact(minimum): rounds the request, asks
             // the compactor for its largest free block and Windows 98 takes the
             // six bytes of a movable header off the answer. The thunk
             // takes one argument word.
        words = 1;
        result = Win16LocalOperation(Win16LocalLargestFree, frame.Word(0));
        return true;
    case 121: // LocalShrink(segment, size): Windows 98 answers the size of the
              // heap after shrinking it. The segment of this port never
              // shrinks, so the answer is the size the heap has, which is the
              // word at heap+38 that wrote. The thunk is
              // `retf 4`, so it carries two words.
        words = 2;
        // switches to the segment of the first argument before it
        // works. Only the heap of the caller's own data segment is staged
        // here, so another segment stops the run instead of answering for the
        // wrong heap.
        if (frame.Word(1) && frame.Word(1) != Register(Reg::Ds))
        {
            Fail("Win16 LocalShrink on a segment other than the caller's data segment is not ported yet");
            return false;
        }
        result = Win16LocalOperation(Win16LocalHeapSize, 0);
        return true;
    case 16: // GlobalReAlloc(handle, bytes, flags)
    {
        words = 4;
        result = Win16GlobalReAlloc(frame.Word(3), frame.Long(1), frame.Word(0));
        return true;
    }
    case 197: // GlobalFix(handle) and GlobalUnfix(handle): a block of this
    case 198: // side never moves, so its selector is already fixed. GlobalFix
              // answers the selector, as KRNL386 does.
    {
        words = 1;
        auto *block = Win16GlobalBlock(frame.Word(0));
        result = ordinal == 197 ? (block ? block->selector : 0u) : 0u;
        return true;
    }
    case 199: // SetHandleCount(count): the file table of a task of this port
              // holds sixty four handles and does not grow, so the answer is
              // what the caller asked for, capped at that, which is the
              // contract of 1:...
        words = 1;
        result = frame.Word(0) < 64 ? frame.Word(0) : 64u;
        return true;
    case 136: // GetDriveType(drive): the drive is a number, A being zero.
              // KRNL386 forwards through its KERNEL32 service, but the
              // Win16 result set predates DRIVE_NO_ROOT_DIR: an absent root is
              // DRIVE_UNKNOWN. Win16 also reports CD-ROM drives as remote, a
              // Windows 3.1/95 compatibility rule used by File Manager.
    {
        words = 1;
        char root[4] = {(char)('A' + (frame.Word(0) & 31)), ':', '\\', 0};
        const unsigned scratch = Win16Scratch();
        if (!scratch)
            return false;
        Store(scratch, root, sizeof root);
        const unsigned a[] = {scratch};
        result = Api("GetDriveTypeA", a);
        if (result == 1) result = 0;
        else if (result == 5) result = 4;
        return true;
    }
    case 170: // AllocCStoDSAlias(selector) and
    case 172: // AllocAlias(selector): a data descriptor over the same bytes
    case 171: // AllocDStoCSAlias(selector): a code descriptor over the same
              // bytes, so a program can execute what it built in a data
              // segment. The alias is a descriptor of its own; freeing it is
              // FreeSelector.
    case 175: // AllocSelector(selector): a second descriptor over the same
              // bytes, or a fresh one when the argument is zero.
    {
        words = 1;
        const auto *entry = selectors.At(frame.Word(0));
        if (!entry && frame.Word(0))
        {
            result = 0;
            return true;
        }
        // Return the new selector in AX. AllocDStoCSAlias preserves the
        // supplied selector in DX. AllocSelector copies all eight descriptor
        // bytes, including the code/data type, so callers can relocate and
        // execute a copied code selector.
        const unsigned alias = Win16Segment(entry ? entry->base : 0, entry ? entry->limit : 65535,
                                            ordinal == 171 || (ordinal == 175 && entry && entry->code), false);
        result = alias ? (frame.Word(0) << 16 | alias) : 0;
        return true;
    }
    case 176: // FreeSelector(selector)
        words = 1;
        selectors.Unmap(frame.Word(0) << 16);
        result = 0;
        return true;
    case 25:  // GlobalCompact(minimum): the largest free block
    case 169: // GetFreeSpace(flags): the free space in all
    {
        // The global heap of this side is the flat arena of the process
        // (Win16GlobalAlloc takes its blocks from Allocate), so both answers
        // come from there instead of a fixed number. A single block
        // cannot outgrow the size limit of KRNL386.
        words = ordinal == 25 ? 2u : 1u;
        // GetFreeSpace(1794h), GFS_PHYSICALRAMSIZE: the installed memory in
        // bytes. System Properties ("253.8MB RAM" was the free heap) and
        // MSINFO32 show it; it is the figure GlobalMemoryStatus and DPMI
        // 0500h give (src/x86_memory_status.inc).
        if (ordinal == 169 && frame.Word(0) == 0x1794)
        {
            unsigned demand[14] = {};
            if (GuestDemandInfo(demand))
            {
                // In whole megabytes: the pages are held one below 1 GB
                // (3FFFFh), and the page General would print "1023.10MB".
                result = ((demand[1] + 255) & ~255u) << 12;
                return true;
            }
        }
        unsigned total = 0, largest = 0;
        HeapFree(total, largest);
        result = ordinal == 25 ? (largest > Win98NeTask::SizeLimit ? Win98NeTask::SizeLimit : largest) : total;
        return true;
    }
    case 54: // GetInstanceData(instance, data, count): the bytes at the same
             // offset in the other instance of the module. A module has one
             // instance in this port, so the source and the target are the
             // same block and nothing is copied; the answer is the count.
        words = 3;
        result = frame.Word(0);
        return true;
    case 59: // WriteProfileString(section, key, value)
    {
        words = 6;
        const unsigned a[] = {frame.Pointer(4), frame.Pointer(2), frame.Pointer(0)};
        result = Api("WriteProfileStringA", a);
        return true;
    }
    case 60: // FindResource(instance, name, type)
    case 61: // LoadResource(instance, resource)
    case 62: // LockResource(resource)
    case 63: // FreeResource(resource)
    case 64: // AccessResource(instance, resource)
    case 65: // SizeofResource(instance, resource)
        return Win16ResourceCall(ordinal, frame, result, words);
    case 95: // LoadLibrary(name)
    {
        words = 2;
        const unsigned at = frame.Pointer(0);
        result = 0;
        if (!at)
            return true;
        char name[260];
        snprintf(name, sizeof name, "%s", ReadString(at));
        result = Win16LoadNamedLibrary(name);
        if (log && result < 32)
        {
            char text[300];
            snprintf(text, sizeof text, "WIN16 LoadLibrary %s failed error=%u", name, result);
            log(text);
        }
        return state != Failed;
    }
    case 96: // FreeLibrary(instance), `retf 2`: no answer, AX is what
             // the body leaves.
        words = 1;
        result = 0;
        return Win16FreeLibrary(frame.Word(0));
    case 46: // FreeModule(module): the entry beside FreeLibrary, with the
             // same body behind it (the count goes down, the last reference
             // unloads). The answer is what is left of the count; zero says
             // the module is gone. AVIFILE lets go of a handler DLL this way.
    {
        words = 1;
        const unsigned word = frame.Word(0);
        unsigned left = 0;
        for (unsigned i = 0; i < machine.module_count; ++i)
            if (word && (machine.modules[i].handle == word || machine.modules[i].instance == word) &&
                machine.modules[i].references > 1)
                left = machine.modules[i].references - 1;
        if (!Win16FreeLibrary(word)) return false;
        result = left;
        return true;
    }
    case 115: // OutputDebugString(text)
    {
        words = 2;
        const unsigned at = frame.Pointer(0);
        if (at && log)
        {
            char text[300];
            snprintf(text, sizeof text, "WIN16 OutputDebugString %s", ReadString(at));
            log(text);
        }
        // KRNL386 hands the string to the debugger hook as ptrace event
        // 12h, which TOOLHELP turns into NFY_OUTSTR with the far pointer

        if (at)
            Win16ToolhelpNotify(11, frame.Long(0));
        result = 0;
        return state != Failed;
    }
    case 348: // hmemcpy(destination, source, count): huge pointers, but the
              // address space is flat behind the descriptors, so the copy is
              // one run.
    {
        words = 6;
        const unsigned to = frame.Pointer(4), from = frame.Pointer(2);
        unsigned count = frame.Long(0);
        result = 0;
        if (!to || !from)
            return true;
        unsigned char page[1024];
        for (unsigned done = 0; done < count; done += sizeof page)
        {
            const unsigned part = count - done < sizeof page ? count - done : sizeof page;
            if (!Read(from + done, page, part) || !Store(to + done, page, part))
                break;
        }
        return true;
    }
    case 349: // _hread(handle, buffer, count): _lread with a long count.
    case 350: // _hwrite(handle, buffer, count): the same loop as _hread with DOS function 40h.
        return Win16FileCall(ordinal, frame, result, words);
    case 353: // lstrcpyn(destination, source, count)
    {
        // The shared body, without the 1024-byte cut. A count of
        // zero writes nothing and still answers the destination, as the
        // 32-bit entry does.
        words = 5;
        const unsigned to = frame.Pointer(3), from = frame.Pointer(1);
        result = frame.Long(3);
        if (!to)
        {
            result = 0;
            return true;
        }
        const unsigned a[] = {to, from, frame.Word(0)};
        Api("lstrcpynA", a);
        return true;
    }
    case 216: // RegEnumKey(key, index, name, size)
    case 219: // RegDeleteKey(key, subkey)
    case 221: // RegSetValue(key, subkey, type, data, size)
    case 227: // RegFlushKey(key)
    case 217: // RegOpenKey(key, subkey, result)
    case 218: // RegCreateKey(key, subkey, result)
    case 220: // RegCloseKey(key)
    case 222: // RegDeleteValue(key, name)
    case 223: // RegEnumValue(key, index, name, name size, reserved, type, data, data size)
    case 224: // RegQueryValue(key, subkey, buffer, size)
    case 225: // RegQueryValueEx(key, name, reserved, type, data, size)
    case 226: // RegSetValueEx(key, name, reserved, type, data, size)
        return Win16RegistryCall(ordinal, frame, result, words);
    case 186: // GetSelector: KRNL386 reads descriptor.
    {
        words = 1;
        const auto *entry = selectors.At(frame.Word(0));
        result = entry ? entry->base : 0;
        return true;
    }
    case 187: // SetSelector: KRNL386 writes all four base bytes.
    {
        words = 3;
        const unsigned selector = frame.Word(2);
        auto *entry = selectors.Find(selector << 16);
        if (!entry) { result = 0; return true; }
        const unsigned previous = entry->base;
        entry->base = frame.Long(0);
        if (!Win16Descriptor(selector)) { entry->base = previous; result = 0; return true; }
        result = selector;
        return true;
    }
    case 188: // GetSelectorLimit(selector).
              // Return the descriptor limit in DX:AX, or zero if the selector
              // cannot be loaded.
        words = 1;
        result = Win16Limit(frame.Word(0));
        return true;
    case 107: // SetErrorMode(mode)
    {
        // The mode a task sets is read again by FatalAppExit and by the
        // critical-error path; answering zero and keeping nothing left
        // SEM_NOOPENFILEERRORBOX without effect.
        words = 1;
        const unsigned a[] = {frame.Word(0)};
        result = Api("SetErrorMode", a) & 0xffff;
        return true;
    }
    case 130: // FileCDR(far proc).
              // Windows 98 no longer invokes per-file-operation callbacks.
              // Accept the far pointer and return zero.
        words = 2;
        result = 0;
        return true;
    case 334: // IsBadReadPtr(lp, cb), KRNL386
    case 335: // IsBadWritePtr(lp, cb),
    case 346: // IsBadHugeReadPtr(lp, cb),
    case 347: // IsBadHugeWritePtr(lp, cb),
    {
        // KRNL386 touches the first and the last byte, reading or, for the
        // write forms, with "or byte ptr es:[bx], 0", and its fault handler
        // answers TRUE. A count of zero touches nothing. The huge forms walk
        // the tiled selectors in steps of 8 for every 64 KiB.
        const bool huge = ordinal == 346 || ordinal == 347, write = ordinal == 335 || ordinal == 347;
        words = huge ? 4 : 3;
        const unsigned count = huge ? frame.Long(0) : frame.Word(0);
        const unsigned far = huge ? frame.Long(2) : frame.Long(1);
        result = 0;
        if (!count)
            return true;
        auto touch = [&](unsigned selector, unsigned offset) {
            const auto *entry = selectors.At(selector);
            if (!entry || !entry->present || offset > entry->limit || (write && entry->code))
                return false;
            return virtual_memory.Access(entry->base + offset, 1, write);
        };
        const unsigned selector = far >> 16, offset = far & 0xffff;
        const unsigned long long last = (unsigned long long)offset + count - 1;
        if (!huge && last > 0xffff)
        {
            result = 1;
            return true;
        }
        const unsigned last_selector = selector + unsigned(last >> 16) * 8;
        result = touch(selector, offset) && touch(last_selector, unsigned(last & 0xffff)) ? 0u : 1u;
        return true;
    }
    case 167: // GetExpWinVer(hModule), KRNL386: the expected Windows
              // version at +3Eh of the module GetExePtr finds, zero
              // for a handle of none. A zero handle is the current task's
              // module, and a thread of a 32-bit process has none.
    {
        words = 1;
        const unsigned handle = frame.Word(0);
        result = 0;
        for (unsigned i = 0; i < machine.module_count; ++i)
        {
            Win16Module &candidate = machine.modules[i];
            bool match = handle ? candidate.handle == handle || candidate.instance == handle
                                : !machine.hosted && !candidate.library && candidate.handle;
            for (unsigned k = 1; handle && !match && k <= candidate.segments; ++k)
                match = candidate.selector[k] == handle;
            if (match && candidate.handle)
            {
                result = candidate.image.ExpectedVersion();
                break;
            }
        }
        return true;
    }
    case 184: // GlobalDOSAlloc(dwBytes), KRNL386: a fixed block in
              // conventional memory, DX the real mode segment and AX the
              // selector, zero when the arena has no room. The arena here is
              // 10000h to A0000h of the process, committed on use.
    {
        words = 2;
        result = 0;
        const unsigned bytes = (frame.Long(0) + 15) & ~15u;
        if (!bytes || bytes > 0x90000)
            return true;
        unsigned base = 0x10000;
        for (bool moved = true; moved;)
        {
            moved = false;
            if (base < SystemVm::KernelEnd && SystemVm::KernelFirst < base + bytes)
            {
                base = SystemVm::KernelEnd;
                moved = true;
            }
            for (const auto &dos : machine.dos_blocks)
                if (dos.size && base < dos.base + dos.size && dos.base < base + bytes)
                {
                    base = (dos.base + dos.size + 15) & ~15u;
                    moved = true;
                }
        }
        Win16Machine::DosBlock *free_slot = nullptr;
        for (auto &dos : machine.dos_blocks)
            if (!dos.size) { free_slot = &dos; break; }
        if (base + bytes > 0xa0000 || !free_slot)
            return true;
        const unsigned first = base & ~0xfffu, last = (base + bytes + 0xfff) & ~0xfffu;
        for (unsigned page = first; page < last; page += 0x1000)
            if (!virtual_memory.Access(page, 1, true))
            {
                if (!Map(page, 0x1000, 3)) return true;
                if (!EnsureSystemVmMemory() || !Check(cpu->Unmap(page, 0x1000)) ||
                    !Check(cpu->MapHost(page, 0x1000, Prot::All, system_vm_memory + page)))
                    return true;
            }
        Win16Block *slot = Win16GlobalSlot();
        if (!slot)
            return true;
        Win16Clear(base, bytes);
        *slot = Win16Block{};
        slot->memory = base;
        slot->size = bytes;
        slot->dos = true;
        if (!Win16GlobalMap(*slot, base, bytes))
        {
            *slot = Win16Block{};
            return true;
        }
        slot->handle = Win98NeTask::HandleOf(slot->selector, false);
        *free_slot = {base, bytes};
        result = (base >> 4) << 16 | slot->selector;
        return true;
    }
    case 185: // GlobalDOSFree(selector), : zero when it is freed
    {
        words = 1;
        auto *block = Win16GlobalBlock(frame.Word(0));
        result = block && block->dos && Win16GlobalFree(frame.Word(0)) ? 0u : frame.Word(0);
        return true;
    }
    case 191: // GlobalPageLock increments the page lock count.
    case 192: // GlobalPageUnlock decrements the page lock count.
    {
        words = 1;
        Win16Block *block = Win16GlobalBlock(frame.Word(0));
        result = 0;
        if (block)
        {
            if (ordinal == 191 && block->page_locks < 0xffff)
                ++block->page_locks;
            else if (ordinal == 192 && block->page_locks)
                --block->page_locks;
            result = block->page_locks;
        }
        return true;
    }
    // The generic thunks of KRNL386 into 32-bit code (MSVIDEO reaches
    // MSVFW32!ICMThunk32 through them, MSVIDEO ): LoadLibraryEx32W,
    // FreeLibrary32W, GetProcAddress32W take and answer 32-bit values, and
    // CallProc32W(p1..pN, lpfn, fAddressConvert, cParams) calls lpfn with the
    // N dwords in their order; bit i of fAddressConvert makes the i-th
    // parameter from the end a flat pointer out of a 16:16 one. The callee
    // removes all of its arguments, cParams + 3 dwords.
    case 513: // LoadLibraryEx32W(lpszLibFile, hFile, dwFlags)
    {
        words = 6;
        const unsigned a[] = {frame.Pointer(4)};
        CpuContext *saved = nullptr;
        unsigned top = 0;
        if (!Win16FlatEnter(saved, top))
            return false;
        result = Api("LoadLibraryA", a);
        Win16FlatLeave(saved, top);
        return state != Failed;
    }
    case 514: // FreeLibrary32W(hInstance)
    {
        words = 2;
        const unsigned a[] = {frame.Long(0)};
        CpuContext *saved = nullptr;
        unsigned top = 0;
        if (!Win16FlatEnter(saved, top))
            return false;
        result = Api("FreeLibrary", a);
        Win16FlatLeave(saved, top);
        return state != Failed;
    }
    case 515: // GetProcAddress32W(hInstance, lpszProc)
    {
        words = 4;
        const unsigned name = frame.Long(0);
        const unsigned a[] = {frame.Long(2), name >> 16 ? frame.Pointer(0) : name};
        result = Api("GetProcAddress", a);
        return state != Failed;
    }
    case 517: // CallProc32W(p1..pN, lpfn, fAddressConvert, cParams)
    {
        const unsigned count = frame.Long(0), convert = frame.Long(2), procedure = frame.Long(4);
        if (count > 32)
        {
            Fail("CallProc32W with more than 32 parameters");
            return false;
        }
        words = 6 + count * 2;
        unsigned arguments[32] = {};
        for (unsigned i = 0; i < count; ++i)
        {
            // Parameter i from the end sits right above the three fixed ones.
            unsigned value = frame.Long(6 + i * 2);
            if (convert & (1u << i))
                value = value >> 16 ? selectors.Linear(value) : 0;
            arguments[count - 1 - i] = value;
        }
        CpuContext *saved = nullptr;
        unsigned top = 0;
        if (!Win16FlatEnter(saved, top))
            return false;
        result = Call(procedure, arguments, count);
        Win16FlatLeave(saved, top);
        if (log && trace_calls)
        {
            char text[200];
            snprintf(text, sizeof text, "CALLPROC32W %08x (%08x,%08x,%08x,%08x,%08x,%08x) result=%08x", procedure,
                     arguments[0], arguments[1], arguments[2], arguments[3], arguments[4], arguments[5], result);
            log(text);
        }
        return state != Failed;
    }
    case 471: // KRNL386: the identifier of the current process, DX:AX
              // (the process of the current thread, as KERNEL32 hands it out).
        words = 0;
        result = Api("GetCurrentProcessId", nullptr);
        return true;
    case 631: // C16ThkSL01, the 16-to-32 flat thunk (src/x86_thunk_hosted.cpp)
        return Win16SlThunk(frame, result, words);
    case 651: // ThunkConnect16(pszDll16, pszDll32, hInst, dwReason, block, name, cs)
        words = 12;
        return Win16ThunkConnect16(frame, result);
    case 337: // IsBadStringPtr(lpsz, cchMax), : REPNE SCASB over at most
              // cchMax bytes. A zero count, no terminator within the count, or
              // a byte the segment does not cover (the fault handler) answer
              // TRUE; a terminator within the count answers FALSE.
    {
        words = 3;
        const unsigned far = frame.Long(1), count = frame.Word(0);
        const auto *entry = selectors.At(far >> 16);
        result = 1;
        if (!count || !entry || !entry->present)
            return true;
        for (unsigned i = 0, offset = far & 0xffff; i < count; ++i, ++offset)
        {
            unsigned char byte = 0;
            if (offset > 0xffff || offset > entry->limit || !virtual_memory.Access(entry->base + offset, 1, false) ||
                !Read(entry->base + offset, &byte, 1))
                return true;
            if (!byte)
            {
                result = 0;
                return true;
            }
        }
        return true;
    }
    case 336: // IsBadCodePtr(lpfn): a code segment the offset lies in.
    {
        words = 2;
        const unsigned far = frame.Long(0);
        const auto *entry = selectors.At(far >> 16);
        result = entry && entry->present && entry->code && (far & 0xffff) <= entry->limit ? 0u : 1u;
        return true;
    }
    case 3:   // GetVersion, KRNL386: Windows 3.95 for Win16,
              // or 3.10 when GetAppCompatFlags(0) has bit 00200000h.
              // InstallShield uses minor >= 95 to select its Win32 engine.
    case 354: // GetAppCompatFlags(hTask), KRNL386: the DWORD at TDB+4Eh.
    {
        // KRNL386 fills the compatibility flags when it makes the task.
        // An image for a version above 4.00 gets zero; any
        // other reads WIN.INI [Compatibility] for a 16-bit module,
        // [Compatibility95] for a 32-bit one expecting 4.00 and
        // [Compatibility32] for an older 32-bit one, with the module name as
        // the key and "0x" and hex digits as the value. The flags of a 16-bit
        // module expecting 3.10 or later keep only the mask FFE484AFh.
        words = ordinal == 3 ? 0u : 1u;
        // A null hTask and the handle of GetCurrentTask (KERNEL.36) both
        // name the current task; PSCRIPT.DRV asks with the handle.
        unsigned current = machine.task;
        if (machine.hosted)
            for (const auto &record : machine.hosted_threads)
                if (record.thread == ThreadId() && record.task)
                    current = record.task;
        if (ordinal == 354 && frame.Word(0) && frame.Word(0) != (current & 0xffff))
        {
            Fail("GetAppCompatFlags of another task than the current one is not built yet");
            return false;
        }
        const bool wide = !Win16Process();
        const unsigned version = wide ? ExpectedWindowsVersion() : win16.state->modules[0].image.ExpectedVersion();
        result = ordinal == 3 ? 0x070a5f03u : 0u;
        if (version > 0x400)
            return true;
        char name[16] = {};
        const char *path = wide ? program_name : win16.state->modules[0].path;
        const char *base = path;
        for (const char *p = path; *p; ++p)
            if (*p == '\\' || *p == '/' || *p == ':')
                base = p + 1;
        for (unsigned i = 0; i < 8 && base[i] && base[i] != '.'; ++i)
            name[i] = base[i] >= 'a' && base[i] <= 'z' ? char(base[i] - 32) : base[i];
        const unsigned text = Allocate(0x60);
        if (!text)
            return false;
        PutString(text, !wide ? "Compatibility" : version == 0x400 ? "Compatibility95" : "Compatibility32");
        PutString(text + 0x20, name);
        PutString(text + 0x30, "");
        const unsigned profile[] = {text, text + 0x20, text + 0x30, text + 0x40, 12};
        Api("GetProfileStringA", profile);
        const char *value = ReadString(text + 0x40);
        unsigned flags = 0;
        if (value && value[0] == '0' && (value[1] | 0x20) == 'x')
            for (const char *p = value + 2;; ++p)
            {
                unsigned digit;
                if (*p >= '0' && *p <= '9') digit = unsigned(*p - '0');
                else if ((*p | 0x20) >= 'a' && (*p | 0x20) <= 'f') digit = unsigned((*p | 0x20) - 'a' + 10);
                else break;
                flags = flags << 4 | digit;
            }
        Free(text);
        if (flags && !wide && version >= 0x30a)
            flags &= 0xffe484afu;
        // DOS 7.10 stays in DX, major byte first. KERNEL32 GetVersion
        // has a separate contract and continues to report Windows 4.10.
        result = ordinal == 3 ? (flags & 0x00200000u ? 0x070a0a03u : 0x070a5f03u) : flags;
        return true;
    }
    case 132: // GetWinFlags, KRNL386: the same cell __WINFLAGS names.
        words = 0;
        if (!Win16KernelConstant(178, result)) return false;
        return true;
    case 207: // IsDBCSLeadByte(byte): the same lead-byte table the 32-bit
              // entry reads for the ANSI code page. SETUPX walks INF lines
              // with it.
    {
        words = 1;
        const unsigned a[] = {frame.Word(0) & 0xff};
        result = Api("IsDBCSLeadByte", a) ? 1u : 0u;
        return state != Failed;
    }
    case 431: // IsPeFormat(name, file), KRNL386, `retf 6`.
    {
        // With a name it opens the file itself (OpenFile, style 20h) and
        // closes it again, else it reads the handle it was given.
        // reads the 40h-byte MZ header, seeks to e_lfanew, reads F8h bytes
        // and seeks past the optional header; the answer is one when all of
        // that worked and the first dword is "PE\0\0". RUNDLL.EXE asks this
        // before it chooses between LoadLibrary and LoadLibrary32.
        words = 3;
        result = 0;
        const unsigned name = frame.Pointer(1);
        unsigned handle = 0;
        if (name)
        {
            const unsigned open[] = {name, 0x20};
            handle = Api("_lopen", open);
            if (handle == 0xffffffffu)
                return state != Failed;
        }
        else
        {
            if (frame.Word(0) == 0xffff)
                return true;
            handle = Win16FileNative(frame.Word(0));
        }
        const unsigned scratch = Win16Scratch();
        bool pe = false;
        if (scratch)
        {
            auto seek = [&](unsigned offset) {
                const unsigned a[] = {handle, offset, 0};
                return Api("_llseek", a) != 0xffffffffu;
            };
            auto read = [&](unsigned bytes) {
                const unsigned a[] = {handle, scratch, bytes};
                return Api("_lread", a) == bytes;
            };
            unsigned char header[0xf8] = {};
            if (seek(0) && read(0x40) && Read(scratch, header, 0x40) && header[0] == 'M' && header[1] == 'Z')
            {
                unsigned lfanew = 0;
                memcpy(&lfanew, header + 0x3c, 4);
                if (seek(lfanew) && read(0xf8) && Read(scratch, header, 0xf8))
                {
                    unsigned short optional = 0;
                    memcpy(&optional, header + 0x14, 2);
                    pe = seek(lfanew + 0x18 + optional) && !memcmp(header, "PE\0\0", 4);
                }
            }
        }
        if (name)
        {
            const unsigned close[] = {handle};
            Api("_lclose", close);
        }
        result = pe ? 1u : 0u;
        return state != Failed;
    }
    // ------------------------------------------------ the rest of the export
    // table (tools/win16_kernel_stock.py). First what a program can
    // meaningfully call, over the same native services the 32-bit entrance
    // uses; then the entries only the 16-bit system components of Windows
    // use, with the answer Windows gives where nothing stands behind them.
    case 27: // GetModuleName(hModule, lpBuffer, nSize): the module name of the
             // module data, copied; TRUE when the handle names a module.
    {
        words = 4;
        const unsigned handle = frame.Word(3), buffer = frame.Pointer(1), size = frame.Word(0);
        result = 0;
        for (unsigned i = 0; i < machine.module_count && !result; ++i)
        {
            const Win16Module &candidate = machine.modules[i];
            bool match = candidate.handle && (candidate.handle == handle || candidate.instance == handle);
            for (unsigned k = 1; !match && candidate.handle && k <= candidate.segments; ++k)
                match = candidate.selector[k] == handle;
            if (!match || !buffer || !size) continue;
            char name[16] = {};
            snprintf(name, sizeof name < size ? sizeof name : size, "%s", candidate.name);
            Store(buffer, name, (unsigned)strlen(name) + 1);
            result = 1;
        }
        return true;
    }
    case 133: // GetExePtr(handle): the module database behind a module
              // handle, an instance or one of its segments; zero for
              // a handle of none.
    {
        words = 1;
        const unsigned handle = frame.Word(0);
        result = 0;
        for (unsigned i = 0; i < machine.module_count && !result; ++i)
        {
            const Win16Module &candidate = machine.modules[i];
            bool match = candidate.handle && (candidate.handle == handle || candidate.instance == handle);
            for (unsigned k = 1; !match && candidate.handle && k <= candidate.segments; ++k)
                match = candidate.selector[k] == handle;
            if (match) result = candidate.handle;
        }
        return true;
    }
    case 105: // GetExeVersion: the Windows version the module of the current
              // task expects (+3Eh of its header).
    {
        words = 0;
        result = 0;
        for (unsigned i = 0; i < machine.module_count && !result; ++i)
            if (!machine.modules[i].library && machine.modules[i].handle)
                result = machine.modules[i].image.ExpectedVersion();
        return true;
    }
    case 93: // GetCodeHandle(lpProc): the handle of the segment a procedure
             // lies in, in AX and DX. A code segment is fixed here, its
             // handle is its selector.
        words = 2;
        result = selectors.At(frame.Word(1)) ? frame.Word(1) << 16 | frame.Word(1) : 0;
        return true;
    case 45: // LoadModule(lpModuleName, lpParameterBlock): a program started
             // with the command tail and the show word of the block (the
             // environment word, the far pointer to the counted tail, the
             // far pointer to the two show words). The answer is that of
             // WinExec: above 31 the new instance, else the error.
    {
        words = 4;
        const unsigned name = frame.Pointer(2), block = frame.Pointer(0);
        result = 2;
        if (!name) return true;
        char line[400];
        unsigned length = (unsigned)snprintf(line, sizeof line, "%s", ReadString(name));
        unsigned show = 1;
        if (block)
        {
            const unsigned tail = selectors.Linear(Read32(block + 2)), shows = selectors.Linear(Read32(block + 6));
            unsigned char count = 0;
            if (Read32(block + 2) && tail && Read(tail, &count, 1) && count && length + count + 2 < sizeof line)
            {
                line[length++] = ' ';
                Read(tail + 1, line + length, count);
                length += count;
                line[length] = 0;
            }
            if (Read32(block + 6) && shows && Win16Word(shows) == 2) show = Win16Word(shows + 2);
        }
        const unsigned text = String(line);
        if (!text) { result = 8; return true; }
        const unsigned a[] = {text, show};
        result = Api("WinExec", a);
        Free(text);
        return state != Failed;
    }
    case 320: // IsTask(hTask), : the handle of a task that lives. A
              // task is the program of this process, or a thread a driver
              // started as one (mmTaskCreate, mmThreadCreate).
    {
        words = 1;
        const unsigned task = frame.Word(0);
        result = task && task == machine.task;
        for (const auto &record : machine.hosted_threads)
            if (task && record.task == task) result = 1;
        return true;
    }
    case 122: // IsTaskLocked: the task LockCurrentTask holds, none here.
    case 158: // IsWinOldApTask(hTask): a task is never the DOS box.
        words = ordinal == 158 ? 1 : 0;
        result = 0;
        return true;
    case 33: // LockCurrentTask(fLock): keeps the scheduler from switching
             // tasks. Tasks of this port switch only where the task itself
             // yields or waits, so there is nothing to hold.
        words = 1;
        result = 0;
        return true;
    case 117: // OldYield, the Yield of Windows 2: the same entry here.
    case 150: // DirectedYield(hTask): a yield; the task named is not preferred.
    {
        words = ordinal == 150 ? 1 : 0;
        const unsigned zero[] = {0};
        Api("Sleep", zero);
        result = 0;
        return state != Failed;
    }
    case 155: // GetTaskDS: the automatic data segment of the current task.
        words = 0;
        result = 0;
        for (unsigned i = 0; i < machine.module_count && !result; ++i)
            if (!machine.modules[i].library) result = machine.modules[i].instance;
        return true;
    case 157: // GetCurPID(reserved): the process identifier, as ordinal 471.
        words = 2;
        result = Api("GetCurrentProcessId", nullptr);
        return true;
    case 92: // GetTempDrive(drive): the letter of the drive temporary files
             // go to in AL, a colon in AH.
    {
        words = 1;
        result = (unsigned)':' << 8 | 'C';
        const unsigned path = Allocate(260);
        if (path)
        {
            const unsigned a[] = {260, path};
            unsigned char letter = 0;
            if (Api("GetTempPathA", a) && Read(path, &letter, 1) && letter) result = (unsigned)':' << 8 | (letter & ~0x20u);
            Free(path);
        }
        return true;
    }
    case 97: // GetTempFileName(drive, prefix, unique, buffer): a name in the
             // temporary folder (TF_FORCEDRIVE, 80h, names a drive instead:
             // its root). The answer is the number in the name.
    {
        words = 6;
        const unsigned drive = frame.Word(5) & 0xff, prefix = frame.Pointer(3), unique = frame.Word(2), buffer = frame.Pointer(0);
        const unsigned path = Allocate(260);
        result = 0;
        if (!path || !buffer) { if (path) Free(path); return true; }
        if (drive & 0x80)
        {
            const char root[4] = {(char)(drive & 0x7f ? (drive & 0x7f) : 'C'), ':', '\\', 0};
            Store(path, root, sizeof root);
        }
        else
        {
            const unsigned a[] = {260, path};
            if (!Api("GetTempPathA", a)) Store(path, "C:\\", 4);
        }
        const unsigned a[] = {path, prefix, unique, buffer};
        result = Api("GetTempFileNameA", a) & 0xffff;
        Free(path);
        return true;
    }
    case 144: // CreateDirectory(path, security)
    case 145: // RemoveDirectory(path)
    case 146: // DeleteFile(path)
    case 76:  // DeletePathname(path), the name DeleteFile had in Windows 2
    {
        // The entries KRNL386 hands to KERNEL32:
        // TRUE or FALSE, the error in the last error of the thread.
        words = ordinal == 144 ? 4 : 2;
        const unsigned a[] = {frame.Pointer(ordinal == 144 ? 2 : 0), 0};
        result = Api(ordinal == 144 ? "CreateDirectoryA" : ordinal == 145 ? "RemoveDirectoryA" : "DeleteFileA", a);
        return true;
    }
    case 420: // GetFileAttributes(path): the attributes, FFFFFFFFh on failure
        words = 2;
        {
            const unsigned a[] = {frame.Pointer(0)};
            result = Api("GetFileAttributesA", a);
        }
        return true;
    case 421: // SetFileAttributes(path, attributes)
        words = 4;
        {
            const unsigned a[] = {frame.Pointer(2), frame.Long(0)};
            result = Api("SetFileAttributesA", a);
        }
        return true;
    case 413: // FindFirstFile(pattern, lpFindData): the WIN32_FIND_DATA of
              // the 32-bit side, behind a far pointer; the handle in DX:AX.
        words = 4;
        {
            const unsigned a[] = {frame.Pointer(2), frame.Pointer(0)};
            result = Api("FindFirstFileA", a);
        }
        return true;
    case 414: // FindNextFile(handle, lpFindData)
        words = 4;
        {
            const unsigned a[] = {frame.Long(2), frame.Pointer(0)};
            result = Api("FindNextFileA", a);
        }
        return true;
    case 415: // FindClose(handle)
        words = 2;
        {
            const unsigned a[] = {frame.Long(0)};
            result = Api("FindClose", a);
        }
        return true;
    case 422: // GetDiskFreeSpace(root, sectors, bytes, free clusters, clusters)
        words = 10;
        {
            const unsigned a[] = {frame.Pointer(8), frame.Pointer(6), frame.Pointer(4), frame.Pointer(2), frame.Pointer(0)};
            result = Api("GetDiskFreeSpaceA", a);
        }
        return true;
    case 432: // FileTimeToLocalFileTime(lpFileTime, lpLocalFileTime)
        words = 4;
        {
            const unsigned a[] = {frame.Pointer(2), frame.Pointer(0)};
            result = Api("FileTimeToLocalFileTime", a);
        }
        return true;
    case 142: // GetProfileSectionNames(buffer, size)
    case 143: // GetPrivateProfileSectionNames(buffer, size, file)
    {
        words = ordinal == 143 ? 5 : 3;
        const unsigned shift = ordinal == 143 ? 2 : 0;
        // WIN.INI by its name: the profile functions take it for a missing file.
        const unsigned win_ini = ordinal == 142 ? String("WIN.INI") : 0;
        const unsigned a[] = {frame.Pointer(1 + shift), frame.Word(shift), ordinal == 143 ? frame.Pointer(0) : win_ini};
        result = Api("GetPrivateProfileSectionNamesA", a) & 0xffff;
        if (win_ini) Free(win_ini);
        return true;
    }
    case 418: // GetPrivateProfileSection(section, buffer, size, file)
    case 419: // GetProfileSection(section, buffer, size)
    {
        words = ordinal == 418 ? 7 : 5;
        const unsigned shift = ordinal == 418 ? 2 : 0;
        const unsigned a[] = {frame.Pointer(3 + shift), frame.Pointer(1 + shift), frame.Word(shift),
                              ordinal == 418 ? frame.Pointer(0) : 0};
        result = Api(ordinal == 418 ? "GetPrivateProfileSectionA" : "GetProfileSectionA", a) & 0xffff;
        return true;
    }
    case 416: // WritePrivateProfileSection(section, text, file)
    case 417: // WriteProfileSection(section, text)
    {
        words = ordinal == 416 ? 6 : 4;
        const unsigned shift = ordinal == 416 ? 2 : 0;
        const unsigned a[] = {frame.Pointer(2 + shift), frame.Pointer(shift), ordinal == 416 ? frame.Pointer(0) : 0};
        result = Api(ordinal == 416 ? "WritePrivateProfileSectionA" : "WriteProfileSectionA", a);
        return true;
    }
    case 406: // WritePrivateProfileStruct(section, key, data, size, file)
    case 407: // GetPrivateProfileStruct(section, key, data, size, file)
    {
        words = 9;
        const unsigned a[] = {frame.Pointer(7), frame.Pointer(5), frame.Pointer(3), frame.Word(2), frame.Pointer(0)};
        result = Api(ordinal == 406 ? "WritePrivateProfileStructA" : "GetPrivateProfileStructA", a);
        return true;
    }
    case 315: // WriteOutProfiles: the cached profile goes to its file. The
              // profile provider writes at once.
    case 319: // FlushCachedFileHandle(hModule): the cached handle of a
              // module's file; this loader keeps none open.
        words = ordinal == 319 ? 1 : 0;
        result = 0;
        return true;
    case 232: // RegLoadKey(key, subkey, file)
    case 234: // RegSaveKey(key, file, security)
    {
        words = 6;
        const unsigned key = frame.Long(4);
        const unsigned a[] = {key == 1 ? 0x80000000u : key, frame.Pointer(2), frame.Pointer(0)};
        result = Api(ordinal == 232 ? "RegLoadKeyA" : "RegSaveKeyA", a);
        return true;
    }
    case 233: // RegUnLoadKey(key, subkey)
    {
        words = 4;
        const unsigned key = frame.Long(2);
        const unsigned a[] = {key == 1 ? 0x80000000u : key, frame.Pointer(0)};
        result = Api("RegUnLoadKeyA", a);
        return true;
    }
    case 352: // lstrcatn(destination, source, size): appends as much of the
              // source as the buffer of `size` bytes holds with its end;
              // the answer is the destination.
    {
        words = 5;
        const unsigned to = frame.Pointer(3), from = frame.Pointer(1), size = frame.Word(0);
        result = frame.Long(3);
        if (!to || !from || !size) return true;
        unsigned length = 0;
        unsigned char byte = 0;
        while (length < size && Read(to + length, &byte, 1) && byte) ++length;
        for (unsigned i = 0; length + 1 < size && Read(from + i, &byte, 1) && byte; ++i, ++length) Store(to + length, &byte, 1);
        byte = 0;
        if (length < size) Store(to + length, &byte, 1);
        return true;
    }
    case 161: // LocalCountFree: the free bytes of the local heap of DS
    case 162: // LocalHeapSize: the bytes between its first and last arena
    {
        // The heap information stands at the offset the word at DS:6 names:
        // hi_first at +6, hi_last at +8. An arena begins with la_prev (bit 0
        // set: in use) and la_next.
        words = 0;
        result = 0;
        const unsigned base = Win16Linear(Register(Reg::Ds), 0);
        const unsigned info = base ? Win16Word(base + 6) : 0;
        if (!info) return true;
        const unsigned first = Win16Word(base + info + 6), last = Win16Word(base + info + 8);
        if (ordinal == 162) { result = last > first ? last - first : 0; return true; }
        for (unsigned at = first, guard = 0; at && at < last && guard < 8192; ++guard)
        {
            const unsigned next = Win16Word(base + at + 2);
            if (next <= at) break;
            if (!(Win16Word(base + at) & 1) && next - at > 4) result += next - at - 4;
            at = next;
        }
        return true;
    }
    case 310: // LocalHandleDelta(delta): how many handle entries the local
              // heap adds when its table is full, at +18h of the heap
              // information; zero asks without changing.
    {
        words = 1;
        const unsigned base = Win16Linear(Register(Reg::Ds), 0);
        const unsigned info = base ? Win16Word(base + 6) : 0;
        result = 0;
        if (!info) return true;
        if (frame.Word(0)) Win16Word(base + info + 0x18, frame.Word(0));
        result = Win16Word(base + info + 0x18);
        return true;
    }
    case 230: // GlobalSmartPageLock(selector) and
    case 231: // GlobalSmartPageUnlock: the page lock of ordinals 191 and 192
    {
        words = 1;
        Win16Block *block = Win16GlobalBlock(frame.Word(0));
        result = 0;
        if (block)
        {
            if (ordinal == 230 && block->page_locks < 0xffff) ++block->page_locks;
            else if (ordinal == 231 && block->page_locks) --block->page_locks;
            result = block->page_locks;
        }
        return true;
    }
    case 163: // GlobalLRUOldest(handle) and
    case 164: // GlobalLRUNewest: the place of a discardable block in the
              // order blocks are discarded in. Nothing is discarded here;
              // the answer is the handle.
        words = 1;
        result = frame.Word(0);
        return true;
    case 189: // SetSelectorLimit(selector, limit), : writes the limit of
              // the descriptor; the answer is zero.
    {
        words = 3;
        auto *entry = selectors.Find(frame.Word(2) << 16);
        result = 0;
        if (!entry) return true;
        const unsigned previous = entry->limit;
        entry->limit = frame.Long(0);
        if (!Win16Descriptor(frame.Word(2))) entry->limit = previous;
        return true;
    }
    case 177: // PrestoChangoSelector(source, destination): the descriptor of
              // the source in the destination with code and data exchanged;
              // the answer is the destination.
    {
        words = 2;
        const unsigned source = frame.Word(1), destination = frame.Word(0);
        const auto *from = selectors.At(source);
        auto *to = selectors.Find(destination << 16);
        result = 0;
        if (!from || !to) return true;
        // The destination takes base and limit; a descriptor of the other
        // kind over the same bytes is what AllocDStoCSAlias builds, so the
        // destination is given up for such an alias when the kinds differ.
        to->base = from->base;
        to->limit = from->limit;
        if (Win16Descriptor(destination)) result = destination;
        return true;
    }
    case 180: // LongPtrAdd(pointer, delta): moves the base of the pointer's
              // selector up by delta, so offsets beyond 64 KiB of a huge
              // block come into reach.
    {
        words = 4;
        const unsigned selector = frame.Word(3);
        auto *entry = selectors.Find(selector << 16);
        result = frame.Long(2);
        if (!entry) return true;
        const unsigned previous = entry->base;
        entry->base += frame.Long(0);
        if (!Win16Descriptor(selector)) entry->base = previous;
        return true;
    }
    case 206: // AllocSelectorArray(count): `count` descriptors in a row
    {
        words = 1;
        // One descriptor at a time; they come out in a row only when the
        // table has a free run, which is checked.
        const unsigned count = frame.Word(0);
        unsigned first = 0, previous = 0;
        bool row = count != 0;
        for (unsigned i = 0; i < count && row; ++i)
        {
            const unsigned selector = Win16Segment(0, 0, false, false);
            if (!selector || (previous && selector != previous + 8)) row = false;
            if (!first) first = selector;
            previous = selector;
        }
        if (!row)
        {
            for (unsigned selector = first; selector && selector <= previous; selector += 8) selectors.Unmap(selector << 16);
            first = 0;
        }
        result = first;
        return true;
    }
    case 516: // GetVDMPointer32W(vp, fMode): the flat address of a 16:16 pointer
    {
        words = 4;
        const unsigned pointer = frame.Long(2);
        result = pointer >> 16 ? selectors.Linear(pointer) : 0;
        return true;
    }
    case 627: // IsBadFlatReadWritePtr(pointer, bytes, write)
    {
        words = 5;
        const unsigned pointer = frame.Long(3), bytes = frame.Long(1);
        result = !pointer || (bytes && !virtual_memory.Access(pointer, bytes, frame.Word(0) != 0));
        return true;
    }
    case 518: // _CallProcEx32W(nParams, fAddressConvert, lpProcAddress,...):
              // CallProc32W with the C calling convention on the 16-bit
              // side: the three fixed arguments come first, the parameters
              // behind them, and the caller takes all of it off the stack.
              // Bit 31 of nParams says the 32-bit procedure is cdecl too.
    {
        words = 0;
        const unsigned count = frame.Long(0) & 0x7fffffff, convert = frame.Long(2), procedure = frame.Long(4);
        if (count > 32)
        {
            Fail("_CallProcEx32W with more than 32 parameters");
            return false;
        }
        unsigned arguments[32] = {};
        for (unsigned i = 0; i < count; ++i)
        {
            unsigned value = frame.Long(6 + i * 2);
            // The low bit of the mask belongs to the last parameter.
            if (convert & (1u << (count - 1 - i)))
                value = value >> 16 ? selectors.Linear(value) : 0;
            arguments[i] = value;
        }
        CpuContext *saved = nullptr;
        unsigned top = 0;
        if (!Win16FlatEnter(saved, top))
            return false;
        result = Call(procedure, arguments, count);
        Win16FlatLeave(saved, top);
        return state != Failed;
    }
    case 360: // OpenFileEx(name, lpOFSTRUCTEX, style): OpenFile with the
              // structure that has room for a long path: the count a word,
              // the fixed disk byte, the error word, four reserved bytes and
              // 260 bytes of path. The opening rule is that of OpenFile; a
              // path the short structure of the 32-bit entry cannot hold is
              // refused with 206, as OpenFile refuses it.
    {
        words = 5;
        const unsigned name = frame.Pointer(3), buffer = frame.Pointer(1), style = frame.Word(0);
        const unsigned scratch = Win16Scratch();
        result = 0xffff;
        if (!scratch) return false;
        unsigned char wide[269] = {}, of[136] = {};
        unsigned room = 0;
        if (buffer)
        {
            room = Win16Limit(frame.Word(2)) + 1;
            room = room > frame.Word(1) ? room - frame.Word(1) : 0u;
            if (room > sizeof wide) room = sizeof wide;
            Read(buffer, wide, room);
            of[0] = sizeof of;
            of[1] = wide[2];
            memcpy(of + 2, wide + 3, 2);
            memcpy(of + 4, wide + 5, 4);
            snprintf((char *)of + 8, 128, "%s", (const char *)wide + 9);
            if ((style & 0x8000) && strlen((const char *)wide + 9) > 127) // OF_REOPEN
            {
                wide[3] = 206;
                wide[4] = 0;
                Store(buffer, wide, room);
                return true;
            }
        }
        Store(scratch, of, sizeof of);
        const unsigned a[] = {name, scratch, style};
        const unsigned handle = Api("OpenFile", a);
        if (buffer && room)
        {
            Read(scratch, of, sizeof of);
            memset(wide, 0, sizeof wide);
            wide[0] = (unsigned char)sizeof wide;
            wide[1] = (unsigned char)(sizeof wide >> 8);
            wide[2] = of[1];
            memcpy(wide + 3, of + 2, 2);
            memcpy(wide + 5, of + 4, 4);
            memcpy(wide + 9, of + 8, 128);
            Store(buffer, wide, room);
        }
        if (handle == 0xffffffffu) return true;
        if (style & 0x100) { result = 0; return true; }             // OF_PARSE
        if (style & (0x200 | 0x4000)) { result = 1; return true; }  // OF_DELETE, OF_EXIST
        result = Win16FileWord(handle);
        if (result == 0xffff) files.Close(handle);
        return true;
    }
    case 491: // RegisterServiceProcess(pid, type): KERNEL32's own
    {
        words = 4;
        const unsigned a[] = {frame.Long(2), frame.Long(0)};
        result = Api("RegisterServiceProcess", a);
        return true;
    }
    case 236: // GetProductName(buffer, size): the name of the product from
              // the registry (CurrentVersion, ProductName); the answer is
              // its length.
    {
        words = 3;
        const unsigned buffer = frame.Pointer(1), size = frame.Word(0);
        result = 0;
        if (!buffer || !size) return true;
        const unsigned scratch = Allocate(8 + 128);
        const unsigned key_name = String("Software\\Microsoft\\Windows\\CurrentVersion"), value_name = String("ProductName");
        if (scratch && key_name && value_name)
        {
            const unsigned open[] = {0x80000002u, key_name, scratch};
            if (!Api("RegOpenKeyA", open))
            {
                Write32(scratch + 4, 128);
                const unsigned query[] = {Read32(scratch), value_name, 0, 0, scratch + 8, scratch + 4};
                if (!Api("RegQueryValueExA", query))
                {
                    const char *name = ReadString(scratch + 8);
                    char text[128];
                    snprintf(text, sizeof text < size ? sizeof text : size, "%s", name ? name : "");
                    Store(buffer, text, (unsigned)strlen(text) + 1);
                    result = (unsigned)strlen(text);
                }
                const unsigned close[] = {Read32(scratch)};
                Api("RegCloseKey", close);
            }
        }
        if (scratch) Free(scratch);
        if (key_name) Free(key_name);
        if (value_name) Free(value_name);
        return true;
    }
    case 120: // UndefDynLink: where an import nobody resolved leads. KRNL386
              // ends the task with "Call to Undefined Dynalink".
    {
        words = 0;
        const unsigned text = String("Call to Undefined Dynalink");
        FatalAppExit(text);
        if (text) Free(text);
        return state != Failed;
    }
    case 103: // NetBIOSCall: INT 5Ch with the NCB at ES:BX. No NetBIOS is
              // bound here; the NCB gets the answer of a machine without an
              // adapter of that number, NRC_BRIDGE (23h), in ncb_retcode and
              // ncb_cmd_cplt, and AL carries it.
    {
        words = 0;
        const unsigned block = Win16Linear(Register(Reg::Es), Register(Reg::Ebx) & 0xffff);
        const unsigned char code = 0x23;
        if (block)
        {
            Store(block + 1, &code, 1);
            Store(block + 49, &code, 1);
        }
        result = code;
        return true;
    }
    // ---- entries that hold a setting or a hook of the debugging kernel and
    // of real mode. Each answers what KRNL386 answers when nothing is set.
    case 14:  // LocalNotify(lpNotifyFunc): the handler before, none
    case 154: // GlobalNotify(lpNotifyProc): no block is ever discarded, so
              // the procedure would never be called
    case 202: // RegisterPtrace(lpNotify): the debugger notification
    case 341: // ToolHelpHook(lpfn): the hook before, none
    case 318: // FatalExitHook(lpfn): the hook before, none
        words = 2;
        result = 0;
        return true;
    case 106: // SetSwapAreaSize(size): the code swap area of real mode; in
              // protected mode nothing is set and the answer is the request.
        words = 1;
        result = frame.Word(0);
        return true;
    case 94:  // DefineHandleTable(offset): the handle table of real mode;
              // protected mode needs none and answers TRUE.
        words = 1;
        result = 1;
        return true;
    case 98:  // GetLastDiskChange: no disk was ever swapped
    case 99:  // GetLPErrMode
    case 100: // ValidateCodeSegments: the check of the debugging kernel
    case 123: // KbdRst
    case 124: // EnableKernel
    case 125: // DisableKernel
    case 126: // MemoryFreed
    case 165: // A20Proc
    case 200: // ValidateFreeSpaces: the check of the debugging kernel
    case 203: // DebugBreak: the break for a debugger; none listens
    case 204: // SwapRecording(flag): the swap log of the debugging kernel
    case 205: // CVWBreak
    case 235: // InvalidateNLSCache
    case 41:  // EnableDos
    case 42:  // DisableDos
        words = ordinal == 204 || ordinal == 126 || ordinal == 165 ? 1 : 0;
        result = 0;
        return true;
    case 156: // LimitEmsPages(pages): expanded memory of real mode, none: zero
        words = 2;
        result = 0;
        return true;
    case 323: // IsROMModule(hModule) and
    case 326: // IsROMFile(hFile): Windows in ROM; never
    case 345: // IsSharedSelector(selector): of a library; this port keeps
              // every library in the process that loaded it
    case 338: // HasGPHandler(address): the table of handlers a module brings
              // for faults in its own code; none of the modules here has one
        words = ordinal == 338 ? 2 : 1;
        result = 0;
        return true;
    case 324: // LogError(error, info) and
    case 325: // LogParamError(error, lpfn, param): the report to a debugger
        words = ordinal == 324 ? 3 : 5;
        result = 0;
        return true;
    case 328: // _DebugOutput(flags, format,...), cdecl: text for a debugger
        words = 0;
        result = 0;
        return true;
    // MapSL, MapLS and UnMapLS of the 16-bit KERNEL, the same mappings as the
    // KERNEL32 exports of those names (src/x86_kernel_thunks.inc). AVICAP.DLL
    // maps the 32-bit buffers AVICAP32 hands it with them.
    case 357: // MapSL(segptr): the linear address of a 16:16 pointer
        words = 2;
        result = frame.Long(0) >> 16 ? Win16Linear(frame.Long(0) >> 16, frame.Long(0) & 0xffff) : 0;
        return true;
    case 358: // MapLS(linear): a 16:16 pointer to a linear address
        words = 2;
        result = SegmentMap(frame.Long(0));
        return true;
    case 359: // UnMapLS(segptr): gives the selector of MapLS back
        words = 2;
        selectors.Unmap(frame.Long(0));
        result = 0;
        return true;
    case 355: // GetWinDebugInfo(lpInfo, flags): FALSE, this is no debugging kernel
    case 356: // SetWinDebugInfo(lpInfo): FALSE
        words = ordinal == 355 ? 3 : 2;
        result = 0;
        return true;
    case 314: // DebugDefineSegment(module, segment, selector, instance, data)
        words = 7;
        result = 0;
        return true;
    // ---- the string helpers KRNL386 exports without a name. to
    // are four entries into one body, each loading CL with 3, 4, 2
    // and 1 inside the immediate of the one before; the Wine spec names them
    // Reserved1 to Reserved4 for AnsiNext, AnsiPrev, AnsiUpper and AnsiLower,
    // and 87, Reserved5, compares two strings through the collation
    // of lstrcmp. They take the frames of the USER entries of the same
    // names, which answer them here.
    case 77: return Win16UserCall(472, frame, result, words); // AnsiNext
    case 78: return Win16UserCall(473, frame, result, words); // AnsiPrev
    case 79: return Win16UserCall(431, frame, result, words); // AnsiUpper
    case 80: return Win16UserCall(432, frame, result, words); // AnsiLower
    case 87: return Win16UserCall(430, frame, result, words); // lstrcmp
    case 462: // GetCurrentThreadId: Windows 98 reads the id out of the current
              // thread's record, the answer of KERNEL32.
    {
        words = 0;
        const unsigned a[] = {};
        result = Api("GetCurrentThreadId", a);
        return true;
    }
    // ---- entries of the system components themselves. USER, GDI and the
    // drivers of Windows call them; here those components are native and do
    // not. A 16-bit module that reaches one gets the answer of a failed
    // call: zero.
    case 26:  // GlobalFreeAll(owner)
    case 28:  // GlobalMasterHandle
    case 31:  // PostEvent(hTask)
    case 32:  // SetPriority(hTask, change)
    case 35:  // GetTaskQueue(hTask)
    case 66:  // AllocResource(hInstance, hResInfo, size)
    case 67:  // SetResourceHandler(hInstance, type, lpLoadFunc)
    case 104: // GetCodeInfo(lpProc, lpSegInfo)
    case 138: // GetHeapSpaces(hModule)
    case 168: // DirectResAlloc(hInstance, flags, size)
    case 196: // SelectorAccessRights(selector, operation, value)
    case 201: // ReplaceInst
    case 311: // GetSetKernelDOSProc(lpProc)
    case 351: // BUNNY_351
    case 361: // PIGLET_361
    {
        static const struct { unsigned short ordinal, words; } frames[] = {
            {26, 1}, {28, 0}, {31, 1}, {32, 2}, {35, 1}, {66, 4}, {67, 5}, {104, 4}, {138, 1}, {168, 3}, {196, 3},
            {201, 0}, {311, 2}, {351, 0}, {361, 0}};
        words = 0;
        for (const auto &entry : frames)
            if (entry.ordinal == ordinal) words = entry.words;
        result = 0;
        return true;
    }
    default:
        break;
    }
    char text[256];
    snprintf(text, sizeof text, "Win16 KERNEL ordinal %u is not ported yet", ordinal);
    Fail(text);
    return false;
}
// The file entries of KRNL386 a 16-bit program uses. They are the same native
// file provider the 32-bit entrance uses; only the handle is a word here and
// OpenFile fills the OFSTRUCT the caller hands over.
// The 16-bit side names a file with a word, the way DOS does, and the native
// file session hands out a tagged dword (src/files.cpp, Session::Handle checks
// 40000000h). Truncating the native handle to sixteen bits loses the tag, so
// the task keeps a table instead. Five is the first free DOS handle.
unsigned Application::Win16FileWord(unsigned native)
{
    auto &machine = *win16.state;
    for (unsigned i = 0; i < 64; ++i)
        if (!machine.files[i])
        {
            machine.files[i] = native;
            return i + 5;
        }
    return 0xffff;
}
unsigned Application::Win16FileNative(unsigned word)
{
    auto &machine = *win16.state;
    const unsigned index = (word & 0xffff) - 5;
    return (word & 0xffff) >= 5 && index < 64 ? machine.files[index] : 0u;
}
// The standard DOS handles zero to four of a Win16 task are the character
// devices of the system VM it inherits: CON for input, output and error, AUX
// and PRN. Under Win98 what a task writes there goes to the invisible screen
// of the system VM and the call succeeds with the full count; the port has no
// such screen, so the bytes are taken and dropped. Reading CON there would
// wait for a keyboard the system VM never gets; the port answers zero bytes
// instead of blocking the task.
bool Application::Win16StandardHandle(unsigned word)
{
    word &= 0xffff;
    return word < 5 && !(win16.state->standard_closed & (1u << word));
}
void Application::Win16FileRelease(unsigned word)
{
    auto &machine = *win16.state;
    const unsigned index = (word & 0xffff) - 5;
    if ((word & 0xffff) >= 5 && index < 64)
        machine.files[index] = 0;
}
bool Application::Win16FileCall(unsigned ordinal, const Win16Frame &frame, unsigned &result,
                                unsigned &words)
{
    words = ordinal == 74   ? 5u
            : ordinal == 81 ? 1u
            : ordinal == 83 || ordinal == 85
                ? 3u
                // _hread and _hwrite take a long count where _lread takes a word.
                : ordinal == 349 || ordinal == 350 ? 5u
                                                   : 4u;
    auto pointer_at = [&](unsigned start) { return frame.Pointer(words - 2 - start); };
    auto word_at = [&](unsigned start) { return frame.Word(words - 1 - start); };
    result = 0xffff;
    if (ordinal != 74 && ordinal != 83 && ordinal != 85 && Win16StandardHandle(word_at(0)))
    {
        // A standard device: a write takes every byte, a read and a
        // seek answer zero, a close frees the handle for good.
        const bool huge = ordinal == 349 || ordinal == 350;
        if (ordinal == 81)
            win16.state->standard_closed |= 1u << (word_at(0) & 0xffff);
        result = ordinal == 86 || ordinal == 350 ? (huge ? frame.Long(0) : frame.Word(0)) : 0u;
        return true;
    }
    switch (ordinal)
    {
    case 74:
    {
        // OFSTRUCT has the same 136 bytes on both sides, so the whole opening
        // rule stays with the 32-bit entry: OF_PARSE, OF_DELETE, OF_REOPEN,
        // the share table of KRNL386, the refusal of an impossible access mode
        // with 87 and the path length answer 206. This gate only
        // stages the structure, because the buffer of a 16-bit caller often
        // sits at the end of a segment and the descriptor is the only bound
        // there is.
        const unsigned name = pointer_at(0), buffer = pointer_at(2), style = frame.Word(0);
        const unsigned scratch = Win16Scratch();
        if (!scratch)
            return false;
        unsigned room = 0;
        if (buffer)
        {
            const unsigned selector = frame.Word(words - 2 - 2 + 1);
            const unsigned offset = frame.Word(words - 2 - 2);
            room = Win16Limit(selector) + 1;
            room = room > offset ? room - offset : 0u;
            if (room > 136)
                room = 136;
            unsigned char of[136] = {};
            Read(buffer, of, room);
            Store(scratch, of, sizeof of);
        }
        const unsigned a[] = {name, scratch, style};
        const unsigned handle = Api("OpenFile", a);
        if (buffer && room)
        {
            unsigned char of[136] = {};
            Read(scratch, of, sizeof of);
            Store(buffer, of, room);
        }
        if (handle == 0xffffffffu)
            return true;
        if (style & 0x100) // OF_PARSE: the structure is filled, nothing opened
        {
            result = 0;
            return true;
        }
        if (style & (0x200 | 0x4000)) // OF_DELETE and OF_EXIST keep no handle
        {
            result = 1;
            return true;
        }
        result = Win16FileWord(handle);
        if (result == 0xffff)
            files.Close(handle);
        return true;
    }
    case 81: // _lclose(handle)
    {
        // The 32-bit entry answers HFILE_ERROR and sets the last error when the
        // handle is not one of ours; the word of the table is released only
        // when the file really closed.
        const unsigned a[] = {Win16FileNative(word_at(0))};
        const unsigned answer = Api("_lclose", a);
        if (answer == 0xffffffffu)
        {
            result = 0xffff;
            return true;
        }
        Win16FileRelease(word_at(0));
        result = 0;
        return true;
    }
    case 83: // _lcreat(name, attributes)
    case 85: // _lopen(name, mode)
    {
        // The share table, the refusal of an impossible mode with 87 and the
        // device names belong to the 32-bit entry; opening the file straight
        // from here always asked for FILE_SHARE_READ, so the second task that
        // opened the same file was refused.
        const unsigned a[] = {pointer_at(0), frame.Word(0)};
        const unsigned handle = Api(ordinal == 83 ? "_lcreat" : "_lopen", a);
        if (handle == 0xffffffffu)
            return true;
        result = Win16FileWord(handle);
        if (result == 0xffff)
        {
            const unsigned close[] = {handle};
            Api("_lclose", close);
        }
        return true;
    }
    case 82:  // _lread(handle, buffer, bytes)
    case 86:  // _lwrite(handle, buffer, bytes)
    case 349: // _hread(handle, buffer, bytes)
    case 350: // _hwrite(handle, buffer, bytes)
    {
        // The transfer itself belongs to the 32-bit entry: it holds the buffer
        // pool instead of a fresh new for a DWORD count, the refusal of more
        // than eight megabytes with 8, the answer 998 for a guest buffer that
        // does not read, the last error and the rule that _lwrite
        // with a length of zero truncates the file at the current position.
        const bool huge = ordinal == 349 || ordinal == 350;
        const unsigned a[] = {Win16FileNative(word_at(0)), pointer_at(1),
                              huge ? frame.Long(0) : frame.Word(0)};
        const unsigned answer = Api(ordinal == 82   ? "_lread"
                                    : ordinal == 86 ? "_lwrite"
                                    : ordinal == 349 ? "_hread"
                                                     : "_hwrite",
                                    a);
        result = answer == 0xffffffffu && !huge ? 0xffffu : answer;
        return true;
    }
    case 84: // _llseek(handle, offset, origin)
    {
        // The seek rule and its last error belong to the 32-bit entry, like
        // the transfers above; the gate only turns the word into the handle
        // of the file session.
        const unsigned a[] = {Win16FileNative(word_at(0)), frame.Long(1), frame.Word(0)};
        result = Api("_llseek", a);
        return true;
    }
    default:
        break;
    }
    return true;
}
// The local heap of the automatic data segment. The ported heap of
// src/win98_local_heap.h works on a host pointer, and the segment lives in
// guest memory, so it is staged here and written back, the same way LocalInit
// does it.
namespace
{
// The local heap port works on a host pointer, and the segment lives in guest
// memory, so every operation runs on a copy of the segment that goes back when
// it worked. Compaction and the notification callback are not ported; the
// heap sees a provider that moves nothing and refuses nothing.
struct LocalStaging final : Win98LocalHeap::Environment
{
    unsigned char bytes[65536] = {};
    unsigned capacity = 65536, required_capacity = 0;
    bool failed = false;
    unsigned code = 0;
    LocalStaging() : Environment(bytes) {}
    // Windows 98 answers the size and the address of the largest free block after
    // moving or discarding whatever it may. This port moves and discards
    // nothing, so the free list is read as it stands; that is the outcome a
    // compaction has when it finds nothing movable. The two sentinels of the
    // list carry the allocated bit, so they drop
    // out of the walk and their ten bytes are not counted as free space.
    unsigned Compact(unsigned, unsigned &block) override
    {
        const unsigned heap = W(6);
        unsigned largest = 0, at = W(heap + 6), steps = 0;
        block = 0;
        while (++steps < 16384)
        {
            const unsigned next = W(at + 8);
            if (!next || next == at)
                break;
            at = next;
            if (!(W(at) & 1) && W(at + 4) > largest)
            {
                largest = W(at + 4);
                block = at;
            }
        }
        return largest;
    }
    // Reason zero is the out-of-memory notification. The default handler of
    // KRNL386 enlarges the automatic data segment and moves the tail
    // of the heap into the new space. Stage the new tail, then grow a dynamic
    // global block before committing the staged bytes.
    unsigned Notify(unsigned reason, unsigned, unsigned size) override
    {
        if (reason)
            return 0;
        const unsigned heap = W(6), tail = W(heap + 10);
        unsigned wanted = tail + 10 + (size > 4096 ? size : 4096u);
        if (wanted > capacity)
            wanted = capacity;
        if (!Win98LocalHeap::Grow(*this, wanted)) return 0;
        if (wanted > required_capacity) required_capacity = wanted;
        return 1;
    }
    void Error(unsigned value) override
    {
        failed = true;
        code = value;
    }
};
} // namespace
// One operation on the local heap of the segment in DS, for a gate that needs
// a block there without a 16-bit caller having asked for one. `kind` is the
// operation; the arguments are the ones of the KRNL386 entry behind it.
unsigned Application::Win16LocalOperation(unsigned kind, unsigned a, unsigned b, unsigned c)
{
    const unsigned selector = Register(Reg::Ds);
    const unsigned base = Win16Linear(selector, 0), bytes = Win16Limit(selector) + 1;
    if (!base || bytes > 65536)
        return 0;
    auto staging = new LocalStaging;
    // VB also builds local heaps in dynamically allocated global blocks.
    // The default notification may grow those segments, not just DGROUP.
    staging->capacity = Win16GlobalBlock(selector) ? 65536u : bytes;
    if (!Read(base, staging->bytes, bytes))
    {
        delete staging;
        return 0;
    }
    unsigned result = 0;
    switch (kind)
    {
    case Win16LocalAllocate: result = Win98LocalHeap::Allocate(*staging, a, b); break;
    case Win16LocalReAllocate: result = Win98LocalHeap::ReAllocate(*staging, a, b, c); break;
    // LocalFree must also handle the
    // unpatched prologue of LZEXPAND; since then no 16-bit program gave local
    // memory back. SETUPX's heap filled up after the first INF of a driver
    // index rebuild and DRVIDX.BIN kept only that INF's entries.
    case Win16LocalFree: result = Win98LocalHeap::Free(*staging, a); break;
    case Win16LocalLock: result = Win98LocalHeap::Lock(*staging, a); break;
    case Win16LocalUnlock: result = Win98LocalHeap::Unlock(*staging, a); break;
    case Win16LocalFlags: result = Win98LocalHeap::Flags(*staging, a); break;
    case Win16LocalHandle: result = Win98LocalHeap::Handle(*staging, a); break;
    case Win16LocalSizeOf:
    {
        // The size of a local block is the distance from its payload to the
        // start of the next physical block, which is the word at block+2.
        unsigned handle = 0, block = 0;
        const unsigned pointer = Win98LocalHeap::Resolve(*staging, a, handle, block);
        result = pointer && block ? (staging->W(block + 2) - pointer) & 65535 : 0u;
        break;
    }
    case Win16LocalLargestFree:
    {
        // rounds the request the same way an allocation does,
        // asks the compactor and Windows 98 takes the six bytes of a movable
        // header off a non-zero answer.
        unsigned found = 0;
        const unsigned largest = staging->Compact(Win98LocalHeap::Round(a), found);
        result = largest > 6 ? largest - 6 : 0u;
        break;
    }
    case Win16LocalHeapSize:
        // Windows 98 writes the size of the heap into the word at heap+38.
        result = staging->W(staging->W(6) + 38);
        break;
    default: break;
    }
    bool failed = staging->failed;
    unsigned destination = base, length = bytes;
    if (!failed && staging->required_capacity > bytes)
    {
        if (!Win16GlobalReAlloc(selector, staging->required_capacity, 0)) failed = true;
        else { destination = Win16Linear(selector, 0); length = Win16Limit(selector) + 1; }
    }
    if (!failed && !Store(destination, staging->bytes, length)) failed = true;
    delete staging;
    return failed ? 0u : result;
}
bool Application::Win16LocalCall(unsigned ordinal, const Win16Frame &frame, unsigned &result,
                                 unsigned &words)
{
    // The word counts are the ones the public entries of KRNL386 take off the
    // stack: LocalAlloc is `retf 4`, LocalReAlloc is
    // `retf 6`, and LocalFree, LocalLock and LocalUnlock are `retf 2`.
    words = ordinal == 5 ? 2u : ordinal == 6 ? 3u : 1u;
    result = 0;
    switch (ordinal)
    {
    case 5: // Windows 98 reads the flags out of the deeper argument word.
        result = Win16LocalOperation(Win16LocalAllocate, frame.Word(1), frame.Word(0));
        return true;
    case 6: // Windows 98 reads the handle, then the size and the flags;.
        result = Win16LocalOperation(Win16LocalReAllocate, frame.Word(2), frame.Word(1), frame.Word(0));
        return true;
    case 7:
        result = Win16LocalOperation(Win16LocalFree, frame.Word(0));
        return true;
    case 8: // LocalLock answers the near pointer of the block.
        result = Win16LocalOperation(Win16LocalLock, frame.Word(0));
        return true;
    case 9:
        result = Win16LocalOperation(Win16LocalUnlock, frame.Word(0));
        return true;
    default:
        break;
    }
    return true;
}
// Unicorn calls this for every software interrupt of the 16-bit side.
void Application::Win16Interrupt(Cpu &cpu, unsigned number, void *context)
{
    auto &app = *static_cast<Application *>(context);
    if (number == 0x10 && app.VgaInterrupt()) return;
    if (number == 0x21)
    {
        unsigned result = 0;
        if (app.Win16DosCall(result))
        {
            app.Register(Reg::Eax, (app.Register(Reg::Eax) & 0xffff0000u) | (result & 0xffff));
            // Function 4Ch ends the task. Setting the state is not enough:
            // Unicorn is inside uc_emu_start and would go on with the byte
            // behind the interrupt. WINHELP.EXE is nothing but a stub that
            // starts WINHLP32 and ends, and its segment is 2Fh bytes long, so
            // it ran off the end of its own segment.
            if (app.state == Exited || app.state == Failed)
                cpu.Stop();
            return;
        }
    }
    else if (number == 0x1a)
    {
        // The BIOS clock. Subfunction 0 answers the tick count of the day in
        // CX:DX, the ticks since midnight of the local time, and AL nonzero
        // once when midnight has passed since the last read; a 16-bit program
        // seeds its random numbers with it and times its animations by it.
        // One tick of the timer chip is 65536/1193182 of a second, 18.2 ticks a
        // second and 1573040 a day. The time of day is the guest's,
        // the one GetLocalTime and DOS function 2Ch answer.
        if (!(app.Register(Reg::Eax) & 0xff00))
        {
            const unsigned scratch = app.Win16Scratch();
            if (!scratch)
            {
                app.Fail("Win16 INT 1Ah: no scratch block for GetLocalTime");
                return;
            }
            const unsigned a[] = {scratch};
            app.Api("GetLocalTime", a);
            const unsigned day = app.Win16Word(scratch + 6), hour = app.Win16Word(scratch + 8),
                           minute = app.Win16Word(scratch + 10), second = app.Win16Word(scratch + 12),
                           milli = app.Win16Word(scratch + 14);
            const unsigned long long ms = ((hour * 60ull + minute) * 60ull + second) * 1000ull + milli;
            const unsigned ticks = unsigned(ms * 1193182ull / 65536000ull);
            auto &machine = *app.win16.state;
            const bool midnight = machine.bios_day && machine.bios_day != day + 1;
            machine.bios_day = day + 1;
            app.Register(Reg::Ecx, (ticks >> 16) & 0xffff);
            app.Register(Reg::Edx, ticks & 0xffff);
            app.Register(Reg::Eax, midnight ? 1u : 0u);
            return;
        }
        char text[128];
        snprintf(text, sizeof text, "Win16 interrupt 1ah function %02xh is not ported yet",
                 (app.Register(Reg::Eax) >> 8) & 0xff);
        app.Fail(text);
    }
    else if (number == 0x3d)
    {
        // Legacy standalone FWAIT emitted by VBRUN300 (segment ).
        // Unicorn completes x87 operations synchronously. Preserve the CPU
        // context; do not conceal a pending, unmasked arithmetic exception.
        unsigned status = 0, control = 0;
        app.Check(cpu.Get(Reg::Fpsw, status));
        app.Check(cpu.Get(Reg::Fpcw, control));
        if (!(status & ~control & 0x3f)) return;
        app.Fail("Win16 FWAIT has an unmasked floating-point exception; exception delivery is not ported yet");
    }
    else if (number == 0x11)
    {
        // The BIOS equipment list. KRNL386 reads it and takes bit 1
        // for WF_80x87; the guest of this port has a coprocessor, so the bit
        // is set. The rest of the word is what a machine without floppy
        // drives, without a game port and with the initial video mode of
        // 80 by 25 in colour answers: bit 5 and 4 are 10b for that mode.
        app.Register(Reg::Eax,
                     (app.Register(Reg::Eax) & 0xffff0000u) | 0x0022u);
        return;
    }
    else if (number == 0x12)
    {
        if (app.Win16SetupxRealModeCall())
            return;
        // The BIOS memory size: the conventional memory in kilobytes, 640 on
        // every AT. The system summary the Device Manager prints
        // asks for it.
        app.Register(Reg::Eax, (app.Register(Reg::Eax) & 0xffff0000u) | 640u);
        return;
    }
    else if (number == 0x2f && (app.Register(Reg::Eax) & 0xff00) == 0x1500)
    {
        // MSCDEX (src/x86_mscdex.cpp). The functions with real mode addresses
        // come through DPMI 0300h instead.
        unsigned ax = app.Register(Reg::Eax) & 0xffff, bx = app.Register(Reg::Ebx) & 0xffff,
                 cx = app.Register(Reg::Ecx) & 0xffff;
        bool carry = false;
        if (!app.MscdexInterrupt(ax, bx, cx, carry))
        {
            char text[160];
            snprintf(text, sizeof text, "MSCDEX function %04xh in protected mode at %04x:%04x is not ported yet", ax,
                     app.Register(Reg::Cs), app.Register(Reg::Eip) & 0xffff);
            app.Fail(text);
            return;
        }
        app.Register(Reg::Eax, (app.Register(Reg::Eax) & 0xffff0000u) | ax);
        app.Register(Reg::Ebx, (app.Register(Reg::Ebx) & 0xffff0000u) | bx);
        app.Register(Reg::Ecx, (app.Register(Reg::Ecx) & 0xffff0000u) | cx);
        app.Register(Reg::Eflags, carry ? app.Register(Reg::Eflags) | 1u : app.Register(Reg::Eflags) & ~1u);
        return;
    }
    else if (number == 0x2f)
    {
        // The DOS multiplex interrupt. A program asks whether a resident
        // extension is there; none is, and the answer of DOS to that is AL
        // unchanged, which for the installation check AL = 0 means "not
        // installed". Nothing of this port installs a multiplex handler.
        // 1684h, Get Device Entry Point, is the VMM's own function: BX names
        // a VxD, and ES:DI comes back as the entry of its protected-mode API
        // or 0:0 for a device without one. The VxDs of this port are native
        // and offer no such entry to 16-bit code, so the answer is 0:0; the
        // caller then leaves the device alone (SYSDM's Performance page asks
        // IOS, 10h, and checks for 0:0 ). Leaving
        // ES:DI as they were sent SYSDM into a far call through them.
        // 1600h, the enhanced mode installation check, is VMM's as well: it
        // answers the Windows version with the major in AL and the minor in
        // AH, 4.10 for Windows 98 (0A04h). NETAPI.DLL asks it and
        // takes the protected mode INT 21h path when AL has a version; with
        // AL = 0 it went to DPMI 0300h for real mode DOS.
        if ((app.Register(Reg::Eax) & 0xffff) == 0x1600)
            app.Register(Reg::Eax, (app.Register(Reg::Eax) & 0xffff0000u) | 0x0a04u);
        if ((app.Register(Reg::Eax) & 0xffff) == 0x1684)
        {
            // CONFIGMG (33h) is the exception: its protected-mode API is
            // native, src/win16_config_manager.cpp.
            unsigned entry = 0;
            if ((app.Register(Reg::Ebx) & 0xffff) == 0x33)
                if (const unsigned gate = app.Win16Gate("CONFIGMG", "PM_API", 1))
                    entry = app.Win16GatePointer(gate);
            app.Register(Reg::Es, entry >> 16);
            app.Register(Reg::Edi, (app.Register(Reg::Edi) & 0xffff0000u) | (entry & 0xffff));
        }
        app.Register(Reg::Eflags, app.Register(Reg::Eflags) & ~1u);
        return;
    }
    else if (number == 0x31)
    {
        // DPMI, which the virtual machine manager serves to every Win16 task.
        // Only the functions a module of the collection is seen calling are
        // here; any other one stops with its number.
        const unsigned function = app.Register(Reg::Eax) & 0xffff;
        const unsigned flags = app.Register(Reg::Eflags);
        auto answer = [&](unsigned error) {
            if (error)
                app.Register(Reg::Eax, (app.Register(Reg::Eax) & 0xffff0000u) | error);
            app.Register(Reg::Eflags, error ? flags | 1u : flags & ~1u);
        };
        if (function == 0x0008)
        {
            // Set segment limit: BX the selector, CX:DX the limit. A limit
            // above 1 MiB must end on a page (its low twelve bits all ones),
            // because the descriptor then counts pages; DPMI answers 8025h
            // otherwise and 8022h for a selector that is not allocated.
            // MSVIDEO sizes its selector over the DCI surface this way.
            const unsigned selector = app.Register(Reg::Ebx) & 0xffff;
            const unsigned limit = (app.Register(Reg::Ecx) & 0xffff) << 16 | (app.Register(Reg::Edx) & 0xffff);
            auto *entry = app.selectors.Find(selector << 16);
            if (!entry || !(selector & 4))
                return answer(0x8022);
            if (limit > 0xfffff && (limit & 0xfff) != 0xfff)
                return answer(0x8025);
            const unsigned previous = entry->limit;
            entry->limit = limit;
            if (!app.Win16Descriptor(selector))
            {
                entry->limit = previous;
                return answer(0x8022);
            }
            return answer(0);
        }
        if (function == 0x000a)
        {
            // Create alias descriptor: BX the selector, AX the new one, a
            // read/write data descriptor over the same base and limit. DPMI
            // answers 8022h for a selector that is not allocated and 8011h
            // when no descriptor is free. WLOADER.EXE (Coktel) writes into its
            // own code this way right after it found 256 colours.
            const unsigned selector = app.Register(Reg::Ebx) & 0xffff;
            const auto *entry = app.selectors.Find(selector << 16);
            if (!entry || !selector)
                return answer(0x8022);
            const unsigned alias = app.Win16Segment(entry->base, entry->limit, false, false);
            if (!alias)
                return answer(0x8011);
            app.Register(Reg::Eax, (app.Register(Reg::Eax) & 0xffff0000u) | alias);
            return answer(0);
        }
        if (function == 0x0000)
        {
            // Allocate LDT descriptors: CX of them, data, base 0, limit 0,
            // the first selector in AX. Only one at a time here: consecutive
            // selectors are not promised by this port's table, and a request
            // for more answers 8011h like a full LDT. NETAPI.DLL asks one for
            // DOS's collating table.
            if ((app.Register(Reg::Ecx) & 0xffff) != 1)
                return answer(0x8011);
            const unsigned selector = app.Win16Segment(0, 0, false, false);
            if (!selector)
                return answer(0x8011);
            app.Register(Reg::Eax, (app.Register(Reg::Eax) & 0xffff0000u) | selector);
            return answer(0);
        }
        if (function == 0x0001)
        {
            // Free an LDT descriptor, BX the selector; 8022h for one that is
            // not allocated.
            const unsigned selector = app.Register(Reg::Ebx) & 0xffff;
            if (!selector || !app.selectors.Find(selector << 16))
                return answer(0x8022);
            app.selectors.Unmap(selector << 16);
            return answer(0);
        }
        if (function == 0x0006 || function == 0x0007)
        {
            // Get (0006h) and set (0007h) the base of the descriptor in BX,
            // CX:DX the linear address. The first megabyte and the 64 KiB
            // above it are the virtual machine's own memory, which Win98 has
            // at linear 0 and this port in the block of
            // Win16RealModeSelector; a base there is moved into that block,
            // so a selector a program builds from a real mode address (the
            // table of INT 21h 6506h) reaches the same bytes as __0000H.
            const unsigned selector = app.Register(Reg::Ebx) & 0xffff;
            auto *entry = app.selectors.Find(selector << 16);
            if (!selector || !entry)
                return answer(0x8022);
            const unsigned image = app.Win16RealModeImage();
            if (function == 0x0006)
            {
                unsigned base = entry->base;
                if (image && base >= image && base - image < 0x110000)
                    base -= image;
                app.Register(Reg::Ecx, (app.Register(Reg::Ecx) & 0xffff0000u) | (base >> 16));
                app.Register(Reg::Edx, (app.Register(Reg::Edx) & 0xffff0000u) | (base & 0xffff));
                return answer(0);
            }
            unsigned base = (app.Register(Reg::Ecx) & 0xffff) << 16 | (app.Register(Reg::Edx) & 0xffff);
            if (base >= 0xa0000 && base < 0xc0000)
            {
                if (!app.EnsureVga()) return answer(0x8025);
            }
            else if (base < 0x110000)
            {
                if (!image)
                    return answer(0x8025);
                base += image;
            }
            const unsigned previous = entry->base;
            entry->base = base;
            if (!app.Win16Descriptor(selector))
            {
                entry->base = previous;
                return answer(0x8025);
            }
            return answer(0);
        }
        if (function == 0x0301)
        {
            // Call a real mode far procedure: ES:DI the call structure with
            // CS:IP at +2Ch/+2Ah and AX at +1Ch. The only real mode code a
            // program reaches here is DOS's case map routine of INT 21h 6501h, and that runs natively.
            const unsigned structure =
                app.Win16Linear(app.Register(Reg::Es), app.Register(Reg::Edi) & 0xffff);
            if (structure)
            {
                unsigned ax = app.Win16Word(structure + 0x1c);
                if (app.Win16DosCaseMap(app.Win16Word(structure + 0x2c), app.Win16Word(structure + 0x2a), ax))
                {
                    app.Win16Word(structure + 0x1c, ax);
                    return answer(0);
                }
            }
            char text[160];
            snprintf(text, sizeof text, "Win16 DPMI 0301h to real mode %04x:%04x at %04x:%04x is not ported yet",
                     structure ? app.Win16Word(structure + 0x2c) : 0, structure ? app.Win16Word(structure + 0x2a) : 0,
                     app.Register(Reg::Cs), app.Register(Reg::Eip) & 0xffff);
            app.Fail(text);
            return;
        }
        if (function == 0x0303 || function == 0x0304)
        {
            // Allocate (0303h) and free (0304h) a real mode callback address:
            // real mode code that far-calls CX:DX reaches the protected mode
            // procedure DS:SI with the registers in ES:DI. No real mode code
            // runs in this port, so nothing ever calls one; the addresses are
            // distinct entries of one segment, as VMM hands them out, and
            // 8015h says the table is full, 8024h that an address is not
            // one of them. NETAPI.DLL takes two at its start
            // and gives them back when it ends.
            enum : unsigned { CallbackSegment = 0xf000, CallbackBase = 0xe000, CallbackCount = 16 };
            static bool used[CallbackCount];
            if (function == 0x0303)
            {
                for (unsigned i = 0; i < CallbackCount; ++i)
                    if (!used[i])
                    {
                        used[i] = true;
                        app.Register(Reg::Ecx, (app.Register(Reg::Ecx) & 0xffff0000u) | CallbackSegment);
                        app.Register(Reg::Edx, (app.Register(Reg::Edx) & 0xffff0000u) | (CallbackBase + i * 4));
                        return answer(0);
                    }
                return answer(0x8015);
            }
            const unsigned segment = app.Register(Reg::Ecx) & 0xffff, offset = app.Register(Reg::Edx) & 0xffff;
            const unsigned index = (offset - CallbackBase) / 4;
            if (segment != CallbackSegment || offset < CallbackBase || (offset - CallbackBase) % 4 ||
                index >= CallbackCount || !used[index])
                return answer(0x8024);
            used[index] = false;
            return answer(0);
        }
        if (function == 0x0500)
        {
            // Get free memory information: 30h bytes at ES:DI. KRNL386 asks
            // it at startup and sets WF_PAGING from the size of the
            // paging file at +20h; the fields are those of
            // _GetDemandPageInfo, as VMM answers them.
            unsigned info[12];
            if (!app.DpmiFreeMemoryInfo(info))
                return answer(0x8013);
            const unsigned structure =
                app.Win16Linear(app.Register(Reg::Es), app.Register(Reg::Edi) & 0xffff);
            if (!structure)
                return answer(0x8021);
            app.Store(structure, info, sizeof info);
            return answer(0);
        }
        if (function == 0x0300)
        {
            // Simulate real mode interrupt: BL the interrupt, ES:DI the real
            // mode call structure (registers, flags, segments). The only real
            // mode service here is MSCDEX on INT 2Fh (src/x86_mscdex.cpp).
            const unsigned vector = app.Register(Reg::Ebx) & 0xff;
            const unsigned structure =
                app.Win16Linear(app.Register(Reg::Es), app.Register(Reg::Edi) & 0xffff);
            const unsigned ax = structure ? app.Win16Word(structure + 0x1c) : 0;
            if (vector == 0x2f && (ax & 0xff00) == 0x1500 && app.MscdexRealMode(structure))
                return answer(0);
            // INT 21h AX=3305h, the boot drive in DL (1 is A:): the drive C:
            // Win98 starts from, as the protected mode call answers it.
            // MSISYS.OCX of MSINFO32 asks so.
            if (vector == 0x21 && ax == 0x3305 && structure)
            {
                app.Win16Word(structure + 0x14, (app.Win16Word(structure + 0x14) & 0xff00) | 3);
                app.Win16Word(structure + 0x20, app.Win16Word(structure + 0x20) & ~1u);
                return answer(0);
            }
            // INT 21h AH=44h, the device control group: DOS answers it as
            // the protected mode call.
            if (vector == 0x21 && structure && app.Win16RealModeDos(structure))
                return answer(0);
            char text[160];
            snprintf(text, sizeof text,
                     "Win16 DPMI 0300h for real mode INT %02xh AX=%04xh at %04x:%04x is not ported yet", vector,
                     ax, app.Register(Reg::Cs), app.Register(Reg::Eip) & 0xffff);
            app.Fail(text);
            return;
        }
        char text[160];
        snprintf(text, sizeof text, "Win16 DPMI function %04xh (INT 31h) at %04x:%04x is not ported yet", function,
                 app.Register(Reg::Cs), app.Register(Reg::Eip) & 0xffff);
        app.Fail(text);
        return;
    }
    else if (number == 0x25 || number == 0x26)
    {
        // The absolute sector read and write. They reach the block device
        // driver, and this port has no sectors at all: the tree of files is a
        // native file session, not a file system on a disk image. DOS answers
        // a drive that cannot carry the call with carry set and the error in
        // AX, AL two for "drive not ready". The caller has that path, because
        // a program that reads sectors has to cope with a drive that refuses.
        // Both leave the flags word of the caller on the stack; the caller
        // pops it itself, and every caller does. Unicorn hands an interrupt to
        // this hook without pushing anything, so the word has to be put there
        // or the caller's own `pop` takes a word of its frame and the next far
        // return lands nowhere. That was a general protection fault at
        // in WINFILE.
        const unsigned flags = app.Register(Reg::Eflags) | 1u;
        const unsigned pointer = (app.Register(Reg::Esp) - 2) & 0xffff;
        app.Win16Word(app.Win16Linear(app.Register(Reg::Ss), pointer), flags & 0xffff);
        app.Register(Reg::Esp, pointer);
        app.Register(Reg::Eax, 0x0002);
        app.Register(Reg::Eflags, flags);
        return;
    }
    else if (number == 0x0b)
    {
        // A segment that is not present. The only descriptors of this port
        // without their presence bit are the blocks of the global heap that
        // carry no memory, so the instruction loaded the selector of a
        // discarded block instead of asking GlobalReAlloc for it back.
        char text[200];
        snprintf(text, sizeof text,
                 "Win16 segment not present at %04x:%04x; the selector belongs to a discarded global "
                 "block that was not reallocated",
                 app.Register(Reg::Cs), app.Register(Reg::Eip) & 0xffff);
        app.Fail(text);
    }
    else if (number == 0x0d || number == 0x08 || number == 0 || number == 3 || number == 0x0c)
    {
        // A fault or trap the program may catch with TOOLHELP's
        // InterruptRegister: the callbacks run first, and only
        // when every one passes it on does the port stop with its diagnosis.
        if (app.Win16ToolhelpFault(number == 0x08 ? 0x0d : number))
            return;
        app.Win16FaultStop(number);
    }
    else
        app.Win16FaultStop(number);
    cpu.Stop();
}
// The stop of the port for a fault of 16-bit code that nothing caught: the
// general protection fault with the bytes around it, every other interrupt
// with its number.
bool Application::Win16FaultStop(unsigned number)
{
    if (number == 0x0d || number == 0x08)
    {
        // Not a call. Unicorn raises interrupt 13 for a general protection
        // fault of the 16-bit side, and 8 behind it because no interrupt
        // descriptor table catches the first one. EIP still stands on the
        // instruction that faulted, so the bytes of that instruction and the
        // segment registers around it belong into the diagnosis; without them
        // the message names an address that nobody can place.
        char text[480], where[220] = {};
        Win16DescribeAddress(where, sizeof where, Register(Reg::Cs), Register(Reg::Eip) & 0xffff);
        // The top of the stack says what a `pop` or a far return was about to
        // load, which is the usual source of the fault.
        const unsigned top = Win16Linear(Register(Reg::Ss), Register(Reg::Esp) & 0xffff);
        snprintf(text, sizeof text,
                 "Win16 general protection fault at %04x:%04x (interrupt %02xh); no descriptor of the "
                 "LDT covers what the instruction reached%s, ds=%04x es=%04x ss=%04x:%04x "
                 "eax=%08x ebx=%08x ecx=%08x edx=%08x esi=%08x edi=%08x bp=%04x stack=%04x %04x %04x %04x",
                 Register(Reg::Cs), Register(Reg::Eip) & 0xffff, number, where,
                 Register(Reg::Ds), Register(Reg::Es),
                 Register(Reg::Ss), Register(Reg::Esp) & 0xffff,
                 Register(Reg::Eax), Register(Reg::Ebx),
                 Register(Reg::Ecx), Register(Reg::Edx),
                 Register(Reg::Esi), Register(Reg::Edi),
                 Register(Reg::Ebp) & 0xffff, top ? Win16Word(top) : 0,
                 top ? Win16Word(top + 2) : 0, top ? Win16Word(top + 4) : 0,
                 top ? Win16Word(top + 6) : 0);
        if (log)
            log(text);
        Fail(text);
        return false;
    }
    char text[128];
    snprintf(text, sizeof text, "Win16 interrupt %02xh at %04x:%04x is not ported yet", number, Register(Reg::Cs),
             Register(Reg::Eip) & 0xffff);
    Fail(text);
    return false;
}
// DOS3Call carries the DOS function in AH and answers in AX with the carry
// flag. Only the calls a Win16 startup really makes are here; every other one
// stops the run instead of answering something invented.
// The search of DOS functions 4Eh and 4Fh. The record of the disk transfer
// address is the one every DOS since 2.0 writes: twenty one bytes the search
// carries between the two calls, the attribute byte at 15h, the packed time
// and date at 16h and 18h, the size at 1Ah and the eight-three name at 1Eh.
// This port keeps the search of the session, not a DOS directory cluster, so
// the twenty one bytes carry a mark and the index of the open search.
// The volume label for a 4Eh search with the label bit: the label of the
// drive of the pattern, attribute 8, in the name field of the transfer area
// with a dot after the eighth character, as DOS writes the eleven bytes of
// the directory entry. A drive without a label has nothing to find.
bool Application::Win16DosVolumeLabel(unsigned pattern, unsigned dta, unsigned &error)
{
    const char *text = ReadString(pattern);
    char root[4] = {text && text[0] && text[1] == ':' ? text[0] : files.current[0], ':', '\\', 0};
    const unsigned scratch = Win16Scratch();
    if (!scratch)
        return false;
    Store(scratch, root, 4);
    const unsigned label = scratch + 16;
    const unsigned a[] = {scratch, label, 32, 0, 0, 0, 0, 0};
    if (!Api("GetVolumeInformationA", a) || !Read32(label))
    {
        error = 18;
        return false;
    }
    const char *source = ReadString(label);
    unsigned char name[14] = {};
    unsigned at = 0, count = 0;
    while (source && source[count] && count < 11)
    {
        if (count == 8)
            name[at++] = '.';
        const unsigned char c = (unsigned char)source[count++];
        name[at++] = c >= 'a' && c <= 'z' ? (unsigned char)(c - 32) : c;
    }
    unsigned char state[0x2b] = {};
    state[0x15] = 8;
    memcpy(state + 0x1e, name, 13);
    Store(dta, state, sizeof state);
    return true;
}
bool Application::Win16DosFind(bool first, unsigned dta, unsigned &error)
{
    auto &machine = *win16.state;
    const unsigned scratch = Win16Scratch();
    if (!scratch)
        return false;
    // The 32-bit WIN32_FIND_DATA: attributes, three file times, the two size
    // halves, two reserved dwords and the two names.
    const unsigned data = scratch + 512;
    // CX of 4Eh is the search attribute, and the search keeps it in the
    // reserved part of the transfer area for 4Fh. DOS always returns plain
    // files; one that is hidden, system or a directory only when its bit is
    // in the mask. The volume label bit asks for the label alone. Without the
    // filter WINFILE listed the hidden RECYCLED folder in its tree and named
    // the drive after the first file of the root, "[DBLSPACEBIN]".
    unsigned char mask = 0;
    if (first)
        mask = (unsigned char)(Register(Reg::Ecx) & 0xff);
    else
        Read(dta + 4, &mask, 1);
    if (first)
    {
        if (machine.find_handle)
        {
            const unsigned close[] = {machine.find_handle};
            Api("FindClose", close);
            machine.find_handle = 0;
        }
        const unsigned pattern = Win16Linear(Register(Reg::Ds), Register(Reg::Edx) & 0xffff);
        if (!pattern)
        {
            error = 2;
            return false;
        }
        // The label alone is the answer when the label bit comes without
        // the hidden, system and directory bits. With them (CX = 1Fh, "every
        // entry") DOS searches the directory, where a label can only stand in
        // the root: SETUPX asks with 1Fh whether C:\WINDOWS\SETUP.INI exists
        // before it writes the Start menu groups of an INF, and the label of
        // C: made that file seem to exist.
        if ((mask & 8) && !(mask & 0x16))
            return Win16DosVolumeLabel(pattern, dta, error);
        const unsigned a[] = {pattern, data};
        const unsigned handle = Api("FindFirstFileA", a);
        if (!handle || handle == 0xffffffffu)
        {
            error = 2; // ERROR_FILE_NOT_FOUND, the answer of 4Eh with no match
            return false;
        }
        machine.find_handle = handle;
        Win16Word(dta, 0x4446); // "FD", so 4Fh can tell a search of ours apart
        Win16Word(dta + 2, 1);
        Store(dta + 4, &mask, 1);
    }
    else
    {
        if (Win16Word(dta) != 0x4446 || !machine.find_handle)
        {
            error = 18; // ERROR_NO_MORE_FILES
            return false;
        }
        const unsigned a[] = {machine.find_handle, data};
        if (!Api("FindNextFileA", a))
        {
            const unsigned close[] = {machine.find_handle};
            Api("FindClose", close);
            machine.find_handle = 0;
            error = 18;
            return false;
        }
    }
    // Skip what the mask does not ask for: hidden (2), system (4) and
    // directory (10h) entries without their bit.
    while (Read32(data) & 0x16 & ~(unsigned)mask)
    {
        const unsigned a[] = {machine.find_handle, data};
        if (!Api("FindNextFileA", a))
        {
            const unsigned close[] = {machine.find_handle};
            Api("FindClose", close);
            machine.find_handle = 0;
            error = first ? 2 : 18;
            return false;
        }
    }
    // The write time of the record is the packed DOS pair.
    const unsigned pair = scratch + 16;
    const unsigned convert[] = {data + 20, pair, pair + 2};
    Api("FileTimeToDosDateTime", convert);
    const unsigned attributes = Read32(data), size = Read32(data + 32);
    unsigned char name[14] = {};
    const char *source = ReadString(Read32(data + 304) || Win16Word(data + 304) ? data + 304 : data + 44);
    if (!source || !*source)
        source = ReadString(data + 44);
    unsigned at = 0;
    while (source && *source && at < 12)
    {
        unsigned char c = (unsigned char)*source++;
        name[at++] = c >= 'a' && c <= 'z' ? (unsigned char)(c - 32) : c;
    }
    name[at] = 0;
    unsigned char state[21] = {};
    Read(dta, state, sizeof state);
    Store(dta, state, sizeof state);
    unsigned char byte = (unsigned char)(attributes & 0xff);
    Store(dta + 0x15, &byte, 1);
    Win16Word(dta + 0x16, Win16Word(pair + 2));
    Win16Word(dta + 0x18, Win16Word(pair));
    Win16Word(dta + 0x1a, size & 0xffff);
    Win16Word(dta + 0x1c, size >> 16);
    Store(dta + 0x1e, name, 13);
    if (log && trace_calls)
    {
        char text[96];
        snprintf(text, sizeof text, "WIN16DOS found %s attributes=%02x size=%u", (const char *)name, attributes & 0xff, size);
        log(text);
    }
    return true;
}
// The search of DOS functions 11h and 12h, the one that goes through a file
// control block. DS:DX names an unopened FCB, and the answer is another one in
// the disk transfer address: the drive at 0, the eight-three name at 1, the
// attribute at 0Ch, the time at 16h, the date at 18h and the size at 1Ch. An
// extended block carries FFh and five zeroes in front, the attribute to search
// for at 6, and the block itself at 7; the answer keeps that shape. AL is zero
// for a match and FFh for none, which is the only thing a caller reads.
bool Application::Win16DosFileControlFind(bool first, unsigned &result, unsigned flags)
{
    auto &machine = *win16.state;
    const unsigned ax = Register(Reg::Eax) & 0xffff;
    const unsigned block = Win16Linear(Register(Reg::Ds), Register(Reg::Edx) & 0xffff);
    const unsigned dta = machine.transfer_address
                             ? machine.transfer_address
                             : machine.command_prefix + Win98NeTask::ProgramPrefixCommand;
    if (!block)
    {
        result = (ax & 0xff00) | 0xff;
        Register(Reg::Eflags, flags & ~1u);
        machine.RememberDosError(2);
        return true;
    }
    unsigned char head = 0;
    Read(block, &head, 1);
    const bool extended = head == 0xff;
    const unsigned body = extended ? block + 7 : block;
    unsigned char attribute = 0;
    if (extended)
        Read(block + 6, &attribute, 1);
    unsigned char name[12] = {};
    Read(body, name, 1);
    const unsigned drive = name[0];
    Read(body + 1, name, 11);
    const unsigned scratch = Win16Scratch();
    if (!scratch)
        return false;
    const unsigned data = scratch + 512;
    // The attribute of an extended block filters as CX of 4Eh does: plain
    // files always, hidden, system and directory entries only with their
    // bit, and the volume label bit asks for the label alone. A plain block
    // searches with attribute 0. WINFILE reads the drive label this way
    // (11h with an extended block, attribute 8) and named C: after the first
    // file of the root, "[DBLSPACEBIN]".
    const unsigned char mask = extended ? attribute : 0;
    if (first && (mask & 8))
    {
        if (machine.find_handle)
        {
            const unsigned close[] = {machine.find_handle};
            Api("FindClose", close);
            machine.find_handle = 0;
        }
        const char root[4] = {drive ? (char)('A' + drive - 1) : files.current[0], ':', '\\', 0};
        Store(scratch, root, 4);
        const unsigned label = scratch + 16;
        const unsigned a[] = {scratch, label, 32, 0, 0, 0, 0, 0};
        if (!Api("GetVolumeInformationA", a) || !Read32(label))
        {
            result = (ax & 0xff00) | 0xff;
            Register(Reg::Eflags, flags & ~1u);
            machine.RememberDosError(18);
            return true;
        }
        // The label is the eleven bytes of its directory entry, blank filled.
        unsigned char answer[11];
        memset(answer, ' ', sizeof answer);
        const char *text = ReadString(label);
        for (unsigned i = 0; text && text[i] && i < 11; ++i)
            answer[i] = (unsigned char)(text[i] >= 'a' && text[i] <= 'z' ? text[i] - 32 : text[i]);
        unsigned char prefix[7] = {0xff, 0, 0, 0, 0, 0, attribute};
        Store(dta, prefix, sizeof prefix);
        const unsigned char letter = (unsigned char)(drive ? drive :
            ((files.current[0] >= 'a' ? files.current[0] - 'a' : files.current[0] - 'A') + 1));
        unsigned char entry[0x20] = {};
        entry[0] = letter;
        memcpy(entry + 1, answer, 11);
        entry[0x0c] = 8;
        Store(dta + 7, entry, sizeof entry);
        result = ax & 0xff00;
        Register(Reg::Eflags, flags & ~1u);
        return true;
    }
    if (first)
    {
        if (machine.find_handle)
        {
            const unsigned close[] = {machine.find_handle};
            Api("FindClose", close);
            machine.find_handle = 0;
        }
        // The pattern: the directory of the drive the block names, then the
        // eight and the three with their trailing blanks cut off.
        char directory[260] = {}, pattern[280];
        files.Directory(drive ? (char)('A' + drive - 1) : files.current[0], directory);
        unsigned at = 0;
        while (directory[at] && at < 250)
            ++at;
        if (at && directory[at - 1] != '\\' && at < 250)
            directory[at++] = '\\';
        directory[at] = 0;
        unsigned length = 0;
        for (unsigned i = 0; i < 8 && name[i] != ' '; ++i)
            pattern[length++] = (char)name[i];
        unsigned tail = 0;
        for (unsigned i = 8; i < 11 && name[i] != ' '; ++i)
            ++tail;
        if (tail)
        {
            pattern[length++] = '.';
            for (unsigned i = 8; i < 8 + tail; ++i)
                pattern[length++] = (char)name[i];
        }
        pattern[length] = 0;
        char full[300];
        snprintf(full, sizeof full, "%s%s", directory, pattern);
        Store(scratch, full, (unsigned)strlen(full) + 1);
        const unsigned a[] = {scratch, data};
        const unsigned handle = Api("FindFirstFileA", a);
        if (!handle || handle == 0xffffffffu)
        {
            result = (ax & 0xff00) | 0xff;
            Register(Reg::Eflags, flags & ~1u);
            machine.RememberDosError(2);
            return true;
        }
        machine.find_handle = handle;
    }
    else
    {
        const unsigned a[] = {machine.find_handle, data};
        if (!machine.find_handle || !Api("FindNextFileA", a))
        {
            if (machine.find_handle)
            {
                const unsigned close[] = {machine.find_handle};
                Api("FindClose", close);
                machine.find_handle = 0;
            }
            result = (ax & 0xff00) | 0xff;
            Register(Reg::Eflags, flags & ~1u);
            machine.RememberDosError(18);
            return true;
        }
    }
    // Skip what the attribute does not ask for.
    while (machine.find_handle && (Read32(data) & 0x16 & ~(unsigned)mask))
    {
        const unsigned a[] = {machine.find_handle, data};
        if (!Api("FindNextFileA", a))
        {
            const unsigned close[] = {machine.find_handle};
            Api("FindClose", close);
            machine.find_handle = 0;
            result = (ax & 0xff00) | 0xff;
            Register(Reg::Eflags, flags & ~1u);
            machine.RememberDosError(first ? 2 : 18);
            return true;
        }
    }
    const unsigned pair = scratch + 16;
    const unsigned convert[] = {data + 20, pair, pair + 2};
    Api("FileTimeToDosDateTime", convert);
    const unsigned attributes = Read32(data), size = Read32(data + 32);
    // The eight and the three of the answer, blank filled, in upper case.
    unsigned char answer[11];
    for (unsigned i = 0; i < 11; ++i)
        answer[i] = ' ';
    const char *found = ReadString(data + 44);
    unsigned i = 0, position = 0;
    while (found && found[i] && found[i] != '.' && position < 8)
    {
        const unsigned char c = (unsigned char)found[i++];
        answer[position++] = c >= 'a' && c <= 'z' ? (unsigned char)(c - 32) : c;
    }
    while (found && found[i] && found[i] != '.')
        ++i;
    if (found && found[i] == '.')
    {
        ++i;
        position = 8;
        while (found[i] && position < 11)
        {
            const unsigned char c = (unsigned char)found[i++];
            answer[position++] = c >= 'a' && c <= 'z' ? (unsigned char)(c - 32) : c;
        }
    }
    unsigned out = dta;
    if (extended)
    {
        unsigned char prefix[7] = {0xff, 0, 0, 0, 0, 0, (unsigned char)attribute};
        Store(dta, prefix, sizeof prefix);
        out = dta + 7;
    }
    const unsigned char letter = (unsigned char)(drive ? drive :
        ((files.current[0] >= 'a' ? files.current[0] - 'a' : files.current[0] - 'A') + 1));
    Store(out, &letter, 1);
    Store(out + 1, answer, 11);
    const unsigned char kind = (unsigned char)(attributes & 0xff);
    Store(out + 0x0c, &kind, 1);
    Win16Word(out + 0x16, Win16Word(pair + 2));
    Win16Word(out + 0x18, Win16Word(pair));
    Win16Word(out + 0x1a, 0); // the first cluster, which this port has none of
    Win16Word(out + 0x1c, size & 0xffff);
    Win16Word(out + 0x1e, size >> 16);
    result = ax & 0xff00;
    Register(Reg::Eflags, flags & ~1u);
    return true;
}
// Function 71h, the long file name group of Win98. The session of this port
// keeps long names anyway, so the subfunctions that only differ in the name
// length are the short name functions. A subfunction that is not here answers
// the way a DOS without the extension answers: carry set and AX = 7100h, and
// every caller of this group has that fall back because the group is optional.
// MKDIR (39h) and RMDIR (3Ah) of DS:DX, the short and the long name form.
// The DOS answer for a directory that is there already, or that cannot go, is
// 5 (access denied); only a missing parent or path is 3. The WinZip
// self-extractor of Clickomania makes every folder of its target path and
// takes 5 as "is there", but stopped at 3 with "Could not create".
unsigned Application::Win16DosDirectory(bool create, unsigned source)
{
    if (!source)
        return 3;
    const unsigned a[] = {source, 0};
    last_error = 0;
    if (Api(create ? "CreateDirectoryA" : "RemoveDirectoryA", a))
        return 0;
    return last_error == 3 || last_error == 2 || last_error == 123 ? 3u : 5u;
}
bool Application::Win16DosLongName(unsigned ax, unsigned &result, unsigned flags)
{
    auto &machine = *win16.state;
    const unsigned sub = ax & 0xff;
    auto refuse = [&]() {
        Register(Reg::Eflags, flags | 1u);
        result = 0x7100;
        return true;
    };
    auto answer = [&](unsigned error) {
        Register(Reg::Eflags, error ? flags | 1u : flags & ~1u);
        result = error ? error : ax;
        if (error)
            machine.RememberDosError(error);
        return true;
    };
    const unsigned source = Win16Linear(Register(Reg::Ds), Register(Reg::Edx) & 0xffff);
    switch (sub)
    {
    case 0x39: // Create a directory.
    case 0x3a: // Remove a directory.
        return answer(Win16DosDirectory(sub == 0x39, source));
    case 0x3b: // Set the current directory.
    {
        const unsigned a[] = {source};
        return answer(source && Api("SetCurrentDirectoryA", a) ? 0u : 3u);
    }
    case 0x41: // Delete a file.
    {
        const unsigned a[] = {source};
        return answer(source && Api("DeleteFileA", a) ? 0u : 2u);
    }
    case 0x43: // Get (BL = 0) or set (BL = 1) the attributes.
    {
        const unsigned which = Register(Reg::Ebx) & 0xff;
        if (!source)
            return answer(2);
        if (which == 0)
        {
            const unsigned a[] = {source};
            const unsigned attributes = Api("GetFileAttributesA", a);
            if (attributes == 0xffffffffu)
                return answer(2);
            Register(Reg::Ecx, attributes & 0xffff);
            return answer(0);
        }
        if (which == 1)
        {
            const unsigned a[] = {source, Register(Reg::Ecx) & 0xffff};
            return answer(Api("SetFileAttributesA", a) ? 0u : 5u);
        }
        return refuse();
    }
    case 0x47: // The current directory into DS:SI, without the drive letter.
    {
        const unsigned at = Win16Linear(Register(Reg::Ds), Register(Reg::Esi) & 0xffff);
        const unsigned drive = Register(Reg::Edx) & 0xff;
        char directory[260] = {};
        files.Directory(drive ? (char)('A' + drive - 1) : 'C', directory);
        const char *tail = directory;
        if (tail[0] && tail[1] == ':')
            tail += 2;
        if (*tail == '\\')
            ++tail;
        if (at)
            PutString(at, tail, 260);
        return answer(0);
    }
    case 0x56: // Rename, the new name in ES:DI.
    {
        const unsigned target = Win16Linear(Register(Reg::Es), Register(Reg::Edi) & 0xffff);
        const unsigned a[] = {source, target};
        return answer(source && target && Api("MoveFileA", a) ? 0u : 2u);
    }
    case 0x60: // Truename. CL picks the form; this port answers the full path
               // for every one of them, because its session has one spelling.
               // The name is at DS:SI, not DS:DX (SETUPX asks for the
               // short form of the INF directory this way).
    {
        const unsigned name = Win16Linear(Register(Reg::Ds), Register(Reg::Esi) & 0xffff);
        const unsigned target = Win16Linear(Register(Reg::Es), Register(Reg::Edi) & 0xffff);
        if (!name || !target)
            return answer(2);
        char full[260];
        if (files.FullPath(ReadString(name), full))
            return answer(2);
        PutString(target, full, 260);
        return answer(0);
    }
    case 0xa1: // Close a search.
        if (machine.find_handle)
        {
            const unsigned a[] = {machine.find_handle};
            Api("FindClose", a);
            machine.find_handle = 0;
        }
        return answer(0);
    default:
        break;
    }
    return refuse();
}
// Win98 reflects a real mode INT 21h of DPMI 0300h into the virtual machine,
// where DOS answers it; IFSMGR and the block drivers see it as they see the
// protected mode call. This port has one DOS, Win16DosCall, so the call goes
// there with the registers of the structure at ES:DI (EDI, ESI, EBP, -, EBX,
// EDX, ECX, EAX at 00h..1Ch, the flags at 20h, ES and DS at 22h and 24h).
// DS and ES are real mode segments there: for the call they become selectors
// of the same bytes of the first megabyte, as DPMI 0002h makes them, and are
// freed after it. Only the device control group (AH = 44h) comes this way so
// far: DEFRAG, DRVSPACE and DSKMAINT ask their drive questions through it, and it hands no segment back.
bool Application::Win16RealModeDos(unsigned structure)
{
    const unsigned ax = Win16Word(structure + 0x1c);
    if ((ax >> 8) != 0x44)
        return false;
    const unsigned image = Win16RealModeImage();
    if (!image)
        return false;
    static const int order[8] = {Reg::Edi, Reg::Esi, Reg::Ebp, -1, Reg::Ebx, Reg::Edx, Reg::Ecx, Reg::Eax};
    unsigned saved[8] = {};
    for (unsigned i = 0; i < 8; ++i)
        if (order[i] >= 0)
            saved[i] = Register(order[i]);
    const unsigned saved_ds = Register(Reg::Ds), saved_es = Register(Reg::Es), saved_flags = Register(Reg::Eflags);
    const unsigned es = Win16Segment(image + Win16Word(structure + 0x22) * 16, 0xffff, false, false);
    const unsigned ds = Win16Segment(image + Win16Word(structure + 0x24) * 16, 0xffff, false, false);
    bool answered = false;
    if (es && ds)
    {
        for (unsigned i = 0; i < 8; ++i)
            if (order[i] >= 0)
                Register(order[i], Read32(structure + i * 4));
        Register(Reg::Ds, ds);
        Register(Reg::Es, es);
        Register(Reg::Eflags, saved_flags & ~1u);
        unsigned result = 0;
        answered = Win16DosCall(result);
        if (answered)
        {
            Register(Reg::Eax, (Register(Reg::Eax) & 0xffff0000u) | (result & 0xffff));
            for (unsigned i = 0; i < 8; ++i)
                if (order[i] >= 0)
                    Write32(structure + i * 4, Register(order[i]));
            Win16Word(structure + 0x20, (Win16Word(structure + 0x20) & ~1u) | (Register(Reg::Eflags) & 1u));
        }
        for (unsigned i = 0; i < 8; ++i)
            if (order[i] >= 0)
                Register(order[i], saved[i]);
        Register(Reg::Ds, saved_ds);
        Register(Reg::Es, saved_es);
        Register(Reg::Eflags, saved_flags);
    }
    if (es)
        selectors.Unmap(es << 16);
    if (ds)
        selectors.Unmap(ds << 16);
    return answered;
}
bool Application::Win16DosCall(unsigned &result)
{
    auto &machine = *win16.state;
    const unsigned ax = Register(Reg::Eax) & 0xffff, function = ax >> 8;
    unsigned flags = Register(Reg::Eflags);
    if (log && trace_calls)
    {
        char text[400];
        const unsigned at = Win16Linear(Register(Reg::Ds), Register(Reg::Edx) & 0xffff);
        const char *name = at ? ReadString(at) : nullptr;
        snprintf(text, sizeof text, "WIN16DOS ax=%04x bx=%04x cx=%04x dx=%04x si=%04x ds:dx=%.200s", ax,
                 Register(Reg::Ebx) & 0xffff, Register(Reg::Ecx) & 0xffff, Register(Reg::Edx) & 0xffff,
                 Register(Reg::Esi) & 0xffff, name && name[0] >= 0x20 ? name : "-");
        log(text);
    }
    switch (function)
    {
    case 0x3b: // CHDIR DS:DX. AL is not a subfunction of this DOS entry.
    {
        const unsigned name = Win16Linear(Register(Reg::Ds), Register(Reg::Edx) & 0xffff);
        char previous[260];
        CopyText(previous, files.current, sizeof previous);
        unsigned error = name ? files.SetDirectory(ReadString(name)) : 3u;
        // Unlike SetCurrentDirectoryA, DOS CHDIR on another drive changes
        // that drive's saved directory without selecting it as default.
        if (!error && files.current[0] != previous[0])
            files.Adopt(previous);
        if (error == 2 || error == 123 || error == 267) error = 3;
        Register(Reg::Eflags, error ? flags | 1u : flags & ~1u);
        result = error ? error : ax;
        if (error) machine.RememberDosError(error);
        return true;
    }
    case 0x33: // 05h, the boot drive in DL, A being one: C. The page
               // Hardware Profiles of SYSDM asks before it copies a
               // profile. 00h and 01h read and set the break
               // flag, which no program of this session is ever broken by:
               // it reads back as off. 06h is the true version, 7.10.
        if ((ax & 0xff) == 0x05 || (ax & 0xff) <= 0x01)
        {
            if ((ax & 0xff) != 0x01)
                Register(Reg::Edx, (Register(Reg::Edx) & 0xffffff00u) | ((ax & 0xff) == 0x05 ? 3u : 0u));
            result = ax;
            Register(Reg::Eflags, flags & ~1u);
            return true;
        }
        if ((ax & 0xff) == 0x06)
        {
            Register(Reg::Ebx, (Register(Reg::Ebx) & 0xffff0000u) | 0x0a07u);
            Register(Reg::Edx, Register(Reg::Edx) & 0xffff0000u);
            result = ax;
            Register(Reg::Eflags, flags & ~1u);
            return true;
        }
        break;
    case 0x30: // Get DOS version: 7.10 under Win98.
        result = 0x0a07;
        Register(Reg::Eflags, flags & ~1u);
        return true;
    case 0x25: // Set interrupt vector: the 16-bit runtime installs its own
               // critical error handler. Nothing of ours calls it.
        result = ax;
        Register(Reg::Eflags, flags & ~1u);
        return true;
    case 0x35: // Get interrupt vector. The runtime keeps the old handler to
               // put it back; no vector of ours is installed, so it reads
               // back as zero, which is what an unhooked vector is.
        Register(Reg::Es, 0);
        Register(Reg::Ebx, 0);
        result = ax;
        Register(Reg::Eflags, flags & ~1u);
        return true;
    case 0x52: // Get the list of lists: ES:BX names SysVars in the data
               // segment of DOS. Under Win98 the DOS extender hands a
               // protected mode program a selector over that segment. The
               // one caller met, IOSCLASS.DLL (the page Settings of
               // a drive in the Device Manager), does not use BX at all: it
               // reads the byte at ES:0047h, LASTDRIVE at +21h of the
               // SysVars that DOS 7 keeps at 0026h of its segment. The
               // segment here is paragraph 0080h of the first megabyte,
               // empty but for that byte: 26 drives, the LASTDRIVE of a
               // Win98 without the line in CONFIG.SYS.
    {
        constexpr unsigned DosDataParagraph = 0x0080, SysVars = 0x26, LastDrive = SysVars + 0x21;
        const unsigned selector = Win16RealModeSelector(DosDataParagraph);
        if (!selector)
            break;
        const unsigned char drives = 26;
        Store(Win16RealModeImage() + DosDataParagraph * 16 + LastDrive, &drives, 1);
        Register(Reg::Es, selector);
        Register(Reg::Ebx, (Register(Reg::Ebx) & 0xffff0000u) | SysVars);
        result = ax;
        Register(Reg::Eflags, flags & ~1u);
        return true;
    }
    case 0x43: // Get (AL = 0) or set (AL = 1) the attributes of DS:DX.
    {
        const unsigned name = Win16Linear(Register(Reg::Ds), Register(Reg::Edx) & 0xffff);
        if (name)
        {
            if ((ax & 255) == 0)
            {
                const unsigned a[] = {name};
                const unsigned attributes = Api("GetFileAttributesA", a);
                if (attributes != 0xffffffffu)
                {
                    Register(Reg::Ecx, attributes & 0xffff);
                    Register(Reg::Eflags, flags & ~1u);
                    result = attributes & 0xffff;
                    return true;
                }
            }
            else if ((ax & 255) == 1)
            {
                const unsigned a[] = {name, Register(Reg::Ecx) & 0xffff};
                if (Api("SetFileAttributesA", a))
                {
                    Register(Reg::Eflags, flags & ~1u);
                    result = ax;
                    return true;
                }
            }
            else
                break;
        }
        // The DOS contract: carry set and the error number in AX.
        Register(Reg::Eflags, flags | 1u);
        result = 2; // ERROR_FILE_NOT_FOUND
        machine.RememberDosError(result);
        return true;
    }
    case 0x2a: // Get the date: CX the year, DH the month, DL the day, AL the
               // weekday. The clock of the run is the one KERNEL32 reads.
    case 0x2c: // Get the time: CH the hour, CL the minute, DH the second, DL
               // the hundredth.
    {
        const unsigned scratch = Win16Scratch();
        if (!scratch)
            return false;
        const unsigned a[] = {scratch};
        Api("GetLocalTime", a);
        const unsigned year = Win16Word(scratch), month = Win16Word(scratch + 2),
                       weekday = Win16Word(scratch + 4), day = Win16Word(scratch + 6),
                       hour = Win16Word(scratch + 8), minute = Win16Word(scratch + 10),
                       second = Win16Word(scratch + 12), milli = Win16Word(scratch + 14);
        if (function == 0x2a)
        {
            Register(Reg::Ecx, year);
            Register(Reg::Edx, (month << 8) | day);
            result = (ax & 0xff00) | (weekday & 7);
        }
        else
        {
            Register(Reg::Ecx, (hour << 8) | minute);
            Register(Reg::Edx, (second << 8) | (milli / 10));
            result = ax & 0xff00;
        }
        Register(Reg::Eflags, flags & ~1u);
        return true;
    }
    case 0x2f: // Get the disk transfer address: the one function 1Ah set, or
               // the default of the program prefix at 80h, which is where a
               // DOS program finds its own command line.
        Register(Reg::Es, machine.transfer_selector ? machine.transfer_selector : machine.prefix);
        Register(Reg::Ebx, machine.transfer_selector ? machine.transfer_offset : 0x80u);
        result = ax;
        Register(Reg::Eflags, flags & ~1u);
        return true;
    case 0x3d: // Open DS:DX with the access and sharing mode in AL.
    case 0x3c: // Create DS:DX with the attributes in CX.
    case 0x5b: // Create a new file: as 3Ch, but a file that is there already
               // is refused with 50h, ERROR_FILE_EXISTS. SETUPX creates its
               // work files with it.
    {
        // The DOS layer of Win98 goes through the same file management as the
        // Win16 entries: AL of 3Dh is the OF_ access and share mode _lopen
        // takes, CX of 3Ch the attribute of _lcreat. The share table, the
        // device names and the error of a refused mode belong to that entry; its last error is the DOS error, the numbers are the same.
        const unsigned name = Win16Linear(Register(Reg::Ds), Register(Reg::Edx) & 0xffff);
        // Bit 7 of AL keeps the handle from a child's handle table; no DOS
        // child inherits the table of a Win16 task here, so it is dropped.
        const bool create = function == 0x3c || function == 0x5b;
        const unsigned a[] = {name, create ? Register(Reg::Ecx) & 0xffff : ax & 0x7f};
        if (function == 0x5b && name)
        {
            const unsigned probe[] = {name};
            if (Api("GetFileAttributesA", probe) != 0xffffffffu)
            {
                Register(Reg::Eflags, flags | 1u);
                result = 0x50;
                machine.RememberDosError(result);
                return true;
            }
        }
        last_error = 0;
        const unsigned handle = name ? Api(create ? "_lcreat" : "_lopen", a) : 0xffffffffu;
        if (handle == 0xffffffffu)
        {
            Register(Reg::Eflags, flags | 1u);
            result = name && last_error ? last_error & 0xffff : 2u;
            if (result == 87) // an access or share mode DOS does not know
                result = 0x0c; // ERROR_INVALID_ACCESS
            machine.RememberDosError(result);
            return true;
        }
        const unsigned word = Win16FileWord(handle);
        if (word == 0xffff)
        {
            const unsigned close[] = {handle};
            Api("_lclose", close);
            Register(Reg::Eflags, flags | 1u);
            result = 4; // ERROR_TOO_MANY_OPEN_FILES
            machine.RememberDosError(result);
            return true;
        }
        Register(Reg::Eflags, flags & ~1u);
        result = word;
        return true;
    }
    case 0x56: // Rename DS:DX to ES:DI, also into another directory of the
               // same drive; an existing target is refused. The rule is
               // MoveFile's, whose last error is the DOS error.
    {
        const unsigned from = Win16Linear(Register(Reg::Ds), Register(Reg::Edx) & 0xffff);
        const unsigned to = Win16Linear(Register(Reg::Es), Register(Reg::Edi) & 0xffff);
        last_error = 0;
        const unsigned a[] = {from, to};
        const bool moved = from && to && Api("MoveFileA", a);
        if (!moved)
        {
            Register(Reg::Eflags, flags | 1u);
            result = from && to && last_error ? last_error & 0xffff : 2u;
            if (result == 183) // ERROR_ALREADY_EXISTS: DOS says access denied
                result = 5;
            machine.RememberDosError(result);
            return true;
        }
        Register(Reg::Eflags, flags & ~1u);
        result = ax;
        return true;
    }
    case 0x3e: // Close the handle in BX.
    {
        const unsigned word = Register(Reg::Ebx) & 0xffff;
        if (Win16StandardHandle(word))
        {
            machine.standard_closed |= 1u << word;
            Register(Reg::Eflags, flags & ~1u);
            result = ax;
            return true;
        }
        const unsigned native = Win16FileNative(word);
        const unsigned error = native ? files.Close(native) : 6u;
        if (!error)
            Win16FileRelease(word);
        Register(Reg::Eflags, error ? flags | 1u : flags & ~1u);
        result = error ? error : ax;
        if (error) machine.RememberDosError(error);
        return true;
    }
    case 0x3f: // Read CX bytes from BX into DS:DX.
    case 0x40: // Write CX bytes from DS:DX to BX.
    {
        // _lread and _lwrite, like the gates KERNEL.82 and 86: a write of
        // zero bytes sets the end of the file at the current position, as
        // DOS function 40h does. A standard device takes every
        // byte and reads nothing.
        if (Win16StandardHandle(Register(Reg::Ebx)))
        {
            Register(Reg::Eflags, flags & ~1u);
            result = function == 0x40 ? Register(Reg::Ecx) & 0xffff : 0u;
            return true;
        }
        const unsigned a[] = {Win16FileNative(Register(Reg::Ebx) & 0xffff),
                              Win16Linear(Register(Reg::Ds), Register(Reg::Edx) & 0xffff),
                              Register(Reg::Ecx) & 0xffff};
        last_error = 0;
        const unsigned done = a[0] ? Api(function == 0x3f ? "_lread" : "_lwrite", a) : 0xffffffffu;
        const unsigned error = done == 0xffffffffu ? (a[0] && last_error ? last_error & 0xffff : 6u) : 0u;
        Register(Reg::Eflags, error ? flags | 1u : flags & ~1u);
        result = error ? error : done;
        if (error) machine.RememberDosError(error);
        return true;
    }
    case 0x42: // Seek the handle in BX to CX:DX with the origin in AL.
    {
        if (Win16StandardHandle(Register(Reg::Ebx))) // a device stays at zero
        {
            Register(Reg::Edx, 0);
            Register(Reg::Eflags, flags & ~1u);
            result = 0;
            return true;
        }
        const unsigned handle = Win16FileNative(Register(Reg::Ebx) & 0xffff);
        const unsigned offset =
            ((Register(Reg::Ecx) & 0xffff) << 16) | (Register(Reg::Edx) & 0xffff);
        unsigned position = 0;
        const unsigned error = files.Seek(handle, (int)offset, ax & 255, position);
        if (error)
        {
            Register(Reg::Eflags, flags | 1u);
            result = error;
            machine.RememberDosError(result);
            return true;
        }
        Register(Reg::Edx, position >> 16);
        Register(Reg::Eflags, flags & ~1u);
        result = position & 0xffff;
        return true;
    }
    case 0x57: // The date and time of the handle in BX: AL = 0 reads the last
               // write time into CX (time) and DX (date), AL = 1 sets it. The
               // native session keeps the stamp in DOS form already.
    {
        const unsigned handle = Win16FileNative(Register(Reg::Ebx) & 0xffff);
        unsigned error = 1; // ERROR_INVALID_FUNCTION for another subfunction
        Files::TimeStamp stamp;
        if ((ax & 255) == 0 && !(error = files.GetTime(handle, 2, stamp)))
        {
            Register(Reg::Ecx, stamp.time & 0xffff);
            Register(Reg::Edx, stamp.date & 0xffff);
        }
        else if ((ax & 255) == 1)
        {
            stamp.time = Register(Reg::Ecx) & 0xffff;
            stamp.date = Register(Reg::Edx) & 0xffff;
            error = files.SetTime(handle, 2, stamp);
        }
        if (error)
        {
            Register(Reg::Eflags, flags | 1u);
            result = error;
            machine.RememberDosError(result);
            return true;
        }
        Register(Reg::Eflags, flags & ~1u);
        result = ax;
        return true;
    }
    case 0x44: // The device control group. Subfunction 0 asks for the device
               // information word of the handle in BX; a file of the mounted
               // tree is never a character device. The three a file manager
               // asks are about the drive in BL, and every drive of this guest
               // is a local fixed disk: 08h answers one for "not removable",
               // 09h clears the remote bit of DX and 0Eh answers zero for "no
               // other letter maps to it".
    {
        const unsigned sub = ax & 255;
        if (sub == 0 && Win16StandardHandle(Register(Reg::Ebx)))
        {
            // The device information words DOS keeps for the standard
            // devices: CON is a device with the console input and output
            // bits (80D3h), AUX a plain device (80C0h) and PRN a device that
            // takes output until busy (A8C0h).
            const unsigned handle = Register(Reg::Ebx) & 0xffff;
            Register(Reg::Edx, handle < 3 ? 0x80d3u : handle == 3 ? 0x80c0u : 0xa8c0u);
            Register(Reg::Eflags, flags & ~1u);
            result = ax;
            return true;
        }
        if (sub == 0)
        {
            const unsigned native = Win16FileNative(Register(Reg::Ebx) & 0xffff);
            const Files::Node *node = nullptr;
            if (!native || files.Information(native, node))
            {
                Register(Reg::Eflags, flags | 1u); result = 6; machine.RememberDosError(result); return true;
            }
            Register(Reg::Edx, 0);
            Register(Reg::Eflags, flags & ~1u);
            result = ax;
            return true;
        }
        if (sub == 8 || sub == 9 || sub == 0x0e)
        {
            unsigned drive = Register(Reg::Ebx) & 0xff;
            char letter = drive ? char('A' + drive - 1) : files.current[0];
            if (!files.store.Drive(letter))
            {
                Register(Reg::Eflags, flags | 1u); result = 15; machine.RememberDosError(result); return true;
            }
            Register(Reg::Eflags, flags & ~1u);
            if (sub == 8) result = files.store.DriveType(letter) == 2 ? 0u : 1u;
            else { Register(Reg::Edx, 0); result = ax & 0xff00; }
            return true;
        }
        if (sub == 0x0d && (Register(Reg::Ecx) & 0xffff) == 0x086f)
        {
            // Get Drive Map Info (category 08h, minor 6Fh), which IFSMGR
            // answers for every drive rather than a block driver: the
            // DRIVE_MAP_INFO at DS:DX gets its length, the flags and the
            // BIOS unit. Every drive of this port is served by the protected
            // mode file system, so bit 0 (PROT_MODE_LOGICAL_DRIVE) is set; a
            // fixed disk also has its physical drive in protected mode (bit
            // 1, BIOS unit 80h), a CD-ROM is ejectable (bit 3) and has no
            // BIOS unit. SYSDM's Performance page reports "MS-DOS
            // compatibility mode" for a drive without bit 0 (SYSDM,

            unsigned drive = Register(Reg::Ebx) & 0xff;
            char letter = drive ? char('A' + drive - 1) : files.current[0];
            const unsigned at = Win16Linear(Register(Reg::Ds), Register(Reg::Edx) & 0xffff);
            if (!files.store.Drive(letter) || !at)
            {
                Register(Reg::Eflags, flags | 1u); result = 15; machine.RememberDosError(result); return true;
            }
            const unsigned type = files.store.DriveType(letter);
            const unsigned char length = 0x10;
            unsigned char info[16] = {};
            Read(at, info, 1);
            info[1] = length;
            info[2] = type == 5 ? 0x09 : type == 3 ? 0x03 : 0x01;
            info[3] = type == 3 ? 0x80 : 0xff;
            const unsigned map = 1u << ((letter | 0x20) - 'a');
            memcpy(info + 4, &map, 4);
            const unsigned capacity = info[0] ? (info[0] < length ? info[0] : length) : length;
            Store(at, info, capacity);
            Register(Reg::Eflags, flags & ~1u);
            result = ax;
            return true;
        }
        if (sub == 0x02 || sub == 0x03 || sub == 0x10 || sub == 0x04 || sub == 0x05 || sub == 0x11)
        {
            // The control channel of a device (02h/03h by handle, 04h/05h by
            // the drive in BL) and the query whether a generic call is
            // carried (10h by handle, 11h by drive). DOS reads the driver's
            // attribute word first, and no device of this port says it
            // carries them: a file is no device, and function 09h answers
            // zero attributes for every drive. DOS then answers
            // ERROR_INVALID_FUNCTION, and 15 for a drive that is not there.
            // DRVSPACE asks the drive's driver so.
            unsigned error = 1;
            if (sub == 0x04 || sub == 0x05 || sub == 0x11)
            {
                const unsigned drive = Register(Reg::Ebx) & 0xff;
                if (!files.store.Drive(drive ? char('A' + drive - 1) : files.current[0]))
                    error = 15;
            }
            Register(Reg::Eflags, flags | 1u);
            result = error;
            machine.RememberDosError(result);
            return true;
        }
        if (sub == 0x0d)
        {
            // The generic control call of a block device, CX the category and
            // the function. It reaches the block driver, and this port has
            // none: the tree of files is a native file session. DOS answers
            // for a device that does not carry the call with carry and
            // ERROR_INVALID_FUNCTION, and every caller of the group has that
            // path because the call is optional.
            Register(Reg::Eflags, flags | 1u);
            result = 1;
            machine.RememberDosError(result);
            return true;
        }
        break;
    }
    case 0x1a: // Set the disk transfer address to DS:DX. Only find-first and
               // find-next read it, and they are not carried, so it is kept
               // and handed back by function 2Fh.
        machine.transfer_address = Win16Linear(Register(Reg::Ds), Register(Reg::Edx) & 0xffff);
        machine.transfer_selector = Register(Reg::Ds);
        machine.transfer_offset = Register(Reg::Edx) & 0xffff;
        result = ax;
        Register(Reg::Eflags, flags & ~1u);
        return true;
    case 0x0d: // Reset the disk: every write of this port is already through,
               // so there is nothing to flush.
        result = ax;
        Register(Reg::Eflags, flags & ~1u);
        return true;
    case 0x0e: // Select the drive in DL. Real DOS returns its LASTDRIVE-style
               // upper bound, which includes potentially valid letters. This
               // host has no unmounted DOS device slots, so its corresponding
               // upper bound is one beyond the highest mounted letter. An
               // absent letter leaves the current drive unchanged.
    {
        const unsigned wanted = Register(Reg::Edx) & 0xff;
        if (wanted < 26 && files.store.Drive(char('A' + wanted)))
        {
            char directory[260];
            files.Directory(char('A' + wanted), directory);
            files.Adopt(directory);
        }
        unsigned mask = files.store.DriveMask(), count = 0;
        while (mask) { ++count; mask >>= 1; }
        result = (ax & 0xff00) | count;
        Register(Reg::Eflags, flags & ~1u);
        return true;
    }
    case 0x36: // Free space of the drive in DL: AX sectors per cluster, BX
               // free clusters, CX bytes per sector, DX clusters in all.
    {
        char root[4] = {Register(Reg::Edx) & 0xff
                            ? (char)('A' + (Register(Reg::Edx) & 0xff) - 1)
                            : 'C',
                        ':', '\\', 0};
        const unsigned scratch = Win16Scratch();
        if (!scratch)
            return false;
        Store(scratch, root, sizeof root);
        const unsigned a[] = {scratch, scratch + 8, scratch + 12, scratch + 16, scratch + 20};
        if (!Api("GetDiskFreeSpaceA", a))
        {
            result = 0xffff; // the DOS answer for an invalid drive
            Register(Reg::Eflags, flags & ~1u);
            return true;
        }
        const unsigned per_cluster = Read32(scratch + 8), bytes = Read32(scratch + 12),
                       free_clusters = Read32(scratch + 16), clusters = Read32(scratch + 20);
        Register(Reg::Ebx, free_clusters > 0xffff ? 0xffffu : free_clusters);
        Register(Reg::Ecx, bytes);
        Register(Reg::Edx, clusters > 0xffff ? 0xffffu : clusters);
        result = per_cluster;
        Register(Reg::Eflags, flags & ~1u);
        return true;
    }
    case 0x73: // The FAT32 functions. 03h, the free space of the drive whose
               // path DS:DX names, as the ExtFreeSpace record at ES:DI, CX
               // bytes of it: size and version, sectors per cluster, bytes
               // per sector, free and all clusters, the same two in
               // physical sectors and once more in allocation units. The
               // Virtual Memory dialog of SYSDM lists the drives with it. The figures are the ones GetDiskFreeSpace gives.
               // Any other subfunction answers as a DOS without FAT32:
               // carry and AX = 7300h.
    {
        if ((ax & 0xff) != 0x03)
        {
            Register(Reg::Eflags, flags | 1u);
            result = 0x7300;
            return true;
        }
        const unsigned path = Win16Linear(Register(Reg::Ds), Register(Reg::Edx) & 0xffff);
        const unsigned buffer = Win16Linear(Register(Reg::Es), Register(Reg::Edi) & 0xffff);
        const unsigned capacity = Register(Reg::Ecx) & 0xffff;
        const unsigned scratch = Win16Scratch();
        if (!scratch)
            return false;
        const char *given = path ? ReadString(path) : "";
        char root[4] = {given[0] && given[1] == ':' ? given[0] : 'C', ':', '\\', 0};
        Store(scratch, root, sizeof root);
        const unsigned a[] = {scratch, scratch + 8, scratch + 12, scratch + 16, scratch + 20};
        if (!buffer || capacity < 4 || !Api("GetDiskFreeSpaceA", a))
        {
            Register(Reg::Eflags, flags | 1u);
            result = 15; // ERROR_INVALID_DRIVE
            machine.RememberDosError(result);
            return true;
        }
        const unsigned per_cluster = Read32(scratch + 8), bytes = Read32(scratch + 12),
                       free_clusters = Read32(scratch + 16), clusters = Read32(scratch + 20);
        unsigned char record[0x2c] = {};
        const unsigned fields[] = {per_cluster, bytes, free_clusters, clusters, free_clusters * per_cluster,
                                   clusters * per_cluster, free_clusters, clusters};
        record[0] = sizeof record;
        memcpy(record + 4, fields, sizeof fields);
        Store(buffer, record, capacity < sizeof record ? capacity : (unsigned)sizeof record);
        Register(Reg::Eflags, flags & ~1u);
        result = ax;
        return true;
    }
    case 0x39: // MKDIR DS:DX.
    case 0x3a: // RMDIR DS:DX.
    {
        const unsigned error =
            Win16DosDirectory(function == 0x39, Win16Linear(Register(Reg::Ds), Register(Reg::Edx) & 0xffff));
        Register(Reg::Eflags, error ? flags | 1u : flags & ~1u);
        result = error ? error : ax;
        if (error) machine.RememberDosError(error);
        return true;
    }
    case 0x41: // Delete DS:DX.
    {
        const unsigned name = Win16Linear(Register(Reg::Ds), Register(Reg::Edx) & 0xffff);
        char full[260];
        const unsigned a[] = {name};
        const unsigned error = !name || files.FullPath(ReadString(name), full) ? 2u
                               : Api("DeleteFileA", a)                        ? 0u
                                                                              : 5u;
        Register(Reg::Eflags, error ? flags | 1u : flags & ~1u);
        result = error ? error : ax;
        if (error) machine.RememberDosError(error);
        return true;
    }
    case 0x47: // Get the current directory of the drive in DL into DS:SI,
               // without the drive letter and without a leading backslash.
    {
        const unsigned at = Win16Linear(Register(Reg::Ds), Register(Reg::Esi) & 0xffff);
        const unsigned drive = Register(Reg::Edx) & 0xff;
        char directory[260] = {};
        files.Directory(drive ? (char)('A' + drive - 1) : files.current[0], directory);
        const char *tail = directory;
        if (tail[0] && tail[1] == ':')
            tail += 2;
        if (*tail == '\\')
            ++tail;
        if (at)
            PutString(at, tail, 64);
        Register(Reg::Eflags, flags & ~1u);
        result = ax;
        return true;
    }
    case 0x19: // Get the current drive as a number, A being zero.
        result = (ax & 0xff00) | ((files.current[0] >= 'a' ? files.current[0] - 'a' : files.current[0] - 'A') & 0xff);
        Register(Reg::Eflags, flags & ~1u);
        return true;
    case 0x51: // Get the PSP: BX the selector of the task's PDB, the same
    case 0x62: // word as GetCurrentPDB (KERNEL.37). NETDI's page "Access
               // Control" asks it with 51h.
        result = ax;
        Register(Reg::Ebx, (Register(Reg::Ebx) & 0xffff0000u) | (machine.prefix & 0xffff));
        return true;
    case 0x59: // Get extended error: AX, BH class, BL action, CH locus.
        result = machine.dos_error;
        Register(Reg::Ebx, (machine.dos_error_class << 8) | machine.dos_error_action);
        Register(Reg::Ecx, machine.dos_error_locus << 8);
        Register(Reg::Eflags, flags & ~1u);
        return true;
    case 0x11: // Find first through a file control block.
    case 0x12: // Find next through the one in the transfer address.
        return Win16DosFileControlFind(function == 0x11, result, flags);
    case 0x4e: // Find first: DS:DX names the pattern, CX the attributes, and
               // the answer goes into the disk transfer address.
    case 0x4f: // Find next: the search of the transfer address goes on.
    {
        const unsigned dta = machine.transfer_address
                                 ? machine.transfer_address
                                 : machine.command_prefix + Win98NeTask::ProgramPrefixCommand;
        unsigned error = 0;
        if (!Win16DosFind(function == 0x4e, dta, error))
        {
            if (state == Failed)
                return false;
            Register(Reg::Eflags, flags | 1u);
            result = error;
            machine.RememberDosError(error);
            return true;
        }
        Register(Reg::Eflags, flags & ~1u);
        result = ax;
        return true;
    }
    case 0x5c: // Lock (AL = 0) and unlock (AL = 1) a region of the file in BX,
               // CX:DX the start and SI:DI the length. The native session has
               // both; the DOS answer is carry and the error in AX.
    {
        const unsigned handle = Win16FileNative(Register(Reg::Ebx) & 0xffff);
        const unsigned start = ((Register(Reg::Ecx) & 0xffff) << 16) |
                               (Register(Reg::Edx) & 0xffff);
        const unsigned length = ((Register(Reg::Esi) & 0xffff) << 16) |
                                (Register(Reg::Edi) & 0xffff);
        if (!handle)
        {
            Register(Reg::Eflags, flags | 1u);
            result = 6; // ERROR_INVALID_HANDLE
            machine.RememberDosError(result);
            return true;
        }
        const unsigned a[] = {handle, start, 0, length, 0};
        const bool done = Api((ax & 255) == 0 ? "LockFile" : "UnlockFile", a) != 0;
        Register(Reg::Eflags, done ? flags & ~1u : flags | 1u);
        result = done ? ax : 33u; // ERROR_LOCK_VIOLATION
        if (!done) machine.RememberDosError(result);
        return true;
    }
    case 0x5f: // Network redirection. 5F02h and 5F05h walk the redirection
               // list by the index in BX; the machine has no redirected drive
               // or printer, so the first index already answers what DOS
               // answers past the end of the list: carry and ERROR_NO_MORE_FILES.
               // DXSETUP's post-reboot step (rundll32 dxdllreg.inf) asks.
        if ((ax & 0xff) == 0x02 || (ax & 0xff) == 0x05)
        {
            Register(Reg::Eflags, flags | 1u);
            result = 18;
            machine.RememberDosError(result);
            return true;
        }
        break;
    case 0x65: // Extended country information. 06h answers the collating
               // table: five bytes at ES:DI (06h and its real mode address),
               // CX = 5. The table is DOS's own, a word 256 and the 256
               // bytes of the order, in the first megabyte; Win98 without
               // COUNTRY= in CONFIG.SYS has the built-in one of country 001
               // with code page 437, the same as COUNTRY.SYS carries for it.
               // NETAPI.DLL reads it through a selector of its own (

        if ((ax & 0xff) == 0x06)
        {
            const unsigned address = Win16DosCountry() ? unsigned(DosCollateAddress) : 0u;
            const unsigned buffer = Win16Linear(Register(Reg::Es), Register(Reg::Edi) & 0xffff);
            if (!address || !buffer || (Register(Reg::Ecx) & 0xffff) < 5)
            {
                Register(Reg::Eflags, flags | 1u);
                result = address ? 1 : 2;
                machine.RememberDosError(result);
                return true;
            }
            const unsigned char answer[5] = {6, (unsigned char)(address & 15), 0,
                                             (unsigned char)(address >> 4), (unsigned char)(address >> 12)};
            Store(buffer, answer, sizeof answer);
            Register(Reg::Ecx, (Register(Reg::Ecx) & 0xffff0000u) | 5u);
            Register(Reg::Eflags, flags & ~1u);
            result = ax;
            return true;
        }
        // 01h, the general information: 01h, the word size 26h and the
        // country information of 3801h behind country and code page, as
        // much as CX bytes hold; CX comes back as the bytes written. BX and
        // DX name code page and country, FFFFh the current ones (NETAPI.DLL

        if ((ax & 0xff) == 0x01)
        {
            const unsigned buffer = Win16Linear(Register(Reg::Es), Register(Reg::Edi) & 0xffff);
            const unsigned capacity = Register(Reg::Ecx) & 0xffff;
            const unsigned page = Register(Reg::Ebx) & 0xffff, country = Register(Reg::Edx) & 0xffff;
            if (!Win16DosCountry() || !buffer || capacity < 5 || (page != 0xffff && page != 437) ||
                (country != 0xffff && country != 1))
            {
                Register(Reg::Eflags, flags | 1u);
                result = !buffer || capacity < 5 ? 1 : 2;
                machine.RememberDosError(result);
                return true;
            }
            unsigned char answer[3 + 0x26] = {1, 0x26, 0};
            memcpy(answer + 3, machine.country_info, 0x26);
            const unsigned bytes = capacity < sizeof answer ? capacity : unsigned(sizeof answer);
            Store(buffer, answer, bytes);
            Register(Reg::Ecx, (Register(Reg::Ecx) & 0xffff0000u) | bytes);
            Register(Reg::Eflags, flags & ~1u);
            result = ax;
            return true;
        }
        break;
    case 0x68: // Commit the file in BX (the detection of Add New Hardware,
    case 0x6a: // ). 6Ah is the same call under its DOS 4 number, AL is
               // not a subfunction. Every write
               // of this port is already through to the host file, so a valid
               // handle has nothing left to write; a bad one gets carry and
               // ERROR_INVALID_HANDLE as from DOS.
    {
        const unsigned word = Register(Reg::Ebx) & 0xffff;
        if (Win16StandardHandle(word) || Win16FileNative(word))
        {
            Register(Reg::Eflags, flags & ~1u);
            result = ax;
            return true;
        }
        Register(Reg::Eflags, flags | 1u);
        result = 6;
        machine.RememberDosError(result);
        return true;
    }
    case 0x71: // The long file name functions of Win98. AL picks the one, and
               // every one of them is the short name function with the same
               // number on a session that has long names anyway. A function
               // this port does not carry answers the way DOS without the
               // extension answers it: carry set and AX = 7100h.
        return Win16DosLongName(ax, result, flags);
    case 0x4c: // Terminate with an exit code. This is how the C runtime of a
               // 16-bit program leaves after its window is gone.
        // A task with TOOLHELP notifications first tells them NFY_EXITTASK
        // (ptrace event 62h, TOOLHELP ); that runs 16-bit code, so the
        // end moves to a gate where it can (Win16ToolhelpExit).
        if (Win16ToolhelpExit(ax & 255))
        {
            result = ax;
            return true;
        }
        result = ax;
        exit_code = ax & 255;
        state = Exited;
        if (user_initialized) User::DropPointerLevel(process_id, 0); // USER,
        if (user_initialized) User::DestroyProcessWindows(process_id); // USER
        return true;
    default:
        break;
    }
    char text[160];
    snprintf(text, sizeof text, "Win16 DOS3Call function %02xh subfunction %02xh is not ported yet",
             function, ax & 0xff);
    Fail(text);
    return false;
}
// The route of src/x86_modules.cpp for an NE file that a running process asks
// for by name. A 16-bit DLL of the tree is loaded as original code.
unsigned Application::Win16LoadLibrary(const char *path, const unsigned char *bytes, unsigned length,
                                       unsigned &error)
{
    // A 32-bit process that loads an NE image hosts it; see
    // src/x86_win16_hosted.cpp.
    if (!win16.state && !Win16HostedReady())
    {
        error = 8;
        return 0;
    }
    if (Win16Module *found = Win16Find(path))
    {
        ++found->references;
        return found->handle;
    }
    auto *copy = new unsigned char[length];
    memcpy(copy, bytes, length);
    Win16Module *module = Win16Place(path, copy, length, true, error);
    return module ? module->handle : 0;
}
} // namespace X86
