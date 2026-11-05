// SPDX-License-Identifier: GPL-2.0-or-later
// KERNEL32 segment-map helpers. Stateful descriptors live in the native LDT.
#pragma once
#include "win98_selector_table.h"
namespace Win98SegmentMap {
inline bool Direct(unsigned pointer) { return pointer<=0xffffu; }
inline unsigned SelectorWord(unsigned pointer) { return pointer>>16; }
using Table=Win98Selectors::Table;
}
