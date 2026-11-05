// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace Gdi {
// Destinations for the fixed arrays used by older machine states. Handles
// retain their original index width for the lifetime of the restored machine.
bool PrepareLegacyObjects(unsigned count);
void *LegacyObject(unsigned index);
void *LegacySavedState(void *context);
bool FinishLegacyContext(void *context, unsigned count);
void *PrepareLegacyRegions(unsigned count);
void *PrepareLegacyBitmapRanges(unsigned count);
}
