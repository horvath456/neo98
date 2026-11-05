// SPDX-License-Identifier: GPL-2.0-or-later
// Native connection for the Win98 flat thunk connector and the 32-to-16
// dispatch. The original decisions live in win98_flat_thunk.h. NEO98 has no
// 16-bit module: a transition lands in the native target table below, which
// calls a native SHELL.DLL routine. Unmapped indices stop the run.
//
// The connector publishes a token table at ThunkData32+8 in place of the
// 16-bit target table (x86_kernel_thunks.inc). The templates and the
// generated stubs load one token into EDX and reach QT_Thunk or FT_Prolog;
// both public entries and the armed template gates decode it back to
// (connection, index) and come through FlatThunkDispatch below. See
//
#include "win98_flat_thunk.h"
#include "win98_segment_map.h"
#include "win98_ne_task.h"
#include "win98_shell_about.h"
#include "win98_shell_user_heap.h"
#include "x86_app.h"
#include "win16_machine.h"
#include "win98_win16_module.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
namespace
{
void ThunkCopy(char *out, const char *in, unsigned capacity)
{
    unsigned i = 0;
    if (in)
        for (; i + 1 < capacity && in[i]; ++i)
            out[i] = in[i];
    if (capacity)
        out[i] = 0;
}
const char *ThunkLeaf(const char *name)
{
    const char *result = name;
    for (const char *p = name; p && *p; ++p)
        if (*p == '\\' || *p == '/')
            result = p + 1;
    return result;
}
bool SameText(const char *a, const char *b)
{
    for (; *a && *b; ++a, ++b)
    {
        const char x = *a >= 'A' && *a <= 'Z' ? char(*a + 32) : *a;
        const char y = *b >= 'A' && *b <= 'Z' ? char(*b + 32) : *b;
        if (x != y)
            return false;
    }
    return !*a && !*b;
}
// 16-bit routines NEO98 has implemented, keyed the way the original template
// keys them: by 16-bit module, thunk data block and index byte. "words" is the
// number of 16-bit argument words the generated 32-bit stub pushes.
enum Routine
{
    RegisterShellHook,
    UserHeapProbe,
    PifOpen,
    PifGet,
    PifSet,
    PifClose,
    DriverApplets,
    ModuleHandle16,
    ModuleFileName16,
    AboutResources,
    PifPages,
    PifPageInfo,
    PifPageCreate,
    PifPageDestroy,
    CplApplet16,
    ExitWindows16,
};
struct Target
{
    const char *dll16, *name;
    unsigned index, words;
    Routine routine;
    const char *caller; // The SHELL32 export whose stub selects this index.
};
const Target targets[] = {
    // SHELL32, ordinal 181 (RegisterShellHook), two words.
    {"shell.dll", "Shl3216_ThunkData16", 0x0c, 2, RegisterShellHook, "SHELL32 ordinal 181"},
    // ShellExecuteEx calls this SHELL.DLL routine without arguments.
    {"shell.dll", "Shl3216_ThunkData16", 0x0b, 0, UserHeapProbe, "SHELL32 ordinal 323, ShellExecuteEx"},
    // The PIF property forwarders of SHELL.DLL. Word counts from each routine's own retf; see win98_shell_pif.h.
    {"shell.dll", "Shl3216_ThunkData16", 0x05, 6, PifOpen, "SHELL32 ordinal 9, PifMgr_OpenProperties"},
    {"shell.dll", "Shl3216_ThunkData16", 0x04, 7, PifGet, "SHELL32 ordinal 10, PifMgr_GetProperties"},
    {"shell.dll", "Shl3216_ThunkData16", 0x03, 7, PifSet, "SHELL32 PifMgr_SetProperties"},
    {"shell.dll", "Shl3216_ThunkData16", 0x02, 2, PifClose, "SHELL32 ordinal 13, PifMgr_CloseProperties"},
    // SHELL.DLL: GetNextDriver(hDriver, 0, GND_FIRSTINSTANCEONLY),
    // then GetDriverModuleHandle and GetModuleFileName into the caller's
    // buffer; returns the driver handle. The Control Panel folder (SHELL32) walks the installable 16-bit drivers this way looking for
    // applets. NEO98 loads no installable drivers, so the walk is empty.
    {"shell.dll", "Shl3216_ThunkData16", 0x09, 4, DriverApplets, "SHELL32 Control Panel driver applets"},
    // SHELL forwards module queries to KERNEL GetModuleHandle and
    // GetModuleFileName. SHFileOperation checks both Win32 and Win16 module
    // lists before moving or deleting a loaded module, returning 0x74, 0x75,
    // or 0x77 when blocked.
    {"shell.dll", "Shl3216_ThunkData16", 0x0e, 2, ModuleHandle16, "SHELL32 RVA 0x3ad08, module in use check"},
    {"shell.dll", "Shl3216_ThunkData16", 0x0d, 4, ModuleFileName16, "SHELL32 RVA 0x3ad08, module in use check"},
    // SHELL.DLL fills a word with GetFreeSystemResources(0) and a
    // dword with GetFreeSpace(0x1793). Its only caller is the ShellAbout dialog
    // through the 32-bit stub at SHELL32, which maps both pointers
    // and so pushes four words. See win98_shell_about.h.
    {"shell.dll", "Shl3216_ThunkData16", 0x07, 4, AboutResources, "SHELL32 RVA 0x315c3, ShellAbout resources"},
    // Forward property-page collection to PIFMGR with the callback, mapped
    // DROPFILES, and an array of up to ten page handles. See
    // src/x86_pif_pages.cpp.
    {"shell.dll", "Shl3216_ThunkData16", 0x0a, 6, PifPages, "SHELL32 RVA 0x7e553, PifPropGetPages"},
    // SHELL.DLL, CallCPLEntry16: SHELL32 sends every message of a
    // 16-bit Control Panel applet through the stub at (hInstance,
    // CPlApplet, hwnd, message, lParam1 and the mapped lParam2, nine words);
    // the routine calls the applet and widens CPLINFO and NEWCPLINFO. See Application::CplEntry16.
    {"shell.dll", "Shl3216_ThunkData16", 0x0f, 9, CplApplet16, "SHELL32 RVA 0x314a9, CallCPLEntry16"},
    // Forward ExitWindows with a DWORD argument and a zero WORD.
    // RestartDialog uses this after display settings require a restart.
    {"shell.dll", "Shl3216_ThunkData16", 0x08, 2, ExitWindows16, "SHELL32 RVA 0x315ac, RestartDialog"},
    // COMMCTRL supplies three Win16 property-page operations to COMCTL32
    // proxy pages: query icon/size/title, create the page dialog, and destroy
    // the page handle.
    {"commctrl.dll", "Cctl1632_ThunkData16", 0x00, 9, PifPageInfo, "COMCTL32 proxy page info"},
    {"commctrl.dll", "Cctl1632_ThunkData16", 0x01, 3, PifPageCreate, "COMCTL32 proxy page dialog"},
    {"commctrl.dll", "Cctl1632_ThunkData16", 0x02, 2, PifPageDestroy, "COMCTL32 proxy page destroy"},
};
const Target *FindTarget(const char *dll16, const char *name, unsigned index)
{
    for (const auto &target : targets)
        if (target.index == index && SameText(target.dll16, dll16) && SameText(target.name, name))
            return &target;
    return nullptr;
}

// The provider blocks of the original 16-bit modules. ThunkConnect32 compares
// signature and checksum of both sides before it links them; the values come
// out of the installed NE files and were read with tools/flat_thunk_targets.py.
// `catalog` names an entry of the Win16 module catalogue (Route::Native16 in
// src/dll_routes.h). For such a block the connector loads that module and
// publishes its own dispatch identities as the token table, one per index byte
// of the generated 32-bit stubs, so a transition ends in the implementation
// behind that ordinal and not in a routine of this file. `ordinals` holds the
// catalogue ordinal per index byte; a zero ends the list. This is the shape
// PLAN.md step 8 asks new thunk targets to take.
struct ProviderBlock
{
    const char *dll16, *name;
    unsigned signature, checksum;
    const char *catalog;
    // One catalogue ordinal per index byte of the generated 32-bit stubs. The
    // longest table in the tree is DDRAW16's, with 65 index bytes.
    unsigned char ordinals[96];
    // The 16-bit side runs as the original module, hosted in the process
    // (src/x86_thunk_hosted.cpp).
    bool hosted;
};
const ProviderBlock provider_blocks[] = {
    // SHELL.DLL; their targets are the table above.
    {"shell.dll", "Shl3216_ThunkData16", Win98FlatThunk::Flat, 0x2e50f, nullptr, {}},
    {"shell.dll", "Shl1632_ThunkData16", Win98FlatThunk::Quick, 0x13065, nullptr, {}},
    // COMMCTRL.DLL ordinal 301, segment. COMCTL32 connects this block
    // during DllMain even when no transition follows.
    {"commctrl.dll", "Cctl1632_ThunkData16", Win98FlatThunk::Flat, 0x1e9a, nullptr, {}},
    // VERSION connects VER.DLL ordinals 21 and 22. Its LS01 target indices
    // map to VerQueryValue, VerLanguageName, VerInstallFile, VerFindFile,
    // GetFileVersionInfo, and GetFileVersionInfoSize, corresponding to NE
    // ordinals 11 through 6.
    {"ver.dll", "VerFThk_ThunkData16", Win98FlatThunk::Flat, 0x67b0, "VER.DLL", {11, 10, 9, 8, 7, 6}},
    {"ver.dll", "VerThkSL_ThunkData16", Win98FlatThunk::Quick, 0x10dd, "VER.DLL", {}},
    // MPR.DLL against the 16-bit USER.EXE, SHA-256
    // ac1d4c3af96f02640d0295439a1f33f49e1e7bf1cc77fc9f603be5fa354468f4,
    // export USRMPR_THUNKDATA16, ordinal 381. An "SL01" block carries calls
    // from the 16-bit side into the 32-bit one only, so it needs no target of
    // ours; MPR connects it in DllMain and never asks for a transition, the
    // way COMCTL32 does with its own block.
    {"user.exe", "UsrMpr_ThunkData16", Win98FlatThunk::Quick, 0x236c8, nullptr, {}},
    // DCIMAN32 connects DCIMAN ordinal 16. Sixteen of its nineteen targets
    // use NE export ordinals; the three internal targets use 0x40 plus their
    // index.
    {"dciman.dll", "dcithk_ThunkData16", Win98FlatThunk::Flat, 0x32578, "DCIMAN.DLL",
     {21, 20, 12, 14, 11, 10, 8, 35, 34, 33, 32, 31, 0x4c, 30, 0x4e, 0x4f, 40, 2, 1}},
    // DDRAW connects DDRAW16 ordinals 1 and 5. LS01 has 65 internal targets,
    // represented by catalogue ordinal index+1; 34 have callers. The
    // driver-info targets carry DDHALINFO and DDHALDDRAWFNS. SL01 carries two
    // callbacks into Win32, enabled only when the driver supplies callback
    // pointers.
    {"ddraw16.dll", "thk3216_ThunkData16", Win98FlatThunk::Flat, 0x2f8503, "DDRAW16.DLL",
     { 1,  2,  0,  4,  5,  6,  7,  8,  9, 10, 11,  0,  0,
       0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 24,  0,  0,
       0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 37, 38, 39,
      40, 41, 42,  0, 44, 45, 46, 47, 48, 49, 50, 51, 52,
      53, 54, 55, 56, 57, 58, 59, 60,  0, 62, 63, 64, 65}},
    // DirectX 6.1 uses an LS01 block with checksum 2A7F36h and the same
    // SL01 checksum FA1h as above. Its target table has 62 index bytes.
    // Indices 0..58 match DirectX 9; its last three entries map to catalogue
    // ordinals 62, 64 and 65. DDRAW.DLL 4.06 fails DllMain if the connection
    // is refused.
    {"ddraw16.dll", "thk3216_ThunkData16", Win98FlatThunk::Flat, 0x2a7f36, "DDRAW16.DLL",
     { 1,  2,  0,  4,  5,  6,  7,  8,  9, 10, 11,  0,  0,
       0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 24,  0,  0,
       0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 37, 38, 39,
      40, 41, 42,  0, 44, 45, 46, 47, 48, 49, 50, 51, 52,
      53, 54, 55, 56, 57, 58, 59, 62, 64, 65}},
    {"ddraw16.dll", "thk1632_ThunkData16", Win98FlatThunk::Quick, 0xfa1, nullptr, {}},
    // CVT16.DLL ordinal 5 (CVT_THUNKDATA16) against CVT32.DLL, the pair the
    // FAT32 converter CVT1.EXE loads, and DSKAPI16.DLL ordinal 1
    // (THK_THUNKDATA16) against DSKAPI32.DLL, the pair TUNEUP.EXE loads.
    // Both blocks are "LS01"; the checksums come out of the installed NE
    // files and match the 32-bit blocks CVT32 and DSKAPI32 hand in.
    // Neither module has a Win16 catalogue entry, so a transition still stops
    // at the trap gate; the connector itself must succeed, because both
    // callers treat a refused connection as a failed DLL_PROCESS_ATTACH

    {"cvt16.dll", "cvt_ThunkData16", Win98FlatThunk::Flat, 0x3fc0, nullptr, {}},
    // DSKAPI16.DLL ordinal 1 (THK_THUNKDATA16), the pair TUNEUP.EXE
    // loads through DSKAPI32.DLL. The target table names an NE entry
    // point for each of the 33 index bytes, so the catalogue ordinal of an
    // index is that entry's ordinal. Index 0 is DLL16CALL, the sample's own
    // dialog filler; it keeps no ordinal and stops at the trap.
    // DMUSIC16.DLL ordinal 7 (DMTHUNK_THUNKDATA16), the one block
    // DMusic32.dll connects in its DLL_PROCESS_ATTACH ( through the
    // wrapper at; the 32-bit block is at over
    // 60300000 and carries the same "LS01" signature and the same checksum).
    // DllMain hands the connector's answer back unchanged, so a refused
    // connection means the DLL does not load at all. The target table
    // holds nine index bytes, all of them internal routines, so the
    // catalogue ordinal is the index byte plus one, and all nine have a caller
    // in DMUSIC32.DLL.
    {"dmusic16.dll", "dmthunk_ThunkData16", Win98FlatThunk::Flat, 0xd47a, "DMUSIC16.DLL",
     {1, 2, 3, 4, 5, 6, 7, 8, 9}},
    // TSP3216S.DLL, the 16-bit half of TAPI's bridge TSP3216L.TSP: ordinal 6
    // TAPITHK_THUNKDATA16 ("LS01", 2961Ch) and ordinal 16
    // TAPIFTHK_THUNKDATA16 ("LS01", 6BEE41h), both connected in
    // TSP3216L's DLL_PROCESS_ATTACH; ordinal 66 TAPI32_THUNKDATA16
    // is the "SL01" block back into TAPI32. The FThk target table
    // has 90 index bytes naming NE entry points; the five a TAPI start reaches
    // carry their ordinal, the rest stop at the trap (src/x86_tsp3216s.cpp,

    {"tsp3216s.dll", "TapiThk_ThunkData16", Win98FlatThunk::Flat, 0x2961c, nullptr, {}},
    {"tsp3216s.dll", "TapiFThk_ThunkData16", Win98FlatThunk::Flat, 0x6bee41, "TSP3216S.DLL",
     {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 108, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 31, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 41, 0, 0, 113, 30}},
    {"tsp3216s.dll", "Tapi32_ThunkData16", Win98FlatThunk::Quick, 0x297, nullptr, {}},
    // ICFG95 connects INET16 ordinal 2 during process attach. The LS01 block
    // has checksum 10E07h and nine internal targets.
    {"inet16.dll", "wizthk_ThunkData16", Win98FlatThunk::Flat, 0x10e07, "INET16.DLL", {0, 0, 0, 0, 0, 0, 0, 0, 9}},
    {"dskapi16.dll", "thk_ThunkData16", Win98FlatThunk::Flat, 0xc2784, "DSKAPI16.DLL",
     {0,  37, 36, 35, 34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 22, 21, 20,
      19, 18, 17, 16, 15, 14, 12, 11, 10, 9,  8,  7,  6,  4,  23, 24}, false},
    // MSVFW32 connects MSVIDEO ordinals 5 and 4. MCIAVI and Win16 codecs
    // execute the user's MSVIDEO module, so its LS01 table remains hosted and
    // SL01 links back to MSVFW32.
    {"msvideo.dll", "LS_ThunkData16", Win98FlatThunk::Flat, 0x399f, nullptr, {}, true},
    {"msvideo.dll", "SL_ThunkData16", Win98FlatThunk::Quick, 0xd8e, nullptr, {}, true},
    // W95INF16.DLL ordinal 2 (W95THK_THUNKDATA16), the block W95INF32.DLL
    // connects in its DllMain; "LS01", checksum 3469h, target table
    // with four internal routines: GenFormStrWithoutPlaceHolders,
    // GenInstall, CtlSetLddPath and GetSETUPXErrorText.
    // Each opens the INF and calls SETUPX, the 16-bit setup engine. ADVPACK
    // runs every INF of Active Setup through this pair on Win98 (IE4UINIT,
    // the Desktop Update's Shell.UserStub), and SETUPX is the same original
    // RUNDLL.EXE loads for the per-user stubs of Windows Setup, so W95INF16
    // is hosted like MSVIDEO and calls the original SETUPX.
    {"w95inf16.dll", "w95thk_ThunkData16", Win98FlatThunk::Flat, 0x3469, nullptr, {}, true},
    // AVICAP32 connects to AVICAPF_THUNKDATA16 in AVICAP.DLL during DllMain.
    // Its avicapf_ThunkData32 block uses LS01, checksum 4C039h, and sixteen
    // target routines. AVICAP supplies the Video for Windows capture window
    // and calls MSVIDEO's videoOpen, so it is hosted like MSVIDEO.
    {"avicap.dll", "avicapf_ThunkData16", Win98FlatThunk::Flat, 0x4c039, nullptr, {}, true},
    // MSPRINT is the hosted Win16 half of the Add Printer wizard. Connect
    // both SL01 and LS01 blocks during attach. Its class installer calls the
    // user's SETUPX module.
    {"msprint.dll", "MspThk_ThunkData16", Win98FlatThunk::Quick, 0x270a9, nullptr, {}, true},
    {"msprint.dll", "MspFThk_ThunkData16", Win98FlatThunk::Flat, 0xc88f, nullptr, {}, true},
};
// A 16-bit module can come in more than one release with the same block
// name; DDRAW16.DLL of DirectX 6.1 and of DirectX 9 differ in checksum and in
// their target tables. The row whose checksum the 32-bit block carries wins;
// without one the first row by name answers and the comparison refuses.
const ProviderBlock *FindProvider(const char *dll16, const char *name, unsigned checksum)
{
    const ProviderBlock *named = nullptr;
    for (const auto &block : provider_blocks)
        if (SameText(block.dll16, dll16) && SameText(block.name, name))
        {
            if (block.checksum == checksum)
                return &block;
            if (!named)
                named = &block;
        }
    return named;
}
} // namespace

