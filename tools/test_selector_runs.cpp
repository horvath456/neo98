// SPDX-License-Identifier: GPL-2.0-or-later
// The selector table: runs of consecutive descriptors, the selectors of a
// block larger than 64 KiB, take the place of entries that were freed.
// tools/test_selector_runs.py builds and runs this.
#include "../src/win98_selector_table.h"
#include <stdio.h>
#include <stdlib.h>
static int failures;
static void okay(bool value, const char *what)
{
    if (!value)
    {
        printf("FAIL %s\n", what);
        ++failures;
    }
}
int main()
{
    static Win98Selectors::Table t;
    // A block of five selectors opened and freed a thousand times, the way a
    // film's frame buffer comes and goes, with a single selector kept in
    // between: the table has room for 4092 descriptors and must not fill.
    const unsigned kept = t.Create(0x100000) >> 16;
    unsigned last = 0;
    bool all = true;
    for (unsigned round = 0; round < 1000; ++round)
    {
        const unsigned run = t.CreateRun(5);
        all = all && run;
        if (!run)
            break;
        last = run;
        for (unsigned i = 0; i < 5; ++i)
            t.Unmap((run + i * 8) << 16);
    }
    okay(all, "a thousand runs of five, each freed before the next");
    okay(t.first <= Win98Selectors::Table::Reserved + 6, "the table did not grow with them");
    okay(t.At(kept) != nullptr, "the selector kept in between is still there");
    // The freed entries serve single selectors again, each exactly once.
    unsigned singles[5];
    for (unsigned &single : singles)
        single = t.Create(0x200000) >> 16;
    bool distinct = true;
    for (unsigned i = 0; i < 5; ++i)
        for (unsigned k = i + 1; k < 5; ++k)
            distinct = distinct && singles[i] && singles[i] != singles[k];
    okay(distinct && t.first <= Win98Selectors::Table::Reserved + 6, "five single selectors take the freed run");
    // A run does not take an entry that is in use: free two of the five, not
    // adjacent, and ask for two.
    t.Unmap(singles[0] << 16);
    t.Unmap(singles[2] << 16);
    unsigned low = singles[0], high = singles[2];
    if (low > high) { const unsigned swap = low; low = high; high = swap; }
    const unsigned pair = t.CreateRun(2);
    okay(pair && t.At(pair) && t.At(pair + 8) && (high - low == 8 || (pair != low && pair != high - 8 && pair + 8 != low)),
         "a run of two beside entries in use");
    for (unsigned single : singles)
        if (single != singles[0] && single != singles[2])
            okay(t.At(single) != nullptr && pair != single && pair + 8 != single, "entries in use stay");
    // And the two freed ones are still free for single selectors.
    const unsigned again = t.Create(0x300000) >> 16;
    okay(again && again != pair && again != pair + 8, "a freed entry beside the run is handed out once");
    // A run in the middle of freed entries leaves the others on the list.
    unsigned eight[8];
    for (unsigned &one : eight)
        one = t.Create(0x400000) >> 16;
    const unsigned end = t.first;
    for (unsigned one : eight)
        t.Unmap(one << 16);
    const unsigned three = t.CreateRun(3);
    okay(three && t.first == end, "a run of three out of eight freed entries");
    unsigned taken = 0;
    for (unsigned i = 0; i < 5; ++i)
    {
        const unsigned one = t.Create(0x500000) >> 16;
        if (one && (one < three || one > three + 16) && t.first == end)
            ++taken;
    }
    okay(taken == 5, "the other five serve single selectors, none twice");
    okay(t.first == end, "and the table is as long as before");
    (void)last;
    if (failures)
        return 1;
    printf("SELECTOR RUNS PASS\n");
    return 0;
}
