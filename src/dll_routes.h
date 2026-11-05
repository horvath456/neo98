// SPDX-License-Identifier: GPL-2.0-or-later
// The one table for the DLL rule of PLAN.md section 0: which DLL our loader
// resolves to a native ARM64 provider and which one runs as original x86 code.
// Native are KERNEL32, USER32, GDI32 and everything that is 16-bit code or a
// VxD under Win98. Every other 32-bit DLL runs as the original. A DLL this
// table does not name at all runs as the original as soon as the Win98 tree
// contains it, so a new installation needs no entry here.
// PLAN.md step 7 moved MSVCRT, SHELL32, SHLWAPI, COMCTL32 and COMDLG32 to
// `Original`, so `NativeToday` names no DLL any more; it stays as the route a
// future entry uses while its port is still the default. The implementations
// of the five are not deleted: a test of one of them moves a single DLL back
// with Application::AddNativeModule (probe/host flag --native-module), and the
// number of such overrides is not limited.
// tools/dll_routes.py parses this table, so every entry stays one line of the
// form {"NAME.DLL", Route::X, "reason"}.
#pragma once
namespace DllRoutes
{
enum class Route
{
    Native,        // native ARM64 provider, by the rule in PLAN.md section 0
    NativeToday,   // native provider today; PLAN.md step 7 turns it original
    Native16,      // 16-bit NE module; implemented behind the Win16 module ordinals
    Original,      // original x86 code out of the Win98 tree
};
struct Entry
{
    const char *dll;
    Route route;
    const char *reason;
};
inline constexpr Entry Table[] = {
    {"NEO98TOUCH.DLL", Route::Native, "optional NEO98 multitouch extension; not a Windows 98 system DLL"},
    {"KERNEL32.DLL", Route::Native, "process, memory, files and registry sit on VMM and the VxDs; no portable original lower edge"},
    {"USER32.DLL", Route::Native, "window, message and input state is shared with the 16-bit USER.EXE we implement natively"},
    {"GDI32.DLL", Route::Native, "drawing goes through our DIBENG-compatible renderer and the 16-bit GDI.EXE state"},
    {"ADVAPI32.DLL", Route::Native, "a thin wrapper over the registry KERNEL32 keeps in VMM; the original would call into a VxD"},
    {"WINMM.DLL", Route::Native, "timers and wave output need the host; the original talks to the MMSYSTEM VxDs"},
    {"MSVCRT.DLL", Route::Original, "original x86 code out of the Win98 tree; the native CRT stays in the tree only for its own tests"},
    {"SHELL32.DLL", Route::Original, "original x86 code; the implemented shell folders stay for their own tests"},
    {"SHLWAPI.DLL", Route::Original, "original x86 code; the implemented path and registry helpers stay for their own tests"},
    {"COMCTL32.DLL", Route::Original, "original x86 code; the implemented common controls stay for their own tests"},
    {"COMDLG32.DLL", Route::Original, "original x86 code with the shell view; the implemented file dialog stays for its own tests"},
    {"SERIALUI.DLL", Route::Native16, "16-bit NE module; its exports are implemented behind KERNEL32 ordinals 35 to 37"},
    {"VER.DLL", Route::Native16, "16-bit NE module below VERSION.DLL; its six flat thunk targets are implemented"},
    {"DDRAW16.DLL", Route::Native16, "16-bit NE module below DDRAW.DLL; it talks to the display driver, so it is ours"},
    {"DCIMAN.DLL", Route::Native16, "16-bit NE module below DCIMAN32.DLL; the DCI escapes into the display driver are ours"},
    {"DSKAPI16.DLL", Route::Native16, "16-bit NE module below DSKAPI32.DLL; it talks to the disk compression VxD"},
    {"INET16.DLL", Route::Native16, "16-bit NE half of ICFG95.DLL; it reads the network components out of the registry through SETUPX"},
    {"TSP3216S.DLL", Route::Native16, "16-bit NE half of TAPI's bridge to 16-bit telephony providers; without such providers its answers are constants"},
    {"DMUSIC16.DLL", Route::Native16, "16-bit NE module below DMusic32.dll; its MIDI port sits on MMSYSTEM, so it is ours"},
    {"GLIDEPAG.DLL", Route::Native, "the page Glide of the display properties that comes with the Glide driver, a shell extension of DESK.CPL "},
    {"GLIDE2X.DLL", Route::Native, "the Glide 2 driver of a 3dfx card, a vendor's driver like the display driver's HAL; it draws on the host rasterizer "},
    {"NEO98GL32.DLL", Route::Native, "the OpenGL driver of the display driver, found by OPENGL32 through OPENGL_GETINFO; it draws on the host rasterizer"},
    {"PIFMGR.DLL", Route::Native16, "16-bit NE module of the DOS program properties; its services and its property pages are native"},
    {"NEO98DX.DLL", Route::Native, "the DirectDraw and DirectSound driver below the ARM64 DDRAW.DLL and DSOUND.DLL; it sits on the display driver and the mixer"},
};
// The module references of a 16-bit image name modules, not files: "KERNEL"
// is KRNL386.EXE, "USER" is USER.EXE. Every module named here is ours by the
// rule of PLAN.md section 0, so an import of a 16-bit program that names one
// resolves to a native gate and never loads the original NE file. Everything
// else a 16-bit program imports is a DLL it ships with, like CTL3D, BWCC or
// VBRUN, and runs as the original 16-bit code. See src/ne_loader.cpp.
inline bool Win16System(const char *module)
{
    static const char *const names[] = {"KERNEL",  "USER",   "GDI",   "KEYBOARD", "SOUND",
                                        "SYSTEM",  "SHELL",  "TOOLHELP", "COMMDLG", "COMMCTRL",
                                        "DISPLAY", "MOUSE",  "MMSYSTEM", "WIN87EM", "VER",
                                        "DCIMAN"};
    for (const char *name : names)
    {
        const char *a = name, *b = module;
        while (*a && *b)
        {
            unsigned x = (unsigned char)*a++, y = (unsigned char)*b++;
            if (y >= 'a' && y <= 'z') y -= 32;
            if (x != y) break;
        }
        if (!*a && !*b)
            return true;
    }
    return false;
}
// Route of one DLL leaf name, or Original when the table does not name it.
inline Route Of(const char *leaf)
{
    for (const auto &entry : Table)
    {
        const char *a = entry.dll, *b = leaf;
        while (*a && *b)
        {
            unsigned x = (unsigned char)*a++, y = (unsigned char)*b++;
            if (x >= 'a' && x <= 'z') x -= 32;
            if (y >= 'a' && y <= 'z') y -= 32;
            if (x != y) break;
        }
        if (!*a && !*b)
            return entry.route;
    }
    return Route::Original;
}
inline bool Native(const char *leaf)
{
    const Route route = Of(leaf);
    return route == Route::Native || route == Route::NativeToday;
}
} // namespace DllRoutes
