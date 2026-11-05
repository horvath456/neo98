// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_vmm_mapping.h"
extern "C"
{
    unsigned vmm_mapping_static(unsigned reservation, unsigned flags)
    {
        return Win98VmmMapping::StaticAllowed(reservation, flags);
    }
    unsigned vmm_mapping_increment(unsigned flags) { return Win98VmmMapping::PagerIncrement(flags); }
    unsigned vmm_mapping_advance(unsigned data, unsigned flags)
    {
        return Win98VmmMapping::AdvancePagerData(data, flags);
    }
    unsigned vmm_mapping_install(unsigned entry, unsigned flags)
    {
        return Win98VmmMapping::InstallBits(entry, flags);
    }
    void vmm_mapping_existing(unsigned entry, unsigned flags, unsigned reject, unsigned *out)
    {
        const auto result = Win98VmmMapping::Existing(entry, flags, reject);
        out[0] = unsigned(result.action);
        out[1] = result.entry;
        out[2] = unsigned(result.committed_delta);
    }
}
