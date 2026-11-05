// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace HostDeviceService
{
// The first asynchronous enumeration describes the hardware at boot. Only
// differences from that inventory are device arrivals/removals.
struct CameraInventory
{
    enum Update { Unchanged, Initial, Changed };
    bool initialized = false, present = false;
    Update Observe(bool ready, bool value)
    {
        if (!ready) return Unchanged;
        if (!initialized) { initialized = true; present = value; return Initial; }
        if (present == value) return Unchanged;
        present = value;
        return Changed;
    }
};
}
