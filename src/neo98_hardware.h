// SPDX-License-Identifier: GPL-2.0-or-later
// The names of the hardware this port is, in one place.
// The Win98 folder is installed once on another machine (README.md)
// and its registry names that machine's display adapter and CD-ROM drive.
// The devices a program meets here are the port's own: the display driver on
// the compositor's frame buffer, the CD-ROM drive of the host's disc images.
// The registry the host starts from (src/registry_hives.h, mirrored by
// tools/hardware_setup.py) and the calls that answer for a device
// (EnumDisplayDevices, src/x86_monitors.cpp) take their names from here, so
// the Device Manager, the display properties and DirectX name one device.
#pragma once
namespace Neo98Hardware
{
constexpr char Maker[] = "NEO98";
// OEMINFO.INI is read by the original System Properties control panel.
constexpr char OemInfo[] =
    "[General]\r\n"
    "Manufacturer=NEO98\r\n"
    "Model=ARM64 Mac\r\n"
    "[Support Information]\r\n"
    "Line1=NEO98\r\n"
    "Line2=Windows 98 programs on ARM64 Macs.\r\n"
    "Line3=Source code and bug reports on GitHub:\r\n"
    "Line4=See README.md for project information.\r\n";
// The display adapter. "*NEO9800" has the form of an EISA identifier, three
// letters and four hexadecimal digits, like the "*PNP0905" it replaces.
constexpr char DisplayName[] = "NEO98 Display Adapter";
constexpr char DisplayId[] = "*NEO9800";
constexpr char DisplayNode[] = "Enum\\Root\\*NEO9800\\0000";
constexpr char DisplayMode[] = "32,800,600";
// The files the page Driver names. The driver itself is part of the port;
// these are the files of Win98 it stands in for and the folder has: the
// frame buffer driver over the DIB engine and the virtual display devices.
constexpr char DisplayDriverFile[] = "framebuf.drv";
constexpr char DisplayVdd[] = "*vdd,*vflatd";
// What a mini driver writes under INFO of its driver key and the DirectX
// diagnosis shows: the pictures are drawn with Metal.
constexpr char DisplayChip[] = "NEO98 Metal";
constexpr char DisplayDac[] = "Internal";
constexpr char DisplayRevision[] = "1";
// The CD-ROM drive, as IOS names a SCSI or ATAPI device after its inquiry
// data: vendor (8), product (16) and the first character of the revision,
// blanks as underscores.
constexpr char CdName[] = "NEO98 CD-ROM";
constexpr char CdVendor[] = "NEO98   ";
constexpr char CdProduct[] = "CD-ROM          ";
constexpr char CdRevision[] = "1.0 ";
constexpr char CdId[] = "NEO98___CD-ROM__________1";
constexpr char CdDevice[] = "Enum\\SCSI\\NEO98___CD-ROM__________1";
constexpr char CdHardwareId[] = "NEO98___CD-ROM__________1,GenCD,SCSI\\NEO98___CD-ROM__________1";
// The BIOS the system board reports.
constexpr char BiosVersion[] = "NEO98";
// The sound card: one device with the parts WINMM, the mixer and DirectSound
// name (src/x86_wave_out.cpp, src/x86_midi_out.cpp, src/win98_mixer.h,
// src/x86_dsound_vxd.cpp).
constexpr char SoundName[] = "NEO98 Sound Card";
constexpr char SoundId[] = "*NEO9810";
constexpr char SoundNode[] = "Enum\\Root\\*NEO9810\\0000";
constexpr char SoundDriver[] = "MEDIA\\0000";
constexpr char WaveOutName[] = "NEO98 Wave Out";
constexpr char WaveInName[] = "NEO98 Wave In";
constexpr char MidiOutName[] = "NEO98 MIDI Synth";
constexpr char MixerName[] = "NEO98 Mixer";
// The device loader of a sound card of Win98, the file its page Driver names.
constexpr char SoundLoader[] = "mmdevldr.vxd";
// The parts of the card under Control\\MediaResources, where the Multimedia
// control panel finds the devices of a card: one key per kind, named after
// the driver and its instance. The driver is part of the port.
constexpr char MediaResources[] = "System\\CurrentControlSet\\control\\MediaResources";
constexpr char SoundResource[] = "neo98snd.drv<0000>";
constexpr char SoundResourceDriver[] = "neo98snd.drv";
// The network adapter. Its driver key is the one NETAMD.INF of the CD writes
// for the model *PNP828C, the compatible identifier here.
constexpr char NetName[] = "NEO98 Ethernet Adapter";
constexpr char NetId[] = "*NEO9820";
constexpr char NetCompatibleId[] = "*PNP828C";
// Its software key under Services\\Class (Net\\0000 is the Dial-Up Adapter).
// NDIS names an adapter after the last part of it (NDIS.VXD c0065d75..c0065e10:
// CONFIGMG_Get_DevNode_Key with CM_REGISTRY_SOFTWARE, then the text behind the
// last backslash), and the protocols get that name in their bind call.
constexpr char NetDriver[] = "Net\\0001";
// The monitor: the window or panel of the Mac, which shows every mode the
// display adapter offers (src/win98_display_modes.h).
constexpr char MonitorName[] = "NEO98 Monitor";
constexpr char MonitorId[] = "NEO9830";
constexpr char MonitorNode[] = "Enum\\MONITOR\\NEO9830\\ROOT&*NEO9800&0000";
constexpr char MonitorDriver[] = "Monitor\\0000";
constexpr char MonitorMaxResolution[] = "16384,16384";
// The camera of the host, while it has one: a Video for Windows capture
// device as VIDCAP.INF of the CD installs one, class MEDIA ("Sound, video
// and game controllers") with the device loader of a sound card and the
// capture driver under Drivers\msvideo (src/win16_capture.cpp,
// tools/host_devices/service.h). The Multimedia control panel lists it under
// "Video Capture Devices".
constexpr char CameraName[] = "NEO98 Camera";
constexpr char CameraId[] = "*NEO9840";
constexpr char CameraDevice[] = "Enum\\Root\\*NEO9840";
constexpr char CameraNode[] = "Enum\\Root\\*NEO9840\\0000";
constexpr char CameraDriver[] = "MEDIA\\0001";
constexpr char CameraDriverFile[] = "neo98cap.drv";
constexpr char CameraResource[] = "neo98cap.drv<0001>";
// A day of the port for the drivers that are part of it, in the form the
// class installers write DriverDate.
constexpr char DriverDate[] = "10-13-2026";

// The resources of the devices, as the configuration manager reports them
// (src/win16_config_manager.cpp): the ones of an AT compatible PC with the
// sound card where the DOS machine has its Sound Blaster (A220 I5 D1 H5,
// MPU-401 at 330h, OPL at 388h), the serial port COM1 and the parallel port
// LPT1 where the port's own are. No two devices share a value.
struct Resource
{
    char kind; // 'Q' interrupt request, 'P' I/O ports, 'D' DMA channel, 'M' memory
    unsigned first, last;
};
struct Resources
{
    const char *device; // "ENUMERATOR\\DEVICE\\INSTANCE" below Enum, upper case
    Resource list[8];
};
constexpr Resources DeviceResources[] = {
    {"ROOT\\*PNP0000\\0000", {{'Q', 2, 2}, {'P', 0x20, 0x21}, {'P', 0xa0, 0xa1}}},
    {"ROOT\\*PNP0200\\0000", {{'D', 4, 4}, {'P', 0x00, 0x0f}, {'P', 0x81, 0x83}, {'P', 0x87, 0x87},
                               {'P', 0x89, 0x8b}, {'P', 0x8f, 0x8f}, {'P', 0xc0, 0xdf}}},
    {"ROOT\\*PNP0B00\\0000", {{'Q', 8, 8}, {'P', 0x70, 0x71}}},
    {"ROOT\\*PNP0100\\0000", {{'Q', 0, 0}, {'P', 0x40, 0x43}}},
    {"ROOT\\*PNP0800\\0000", {{'P', 0x61, 0x61}}},
    {"ROOT\\*PNP0C04\\0000", {{'Q', 13, 13}, {'P', 0xf0, 0xff}}},
    {"ROOT\\*PNP0C01\\0000", {{'M', 0x00000000, 0x0009ffff}, {'M', 0x000e0000, 0x000fffff},
                               {'M', 0x00100000, 0x3fffffff}}},
    {"ROOT\\*PNP0303\\0000", {{'Q', 1, 1}, {'P', 0x60, 0x60}, {'P', 0x64, 0x64}}},
    {"ROOT\\*PNP0F0E\\0000", {{'Q', 12, 12}}},
    {"ROOT\\*NEO9800\\0000", {{'P', 0x3b0, 0x3bb}, {'P', 0x3c0, 0x3df}, {'M', 0x000a0000, 0x000bffff},
                               {'M', 0x000c0000, 0x000c7fff}, {'M', 0xe0000000, 0xe0ffffff}}},
    {"ROOT\\*PNP0500\\0000", {{'Q', 4, 4}, {'P', 0x3f8, 0x3ff}}},
    {"ROOT\\*PNP0400\\0000", {{'Q', 7, 7}, {'P', 0x378, 0x37f}}},
    {"ROOT\\*PNP0600\\0000", {{'Q', 14, 14}, {'P', 0x1f0, 0x1f7}, {'P', 0x3f6, 0x3f6}}},
    {"ROOT\\*PNP0600\\0001", {{'Q', 15, 15}, {'P', 0x170, 0x177}, {'P', 0x376, 0x376}}},
    {"ROOT\\*NEO9810\\0000", {{'Q', 5, 5}, {'D', 1, 1}, {'D', 5, 5}, {'P', 0x220, 0x22f}, {'P', 0x330, 0x331},
                               {'P', 0x388, 0x38b}}},
    {"ROOT\\NET\\0001", {{'Q', 10, 10}, {'P', 0x300, 0x31f}}},
};
} // namespace Neo98Hardware
