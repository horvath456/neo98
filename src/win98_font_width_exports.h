// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_clipboard_exports.h"
// GDI32, original zero return + SetLastError(120), four arguments.
static const UserUnsupportedExport OriginalFontWidthUnsupportedExports[]={
 {"GetCharABCWidthsFloatA",230,4},{"GetCharABCWidthsFloatW",231,4},
 {"GetCharABCWidthsW",232,4},{"GetCharWidth32W",234,4},
 {"GetCharWidthFloatA",236,4},{"GetCharWidthFloatW",237,4}
};