// Allocates one API gate per connected block and template shape. Executing it
// is the 32-to-16 transition the original would have made.
unsigned Application::FlatThunkTrap(const char *dll16, unsigned block, unsigned entry)
{
    char name[128];
    snprintf(name, sizeof name, "%s#%08x#%u", dll16, block, entry);
    for (unsigned i = 0; i < import_count; ++i)
        if (imports[i].argc == FlatThunkGate && !strcmp(imports[i].name, name))
            return GateBase + i * 16;
    return NewImport("FLATTHUNK", name, FlatThunkGate);
}

Application::ThunkConnection *Application::FlatThunkEntry(unsigned block)
{
    for (unsigned i = 0; i < thunk_connection_count; ++i)
        if (thunk_connections[i].block == block)
            return &thunk_connections[i];
    if (thunk_connection_count == ThunkConnectionCapacity)
        return nullptr;
    return &thunk_connections[thunk_connection_count++];
}

// The original copies a code template into the block's code area and patches
// the 16-bit target table into it. NEO98 writes a five-byte jump to an API
// gate there instead, so the transition is observed rather than executed. The
// block lives in a data section, so the page is made executable first, which
// is what the original also requires of it.
bool Application::FlatThunkArm(ThunkConnection &connection, unsigned entry)
{
    const auto shape = entry ? Win98FlatThunk::RegisterIndex : Win98FlatThunk::FrameIndex;
    unsigned offset = 0;
    if (!Read(connection.block + Win98FlatThunk::EntryOffsetField(shape), &offset, 4) || !offset ||
        offset > 0x1000)
        return true; // No code area of this shape in this block.
    const unsigned address = connection.block + offset;
    const unsigned gate = FlatThunkTrap(connection.dll16, connection.block, entry);
    if (!gate)
        return false;
    const unsigned page = address & ~0xfffu;
    if (!Protect(page, 0x2000, Prot::Read | Prot::Write | Prot::Exec))
        return false;
    unsigned char jump[5] = {0xe9};
    const unsigned relative = gate - (address + 5);
    memcpy(jump + 1, &relative, 4);
    ++loader_writes;
    const bool written = Store(address, jump, sizeof jump);
    --loader_writes;
    if (!written)
        return false;
    connection.entry_address[entry] = address;
    connection.entry_gate[entry] = gate;
    return true;
}

