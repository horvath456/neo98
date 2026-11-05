// SPDX-License-Identifier: GPL-2.0-or-later
// The registry of the DOS machine provider. src/dos_vm.h.
#include "dos_vm.h"
namespace DosVm
{
namespace
{
Provider installed;
bool present = false;
bool ends_with_program = false;
} // namespace
void EndWithProgram(bool on) { ends_with_program = on; }
bool EndsWithProgram() { return ends_with_program; }
void SetProvider(const Provider *provider)
{
    present = provider != nullptr;
    installed = provider ? *provider : Provider();
}
const Provider *Current() { return present ? &installed : nullptr; }
} // namespace DosVm
