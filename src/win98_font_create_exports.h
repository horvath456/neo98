// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_clipboard_exports.h"
// GDI32/ ->, return 0 and error 120.
static const UserUnsupportedExport OriginalFontCreateUnsupportedExports[]={
    {"CreateFontIndirectW",151,1},{"CreateFontW",152,14}
};