// Older connector used by LZ32: slot, export name, checksum, DLL16, attach.
unsigned Application::LegacyThunkConnect(const unsigned *a)
{
 if(!a[4])return 1;
 if(!SameText(ReadString(a[3]),"SYSTHUNK.DLL")||strcmp(ReadString(a[1]),"FT_LzFThkThkConnectionData")||a[2]!=0x77c1){last_error=126;return 0;}
 if(!virtual_memory.Access(a[0],0x3f,true)){last_error=87;return 0;}
 auto *entry=FlatThunkEntry(a[0]);if(!entry){last_error=8;return 0;}
 entry->block=a[0];entry->connected=true;entry->checksum=a[2];
 ThunkCopy(entry->dll16,"SYSTHUNK.DLL",sizeof entry->dll16);ThunkCopy(entry->name,"FT_LzFThkThkConnectionData",sizeof entry->name);
 for(unsigned shape=0;shape<2;++shape){
  unsigned address=a[0]+(shape?0x3a:4),gate=FlatThunkTrap(entry->dll16,a[0],shape);
  if(!gate||!Protect(address&~0xfffu,0x1000,Prot::Read|Prot::Write|Prot::Exec))return 0;
  unsigned char jump[5]={0xe9};unsigned relative=gate-address-5;memcpy(jump+1,&relative,4);
  ++loader_writes;bool ok=Store(address,jump,5);--loader_writes;if(!ok)return 0;
  entry->entry_address[shape]=address;entry->entry_gate[shape]=gate;
 }
 return 1;
}

