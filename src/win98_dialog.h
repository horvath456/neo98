// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "gdi.h"
namespace Win98Dialog
{
struct Node
{
    unsigned parent = 0, first = 0, next = 0, style = 0, extended = 0;
};
struct Tree
{
    void *context;
    Node (*get)(void *, unsigned);
};
// Original rounding adds half the divisor even
// for negative products. The native adapter keeps 32-bit coordinates.
int Scale(int coordinate, int unit, unsigned divisor);
bool MapRect(Gdi::RECT *, int xunit, int yunit);
// Native USER.EXE and their traversal helpers.
unsigned NextTab(const Tree &, unsigned root, unsigned current, bool previous);
unsigned NextGroup(const Tree &, unsigned root, unsigned current, bool previous);
// USER.EXE: the control of the dialog that holds current, the highest
// ancestor below root or below a WS_EX_CONTROLPARENT window.
unsigned ChildControl(const Tree &, unsigned root, unsigned current);
// USER.EXE: the control after current in the order of the dialog,
// entering WS_EX_CONTROLPARENT windows (flags 1: not when hidden, 2: not when
// disabled) and wrapping at the end; zero current gives the first control.
unsigned NextControl(const Tree &, unsigned root, unsigned current, unsigned flags);
unsigned OriginalSelfTest();
unsigned OriginalTestCount();
} // namespace Win98Dialog
