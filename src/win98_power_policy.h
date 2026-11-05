// SPDX-License-Identifier: GPL-2.0-or-later
// The power policies NTKERN.VXD keeps for NtPowerInformation (int 2Eh, 108h),
// levels 0 to 3, 8 and 9.
// Separate SYSTEM_POWER_POLICY records hold mains and battery settings;
// a machine without a battery uses the mains policy. Initialization supplies
// default policies and administrator limits, then applies registry values.
// Policy validation checks revision 1, administrator limits, power actions,
// supported system states and throttle percentages against capabilities.
// A validated policy replaces the stored one only if it differs.
// Levels 0 and 1 retain the raw input as AcPolicy and DcPolicy. Windows keeps
// these in the volatile Control\Session Manager\Power key and rereads them
// when capabilities or administrator limits change. This port keeps them in
// machine state. It has no power driver, idle detection or thermal handling.
#pragma once
#include <string.h>
namespace Win98PowerPolicy
{
using U = unsigned;
using Byte = unsigned char;
constexpr U PolicyBytes = 0xe8, AdminBytes = 0x18, CapabilityBytes = 0x4c;
constexpr U StatusInvalidParameter = 0xc000000du;
constexpr U StatusBufferTooSmall = 0xc0000023u;
constexpr U StatusBufferOverflow = 0x80000005u;
// The levels of NtPowerInformation.
enum Level : U
{
    PolicyAc = 0,
    PolicyDc = 1,
    VerifyPolicyAc = 2,
    VerifyPolicyDc = 3,
    Capabilities = 4,
    PolicyCurrent = 8,
    AdministratorPolicy = 9,
};
struct Machine
{
    Byte capabilities[CapabilityBytes] = {};
    Byte ac[PolicyBytes] = {}, dc[PolicyBytes] = {};
    Byte admin[AdminBytes] = {};
    // The volatile registry values: the raw input of the last level 0 or 1
    // (AcPolicy, DcPolicy) and of level 9 (AdminPolicy).
    Byte saved_ac[PolicyBytes] = {}, saved_dc[PolicyBytes] = {}, saved_admin[AdminBytes] = {};
    bool has_saved_ac = false, has_saved_dc = false, has_saved_admin = false;
    bool ready = false;
};
inline U Load(const Byte *p) { return U(p[0]) | U(p[1]) << 8 | U(p[2]) << 16 | U(p[3]) << 24; }
inline int Signed(const Byte *p) { return int(Load(p)); }
inline void Put(Byte *p, U v)
{
    p[0] = Byte(v);
    p[1] = Byte(v >> 8);
    p[2] = Byte(v >> 16);
    p[3] = Byte(v >> 24);
}
// The capability bytes the checks read (SYSTEM_POWER_CAPABILITIES).
enum : U
{
    SystemS1 = 3,
    SystemS2 = 4,
    SystemS3 = 5,
    SystemS4 = 6,
    SystemS5 = 7,
    HiberFilePresent = 8,
    ThermalControl = 0xd,
    ProcessorThrottle = 0xe,
    ProcessorMinThrottle = 0xf,
    ProcessorThrottleScale = 0x10,
};
//  the default policy.
inline void Default(Byte *p)
{
    memset(p, 0, PolicyBytes);
    Put(p + 0x00, 1);    // Revision
    Put(p + 0x04, 6);    // PowerButton: PowerActionShutdownOff
    Put(p + 0x10, 2);    // SleepButton: PowerActionSleep
    Put(p + 0x28, 1);    // LidOpenWake: PowerSystemWorking
    Put(p + 0x44, 2);    // MinSleep: S1
    Put(p + 0x48, 4);    // MaxSleep: S3
    Put(p + 0x4c, 2);    // ReducedLatencySleep: S1
    Put(p + 0x5c, 0x19); // BroadcastCapacityResolution
    p[0xd9] = 100;       // FanThrottleTolerance
    p[0xda] = 100;       // ForcedThrottle
    for (U i = 0; i < 4; ++i)
        Put(p + 0x74 + i * 0x18, 2); // DischargePolicy[i].MinSystemState: S1
}
//  a POWER_ACTION_POLICY. The action falls back until the machine
// can do it; an action outside the table is STATUS_INVALID_PARAMETER.
inline U Action(const Machine &m, Byte *p)
{
    const Byte *c = m.capabilities;
    U flags = Load(p + 4);
    if (flags & 0x80000000u)
        Put(p + 4, (flags & ~3u) | 4u);
    const bool hibernate = c[SystemS4] && c[HiberFilePresent];
    const U sleeps = (c[SystemS1] ? 1 : 0) + (c[SystemS2] ? 1 : 0) + (c[SystemS3] ? 1 : 0) + (hibernate ? 1 : 0);
    for (;;)
    {
        const int action = Signed(p);
        int next = action;
        if (action == 0 || action == 4 || action == 5)
            ;
        else if (action == 1)
            next = 2;
        else if (action == 2)
            next = sleeps < 1 ? 0 : 2;
        else if (action == 3)
            next = hibernate ? 3 : 2;
        else if (action == 6)
            next = c[SystemS5] ? 6 : 4;
        else
            return StatusInvalidParameter;
        Put(p, U(next));
        if (next == action)
            return 0;
    }
}
//  a SYSTEM_POWER_STATE folds onto a sleep state the machine has.
inline U State(const Machine &m, Byte *p)
{
    const Byte *c = m.capabilities;
    int s = Signed(p);
    if (s == 0 || s >= 7 || s == 6)
        return StatusInvalidParameter;
    if (s == 1)
        return 0;
    const bool hibernate = c[SystemS4] && c[HiberFilePresent];
    if (s == 5 && !hibernate)
        s = 4;
    if (s == 4 && !c[SystemS3])
        s = 3;
    if (s == 3 && !c[SystemS2])
        s = 2;
    if (s == 2 && !c[SystemS1])
    {
        s = c[SystemS2] ? 3 : 4;
        if (s == 4 && !c[SystemS3])
            s = 5;
        if (s == 5 && !hibernate)
            s = 1;
    }
    Put(p, U(s));
    return 0;
}
//  a throttle percentage, at most 100, at least the given floor and
// the processor's minimum, rounded to the processor's steps.
inline void Throttle(const Machine &m, Byte *p, U floor)
{
    U v = *p;
    if (v > 100)
        v = 100;
    if (v < floor)
        v = floor;
    if (v < m.capabilities[ProcessorMinThrottle])
        v = m.capabilities[ProcessorMinThrottle];
    if (const U scale = m.capabilities[ProcessorThrottleScale])
    {
        U steps = ((50 / scale + v + 1) * scale - 1) / 100;
        v = (steps * 100 + (scale >> 1)) / scale;
    }
    *p = Byte(v);
}
//  the checked form of a policy for mains (ac) or battery.
inline U Check(const Machine &m, bool ac, const Byte *input, Byte *p)
{
    memcpy(p, input, PolicyBytes);
    if (Load(p) != 1)
        return StatusInvalidParameter;
    const Byte *a = m.admin;
    if (Signed(p + 0x44) < Signed(a + 0))
        Put(p + 0x44, Load(a + 0));
    if (Signed(p + 0x48) > Signed(a + 4))
        Put(p + 0x48, Load(a + 4));
    if (Load(p + 0xc0) < Load(a + 8))
        Put(p + 0xc0, Load(a + 8));
    if (Load(p + 0xc0) > Load(a + 0xc))
        Put(p + 0xc0, Load(a + 0xc));
    if (Load(p + 0xd4) < Load(a + 0x10))
        Put(p + 0xd4, Load(a + 0x10));
    if (Load(p + 0xd4) > Load(a + 0x14))
        Put(p + 0xd4, Load(a + 0x14));
    static const U actions[] = {0x04, 0x10, 0x1c, 0x30}, states[] = {0x28, 0x44, 0x48, 0x4c};
    for (U offset : actions)
        if (U status = Action(m, p + offset))
            return status;
    for (U offset : states)
        if (U status = State(m, p + offset))
            return status;
    for (U i = 0; i < 4; ++i)
    {
        Byte *discharge = p + 0x60 + i * 0x18;
        if (!discharge[0])
            continue;
        if (U status = Action(m, discharge + 8))
            return status;
        if (U status = State(m, discharge + 0x14))
            return status;
        if (Load(discharge + 4) > 100)
            Put(discharge + 4, 100);
    }
    if (U status = Action(m, p + 0xdc))
        return status;
    if (!m.capabilities[ProcessorThrottle])
    {
        p[0xd8] = 0;
        p[0xd9] = 100;
        p[0xda] = 100;
    }
    if (!m.capabilities[ThermalControl])
        p[0xd9] = 100;
    // The debug switches are clear on a normal start, so a mains
    // policy never forces a throttle.
    if (ac)
        p[0xda] = 100;
    if (!Load(p + 0x5c))
        Put(p + 0x5c, 100);
    if (!Load(p + 0x30))
        Put(p + 0x3c, 0);
    if (Load(p + 0x3c) && Load(p + 0x3c) < 60)
        Put(p + 0x3c, 60);
    if (p[0x40] > 90)
        p[0x40] = 90;
    if (Signed(p + 0x48) < Signed(p + 0x44))
        Put(p + 0x48, Load(p + 0x44));
    if (Signed(p + 0x4c) > Signed(p + 0x44))
        Put(p + 0x4c, Load(p + 0x44));
    Throttle(m, p + 0xd9, 20);
    Throttle(m, p + 0xdb, 20);
    Throttle(m, p + 0xda, p[0xdb]);
    if (p[0xd9] != 100 || p[0xda] != 100)
        p[0xd8] = 1;
    return 0;
}
//  take a policy. save is what level 0 and 1 pass; the start and
// the reload after a capability change pass zero.
inline U Take(Machine &m, bool save, bool ac, const Byte *input, U bytes)
{
    if (bytes < PolicyBytes)
        return StatusBufferTooSmall;
    if (bytes > PolicyBytes)
        return StatusBufferOverflow;
    Byte checked[PolicyBytes];
    if (U status = Check(m, ac, input, checked))
        return status;
    Byte *current = ac ? m.ac : m.dc;
    if (!memcmp(checked, current, PolicyBytes))
        return 0;
    memcpy(current, checked, PolicyBytes);
    if (save)
    {
        memcpy(ac ? m.saved_ac : m.saved_dc, input, PolicyBytes);
        (ac ? m.has_saved_ac : m.has_saved_dc) = true;
    }
    return 0;
}
//  apply the registry values again, the default where there is none.
// A value NTKERN refuses leaves the stored policy as it is.
inline void Reload(Machine &m)
{
    Byte fallback[PolicyBytes];
    for (int pass = 0; pass < 2; ++pass)
    {
        const bool ac = pass == 0;
        const Byte *source = ac ? (m.has_saved_ac ? m.saved_ac : nullptr) : (m.has_saved_dc ? m.saved_dc : nullptr);
        if (!source)
        {
            Default(fallback);
            source = fallback;
        }
        Take(m, false, ac, source, PolicyBytes);
    }
}
//  the administrator limits.
inline U TakeAdmin(Machine &m, bool save, const Byte *input, U bytes)
{
    if (bytes < AdminBytes)
        return StatusBufferTooSmall;
    if (bytes > AdminBytes)
        return StatusBufferOverflow;
    const int min_sleep = Signed(input), max_sleep = Signed(input + 4);
    if (min_sleep < 2 || min_sleep > 5 || max_sleep > 5 || min_sleep > max_sleep ||
        Load(input + 8) > Load(input + 0xc) || Load(input + 0x10) > Load(input + 0x14))
        return StatusInvalidParameter;
    if (!memcmp(input, m.admin, AdminBytes))
        return 0;
    memcpy(m.admin, input, AdminBytes);
    if (save)
    {
        memcpy(m.saved_admin, input, AdminBytes);
        m.has_saved_admin = true;
    }
    return 0;
}
//  and following, then : the state after NTKERN started.
inline void Start(Machine &m)
{
    if (m.ready)
        return;
    m.ready = true;
    Default(m.ac);
    Default(m.dc);
    Put(m.admin + 0, 2);
    Put(m.admin + 4, 5);
    Put(m.admin + 8, 0);
    Put(m.admin + 0xc, 0xffffffffu);
    Put(m.admin + 0x10, 0);
    Put(m.admin + 0x14, 0xffffffffu);
    Reload(m);
}
} // namespace Win98PowerPolicy