unsigned Application::FlatThunkApi(ApiName, const unsigned *a)
{
    // ThunkConnect32(pThunkData32, pszThunkDataName, pszDll16, pszDll32,
    // hInstance, dwReason).
    const unsigned block = a[0], reason = a[5];
    if (reason != Win98FlatThunk::Attach && reason != Win98FlatThunk::Detach) return 1;
    unsigned signature = 0;
    if (block && !Read(block, &signature, 4))
        return 0;
    const auto decision = Win98FlatThunk::Classify(reason, signature);
    if (decision == Win98FlatThunk::ThreadNoop || decision == Win98FlatThunk::Reject)
        return Win98FlatThunk::Result(decision);
    if (decision == Win98FlatThunk::Disconnect && signature != Win98FlatThunk::Flat && signature != Win98FlatThunk::Quick) return 1;
    const ProviderBlock *provider = nullptr;
    bool original16 = false;
    if (decision == Win98FlatThunk::Connect)
    {
        // Validate both original block signatures and checksums before
        // publishing an entry. A failed lookup must not manufacture a target
        // table, so an unknown 16-bit block fails the comparison with zeros.
        provider = FindProvider(ThunkLeaf(ReadString(a[2])), ReadString(a[1]), Read32(block + 4));
        // In the process of a Win16 program the 16-bit side can be an original
        // module the program already runs (MSPRINT.DLL under RUNDLL.EXE). then finds the block by name in that module and
        // compares signature and checksum, below with the hosted path.
        // The same holds for a 32-bit process that hosts 16-bit modules: the
        // 16-bit side may be a module it already loaded, which is loading
        // this DLL from its own ThunkConnect16 right now (UMDM16 loads
        // UMDM32 for MODEM.CPL).
        if (!provider && win16.state && Win16Find(ThunkLeaf(ReadString(a[2]))))
            original16 = true;
        else if (!Win98FlatThunk::Compatible(signature, Read32(block + 4),
                                             provider ? provider->signature : 0,
                                             provider ? provider->checksum : 0))
            return 0;
    }
    const unsigned offset = Win98FlatThunk::TargetOffset(signature);
    // A block whose 16-bit side runs as the original module: load it, find
    // its block by name and compare that one, the way Windows 98 does.
    unsigned hosted_module = 0, hosted_block = 0;
    if (decision == Win98FlatThunk::Connect && provider && provider->hosted && !Win16HostedEngine())
    {
        // Without an engine for the 16-bit side the connect fails, as it did
        // before that side was hosted; the 32-bit DLL goes on without its
        // 16-bit half.
        if (log)
        {
            char text[200];
            snprintf(text, sizeof text, "FLATTHUNK hosted connect refused dll16=%s: no 16-bit engine",
                     ReadString(a[2]));
            log(text);
        }
        return 0;
    }
    if (decision == Win98FlatThunk::Connect && ((provider && provider->hosted) || original16))
    {
        hosted_block = Win16HostedThunkData(ReadString(a[2]), ReadString(a[1]), hosted_module);
        if (state == Failed)
            return 0;
        if (!hosted_block || Read32(hosted_block) != signature || Read32(hosted_block + 4) != Read32(block + 4))
        {
            if (log)
            {
                char text[320];
                snprintf(text, sizeof text,
                         "FLATTHUNK hosted connect refused dll16=%s name=%s module=%04x block16=%08x",
                         ReadString(a[2]), ReadString(a[1]), hosted_module, hosted_block);
                log(text);
            }
            if (hosted_module)
                Win16FreeLibrary(hosted_module);
            return 0;
        }
    }
    ThunkConnection *entry = nullptr;
    if (decision == Win98FlatThunk::Disconnect)
    {
        for (unsigned i = 0; i < thunk_connection_count; ++i)
            if (thunk_connections[i].block == block) { entry = &thunk_connections[i]; break; }
        if (!entry) return 1;
    }
    else entry = FlatThunkEntry(block);
    if (!entry)
    {
        Fail("Flat thunk connection table exhausted");
        return 0;
    }
    if (decision == Win98FlatThunk::Disconnect)
    {
        // The original unlinks the two blocks. Without a 16-bit side there is
        // nothing to unlink; clearing the target keeps a later transition from
        // reaching a stale trap gate. A catalogue module the connect took a
        // reference on is released here, the way the original frees the
        // 16-bit module it loaded.
        Write32(block + offset, 0);
        if (entry->catalog)
        {
            const unsigned handle = entry->catalog;
            Win16ModuleApi(Win98Win16Module::FreeLibrary16, &handle);
            entry->catalog = 0;
        }
        if (entry->hosted)
        {
            Win16FreeLibrary(entry->hosted);
            entry->hosted = 0;
        }
        entry->tokens = 0;
        entry->connected = false;
        if (log)
        {
            char text[512];
            snprintf(text, sizeof text, "FLATTHUNK disconnect dll16=%s block=%08x", entry->dll16, block);
            log(text);
        }
        return 1;
    }
    const char *dll16 = ReadString(a[2]), *dll32 = ReadString(a[3]), *name = ReadString(a[1]);
    ThunkCopy(entry->dll16, dll16 ? ThunkLeaf(dll16) : "", sizeof entry->dll16);
    ThunkCopy(entry->dll32, dll32 ? ThunkLeaf(dll32) : "", sizeof entry->dll32);
    ThunkCopy(entry->name, name ? name : "", sizeof entry->name);
    entry->block = block;
    entry->signature = signature;
    entry->checksum = Read32(block + 4);
    entry->instance = a[4];
    entry->connected = true;
    entry->trap = FlatThunkTrap(entry->dll16, block, 0);
    if (!entry->trap)
    {
        Fail("Flat thunk trap gate allocation failed");
        return 0;
    }
    // The original writes the flat address of the 16-bit target table here.
    // NEO98 has no such table. For an "LS01" block the field receives a
    // table of tokens, one per index byte, because SHELL32 and
    // the generated stubs of other DLLs read this field themselves before
    // calling QT_Thunk or FT_Prolog. The "SL01" field is read by 16-bit
    // code only and stays zero.
    Write32(block + offset, 0);
    if (hosted_module)
    {
        entry->hosted = hosted_module;
        if (!FlatThunkHostedLink(*entry, hosted_block, a[1]))
        {
            Fail("Flat thunk to a hosted 16-bit module could not be linked");
            return 0;
        }
        if (log)
        {
            char text[320];
            snprintf(text, sizeof text, "FLATTHUNK connect hosted dll16=%s name=%s block=%08x block16=%08x",
                     entry->dll16, entry->name, block, hosted_block);
            log(text);
        }
        return 1;
    }
    // The original loads the 16-bit module before it links the blocks. For a
    // Route::Native16 provider that is the Win16 module catalogue; its handle
    // keeps the descriptor with the dispatch identities alive.
    if (provider && provider->catalog)
    {
        const unsigned handle = Win16ModuleApi(Win98Win16Module::LoadLibrary16, &a[2]);
        if (handle < 32)
        {
            last_error = handle;
            return 0; // The original fails the connect when the module is gone.
        }
        entry->catalog = handle;
    }
    if (signature == Win98FlatThunk::Flat)
    {
        const unsigned connection = (unsigned)(entry - thunk_connections);
        const unsigned table = entry->catalog
                                   ? Win16CatalogTokenTable(connection, entry->catalog,
                                                            provider->ordinals,
                                                            sizeof provider->ordinals)
                                   : ThunkTokenTable(connection);
        if (!table)
        {
            Fail("Flat thunk token table allocation failed");
            return 0;
        }
        entry->tokens = table;
        Write32(block + offset, table);
        for (unsigned shape = 0; shape < 2; ++shape)
            if (!FlatThunkArm(*entry, shape))
            {
                Fail("Flat thunk entry could not be armed");
                return 0;
            }
    }
    if (log)
    {
        char text[512];
        snprintf(text, sizeof text,
                 "FLATTHUNK connect dll16=%s dll32=%s name=%s block=%08x signature=%08x "
                 "checksum=%08x offset=%u trap=%08x frame_entry=%08x register_entry=%08x",
                 entry->dll16, entry->dll32, entry->name, block, signature, entry->checksum, offset,
                 entry->trap, entry->entry_address[0], entry->entry_address[1]);
        log(text);
    }
    return 1;
}

