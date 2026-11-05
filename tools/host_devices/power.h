// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../../src/host_devices_abi.h"
#include <SDL.h>
namespace HostDeviceService
{
inline AH_Power PowerStatus(SDL_PowerState state, int seconds, int percent)
{
    AH_Power out{255,255,255,0,~0u,~0u};
    if (state==SDL_POWERSTATE_NO_BATTERY) return AH_Power{1,128,255,0,~0u,~0u};
    if (state==SDL_POWERSTATE_UNKNOWN) return out;
    out.ac = state==SDL_POWERSTATE_ON_BATTERY ? 0 : 1;
    out.percent = percent>=0 && percent<=100 ? (unsigned char)percent : 255;
    out.flags = out.percent==255 ? 0 : out.percent<5 ? 4 : out.percent<33 ? 2 : out.percent>66 ? 1 : 0;
    if (state==SDL_POWERSTATE_CHARGING) out.flags |= 8;
    if (seconds>=0 && state==SDL_POWERSTATE_ON_BATTERY) out.seconds=unsigned(seconds);
    return out;
}
inline AH_Power ReadPower()
{
    int seconds=-1, percent=-1;
    const auto state=SDL_GetPowerInfo(&seconds,&percent);
    return PowerStatus(state,seconds,percent);
}
}
