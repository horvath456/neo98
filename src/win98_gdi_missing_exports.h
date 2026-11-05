// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_clipboard_exports.h"
// These GDI32 exports return zero and set ERROR_CALL_NOT_IMPLEMENTED (120).
// The third column records the number of argument DWORDs consumed.
static const UserUnsupportedExport OriginalGdiMissingUnsupportedExports[]={
 {"GetLayout",271,1},{"GetWorldTransform",321,2},
 {"ModifyWorldTransform",328,3},{"SetColorAdjustment",381,2},
 {"SetLayout",393,2},{"GetGlyphOutlineW",264,7},
 {"PolyDraw",345,4},{"MaskBlt",327,12},
 // The three wide forms the collection imports., and
 // carry the same three instructions; the linker folded them with
 // ResetDCW, SetICMProfileW and GetICMProfileW.
 {"StartDocW",421,2},{"GetObjectW",284,3},{"GetCharacterPlacementW",240,6},
};
