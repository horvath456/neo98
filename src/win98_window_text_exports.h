// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_clipboard_exports.h"
// USER32 original120 gates, including the InternalGetWindowText alias.
static const UserUnsupportedExport OriginalWindowTextUnsupportedExports[]={
 {"GetDlgItemTextW",255,4},{"GetWindowTextLengthW",343,1},
 {"GetWindowTextW",344,3},{"InternalGetWindowText",368,3},
 {"SetDlgItemTextW",527,3},{"SetWindowTextW",572,2}
};

static const UserUnsupportedExport OriginalWindowTextAnsiExports[]={
 {"GetDlgItemTextA",254,4},{"GetWindowTextA",341,3},{"GetWindowTextLengthA",342,1},
 {"SetDlgItemTextA",526,3},{"SetWindowTextA",571,2}
};
