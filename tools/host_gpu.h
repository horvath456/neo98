// SPDX-License-Identifier: GPL-2.0-or-later
// Host selection for the existing Gpu::Backend contract.
#pragma once
#include "host_metal.h"
#include <cstdlib>
#include <cstring>
#ifdef NEO98_WITH_VULKAN
#include "host_vulkan.h"
#endif
namespace HostGpu
{
inline Gpu::Backend *Install()
{
    const char *wanted = getenv("NEO98_GPU");
    if (wanted && !strcmp(wanted, "none")) return nullptr;
#if defined(__APPLE__)
    return HostMetal::Install();
#elif defined(NEO98_WITH_VULKAN)
    return !wanted || !strcmp(wanted, "auto") || !strcmp(wanted, "vulkan") ? HostVulkan::Install() : nullptr;
#else
    return nullptr;
#endif
}
}
