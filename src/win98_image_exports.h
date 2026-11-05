// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_clipboard_exports.h"
// These Windows 98 image exports return zero and set error 120.
static const UserUnsupportedExport OriginalImageUnsupportedExports[] = {
    {"LoadBitmapW",400,2}, {"LoadCursorW",404,2}, {"LoadIconW",406,2},
    {"LoadImageW",408,6}, {"LoadCursorFromFileW",403,1},
};
