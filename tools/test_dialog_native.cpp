// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_dialog.h"
#include <cassert>
#include <cstdio>
int main()
{
    unsigned failed = Win98Dialog::OriginalSelfTest();
    if (failed)
        printf("DIALOG FAILED case %u\n", failed);
    assert(!failed);
    printf("DIALOG PASS %u original cases\n", Win98Dialog::OriginalTestCount());
}
