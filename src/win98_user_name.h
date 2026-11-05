// SPDX-License-Identifier: GPL-2.0-or-later
// Windows 98 user-name lookup and output-buffer behavior. The environment
// supplies registry, text, and memory services.
#pragma once
namespace Win98UserName
{
// The two user-name strings kept by ADVAPI32.
inline const char *Key()
{
    return "System\\CurrentControlSet\\Control";
}
inline const char *Value()
{
    return "Current User";
}
// ADVAPI32. The error a failed query reports.
constexpr unsigned QueryFailure = 0x4dd;
} // namespace Win98UserName
