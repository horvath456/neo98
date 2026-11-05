// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98Parameters
{
// The provider owns display settings and GDI font objects. Offsets name words
// in the analyzed USER.EXE data segment, not addresses in the ARM kernel.
struct Environment
{
    virtual unsigned short Word(unsigned offset) const = 0;
    virtual bool Font(unsigned handle, unsigned char (&logfont16)[50]) const = 0;
    // The power states of the display, the answer of VDD function 91h
    // (USER over ): 4 low power, 8 power off, 0 none.
    virtual unsigned PowerStates() const { return 0; }
};
void ExpandFont(const unsigned char (&logfont16)[50], unsigned char *logfont32);
// Native read branches of USER.EXE and helper.
// Output retains the original internal wire format: scalar WORDs, LOGFONT16,
// RECT16, or the already 32-bit NONCLIENT/MINIMIZED/ICONMETRICS structures.
// -1 means a branch outside this port, 0 original failure, 1 success.
int Query(const Environment &, unsigned action, unsigned parameter, unsigned char *out, unsigned capacity,
          unsigned &written);
} // namespace Win98Parameters
