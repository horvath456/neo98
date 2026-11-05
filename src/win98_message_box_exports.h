// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_clipboard_exports.h"
// USER32 through KERNEL32 ordinal17. This original entry returns
// error120 without reading MSGBOXPARAMSW; it is not a native dialog port.
static const UserUnsupportedExport OriginalMessageBoxUnsupportedExports[]={
 {"MessageBoxIndirectW",433,1}
};
