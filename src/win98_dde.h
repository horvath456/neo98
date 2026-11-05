// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// DDEML instance handles combine a record token with a generation counter;
// every call validates both. USER32 marshals Win32 arguments to the Win16
// service. String handles use the shared global-atom table for creation,
// lookup, deletion, and name queries.
#include <string.h>
namespace Win98Dde
{
using U = unsigned;
// DMLERR_* as the original stores and returns them.
enum : U
{
    kNoError = 0,
    kAdvacktimeout = 0x4000,
    kBusy = 0x4001,
    kDataacktimeout = 0x4002,
    kDllNotInitialized = 0x4003,
    kDllUsage = 0x4004,
    kExecacktimeout = 0x4005,
    kInvalidParameter = 0x4006,
    kLowMemory = 0x4007,
    kMemoryError = 0x4008,
    kNotProcessed = 0x4009,
    kNoConvEstablished = 0x400a,
    kPokeacktimeout = 0x400b,
    kPostmsgFailed = 0x400c,
    kReentrancy = 0x400d,
    kServerDied = 0x400e,
    kSysError = 0x400f,
    kUnadvacktimeout = 0x4010,
    kUnfoundQueueId = 0x4011,
};
// afCmd bits the original tests by name.
enum : U
{
    kAppclassMonitor = 0x0001,
    kAppcmdClientonly = 0x0010,
    kAppcmdFilterinits = 0x0020,
    kCbfFailSelfconnections = 0x1000,
    kCbfFailConnections = 0x2000,
    kCbfFailAdvises = 0x4000,
    kCbfFailExecutes = 0x8000,
    kCbfFailPokes = 0x10000,
    kCbfFailRequests = 0x20000,
    kCbfSkipConnectConfirms = 0x40000,
    kCbfSkipRegistrations = 0x80000,
    kCbfSkipUnregistrations = 0x100000,
    kCbfSkipDisconnects = 0x200000,
};
// An APPCLASS_MONITOR instance is given every
// CBF_FAIL_* plus SKIP_CONNECT_CONFIRMS and SKIP_DISCONNECTS, but keeps the
// registration notifications a monitor exists for.
constexpr U kMonitorFlags = 0x27f000;
// Merge flags when re-initializing an instance.
// Bits inside the mask keep their old value, bits outside take the new one, so
// a second DdeInitialize can only change the callback filter flags.
constexpr U kReinitKeep = 0xc00fff;
// XTYP_* the ported transactions use: the type number in the low byte, the
// class in the high nibble (XCLASS_BOOL 1000h, XCLASS_DATA 2000h,
// XCLASS_FLAGS 4000h, XCLASS_NOTIFICATION 8000h) and XTYPF_NOBLOCK 2. The
// notifications are all XCLASS_NOTIFICATION; SHDOC401's callback
// switches on exactly these values, and a CONNECT_CONFIRM of the wrong class
// never reached its conversation list, so every execute was refused.
enum : U
{
    kXtypConnect = 0x1062,
    kXtypConnectConfirm = 0x8072,
    kXtypExecute = 0x4050,
    kXtypDisconnect = 0x80c2,
    kXtypRegister = 0x80a2,
    kXtypUnregister = 0x80d2,
};
// WM_DDE_* as USER defines them; the original transport posts these.
enum : U
{
    kWmDdeInitiate = 0x03e0,
    kWmDdeTerminate = 0x03e1,
    kWmDdeAdvise = 0x03e2,
    kWmDdeUnadvise = 0x03e3,
    kWmDdeAck = 0x03e4,
    kWmDdeData = 0x03e5,
    kWmDdeRequest = 0x03e6,
    kWmDdePoke = 0x03e7,
    kWmDdeExecute = 0x03e8,
};
// DNS_* for DdeNameService.
enum : U
{
    kDnsRegister = 1,
    kDnsUnregister = 2,
    kDnsFilteron = 4,
    kDnsFilteroff = 8,
};
// DdeInitialize creates two window classes per instance. It creates
// "DMGClass" with style 0 for every instance and keeps it at record +22h;
// creates "DMGFrame" with WS_POPUP for an instance that is not
// APPCMD_CLIENTONLY and keeps it at +24h. The service-name broadcast sends the private messages 4c9h and 4c8h from the first
// window and carries the second as wParam, which is the handle a client sees
// in WM_DDE_ACK. The window names are empty strings.
constexpr const char *kInstanceClass = "DMGClass";
constexpr const char *kServerClass = "DMGFrame";
constexpr const char *kMonitorClass = "DMGMonitorClass";
// Windows 98 adds the atom "Progman" once per module; and Windows 98 read the
// shutdown timeouts from WIN.INI [DDEML], ShutdownTimeout default "3000" and
// ShutdownRetryTimeout default "30000", falling back to 3000 and 30000 when
// the profile string does not parse.
constexpr U kShutdownTimeout = 3000, kShutdownRetryTimeout = 30000;
// A code page is accepted when it is zero,
// CP_WINANSI (03ech) or the value KEYBOARD.132 GetKBCodePage returns. Any
// other value stores DMLERR_INVALIDPARAMETER and answers zero. NEO98 has no
// keyboard driver code page, so only the first two are accepted here.
constexpr U kCpWinansi = 0x3ec;
inline bool CodePage(U page) { return !page || page == kCpWinansi; }
// the fourth argument of DdeInitialize is reserved and must be zero.
inline bool Reserved(U reserved) { return reserved == 0; }
inline U Merge(U old_flags, U flags) { return ((old_flags ^ flags) & kReinitKeep) ^ flags; }
} // namespace Win98Dde
