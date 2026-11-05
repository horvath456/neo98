// SPDX-License-Identifier: GPL-2.0-or-later
// Windows 98 NTDLL invokes NT services through interrupt 2Eh. EAX contains
// the service number and EDX points to stack arguments; the wrapper removes
// the arguments on return. Native providers implement the supported VxD
// services.
#pragma once
namespace Win98NtServices
{
using U = unsigned;
// The NTSTATUS a service that this port does not answer returns:
// STATUS_NOT_IMPLEMENTED. POWRPROF treats every value other than zero as a
// failure and returns FALSE, preserving its normal error path.
constexpr U NotImplemented = 0xc0000002u;
struct Service
{
    U number;
    U argument_bytes; // what the stub's own `ret` removes
    const char *name;
};
// Supported NT service numbers and stack argument sizes.
// Service 194h has no export name, so it is named by its number.
constexpr Service Services[] = {
    {0x194, 0x0c, "NT service 194h"},
    {0x195, 0x04, "IoUnregisterDeviceInterface"},
    {0x107, 0x10, "NtInitiatePowerAction"},
    {0x108, 0x14, "NtPowerInformation"},
    {0x109, 0x04, "NtRequestWakeupLatency"},
    {0x10b, 0x0c, "NtSetSystemPowerState"},
    {0x196, 0x08, "NtGetDevnodeFromFileHandle"},
};
constexpr unsigned ServiceCount = sizeof Services / sizeof Services[0];

// NtPowerInformation dispatches by information level. Level 4,
// SystemPowerCapabilities, accepts an input record of exactly 4Ch bytes;
// any other length raises STATUS_INVALID_PARAMETER. It sets FullWake and
// returns the capability record. An output buffer must fit the whole record;
// otherwise the service returns STATUS_BUFFER_TOO_SMALL.
// Nothing else fills the record on a machine without APM or ACPI:
// the button, lid and sleep state bytes come from a power driver (VPOWERD or
// ACPI) or a debug switch, and HiberFilePresent (byte 8) from a registry
// value this tree does not have. So the Win98 answer here is all zero with
// FullWake one.
constexpr U SystemPowerCapabilities = 4;
constexpr U PowerCapabilitiesBytes = 0x4c;
constexpr U FullWakeByte = 9;
constexpr U StatusInvalidParameter = 0xc000000du;
constexpr U StatusBufferTooSmall = 0xc0000023u;
constexpr U StatusAccessViolation = 0xc0000005u;
inline const Service *Find(U number)
{
    for (unsigned i = 0; i < ServiceCount; ++i)
        if (Services[i].number == number)
            return &Services[i];
    return nullptr;
}
} // namespace Win98NtServices
