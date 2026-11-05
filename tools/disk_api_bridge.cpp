// SPDX-License-Identifier: GPL-2.0-or-later
// The implementation of src/win98_disk_api.h behind a C entry, so
// tools/test_disk_api_original.py can compare it with the original DSKAPI16.DLL
// running under Unicorn.
#include "../src/win98_disk_api.h"
using U = unsigned;
// `cx` is the CX the 16-bit caller left behind, which three routines of the
// original hand back. `out` receives flag, flag_value, map, map_first,
// map_second, empty_name.
extern "C" U disk_api_no_driver(U ordinal, U drive, U cx, U *out)
{
    Win98DiskApi::Outputs outputs;
    const U result = Win98DiskApi::NoDriver(ordinal, drive, cx, outputs);
    out[0] = outputs.flag;
    out[1] = outputs.flag_value;
    out[2] = outputs.map;
    out[3] = outputs.map_first;
    out[4] = outputs.map_second;
    out[5] = outputs.empty_name;
    return result;
}
extern "C" U disk_api_carried(U ordinal) { return Win98DiskApi::Carried(ordinal); }
