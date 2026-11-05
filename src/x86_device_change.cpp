// SPDX-License-Identifier: GPL-2.0-or-later
// A CD-ROM or a drive that changes while the machine runs.
//
// On Win98 the CD-ROM driver notices the new disc, IOS and CONFIGMG hand the
// event up, and the shell VxD broadcasts WM_DEVICECHANGE to every top-level
// window: DBT_DEVICEREMOVECOMPLETE when a medium leaves, DBT_DEVICEARRIVAL when
// one comes, each with a DEV_BROADCAST_VOLUME that names the drive in its unit
// mask and sets DBTF_MEDIA for a changed disc. Explorer's desktop window
// refreshes My Computer on it and runs the disc's AUTORUN.INF; any program may
// react. The structure lives in memory of the VxD, so every process can read
// it; here that is a block of the system arena above C0000000.
#include "x86_app.h"
#include "cd_audio.h"
#include "user.h"
#include "volume.h"
#include "win98_session_arena.h"
#include <stdio.h>
#include <string.h>

namespace X86
{
namespace
{
constexpr unsigned DeviceChange = 0x0219;
constexpr unsigned Arrival = 0x8000, RemoveComplete = 0x8004;
constexpr unsigned DevtypVolume = 2, MediaFlag = 1;
// DEV_BROADCAST_VOLUME: size, device type, reserved, unit mask, flags.
constexpr unsigned VolumeRecordBytes = 18;
// Posted messages are read later; each announcement gets a slot of its own,
// and 128 of them are more than any receiver leaves unread.
constexpr unsigned SlotBytes = 32, Slots = 128;
constexpr unsigned ArenaAddress = Win98SessionArena::SystemBase + 0x00800000u;
unsigned next_slot = 0;
} // namespace

bool Application::ChangeMedium(char letter, bool announce_change)
{
    if (letter >= 'a' && letter <= 'z')
        letter = char(letter - 32);
    if (letter < 'A' || letter > 'Z' || letter == 'C')
        return false;
    auto &store = files.store;
    const bool before = store.Ready(letter);
    // The store still has the old medium's drive type; Volume has the new one.
    const bool cd_before = store.DriveType(letter) == 5;
    const unsigned error = store.ChangeMedium(letter);
    if (error)
    {
        char text[96];
        snprintf(text, sizeof text, "MEDIA %c: change failed with %u", letter, error);
        if (log) log(text);
        return false;
    }
    const bool after = store.Ready(letter);
    const bool cd_after = Volume::Type(letter) == Volume::Cdrom;
    if (cd_before || cd_after)
        CdAudio::Changed(letter);
    unsigned char *arena = SystemArena("CONFIGMG device broadcast", ArenaAddress, SlotBytes * Slots);
    auto announce = [&](unsigned event, bool cd) {
        if (!arena || !announce_change)
            return;
        const unsigned slot = next_slot++ % Slots;
        unsigned char *record = arena + slot * SlotBytes;
        memset(record, 0, SlotBytes);
        const unsigned words[4] = {VolumeRecordBytes, DevtypVolume, 0, 1u << (letter - 'A')};
        memcpy(record, words, sizeof words);
        const unsigned short flags = cd ? MediaFlag : 0;
        memcpy(record + 16, &flags, 2);
        unsigned windows[User::WindowCapacity], count = 0;
        if (!User::WindowSnapshot(0, windows, User::WindowCapacity, count))
            return;
        unsigned posted = 0;
        for (unsigned i = 0; i < count; ++i)
            if (User::IsWindow(windows[i]) && User::PostMessage(windows[i], DeviceChange, event, ArenaAddress + slot * SlotBytes))
                ++posted;
        char text[128];
        snprintf(text, sizeof text, "MEDIA %c: WM_DEVICECHANGE %s to %u windows", letter,
                 event == Arrival ? "DBT_DEVICEARRIVAL" : "DBT_DEVICEREMOVECOMPLETE", posted);
        if (log) log(text);
    };
    if (before)
        announce(RemoveComplete, cd_before);
    if (after)
        announce(Arrival, cd_after);
    return true;
}
} // namespace X86
