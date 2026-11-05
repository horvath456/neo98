// SPDX-License-Identifier: GPL-2.0-or-later
// DSKAPI16 provider: marshal WORDs and far pointers from DSKAPI32 to the
// operations in src/win98_disk_api.h, then write results back. The stack
// pointer addresses the last argument; earlier arguments are above it.
#include "win98_disk_api.h"
#include "x86_app.h"
#include <stdio.h>
namespace X86
{
namespace
{
// Catalogue ordinal per index byte of thk_ThunkData16, read with
// tools/flat_thunk_targets.py out of the installed DSKAPI16.DLL: the target
// table names an NE entry point for every one of the 33 indices, and
// the catalogue ordinal is that entry's ordinal. Index 0 is DLL16CALL, the
// sample's own dialog filler; it has no catalogue ordinal, so a transition
// there stops at the trap instead of being answered.
struct Row
{
    unsigned char ordinal, words;
};
// Argument words are counted from the pushes of the 32-bit stub, and every one
// of them matches the retf of the 16-bit routine.
const Row rows[] = {
    {Win98DiskApi::DriverVersion, 2},        // 4, retf 4
    {0, 0},                                  // 5 ___EXPORTEDSTUB, no index
    {Win98DiskApi::Capabilities, 0},         // 6
    {Win98DiskApi::DriveMap, 5},             // 7, retf 0xa
    {Win98DiskApi::OrigDrive, 1},            // 8
    {Win98DiskApi::GetAutoMount, 0},         // 9
    {Win98DiskApi::SetAutoMount, 2},         // 10
    {Win98DiskApi::SwitchDrive, 2},          // 11
    {Win98DiskApi::Unmount, 1},              // 12
    {0, 0},                                  // 13 unused in the NE entry table
    {Win98DiskApi::DriveRange, 4},           // 14
    {Win98DiskApi::NumDiskUnits, 0},         // 15
    {Win98DiskApi::NumFragments, 4},         // 16
    {Win98DiskApi::IsDriveProt, 1},          // 17
    {Win98DiskApi::Remount, 1},              // 18
    {Win98DiskApi::SetCompressionAlgo, 1},   // 19
    {Win98DiskApi::SetCompressionThresh, 1}, // 20
    {Win98DiskApi::VolumeFormat, 1},         // 21
    {Win98DiskApi::ClusterSize, 1},          // 22
    {Win98DiskApi::VolumeName, 3},           // 23, retf 6
    {Win98DiskApi::CompressionType, 0},      // 24, retf 0
    {Win98DiskApi::LockDrive, 1},            // 25
    {Win98DiskApi::UnlockDrive, 1},          // 26
    {Win98DiskApi::DriveEcr, 3},             // 27
    {Win98DiskApi::DriveAcr, 3},             // 28
    {Win98DiskApi::FirstCluster, 2},         // 29
    {Win98DiskApi::CompressedFree, 1},       // 30
    {Win98DiskApi::PhysicalFree, 1},         // 31
    {Win98DiskApi::CompressedUsed, 1},       // 32
    {Win98DiskApi::PhysicalUsed, 1},         // 33
    {Win98DiskApi::CompressedTotal, 1},      // 34
    {Win98DiskApi::PhysicalTotal, 1},        // 35
    {Win98DiskApi::Overhead, 1},             // 36
    {Win98DiskApi::GetCompressionAlgo, 0},   // 37
};
const Row *Find(unsigned ordinal)
{
    for (const auto &row : rows)
        if (row.ordinal == ordinal)
            return &row;
    return nullptr;
}
} // namespace

unsigned Application::DiskApi16Words(unsigned ordinal)
{
    const Row *row = Find(ordinal);
    return row ? unsigned(row->words) : ~0u;
}

// The catalogue ordinals of DSKAPI16.DLL. `stack` addresses the last word the
// 32-bit stub pushed.
unsigned Application::DiskApi16(unsigned ordinal, unsigned stack)
{
    const Row *row = Find(ordinal);
    if (!row)
        return 0;
    const unsigned bytes = unsigned(row->words) * 2;
    // The first argument the caller wrote sits at the top of the pushed block.
    auto word = [&](unsigned at) {
        unsigned short value = 0;
        Read(stack + at, &value, 2);
        return unsigned(value);
    };
    auto flat = [&](unsigned at) { return selectors.Linear(Read32(stack + at)); };
    // Only the routines that take a drive index take it as the first word.
    unsigned drive = 0;
    switch (ordinal)
    {
    case Win98DiskApi::OrigDrive:
    case Win98DiskApi::Unmount:
    case Win98DiskApi::IsDriveProt:
    case Win98DiskApi::Remount:
    case Win98DiskApi::SetCompressionAlgo:
    case Win98DiskApi::SetCompressionThresh:
    case Win98DiskApi::VolumeFormat:
    case Win98DiskApi::ClusterSize:
    case Win98DiskApi::LockDrive:
    case Win98DiskApi::UnlockDrive:
    case Win98DiskApi::CompressedFree:
    case Win98DiskApi::PhysicalFree:
    case Win98DiskApi::CompressedUsed:
    case Win98DiskApi::PhysicalUsed:
    case Win98DiskApi::CompressedTotal:
    case Win98DiskApi::PhysicalTotal:
    case Win98DiskApi::Overhead:
    case Win98DiskApi::DriveMap:
    case Win98DiskApi::VolumeName:
    case Win98DiskApi::DriveEcr:
    case Win98DiskApi::DriveAcr:
    case Win98DiskApi::SwitchDrive:
        drive = word(bytes - 2);
        break;
    default:
        break;
    }
    Win98DiskApi::Outputs out;
    // Three routines of the original hand the caller's own CX back; the
    // generated stub leaves it in ECX, with the index byte in CL.
    const unsigned result =
        Win98DiskApi::NoDriver(ordinal, drive, Register(Reg::Ecx), out);
    // Windows 98 writes the flag word through the routine's only far pointer.
    if (out.flag)
        if (const unsigned target = flat(0))
        {
            const unsigned short value = (unsigned short)out.flag_value;
            Store(target, &value, 2);
        }
    // The second argument takes 0x1a, the third 0xffff.
    if (out.map)
    {
        if (const unsigned first = flat(4))
        {
            const unsigned short value = (unsigned short)out.map_first;
            Store(first, &value, 2);
        }
        if (const unsigned second = flat(0))
        {
            const unsigned short value = (unsigned short)out.map_second;
            Store(second, &value, 2);
        }
    }
    // truncates the caller's buffer.
    if (out.empty_name)
        if (const unsigned buffer = flat(0))
        {
            const char end = 0;
            Store(buffer, &end, 1);
        }
    if (log)
    {
        char text[192];
        snprintf(text, sizeof text,
                 "DSKAPI16 ordinal=%u drive=%u result=%08x no compression driver", ordinal, drive,
                 result);
        log(text);
    }
    return result;
}
} // namespace X86
