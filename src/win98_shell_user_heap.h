// SPDX-License-Identifier: GPL-2.0-or-later
// SHELL tests whether USER can allocate 512 bytes, immediately frees a
// successful allocation, and returns one or zero. ShellExecuteEx uses this as
// a resource-reserve check.
#pragma once

#include "win98_user_see_user_do.h"

namespace Win98ShellUserHeap
{
// The block size the original asks for.
constexpr unsigned ProbeBytes = 0x200;

struct Environment
{
    virtual ~Environment() = default;
    // The original reaches USER.216 through a far call. The service keeps the
    // same four word arguments.
    virtual unsigned SeeUserDo(unsigned request, unsigned first, unsigned second,
                               unsigned third) = 0;
};

inline unsigned Probe(Environment &e)
{
    const unsigned handle = e.SeeUserDo(Win98UserSeeUserDo::Allocate, 0, 0, ProbeBytes);
    if (!(handle & 0xffffu))
        return 0;
    e.SeeUserDo(Win98UserSeeUserDo::Release, handle, 0, 0);
    return 1;
}
} // namespace Win98ShellUserHeap
