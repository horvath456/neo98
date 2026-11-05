// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "x86_state.h"
#include "dos_vm.h"
namespace MachineState
{
// The same narrow contract is used before saving and when checking bytes.
inline bool DosUiInnerFrameSupported(unsigned kind, unsigned phase)
{
    if (!((kind >= 1 && kind <= 14) || kind == 32 || kind == 33 ||
          (kind >= 40 && kind <= 51) || (kind >= 53 && kind <= 55) || kind == 61)) return false;
    if (kind == 1 && !(phase & 2)) return false;
    if (kind == 40 && phase != 1 && phase != 2) return false;
    if (kind == 61 && phase > 1) return false;
    if ((kind == 41 || kind == 43 || kind == 48 || kind == 50 || kind == 51) && phase != 1) return false;
    return true;
}
// Only the completed native PIF page collection at modal presentation is
// admitted from the extended property provider. Creation/release callbacks
// need their own contracts and remain outside this combined provider.
template<class Frame>
inline bool DosUiCollectedSheet(const Frame &api, const Frame &collect,
                                const Frame &sheet, const Frame &modal)
{
    return api.kind == 40 && api.d == 2 && collect.kind == 61 && collect.d <= 1 &&
           collect.a && collect.b && collect.data[0] == api.a && collect.data[1] == api.b &&
           collect.data[1] <= 1 && collect.c == collect.data[2] && collect.c <= 99 &&
           collect.data[3] == 2 && sheet.kind == 41 && sheet.d == 1 && sheet.a == collect.a &&
           modal.kind == 1 && (modal.d & 2) && modal.a == sheet.b && modal.a;
}
// Pure preflight for the explicit composite provider, including the launch
// association. It does not allocate target objects or assign UI globals.
bool ValidateDosUiLaunch(Source &, const DosVm::Launch &, char *error, size_t error_bytes);
}
