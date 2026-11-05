// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Original SHELL32 hash 533c5809a9b6bdb236cce17a14c03ff251c4ca4d00790ea8e8034ee5d14d7f61.
// These complete original bodies call SetLastError(120), return zero and pop
// the declared arguments. They are original failures, not image implementations.
struct ShellIconUnsupportedExport { const char *name; unsigned ordinal, encoding; };
static const ShellIconUnsupportedExport OriginalShellIconUnsupportedExports[] = {
    {"ExtractAssociatedIconExW", 226, 4}, //
    {"ExtractIconExW", 232, 5},           //
};
