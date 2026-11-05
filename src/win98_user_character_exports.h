// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_clipboard_exports.h"
// USER32/D9/E2/EB: original SetLastError(120), zero return.
static const UserUnsupportedExport OriginalCharacterUnsupportedExports[] = {
    {"CharLowerW",37,1}, {"CharUpperW",53,1},
    {"CharLowerBuffW",36,2}, {"CharUpperBuffW",52,2},
    {"IsCharLowerW",378,1}, {"IsCharUpperW",380,1},
    {"IsCharAlphaNumericW",375,1}, {"IsCharAlphaW",376,1},
    {"CharNextW",41,1}, {"CharPrevW",45,2},
    {"CharNextExW",40,3}, {"CharPrevExW",44,4}
};
