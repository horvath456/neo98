// SPDX-License-Identifier: GPL-2.0-or-later
// The WINMM entries that answer without a device: the device counts and
// capabilities of wave, MIDI, mixer, aux and joystick, the volume pairs, the
// three error texts, the installable driver entries and the contracts of the
// unsupported device operations.
// Windows 98 keeps the whole device layer in the 16-bit MMSYSTEM.DLL and in
// the VxDs below it. WINMM itself is the argument check in front of a flat
// thunk or a DeviceIoControl, and that check is what this file reproduces.
// Three shapes repeat across the family:
// Device counts: waveInGetNumDevs and its six siblings call the
// corresponding device thunk. Without an
// installed driver the 16-bit side counts zero devices.
// handle waveOutGetVolume, midiOutSetVolume and
// mixerGetID share one test: a value whose high word is
// zero, or 0FFFFFFFFh, is a device identifier and goes straight
// on; anything else is probed with IsBadWritePtr for 4Ch bytes,
// must carry the magic 12345678h at +4 and a device type at +0Ch
// that the family accepts, and only then is [handle] the device
// identifier. A value that fails any of the three is
// MMSYSERR_INVALHANDLE. No device can be opened here, so no such
// record exists and every handle takes that answer.
// text mciGetErrorStringA, waveOutGetErrorTextA and
// midiOutGetErrorTextA probe the buffer, answer
// MMSYSERR_INVALPARAM for a bad one and otherwise read the
// string whose resource identifier is the error number out of
// MMSYSTEM.DLL.
//
#include "x86_app.h"
#include "host_devices.h"
#ifdef NEO98_RUNTIME_CONTENT
#include "windows_runtime_locale.h"
#else
#include "../build/winmm-message/winmm_messages.h"
#endif
#include <stdio.h>
#include <string.h>
namespace X86
{
namespace
{
// MMSYSERR_NOERROR 0, MMSYSERR_BADDEVICEID 2, MMSYSERR_INVALHANDLE 5,
// MMSYSERR_NODRIVER 6, MMSYSERR_NOTSUPPORTED 8, MMSYSERR_BADERRNUM 9,
// MMSYSERR_INVALFLAG 10, MMSYSERR_INVALPARAM 11.
constexpr unsigned BadDeviceId = 2, InvalidHandle = 5, NoDriver = 6;
constexpr unsigned BadErrorNumber = 9, InvalidParameter = 11;
constexpr unsigned WaveMapper = 0xffffffff;
// The four error spaces of the MMSYSTEM string table. An entry names the last
// identifier of its block, and the three text entries each accept the common
// MMSYSERR block plus their own.
constexpr unsigned SystemLast = 20, WaveFirst = 32, WaveLast = 35;
constexpr unsigned MidiFirst = 64, MidiLast = 71, MciFirst = 256;
const char *Message(unsigned identifier)
{
    for (const auto &entry : OriginalMultimediaMessages)
        if (entry.id == identifier) return entry.text;
    return nullptr;
}
} // namespace
// The shared text worker. The original truncates at the buffer and always
// terminates, and an empty buffer is the caller's problem, not an error.
unsigned Application::MultimediaErrorText(unsigned identifier, unsigned buffer, unsigned bytes)
{
    if (!bytes || !virtual_memory.Access(buffer, bytes, true)) return InvalidParameter;
    const char *text = Message(identifier);
    if (!text) return BadErrorNumber;
    unsigned length = (unsigned)strlen(text);
    if (length >= bytes) length = bytes - 1;
    char terminator = 0;
    if (!Store(buffer, text, length) || !Store(buffer + length, &terminator, 1))
        return InvalidParameter;
    return 0;
}
// Handle validation. `types` is the set of device type bits the
// family accepts at +0Ch. A value whose high word is zero, and the wave
// mapper, are device identifiers and pass straight through; everything else
// has to be one of our records. False is MMSYSERR_INVALHANDLE.
bool Application::MultimediaDeviceOfHandle(unsigned handle, unsigned types, unsigned &device)
{
    device = handle;
    if (!(handle >> 16) || handle == WaveMapper) return true;
    if (!virtual_memory.Access(handle, 0x4c, true)) return false;
    if (Read32(handle + 4) != 0x12345678) return false;
    if (Read32(handle + 0xc) & ~types) return false;
    device = Read32(handle);
    return true;
}
// The joystick configuration of the host's controllers, the way the Game
// Controllers panel stores it after "Add": a JOYREGHWCONFIG per identifier
// under MediaResources\Joystick\msjstick.drv\CurrentJoystickSettings and an
// OEM type under PrivateProperties\Joystick\OEM. DirectInput finds joysticks
// only through it (DINPUT.DLL 4.08, JoyReg). A slot whose type is not one of
// ours belongs to the user and stays as it is.
void Application::JoystickRegistry(const AH_Status &status)
{
    unsigned anchor = 0;
    if (registry.Key(0x80000002u, anchor)) return;
    static const char *const settings =
        "System\\CurrentControlSet\\Control\\MediaResources\\Joystick\\msjstick.drv\\CurrentJoystickSettings";
    unsigned key = 0;
    bool created = false;
    if (registry.store.Open(anchor, settings, true, key, created)) return;
    for (unsigned i = 0; i < status.joysticks && i < AH_MAX_JOYS; ++i)
    {
        const auto &joy = status.joystick[i];
        char value[64], type[64];
        snprintf(type, sizeof type, "NEO98 Controller %u", i + 1);
        snprintf(value, sizeof value, "Joystick%uOEMName", i + 1);
        const Registry::Value *existing = nullptr;
        if (!registry.store.Query(key, value, existing) && existing &&
            (existing->size < 6 || memcmp(existing->data, "NEO98 ", 6)))
            continue;
        // JOYREGHWCONFIG: hws {dwFlags, dwNumButtons}, dwUsageSettings, the
        // hardware range (minimum, maximum and centre of six axes), four POV
        // values, dwCalFlags, dwType, dwReserved.
        unsigned config[28] = {};
        const unsigned axes = joy.axes < 6 ? joy.axes : 6;
        config[0] = (axes >= 3 ? 0x1u : 0) | (joy.pov != ~0u ? 0x2u : 0) | (axes >= 4 ? 0x80000u : 0) |
                    (axes >= 5 ? 0x800000u : 0) | (axes >= 6 ? 0x1000000u : 0);
        config[1] = joy.buttons < 32 ? joy.buttons : 32;
        config[2] = 0x2 | 0x4; // JOY_US_PRESENT | JOY_US_ISOEM
        for (unsigned axis = 0; axis < 6; ++axis) { config[3 + axis] = 0; config[9 + axis] = 65535; config[15 + axis] = 32767; }
        config[21] = 0; config[22] = 9000; config[23] = 18000; config[24] = 27000;
        config[25] = 0x3f; // JOY_ISCAL_XY | Z | R | U | V | POV
        config[26] = 1;    // JOY_HW_CUSTOM
        snprintf(value, sizeof value, "Joystick%uConfiguration", i + 1);
        if (registry.store.Query(key, value, existing) || !existing || existing->size != sizeof config ||
            memcmp(existing->data, config, sizeof config))
            registry.store.Set(key, value, 3, config, sizeof config);
        snprintf(value, sizeof value, "Joystick%uOEMName", i + 1);
        registry.store.Set(key, value, 1, type, unsigned(strlen(type)) + 1);
        char path[200];
        snprintf(path, sizeof path, "System\\CurrentControlSet\\Control\\MediaProperties\\PrivateProperties\\Joystick\\OEM\\%s", type);
        unsigned oem = 0;
        if (registry.store.Open(anchor, path, true, oem, created)) continue;
        const char *name = joy.name[0] ? joy.name : "Game Controller";
        registry.store.Set(oem, "OEMName", 1, name, unsigned(strnlen(name, sizeof joy.name - 1)) + 1);
        registry.store.Set(oem, "OEMData", 3, config, 8);
        if (joy.flags & AH_JOY_RUMBLE) JoystickForceFeedback(anchor, oem);
    }
}

// A controller with rumble gets NEO98FF.DLL as its force feedback driver
// (tools/host_devices/ffdriver.c): OEMForceFeedback names the class and the
// effects DINPUT may create, each with its DIEFFECTATTRIBUTES; the class is
// an in-process server in the system directory.
void Application::JoystickForceFeedback(unsigned machine, unsigned oem)
{
    static const char clsid[] = "{A9809800-4652-4F42-9A41-524D39384646}";
    unsigned key = 0, effect = 0;
    bool created = false;
    if (registry.store.Open(oem, "OEMForceFeedback", true, key, created)) return;
    registry.store.Set(key, "CLSID", 1, clsid, sizeof clsid);
    // X and Y act as the actuators: OEM\\<type>\\Axes\\<n> holds the
    // DIOBJECTATTRIBUTES of an axis, DIDOI_FFACTUATOR and the HID usage of
    // the Generic Desktop page.
    for (unsigned axis = 0; axis < 2; ++axis)
    {
        char path[16];
        snprintf(path, sizeof path, "Axes\\%u", axis);
        unsigned node = 0;
        if (registry.store.Open(oem, path, true, node, created)) continue;
        const unsigned attributes[2] = {1, 1u | (0x30u + axis) << 16};
        registry.store.Set(node, "Attributes", 3, attributes, sizeof attributes);
    }
    // DIFFDEVICEATTRIBUTES: dwFlags, dwFFSamplePeriod, dwFFMinTimeResolution.
    const unsigned device[3] = {0, 10000, 10000};
    registry.store.Set(key, "Attributes", 3, device, sizeof device);
    // dwEffectId, dwEffType, dwStaticParams, dwDynamicParams, dwCoords: the
    // identifier NEO98FF.DLL knows, DIEFT_*, DIEP_DURATION | GAIN | AXES |
    // DIRECTION | TYPESPECIFICPARAMS, Cartesian, polar and spherical.
    static const struct { const char *guid, *name; unsigned id, type; } effects[] = {
        {"{13541C20-8E33-11D0-9AD0-00A0C9A06E35}", "Constant Force", 1, 1},
        {"{13541C21-8E33-11D0-9AD0-00A0C9A06E35}", "Ramp Force", 2, 2},
        {"{13541C22-8E33-11D0-9AD0-00A0C9A06E35}", "Square Wave", 3, 3},
        {"{13541C23-8E33-11D0-9AD0-00A0C9A06E35}", "Sine Wave", 4, 3},
        {"{13541C24-8E33-11D0-9AD0-00A0C9A06E35}", "Triangle Wave", 5, 3},
        {"{13541C25-8E33-11D0-9AD0-00A0C9A06E35}", "Sawtooth Up Wave", 6, 3},
        {"{13541C26-8E33-11D0-9AD0-00A0C9A06E35}", "Sawtooth Down Wave", 7, 3},
    };
    for (const auto &e : effects)
    {
        char path[80];
        snprintf(path, sizeof path, "Effects\\%s", e.guid);
        if (registry.store.Open(key, path, true, effect, created)) continue;
        registry.store.Set(effect, "", 1, e.name, unsigned(strlen(e.name)) + 1);
        const unsigned attributes[5] = {e.id, e.type, 0x165, 0x165, 0x70};
        registry.store.Set(effect, "Attributes", 3, attributes, sizeof attributes);
    }
    unsigned classes = 0, server = 0;
    if (registry.Key(0x80000000u, classes)) return;
    char path[80];
    snprintf(path, sizeof path, "CLSID\\%s", clsid);
    if (registry.store.Open(classes, path, true, key, created)) return;
    registry.store.Set(key, "", 1, "NEO98 Force Feedback", 21);
    if (registry.store.Open(key, "InprocServer32", true, server, created)) return;
    static const char file[] = "C:\\WINDOWS\\SYSTEM\\NEO98FF.DLL";
    registry.store.Set(server, "", 1, file, sizeof file);
    registry.store.Set(server, "ThreadingModel", 1, "Both", 5);
    (void)machine;
}

unsigned Application::MultimediaDeviceApi(ApiName name, const unsigned *a)
{
#define API(n) case API_ID(#n):
    switch (name.id)
    {
    // The counts that are still zero. Recording has its own entries in
    // src/x86_wave_in.cpp and the mixer in src/x86_mixer.cpp.
    API(midiInGetNumDevs)
    API(auxGetNumDevs)
        return 0;
    API(joyGetNumDevs)
    {
        AH_Status status{};
        if (!HostDevices::current || HostDevices::current->Status(status)) return 0;
        JoystickRegistry(status);
        return status.joysticks;
    }
    // The capability queries.  and its siblings answer a size of zero
    // with success before anything else, then put the first argument through
    // the handle test, then probe the buffer, and only then ask the device.
    // With zero devices every identifier is out of range, and the mapper has
    // no driver to ask.
    API(midiInGetDevCapsA)
    {
        if (!a[2]) return 0;
        //  and its siblings all accept every device type here.
        unsigned device = 0;
        if (!MultimediaDeviceOfHandle(a[0], 0x77, device)) return InvalidHandle;
        if (!a[1] || !virtual_memory.Access(a[1], a[2], true)) return InvalidParameter;
        return device == WaveMapper ? NoDriver : BadDeviceId;
    }
    //  has no handle test; the aux entries only take device numbers.
    API(auxGetDevCapsA)
    {
        if (!a[2]) return 0;
        if (!a[1] || !virtual_memory.Access(a[1], a[2], true)) return InvalidParameter;
        return a[0] == WaveMapper ? NoDriver : BadDeviceId;
    }
    // A zero size answers zero, a bad buffer 11, and the capability
    // block comes from the joystick driver, which is not there.
    API(joyGetDevCapsA)
    {
        if (!a[2]) return 0;
        if (!a[1] || !virtual_memory.Access(a[1], a[2], true)) return InvalidParameter;
        if (!HostDevices::current) return NoDriver;
        AH_Status status{}; if (HostDevices::current->Status(status)) return NoDriver;
        unsigned char caps[404]={};
        // The identifier -1 asks the driver alone: VJOYD answers only
        // szRegKey, which DirectInput needs for the global joystick
        // settings under MediaResources\Joystick.
        JoystickRegistry(status);
        if (a[0] == ~0u)
        {
            strncpy((char*)caps+112,"msjstick.drv",31);
            return Store(a[1],caps,a[2]<sizeof caps?a[2]:sizeof caps)?0:InvalidParameter;
        }
        if (a[0] >= status.joysticks) return 167;
        const auto &joy=status.joystick[a[0]];
        caps[0]=0x98; caps[1]=0x41; caps[2]=1;
        strncpy((char*)caps+4,joy.name,31);
        auto put=[&](unsigned at,unsigned value){memcpy(caps+at,&value,4);};
        put(40,65535);put(48,65535);put(56,65535);put(60,joy.buttons);put(64,10);put(68,1000);
        put(76,65535);put(84,65535);put(92,65535);
        put(96,(joy.axes>=3?1:0)|(joy.axes>=4?2:0)|(joy.axes>=5?4:0)|(joy.axes>=6?8:0)|(joy.pov!=~0u?16|32:0));
        put(100,6);put(104,joy.axes);put(108,32);
        // szRegKey: the key under MediaResources\Joystick that holds the
        // configuration DirectInput and the Game Controllers panel read.
        strncpy((char*)caps+112,"msjstick.drv",31);
        return Store(a[1],caps,a[2]<sizeof caps?a[2]:sizeof caps)?0:InvalidParameter;
    }
    //  and probe the structure and then reach VJOYD. Without
    // the driver DeviceIoControl fails and the original answers 6.
    API(joyGetPos)
    {
        if (!a[1] || !virtual_memory.Access(a[1], 0x10, true)) return InvalidParameter;
        if (!HostDevices::current) return NoDriver;
        AH_Status status{}; if (HostDevices::current->Status(status)) return NoDriver;
        if (a[0]>=status.joysticks) return 167;
        const auto &joy=status.joystick[a[0]];
        const unsigned data[]={joy.axis[0],joy.axis[1],joy.axis[2],joy.pressed};
        return Store(a[1],data,sizeof data)?0:InvalidParameter;
    }
    API(joyGetPosEx)
    {
        if (!a[1] || !virtual_memory.Access(a[1], 0x34, true)) return InvalidParameter;
        if (Read32(a[1]) < 0x34) return InvalidParameter;
        if (!HostDevices::current) { Write32(a[1]+0x2c,a[0]); return NoDriver; }
        AH_Status status{};if(HostDevices::current->Status(status))return NoDriver;
        if(a[0]>=status.joysticks)return 167;
        const auto &joy=status.joystick[a[0]];const unsigned flags=Read32(a[1]+4);
        for(unsigned axis=0;axis<6;++axis)if(flags&(1u<<axis))Write32(a[1]+8+axis*4,joy.axis[axis]);
        if(flags&0x80){Write32(a[1]+32,joy.pressed);unsigned first=0;while(first<32&&!(joy.pressed&(1u<<first)))++first;Write32(a[1]+36,first<32?first+1:0);}
        if(flags&(0x40|0x200))Write32(a[1]+40,joy.pov==~0u?65535:joy.pov);
        return 0;
    }
    // The three error texts. mciGetErrorStringA answers TRUE or FALSE, the
    // other two answer an MMSYSERR value, and each one only reaches its own
    // block of the MMSYSTEM string table plus the common one.
    API(mciGetErrorStringA)
    {
        if (!a[2] || !virtual_memory.Access(a[1], a[2], true)) return 0;
        return a[0] >= MciFirst && MultimediaErrorText(a[0], a[1], a[2]) == 0 ? 1 : 0;
    }
    // The input and output variants of a device family share their
    // implementation and their block of the MMSYSTEM string
    // table, because WAVERR and MIDIERR have one range each.
    API(waveOutGetErrorTextA) API(waveInGetErrorTextA)
    {
        if (a[0] > SystemLast && (a[0] < WaveFirst || a[0] > WaveLast)) return BadErrorNumber;
        return MultimediaErrorText(a[0], a[1], a[2]);
    }
    API(midiOutGetErrorTextA) API(midiInGetErrorTextA)
    {
        if (a[0] > SystemLast && (a[0] < MidiFirst || a[0] > MidiLast)) return BadErrorNumber;
        return MultimediaErrorText(a[0], a[1], a[2]);
    }
    default:
        break;
    }
    Fail(name);
    return 0;
#undef API
}
// The parts PLAN.md holds back for the sound system and for DirectX. Every
// entry here answers what the original answers on a machine without the
// device or the driver it needs. None of them pretends to succeed.
unsigned Application::MultimediaDeferredApi(ApiName name, const unsigned *a)
{
#define API(n) case API_ID(#n):
    switch (name.id)
    {
    //  and open through the same worker as waveOutOpen. The
    // mapper has no driver, every other identifier is out of range.
    API(midiInOpen)
    {
        if (!a[0] || !virtual_memory.Access(a[0], 4, true)) return InvalidParameter;
        return a[1] == WaveMapper ? NoDriver : BadDeviceId;
    }
    // Everything that needs an open device. No device can be opened, so the
    // handle is never one of ours and the answer is MMSYSERR_INVALHANDLE,
    // which is what the originals answer for a value that fails the 4Ch byte
    // probe and the magic at +4.
    API(midiInClose) API(midiInStart) API(midiInStop) API(midiInReset)
        return InvalidHandle;
    //  probes the command and the answer buffer and then reaches the
    // MCI command table. No MCI device is installed, so the device the
    // command names cannot be found: MCIERR_INVALID_DEVICE_NAME.
    API(mciSendStringA)
    {
        if (!a[0] || !virtual_memory.Access(a[0], 1, false)) return InvalidParameter;
        if (a[1] && a[2] && !virtual_memory.Access(a[1], a[2], true)) return InvalidParameter;
        const char terminator = 0;
        if (a[1] && a[2]) Store(a[1], &terminator, 1);
        return 287;
    }
    //  and are the installable driver loader. No multimedia
    // driver is registered, so OpenDriver finds nothing and answers zero, and
    // both entries that take a driver handle reject the zero handle:
    // and leave through the failure path.
    //  and are the same worker with the flag that tells the
    // wide form from the ANSI one, and the linker folded DrvOpen onto OpenDriver
    // and DrvOpenA onto OpenDriverA, DrvClose onto CloseDriver and
    // DrvSendMessage onto SendDriverMessage. All of them see the same empty
    // driver list.
    API(OpenDriver) API(OpenDriverA) API(DrvOpen) API(DrvOpenA)
        return 0;
    API(CloseDriver) API(SendDriverMessage) API(DrvClose) API(DrvSendMessage)
        return 0;
        // also exported as DrvDefDriverProc. The default answer of an
    // installable driver, and the one entry of this family that needs no driver
    // at all: it looks only at the message. DRV_LOAD (1), DRV_ENABLE (2),
    // DRV_CLOSE (4), DRV_DISABLE (5), DRV_FREE (6), DRV_INSTALL (9) and
    // DRV_REMOVE (10) answer one, everything else zero, DRV_OPEN (3),
    // DRV_CONFIGURE (7) and DRV_QUERYCONFIGURE (8) included.
    API(DefDriverProc) API(DrvDefDriverProc)
    {
        const unsigned message = a[2];
        const bool handled = message == 1 || message == 2 || message == 4 || message == 5 ||
                             message == 6 || message == 9 || message == 10;
        return handled ? 1u : 0u;
    }
    //  (also DrvGetModuleHandle) and walk the driver table
    // under the multimedia lock: a zero handle and a handle above the count
    // leave with the register they cleared first, so both answer zero while no
    // driver is installed. The video codecs of the tree ask for the module
    // handle to find their own resources and take the failure path when it is
    // zero.
    API(GetDriverModuleHandle) API(DrvGetModuleHandle) API(GetDriverFlags)
        return 0;
    default:
        break;
    }
    Fail(name);
    return 0;
#undef API
}
} // namespace X86
