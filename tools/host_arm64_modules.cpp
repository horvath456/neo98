// SPDX-License-Identifier: GPL-2.0-or-later
// The ARM64 system DLLs of the host: DDRAW.DLL and DSOUND.DLL built for ARM64
// with the SDK (sdk/dx), carried in the runner as bytes and handed to the
// loader, which gives them to ARM64 processes under the name of the system
// file. The x86 originals of the Win98 folder stay what x86 processes load.
// tools/arm64_dx.py writes the bytes.
#include "../src/x86_app.h"
#include "../build/generated/arm64_system_modules.inc"

namespace
{
struct Arm64SystemModules
{
    Arm64SystemModules()
    {
        for (const auto &module : arm64_system_module_table)
            if (module.name) X86::Application::AddArm64SystemModule(module.name, module.bytes, module.size);
    }
} arm64_system_modules_registered;
} // namespace
