// SPDX-License-Identifier: GPL-2.0-or-later
// The one place that names the DLLs a fixture of our own implementations keeps
// on the native route.
// Since PLAN.md step 7 MSVCRT, SHELL32, SHLWAPI, COMCTL32 and COMDLG32 run as
// original x86 code (src/dll_routes.h). COMMON.EXE and the
// other guest fixtures are linked against our implementations and test exactly
// those, so their runners turn the route back for their own process with
// Application::AddNativeModule. A runner that starts a real Win98 program never
// calls this.
#pragma once
#include "../src/x86_app.h"

namespace FixtureModules
{
inline constexpr const char *Reconstructed[] = {"MSVCRT.DLL", "SHELL32.DLL", "SHLWAPI.DLL",
                                                "COMCTL32.DLL", "COMDLG32.DLL"};
// Returns false when the application refuses one of the names.
inline bool KeepNative(X86::Application &application)
{
    for (const char *dll : Reconstructed)
        if (!application.AddNativeModule(dll))
            return false;
    return true;
}
} // namespace FixtureModules