// A guest transition reached one of the armed code areas. Recover the index
// byte the original template would have used, then dispatch or stop.
// KERNEL32. The decisions and the limits are in
// src/win98_segment_map.h. The _IP_EBP_N forms take no stack argument; the
// number in the name is the frame offset the shared body loads.
unsigned Application::SegmentMapApi(ApiName name, const unsigned *)
{
    unsigned offset = 0;
    for (const char *p = name; *p; ++p)
        if (*p >= '0' && *p <= '9')
            offset = offset * 10 + unsigned(*p - '0');
    if (offset < 8 || offset > 40 || (offset & 3))
    {
        Fail("Unsupported SMapLS frame offset");
        return 0;
    }
    const unsigned slot=Register(Reg::Ebp)+offset;
    if(!strncmp(name,"SUnMapLS",8)){
        SegmentUnmapNode(Read32(slot));return Register(Reg::Eax);
    }
    const unsigned mapped=SegmentMap(Read32(slot));
    const unsigned node=SegmentNode(mapped);Write32(slot,node);
    Register(Reg::Edx,node);return mapped;
}
bool Application::FlatThunkTransition(Import &item)
{
    const unsigned gate = pending; // Dispatch found the record through this gate address.
    // The Pascal gates of the Win16 loader live in the same region and are
    // reached the same way, but their caller is 16-bit code with a segmented
    // stack. src/ne_loader.cpp, PLAN.md step 9.
    if (Win16OwnsGate(gate))
        return Win16Transition(item);
    ThunkConnection *connection = nullptr;
    unsigned shape = 0;
    for (unsigned i = 0; i < thunk_connection_count && !connection; ++i)
        for (unsigned s = 0; s < 2; ++s)
            if (thunk_connections[i].entry_gate[s] == gate)
            {
                connection = &thunk_connections[i];
                shape = s;
            }
    const unsigned sp = Register(Reg::Esp), ret = Read32(sp), bp = Register(Reg::Ebp);
    // LZ32.DLL lies in the shared arena above 2 GB with its data, as on
    // Win98, so its DllMain connects the thunk once for the whole session: the
    // first process that loads it (SYSTRAY) writes the jump to the gate into
    // the shared block, and every later process (Explorer's Fonts folder,
    // FONTEXT.DLL) comes to the same gate without a connection of its own.
    // The gate's name says it is the LZ service and which entry shape.
    bool lz = connection && !strcmp(connection->name, "FT_LzFThkThkConnectionData");
    if (!connection && !strncmp(item.name, "SYSTHUNK.DLL#", 13))
    {
        const char *last = strrchr(item.name, '#');
        lz = true;
        shape = last && last[1] == '1' ? 1 : 0;
    }
    if(lz){
        const unsigned index = shape == 0 ? (Read32(bp - 4) & 0xff) : (Register(Reg::Ecx) & 0xff);
        ++thunk_transitions;
        static const unsigned counts[]={4,0,2,0,1,4,4,1,5,2};
        if(index>=10){Fail("Invalid legacy LZ thunk index");return false;}
        // The LZ32 wrappers hand their pointer arguments to SMapLS_IP_EBP_n
        // first, which leaves the descriptor address in the frame slot.
        unsigned args[]={SegmentSlotLinear(Read32(bp+8)),SegmentSlotLinear(Read32(bp+12)),
                         SegmentSlotLinear(Read32(bp+16))};
        unsigned value=LzService(index,args);if(state==Failed)return false;
        Register(Reg::Eax,value&65535);Register(Reg::Edx,value>>16);
        Register(Reg::Esp,sp+4+counts[index]*2);Register(Reg::Eip,ret);pending=0;return true;
    }
    if (!connection)
    {
        ++thunk_transitions;
        char message[256];
        snprintf(message, sizeof message, "flat thunk to %s not supported: unnamed thunk data index 4294967295", item.name);
        Fail(message);
        return false;
    }
    // Template takes the index byte from [ebp-4] and jumps into
    // QT_Thunk; template takes it from CL and returns into
    // FT_Prolog. Both load EDX from the target table first.
    const unsigned index = shape == 0 ? (Read32(bp - 4) & 0xff) : (Register(Reg::Ecx) & 0xff);
    // Both templates load the target from the table this provider published at
    // block+8, so the armed gate reads it the same way instead of forming the
    // token again: for a catalogue block the entry is a dispatch identity.
    const unsigned token = connection->tokens
                               ? Read32(connection->tokens + index * 4)
                               : ThunkToken((unsigned)(connection - thunk_connections), index);
    return shape == 0 ? QtThunkEntry(token) : FtPrologEntry(token);
}

