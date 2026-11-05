// SPDX-License-Identifier: GPL-2.0-or-later
#include "host_devices/camera_inventory.h"
#include <cassert>
#include <initializer_list>
int main()
{
    using I = HostDeviceService::CameraInventory;
    for (bool initial : {false, true})
    {
        I inventory;
        // A delayed first scan must not manufacture an absent-device baseline.
        for (int i=0; i<10; ++i) assert(inventory.Observe(false, !initial)==I::Unchanged);
        assert(inventory.Observe(true, initial)==I::Initial);
        assert(inventory.present==initial);
        for (int i=0; i<10; ++i) assert(inventory.Observe(true, initial)==I::Unchanged);
        assert(inventory.Observe(true, !initial)==I::Changed);
        assert(inventory.Observe(true, !initial)==I::Unchanged);
        assert(inventory.Observe(true, initial)==I::Changed);
    }
}
