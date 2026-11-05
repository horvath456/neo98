// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "host_devices_abi.h"
namespace HostDevices
{
struct Provider
{
    virtual ~Provider() = default;
    virtual AH_Power Power() = 0;
    virtual unsigned Status(AH_Status &) = 0;
    virtual unsigned Action(const AH_Request &) = 0;
    virtual unsigned Frame(void *, unsigned, unsigned &) = 0;
    // Whether the host has a camera now; the Device Manager shows it
    // (src/config_manager.cpp).
    virtual bool Camera() { return false; }
};
inline Provider *current = nullptr;
// WinMM capture belongs to the guest session, including across saved states.
struct Capture {
    unsigned window=0,period=0,next=0,changed_only=0,seen=0;
    unsigned previous[4]={};
};
inline Capture captures[2];
inline unsigned thresholds[AH_MAX_JOYS]={};
inline AH_Power Power() { return current ? current->Power() : AH_Power{1,128,255,0,~0u,~0u}; }
inline bool HasCamera(){return current&&current->Camera();}
inline bool HasBattery(){const auto p=Power();return p.flags!=128&&p.flags!=255;}
inline constexpr char BatteryGuid[]="{72631e54-78a4-11d0-bcf7-00aa00b7b32a}";
inline constexpr unsigned char BatteryGuidBytes[]={0x54,0x1e,0x63,0x72,0xa4,0x78,0xd0,0x11,0xbc,0xf7,0x00,0xaa,0x00,0xb7,0xb3,0x2a};
inline constexpr char BatteryId[]="ROOT\\NEO98BAT\\0000";
inline constexpr char BatteryInterface[]="ROOT#NEO98BAT#0000#{72631e54-78a4-11d0-bcf7-00aa00b7b32a}";

}
