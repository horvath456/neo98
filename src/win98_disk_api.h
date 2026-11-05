// SPDX-License-Identifier: GPL-2.0-or-later
// DSKAPI16 reports no compressed volumes when no DoubleSpace/DriveSpace
// driver is installed. The version query returns zero. Drive-map output words
// are written even on failure. The compression-algorithm queries preserve the
// caller's CX when the interrupt has no handler, matching Windows 98
// behavior.
#pragma once
namespace Win98DiskApi
{
using U = unsigned;
// The NE entry ordinals of DSKAPI16.DLL, which are the catalogue ordinals.
enum Ordinal : U
{
    Dll16Call = 3,
    DriverVersion = 4,
    Capabilities = 6,
    DriveMap = 7,
    OrigDrive = 8,
    GetAutoMount = 9,
    SetAutoMount = 10,
    SwitchDrive = 11,
    Unmount = 12,
    DriveRange = 14,
    NumDiskUnits = 15,
    NumFragments = 16,
    IsDriveProt = 17,
    Remount = 18,
    SetCompressionAlgo = 19,
    SetCompressionThresh = 20,
    VolumeFormat = 21,
    ClusterSize = 22,
    VolumeName = 23,
    CompressionType = 24,
    LockDrive = 25,
    UnlockDrive = 26,
    DriveEcr = 27,
    DriveAcr = 28,
    FirstCluster = 29,
    CompressedFree = 30,
    PhysicalFree = 31,
    CompressedUsed = 32,
    PhysicalUsed = 33,
    CompressedTotal = 34,
    PhysicalTotal = 35,
    Overhead = 36,
    GetCompressionAlgo = 37,
};
// What a caller sees besides the DX:AX result.
struct Outputs
{
    // The word DLLDRV_GETDRIVERVERSION writes through its far pointer.
    bool flag = false;
    U flag_value = 0;
    // The two words DLLDRV_GETDRIVEMAP writes through its far pointers, in the
    // order (second argument, third argument).
    bool map = false;
    U map_first = 0, map_second = 0;
    // DLLGETVOLNAME empties the caller's buffer, but only on the path that
    // needs a drive descriptor. It is never taken here and stays false.
    bool empty_name = false;
};
// The result of one routine as DX:AX, with `drive` the zero based drive index
// the caller passed where the routine takes one.
inline U NoDriver(U ordinal, U drive, U cx, Outputs &out)
{
    out = Outputs{};
    switch (ordinal)
    {
    case DriverVersion: // Version zero, flag word zero.
        out.flag = true;
        return 0;
    case DriveMap: // and Windows 98 write first, then returns zero.
        out.map = true;
        out.map_first = 0x1a;
        out.map_second = 0xffff;
        return 0;
    case OrigDrive: // hands the caller's own drive back.
        return drive & 0xffffu;
    case VolumeFormat: // format 9, "no compressed volume".
        return 9;
    case ClusterSize:
        return 0xffff;
    case VolumeName:
        // Windows 98 tests the descriptor's +0xe0 first and Windows 98 returns zero
        // without touching the caller's buffer. The buffer is only emptied
        // when the descriptor names a drive.
        return 0;
    case SetCompressionAlgo:
    case SetCompressionThresh:
    case GetCompressionAlgo:
        // With the inverted test described above, the result contains
        // the caller's CX in the low word and zero in the high word.
        return cx & 0xffffu;
    case CompressedFree:
    case PhysicalFree:
    case CompressedUsed:
    case PhysicalUsed:
    case CompressedTotal:
    case PhysicalTotal:
    case Overhead: // mov ax,ffff / cwd, so DX:AX is all ones.
        return 0xffffffffu;
    default:
        // Capabilities, GetAutoMount, SetAutoMount,
        // SwitchDrive, Unmount, DriveRange,
        // NumDiskUnits, NumFragments, IsDriveProt,
        // Remount, CompressionType, LockDrive,
        // UnlockDrive, DriveEcr, DriveAcr,
        // FirstCluster.
        return 0;
    }
}
// Whether this catalogue ordinal is a routine the port answers at all.
inline bool Carried(U ordinal)
{
    return ordinal >= DriverVersion && ordinal <= GetCompressionAlgo && ordinal != 5 && ordinal != 13;
}
} // namespace Win98DiskApi
