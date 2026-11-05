// SPDX-License-Identifier: GPL-2.0-or-later
// Which guest pages this process may execute from.
// The i386 of the Win9x era has no execute-disable bit, so for the guest every
// readable page is executable and stays that way. The engine
// pays for that: Unicorn keeps TLB_NOTDIRTY on every executable page and sends
// each write of the guest through the slow store path.
// The port therefore hands Unicorn the execute right only for pages that hold
// code, and adds it for any other page the moment the guest really jumps into
// it. The guest sees no difference.
// The record is a bit per page of the 4 GiB address space, 128 KiB in all, plus
// the lowest and highest page in use so a scan over a range stops early. Once a
// page is executable it stays executable until it is unmapped, because a
// program that built code there may jump in again after any VirtualProtect.
#ifndef NEO98_EXECUTE_PAGES_H
#define NEO98_EXECUTE_PAGES_H

namespace X86 {

class ExecutePages
{
  public:
    static const unsigned PageBits = 12, Pages = 1u << 20, Words = Pages / 32;
    ExecutePages() : bits(new unsigned[Words]()), lowest(Pages), highest(0), pages(0), grants(0) {}
    ~ExecutePages() { delete[] bits; }
    ExecutePages(const ExecutePages &) = delete;
    ExecutePages &operator=(const ExecutePages &) = delete;

    bool Test(unsigned address) const
    {
        const unsigned page = address >> PageBits;
        return (bits[page >> 5] >> (page & 31)) & 1u;
    }
    // Answers whether anything changed, so a caller can skip the work that
    // follows when the range was already executable.
    bool Add(unsigned base, unsigned size)
    {
        bool changed = false;
        for (unsigned page = base >> PageBits, last = Last(base, size); page <= last; ++page)
        {
            unsigned &word = bits[page >> 5];
            const unsigned bit = 1u << (page & 31);
            if (word & bit)
                continue;
            word |= bit;
            changed = true;
            ++pages;
            if (page < lowest) lowest = page;
            if (page > highest) highest = page;
        }
        return changed;
    }
    // An unmapped page is gone; a later allocation at the same address is a
    // different object and starts without the right again.
    void Remove(unsigned base, unsigned size)
    {
        if (!pages) return;
        for (unsigned page = base >> PageBits, last = Last(base, size); page <= last; ++page)
        {
            unsigned &word = bits[page >> 5];
            const unsigned bit = 1u << (page & 31);
            if (!(word & bit)) continue;
            word &= ~bit;
            --pages;
        }
        if (!pages) { lowest = Pages; highest = 0; }
    }
    // The first executable page at or above `page` inside [base, base+size),
    // or Pages when there is none. Scanning words keeps a whole image cheap.
    unsigned Next(unsigned base, unsigned size, unsigned page) const
    {
        if (!pages) return Pages;
        const unsigned first = base >> PageBits, last = Last(base, size);
        if (page < first) page = first;
        if (page < lowest) page = lowest;
        const unsigned stop = last < highest ? last : highest;
        while (page <= stop)
        {
            const unsigned word = bits[page >> 5] >> (page & 31);
            if (!word) { page = (page | 31u) + 1; continue; }
            unsigned skip = 0;
            for (unsigned v = word; !(v & 1u); v >>= 1) ++skip;
            page += skip;
            return page <= stop ? page : Pages;
        }
        return Pages;
    }
    unsigned Count() const { return pages; }
    // How often a fetch fault had to add the right; the measurement of
    // The host reports these counters for executable-page diagnostics.
    unsigned Grants() const { return grants; }
    void CountGrant() { ++grants; }

  private:
    static unsigned Last(unsigned base, unsigned size)
    {
        const unsigned long long end = (unsigned long long)base + (size ? size : 1u) - 1;
        return (unsigned)((end > 0xffffffffull ? 0xffffffffull : end) >> PageBits);
    }
    unsigned *bits;
    unsigned lowest, highest, pages, grants;
};

}

#endif
