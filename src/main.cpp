// SPDX-License-Identifier: GPL-2.0-or-later
#include "kernel.h"
#include <circle/startup.h>

int main()
{
    Kernel kernel;
    if (kernel.Initialize()) kernel.Run();
    halt();
    return EXIT_HALT;
}
