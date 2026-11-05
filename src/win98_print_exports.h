// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Original COMDLG32 : SetLastError(120), FALSE, ret 4.
static constexpr struct {
    const char *name;
    unsigned ordinal, encoding;
} OriginalPrintUnsupportedExports[] = {
    {"PrintDlgW", 118, 1},
};