// The 32-to-16 call itself, shared by the template gates, QT_Thunk and
// FT_Thunk. "frame" is the stub's EBP, "arguments" the address of the last
// pushed 16-bit word, "measured" the argument bytes the frame shape implies.
// Runs the implemented routine, or stops on an index without one.
bool Application::FlatThunkDispatch(ThunkConnection *connection, unsigned index, unsigned frame,
                                    unsigned arguments, unsigned measured, unsigned caller,
                                    unsigned &result, unsigned &words_out)
{
    ++thunk_transitions;
    const Target *target = FindTarget(connection->dll16, connection->name, index);
    unsigned words[12] = {};
    if (target)
        for (unsigned i = 0; i < target->words && i < 12; ++i)
            Read(arguments + (target->words - 1 - i) * 2, &words[i], 2);
    if (log)
    {
        char text[512];
        snprintf(text, sizeof text,
                 "FLATTHUNK transition dll16=%s name=%s index=%u caller=%08x call=%u "
                 "block=%08x measured=%u routine=%s",
                 connection->dll16, connection->name, index, caller, api_calls, connection->block,
                 measured, target ? target->caller : "unmapped");
        log(text);
    }
    if (!target)
    {
        char message[256];
        snprintf(message, sizeof message, "flat thunk to %s not supported: %s index %u",
                 connection->dll16, *connection->name ? connection->name : "unnamed thunk data", index);
        Fail(message);
        return false;
    }
    if (measured != Win98FlatThunk::ArgumentBytes(target->words) && log)
    {
        char text[160];
        snprintf(text, sizeof text, "FLATTHUNK argument bytes differ: frame=%u table=%u", measured,
                 Win98FlatThunk::ArgumentBytes(target->words));
        log(text);
    }
    result = 0;
    switch (target->routine)
    {
    case UserHeapProbe:
        result = UserHeapProbeCall();
        break;
    case DriverApplets:
        result = 0;
        break;
    case RegisterShellHook:
        // SHELL32's stub pushes the low words of its own two arguments. NEO98
        // calls the implemented routine, not 16-bit code, and x86_shell.cpp
        // deliberately keeps the complete generation HWND, so the untruncated
        // caller arguments at [ebp+8] and [ebp+0xc] are used. The marshalled
        // words are recorded for comparison.
        result = RegisterShell(Read32(frame + 8), Read32(frame + 0xc));
        break;
    case PifOpen:
    case PifGet:
    case PifSet:
    case PifClose:
        // Index 2 needs the property handle; other entries make no decision
        // based on an argument.
        result = ShellPifCall(index, words[0], words);
        break;
    case ModuleHandle16:
        // The stub mapped its pointer with SMapLS_IP_EBP_8, so the frame slot
        // holds the descriptor node and not the flat address any more.
        result = ShellModuleHandle16(SegmentSlotLinear(Read32(frame + 8)));
        break;
    case ModuleFileName16:
        result = ShellModuleFileName16(Read32(frame + 8) & 0xffff,
                                       SegmentSlotLinear(Read32(frame + 0xc)),
                                       Read32(frame + 0x10) & 0xffff);
        break;
    case PifPages:
    case PifPageInfo:
    case PifPageCreate:
    case PifPageDestroy:
        result = PifPagesThunk(target->routine - PifPages, words, frame);
        break;
    case CplApplet16:
        result = CplEntry16(words[0], words[1] << 16 | words[2], words[3], words[4], words[5] << 16 | words[6],
                            words[7] << 16 | words[8]);
        break;
    case ExitWindows16:
        // USER: the code as given, no flags (EndSession).
        result = EndSession(0, Read32(frame + 8));
        break;
    case AboutResources:
        // Both stubs mapped their pointer, so the frame slots hold descriptor
        // nodes and not the flat addresses of the caller any more.
        result = ShellAboutResources(SegmentSlotLinear(Read32(frame + 8)),
                                     SegmentSlotLinear(Read32(frame + 0xc)));
        break;
    }
    if (state == Failed || state == Exited)
        return false;
    ++api_calls;
    words_out = target->words;
    if (log)
    {
        char text[256];
        snprintf(text, sizeof text,
                 "FLATTHUNK dispatched index=%u words=%04x,%04x caller_arguments=%08x,%08x "
                 "result=%04x routine=%s",
                 index, words[0], words[1], Read32(frame + 8), Read32(frame + 0xc),
                 Win98FlatThunk::ReturnValue(result), target->caller);
        log(text);
    }
    return true;
}

