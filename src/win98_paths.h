// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_arrays.h"
namespace Win98Paths
{
// Native Win98 SHLWAPI ANSI paths, with the runtime's single-byte ACP.
// Error is changed only by the original invalid-argument branch.
unsigned Call(Win98Arrays::Memory &, unsigned ordinal, const unsigned *args, unsigned &error);
// COMDLG32 GetFileTitleA, original parser fallback when shell display-name lookup fails.
unsigned FileTitle(Win98Arrays::Memory &, const unsigned *args, unsigned &dialog_error);
} // namespace Win98Paths
