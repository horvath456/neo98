// SPDX-License-Identifier: GPL-2.0-or-later
// The host media provider behind \\.\NEO98MEDIA (src/x86_kernel_pipes.cpp).
// The host installs it (tools/host_media/service.h); src/ holds no network
// or Kodi code.
#pragma once
#include "host_media_abi.h"
namespace HostMedia
{
struct Provider
{
    virtual ~Provider() = default;
    virtual unsigned Status(HM_Status &) = 0;
    virtual unsigned Browse(const HM_Request &, HM_Listing &) = 0;
    virtual unsigned Picture(const HM_Request &, HM_Picture &) = 0;
    virtual unsigned Control(const HM_Request &) = 0;
    virtual unsigned Dialog(HM_Dialog &) = 0;
};
inline Provider *current = nullptr;
inline constexpr char DeviceName[] = "NEO98MEDIA";
}