// USER's local heap reserve check behind thunk index 11. The implemented
// SHELL routine calls the implemented UserSeeUserDo, whose heap requests use
// the native allocator. Requests outside the heap group need USER's own data
// segment and stop the run.
unsigned Application::UserHeapProbeCall()
{
    struct UserEnvironment final : Win98UserSeeUserDo::Environment
    {
        Application &app;
        explicit UserEnvironment(Application &a) : app(a) {}
        unsigned Unsupported(const char *what)
        {
            char message[160];
            snprintf(message, sizeof message, "UserSeeUserDo %s needs USER's own data segment", what);
            app.Fail(message);
            return 0;
        }
        unsigned LocalAllocate(unsigned flags, unsigned size) override
        {
            const unsigned arguments[] = {flags, size};
            const unsigned pointer = app.Api("LocalAlloc", arguments);
            if (!pointer)
                return 0;
            for (auto &slot : app.user_heap_handles)
                if (!slot.token)
                {
                    slot.token = app.next_user_heap_token++ & 0xffffu;
                    if (!slot.token)
                        slot.token = app.next_user_heap_token++ & 0xffffu;
                    slot.pointer = pointer;
                    return slot.token;
                }
            const unsigned release[] = {pointer};
            app.Api("LocalFree", release);
            return 0; // The original reports an exhausted heap the same way.
        }
        unsigned LocalRelease(unsigned handle) override
        {
            for (auto &slot : app.user_heap_handles)
                if (slot.token && slot.token == (handle & 0xffffu))
                {
                    const unsigned release[] = {slot.pointer};
                    app.Api("LocalFree", release);
                    slot.token = 0;
                    slot.pointer = 0;
                    return 0;
                }
            return handle & 0xffffu; // LocalFree returns the handle it kept.
        }
        unsigned LocalCompact(unsigned) override { return Unsupported("compact"); }
        unsigned DataWord(unsigned) override { return Unsupported("data word"); }
        unsigned DataPair(unsigned) override { return Unsupported("data pair"); }
        unsigned DataSelector() override { return Unsupported("data selector"); }
        unsigned FarCall(unsigned, unsigned, unsigned) override { return Unsupported("far call"); }
        unsigned MenuCall(unsigned, unsigned, unsigned) override { return Unsupported("menu call"); }
    };
    struct ShellEnvironment final : Win98ShellUserHeap::Environment
    {
        UserEnvironment user;
        explicit ShellEnvironment(Application &a) : user(a) {}
        unsigned SeeUserDo(unsigned request, unsigned first, unsigned second, unsigned third) override
        {
            return Win98UserSeeUserDo::Call(user, request, first, second, third);
        }
    } environment(*this);
    return Win98ShellUserHeap::Probe(environment);
}

// SHELL forwards a module-name query to GetModuleHandle. A null name or an
// unloaded module returns zero. CallCPLEntry16 calls a hosted applet, masks
// the message number, and widens CPLINFO and NEWCPLINFO records. If
// CPL_NEWINQUIRE is unsupported, obtain CPL_INQUIRE data and load the icon,
// name, and description to construct the Win32 result.
unsigned Application::CplEntry16(unsigned instance, unsigned entry, unsigned hwnd, unsigned message,
                                 unsigned first, unsigned second)
{
    const unsigned data = Win16HostedInstance(instance);
    auto call = [&](unsigned window, unsigned msg, unsigned l1, unsigned l2) {
        const unsigned push[] = {window & 0xffff, msg & 0x7fff, l1 >> 16, l1 & 0xffff, l2 >> 16, l2 & 0xffff};
        bool returned = false;
        const unsigned answer = Win16HostedCall(entry, push, 6, data, nullptr, &returned);
        return returned ? answer : 0u;
    };
    if (log)
    {
        char text[160];
        snprintf(text, sizeof text, "CPL16 call instance=%04x entry=%08x hwnd=%04x message=%04x lParam1=%08x lParam2=%08x",
                 instance, entry, hwnd, message, first, second);
        log(text);
    }
    const unsigned answer = call(hwnd, message, first, second);
    if (state == Failed || state == Exited)
        return 0;
    const unsigned kind = message & 0x8000 ? 0x8000 : message;
    const unsigned record = second >> 16 ? Win16Linear(second >> 16, second & 0xffff) : 0;
    if (!record)
        return answer;
    if (kind == 3)
    {
        const unsigned short icon = Win16Word(record), name = Win16Word(record + 2), info = Win16Word(record + 4);
        const unsigned data_word = Read32(record + 6);
        Write32(record + 0xc, data_word);
        Write32(record + 8, unsigned(int(short(info))));
        Write32(record + 4, unsigned(int(short(name))));
        Write32(record, unsigned(int(short(icon))));
    }
    else if (kind == 8 || kind == 0x8000)
    {
        if (kind == 8 && Read32(record) != 0xf2)
        {
            // The CPLINFO of the fallback lies on the 16-bit stack of SHELL;
            // here it is a block the applet can reach by a far pointer.
            const unsigned block = Win16GlobalAlloc(0x40, 16);
            auto *memory = block ? Win16GlobalBlock(block) : nullptr;
            if (memory)
            {
                call(0, 3, first, memory->selector << 16);
                const unsigned info = memory->memory;
                const unsigned icon = Win16Word(info), name = Win16Word(info + 2), text = Win16Word(info + 4);
                const unsigned data_word = Read32(info + 6);
                Win16Word(record + 0x10, icon ? Win16LoadImage(instance, false, 0x8000 | icon, nullptr) & 0xffff : 0);
                if (name)
                    Win16LoadStringInto(instance, name, record + 0x12, 0x20);
                else
                    Store(record + 0x12, "", 1);
                if (text)
                    Win16LoadStringInto(instance, text, record + 0x32, 0x40);
                else
                    Store(record + 0x32, "", 1);
                Write32(record + 0xc, data_word);
                Win16GlobalFree(block);
            }
        }
        unsigned char strings[0xe0];
        if (Read(record + 0x12, strings, sizeof strings))
            Store(record + 0x14, strings, sizeof strings);
        Win16Word(record + 0x12, 0);
        Write32(record, 0xf4);
        // Under Win98 a 16-bit icon handle is the 32-bit one; here the
        // 16-bit side holds an alias of the native icon (Win16ImageAlias).
        if (const unsigned native = Win16ImageNative(Win16Word(record + 0x10)))
            Write32(record + 0x10, native);
    }
    if (state == Failed || state == Exited)
        return 0;
    return answer;
}
unsigned Application::ShellModuleHandle16(unsigned name)
{
    const char *text = name ? ReadString(name) : nullptr;
    Win16Module *found = text && *text ? Win16Find(text) : nullptr;
    return found ? found->handle : 0;
}
// Forward GetModuleFileName with its module handle, far buffer, and buffer
// size. A Win32 caller has no current Win16 module to use as fallback, so an
// invalid handle returns zero.
unsigned Application::ShellModuleFileName16(unsigned handle, unsigned buffer, unsigned size)
{
    if (!buffer || !size)
        return 0;
    const char *path = nullptr;
    // GetExePtr takes the instance as well as the module
    // handle, and the handle SHELL32 has is the one LoadLibrary16 answered,
    // the instance: with the module handle alone the Control Panel folder
    // got an empty path for every 16-bit applet and showed the generic
    // icon.
    if (win16.state)
        for (unsigned i = 0; i < win16.state->module_count && !path; ++i)
            if (win16.state->modules[i].handle == handle || win16.state->modules[i].instance == handle)
                path = win16.state->modules[i].path;
    if (!path)
    {
        char end = 0;
        Store(buffer, &end, 1);
        return 0;
    }
    return PutString(buffer, path, size);
}

