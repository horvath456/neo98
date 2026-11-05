// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_path_family.h"

namespace Win98ProcessEntry {
using U = unsigned;

// KERNEL32. Preserve the actual byte probes, including
// read/modify/write of PROCESS_INFORMATION's endpoints. The caller supplies
// the native fault boundary in place of the original FS exception frame.
template<class E> bool Probe(E &e, const U *a) {
    U value;
    for (U i = 0; i < 2; ++i) if (a[i]) {
        U p = a[i], remaining = ~0u;
        do {
            if (!e.Read8(p++, value)) return false;
        } while (--remaining && value);
    }
    for (U i = 2; i < 4; ++i) if (a[i])
        if (!e.Read8(a[i], value) || !e.Read8(a[i] + 11, value)) return false;
    if (a[7] && !e.Read8(a[7], value)) return false;
    if (!e.Read8(a[8], value) || !e.Read8(a[8] + 67, value)) return false;
    for (U i = 0; i < 2; ++i) {
        const U offset = i ? 15 : 0;
        if (!e.Read8(a[9] + offset, value) || !e.Write8(a[9] + offset, value)) return false;
    }
    return true;
}

// FileApiMode comes from the existing file API state. Thread flags use the
// byte at TDB+45h. String conversion, the internal creation service and the
// command retry are providers. This routine itself cannot create a native process.
template<class E> U Launch(E &e, Win98PathFamily::State &state, const U *a) {
    U application = a[0], command = a[1], directory = a[7];
    state.thread_flags |= 0x10;
    const bool converted = Win98PathFamily::FileApiMode(state, 0);
    if (converted) {
        application = e.ConvertInput(application, 0);
        command = e.ConvertInput(command, 0);
        directory = e.ConvertInput(directory, 0);
    }
    const U internal[] = {0, application, command, 0, a[2], a[3], a[4],
                          a[5], a[6], directory, a[8], a[9], 0};
    U result = e.Create(internal);
    if (!result && !application)
        result = e.Retry(application, command, internal, result);
    if (converted) {
        e.FreeInput(application, 0);
        e.FreeInput(command, 0);
        e.FreeInput(directory, 0);
        Win98PathFamily::ClearMode(state);
    }
    state.thread_flags &= ~0x10u;
    return result;
}
}
