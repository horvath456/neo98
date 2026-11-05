// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_clipboard_exports.h"
// Original USER32 error120 dispatcher; class_defaults.py verifies these stubs.
static const UserUnsupportedExport OriginalClassUnsupportedExports[]={
 {"GetClassInfoExW",228,3},
 {"GetClassInfoW",229,3},
 {"GetClassLongW",231,2},
 {"GetClassNameW",233,3},
 {"RegisterClassExW",474,1},
 {"RegisterClassW",475,1},
 {"SetClassLongW",516,3},
 {"UnregisterClassW",615,2},
};
