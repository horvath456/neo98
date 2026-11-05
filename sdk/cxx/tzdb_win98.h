// SPDX-License-Identifier: GPL-2.0-or-later
// Only platform discovery differs from libc++'s timezone implementation.
#pragma once
#include <cstdlib>
#include <string_view>

_LIBCPP_BEGIN_NAMESPACE_STD
namespace chrono {
inline string_view __neo98_tzdb_directory()
{
    const char *path = std::getenv("NEO98_TZDIR");
    return path && *path ? path : "C:\\WINDOWS\\TZDATA";
}
}
_LIBCPP_END_NAMESPACE_STD
