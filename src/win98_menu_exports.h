// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_clipboard_exports.h"
// These Windows 98 exports return zero and set ERROR_CALL_NOT_IMPLEMENTED.
static const UserUnsupportedExport OriginalMenuUnsupportedExports[] = {
 {"AppendMenuW",8,4},{"GetMenuItemInfoW",286,4},{"GetMenuStringW",290,5},
 {"InsertMenuItemW",366,4},{"InsertMenuW",367,5},{"ModifyMenuW",437,5},
 {"SetMenuItemInfoW",541,4},
};
