// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../src/files.h"
// The final x86 cleanup callback leaves proof before CRT exit terminates the process.
inline bool CrtExitContractResult()
{
    unsigned index = 0;
    if (Files::System().Lookup("C:\\CRTEXIT.DAT", index))
        return false;
    const auto *file = Files::System().Get(index);
    // Bytes: on a host volume the four bytes are fetched from the provider.
    const auto *data = file ? Files::System().Bytes(index) : nullptr;
    const bool ok = file && data && file->size == 4 && data[0] == 0x98 && data[1] == 0x98 &&
                    data[2] == 0x98 && data[3] == 0x98;
    return !Files::System().Remove("C:\\CRTEXIT.DAT", false) && ok;
}