// SHELL.DLL behind thunk index 7, the resource query of the ShellAbout
// dialog. The decisions of the 16-bit routine and of USER.EXE ordinal 284 are in
// win98_shell_about.h; this side answers the three questions they ask.
//
// GetFreeSpace is the loader's own implementation of KERNEL ordinal 169
// (src/ne_loader.cpp case 169): the 16-bit global heap of this port is the flat
// arena of the process, so the answer is the size limit of KRNL386. The two
// percentages come from the object tables that take the place of USER's and
// GDI's local heaps. The dialog therefore names the budget a program really
// runs into here, and the numbers move while a session fills those tables.
unsigned Application::ShellAboutResources(unsigned resources, unsigned space)
{
    struct AboutEnvironment final : Win98ShellAbout::Environment
    {
        Application &app;
        explicit AboutEnvironment(Application &a) : app(a) {}
        unsigned GdiFreePercent() override
        {
            return Win98ShellAbout::Percent(Gdi::LiveObjects(), Gdi::ObjectCapacity());
        }
        unsigned UserFreePercent() override
        {
            // USER takes the smallest of its heaps; the port takes the smallest
            // of its object tables the same way.
            const unsigned windows =
                Win98ShellAbout::Percent(User::LiveWindows(), User::WindowCapacity);
            const unsigned menus =
                Win98ShellAbout::Percent(User::LiveMenus(), User::MenuCapacity());
            return windows <= menus ? windows : menus;
        }
        unsigned FreeSpace() override { return Win98NeTask::SizeLimit; }
        void WriteWord(unsigned at, unsigned value) override
        {
            const unsigned short word = (unsigned short)value;
            app.Store(at, &word, sizeof word);
        }
        void WriteDword(unsigned at, unsigned value) override { app.Write32(at, value); }
    } environment(*this);
    Win98ShellAbout::Resources(environment, resources, space);
    // The original leaves the last value it read in DX:AX and its caller drops
    // both, so the return value of the transition carries no meaning.
    return 0;
}

// SHELL.DLL's PIF-property forwarders behind thunk indices 5, 4, 3 and 2. The
// implementation in win98_shell_pif.h makes SHELL's decisions; the provider
// here answers the three KERNEL calls its loader makes and the four far calls
// into PIFMGR.DLL.
// PIFMGR.DLL is 16-bit code and therefore native: src/win98_pifmgr.h holds the
// property services, src/x86_pifmgr.cpp the handle table of this process. The
// loader's LoadLibrary is answered with an instance above the error range, the
// four GetProcAddress calls with the ordinal itself, and a far call through a
// slot reaches PifMgrCall with the ordinal and the block of 16-bit words the
// 32-bit stub pushed, which is what the forwarder copies through. Until 22
// September 2026 the load was refused with 11, ERROR_BAD_FORMAT;
//
unsigned Application::ShellPifCall(unsigned index, unsigned handle, const unsigned *words)
{
    struct PifEnvironment final : Win98ShellPif::Environment
    {
        Application &app;
        const unsigned *words;
        PifEnvironment(Application &a, const unsigned *w) : app(a), words(w) {}
        unsigned LoadModule(const char *name) override
        {
            if (app.log && !app.shell_pif_loads++)
            {
                char text[160];
                snprintf(text, sizeof text, "SHELLPIF load module=%s result=native instance", name);
                app.log(text);
            }
            return 0x1000;
        }
        unsigned Procedure(unsigned, unsigned ordinal) override { return ordinal; }
        void FreeModule(unsigned) override {}
        unsigned CallProcedure(unsigned procedure, unsigned) override { return app.PifMgrCall(procedure, words); }
    } environment(*this, words);
    switch (index)
    {
    case 5:
        return Win98ShellPif::Open(shell_pif, environment);
    case 4:
        return Win98ShellPif::Get(shell_pif, environment);
    case 3:
        return Win98ShellPif::Set(shell_pif, environment);
    default:
        return Win98ShellPif::Close(shell_pif, environment, handle);
    }
}

// A fault elsewhere inside a connected block is still the transition we cannot
// make. Attribution uses a 0x40 window below the faulting address and picks the
// nearest connected block. This only labels an existing fault.
bool Application::FlatThunkFault(char *out, unsigned capacity)
{
    if (!cpu)
        return false;
    unsigned pc = 0;
    if (cpu->Get(Reg::Eip, pc) != CpuError::Ok)
        return false;
    const ThunkConnection *best = nullptr;
    for (unsigned i = 0; i < thunk_connection_count; ++i)
    {
        const auto &c = thunk_connections[i];
        if (!c.connected || pc < c.block || pc >= c.block + 0x40)
            continue;
        if (!best || c.block > best->block)
            best = &c;
    }
    if (!best)
        return false;
    ++thunk_transitions;


    if (log)
    {
        char text[512];
        snprintf(text, sizeof text,
                 "FLATTHUNK transition dll16=%s name=%s index=4294967295 caller=%08x call=%u "
                 "block=%08x shape=2 routine=unmapped",
                 best->dll16, best->name, pc, api_calls, best->block);
        log(text);
    }
    snprintf(out, capacity, "flat thunk to %s not supported: %s at block offset %u", best->dll16,
             *best->name ? best->name : "unnamed thunk data", pc - best->block);
    return true;
}
} // namespace X86
