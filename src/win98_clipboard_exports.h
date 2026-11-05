// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// The unsupported Windows 98 clipboard export returns zero, sets error 120,
// and consumes three arguments.
struct UserUnsupportedExport { const char *name; unsigned ordinal, encoding; };
static const UserUnsupportedExport OriginalUserUnsupportedExports[] = {
    {"GetClipboardFormatNameW", 239, 3},
    // Both indirect-dialog W exports share, error120 and five args.
    {"CreateDialogIndirectParamW", 80, 5},
    {"DialogBoxIndirectParamW", 144, 5},
};
