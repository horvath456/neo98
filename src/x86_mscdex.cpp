// SPDX-License-Identifier: GPL-2.0-or-later
// The MSCDEX interface of the CD-ROM drives, as Windows 98 offers it to a
// 16-bit program: INT 2Fh with AH=15h in protected mode, and the same
// functions through DPMI 0300h ("simulate real mode interrupt") for the ones
// that carry real mode addresses. MCICDA.DRV, the MCI driver of the cdaudio
// device, uses exactly this: 1500h and 150Ch in protected mode (MCICDA), 150Dh and 1510h through DPMI, with its
// request header in conventional memory from GlobalDOSAlloc.
// The drives are the letters Volume::Mount registered as Volume::Cdrom; the
// audio of a drive comes from its CdAudio::Disc (src/cd_audio.h). The device
// requests follow the MSCDEX 2.x specification of the CD-ROM device driver
// interface: IOCTL input (3) and output (12), SEEK (131), PLAY AUDIO (132),
// STOP AUDIO (133) and RESUME AUDIO (136).
#include "cd_audio.h"
#include "volume.h"
#include "x86_app.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
namespace
{
// The status word of a request header.
constexpr unsigned Done = 0x0100, Busy = 0x0200, ErrorBit = 0x8000;
// Device error codes of the status word.
constexpr unsigned NotReady = 2, UnknownCommand = 3, GeneralFailure = 12;
// Win98's own MSCDEX.EXE (the one of the emergency boot disk) is 2.25.
constexpr unsigned Version = 0x0219;
unsigned RedBook(unsigned lba)
{
    const unsigned at = lba + CdAudio::Pregap;
    const unsigned minute = at / (60 * 75), second = at / 75 % 60, frame = at % 75;
    return frame | second << 8 | minute << 16;
}
unsigned Logical(unsigned mode, unsigned address)
{
    if (!mode)
        return address; // High Sierra: the logical block itself
    const unsigned frame = address & 255, second = address >> 8 & 255, minute = address >> 16 & 255;
    const unsigned at = (minute * 60 + second) * 75 + frame;
    return at >= CdAudio::Pregap ? at - CdAudio::Pregap : 0;
}
unsigned Bcd(unsigned value) { return (value / 10) << 4 | value % 10; }
// The drive letters of the CD-ROM drives, in order.
unsigned Drives(char *letters)
{
    unsigned count = 0;
    for (char letter = 'A'; letter <= 'Z'; ++letter)
        if (Volume::Type(letter) == Volume::Cdrom)
            letters[count++] = letter;
    return count;
}
} // namespace

// INT 2Fh, AH=15h, in protected mode. Answers false for a function that needs
// real mode addresses; the caller reports it.
bool Application::MscdexInterrupt(unsigned &ax, unsigned &bx, unsigned &cx, bool &carry)
{
    char letters[26];
    const unsigned count = Drives(letters);
    carry = false;
    switch (ax & 0xff)
    {
    case 0x00: // installation check: BX the number of drives, CX the first
        bx = count;
        if (count)
            cx = unsigned(letters[0] - 'A');
        return true;
    case 0x0b: // drive check: AX nonzero for a CD-ROM drive, BX ADADh
        ax = Volume::Type(char('A' + (cx & 0xff))) == Volume::Cdrom ? 0x5ad8 : 0;
        bx = 0xadad;
        return true;
    case 0x0c: // version
        bx = Version;
        return true;
    default:
        return false;
    }
}

// The same interface with real mode addresses: DPMI 0300h for INT 2Fh. `rm`
// is the linear address of the real mode call structure.
bool Application::MscdexRealMode(unsigned rm)
{
    auto reg16 = [&](unsigned at) { return Win16Word(rm + at); };
    const unsigned ax = reg16(0x1c), bx = reg16(0x10), cx = reg16(0x18), es = reg16(0x22);
    auto set_flags = [&](bool carry) {
        const unsigned flags = reg16(0x20);
        Win16Word(rm + 0x20, carry ? flags | 1 : flags & ~1u);
    };
    char letters[26];
    const unsigned count = Drives(letters);
    const unsigned linear = es * 16 + bx;
    switch (ax & 0xff)
    {
    case 0x00:
    case 0x0b:
    case 0x0c:
    {
        unsigned a = ax, b = bx, c = cx;
        bool carry = false;
        MscdexInterrupt(a, b, c, carry);
        Win16Word(rm + 0x1c, a);
        Win16Word(rm + 0x10, b);
        Win16Word(rm + 0x18, c);
        set_flags(carry);
        return true;
    }
    case 0x0d: // drive letters, one byte each (0 is A:), at ES:BX
        for (unsigned i = 0; i < count; ++i)
        {
            const unsigned char value = (unsigned char)(letters[i] - 'A');
            Store(linear + i, &value, 1);
        }
        set_flags(false);
        return true;
    case 0x10: // send device driver request: CX the drive, ES:BX the header
    {
        const char letter = char('A' + (cx & 0xff));
        if (Volume::Type(letter) != Volume::Cdrom)
        {
            Win16Word(rm + 0x1c, 15); // invalid drive
            set_flags(true);
            return true;
        }
        MscdexRequest(letter, linear);
        set_flags(false);
        return true;
    }
    default:
        return false;
    }
}

// One request of the device driver interface. The header is at `header`.
void Application::MscdexRequest(char letter, unsigned header)
{
    unsigned char command = 0;
    Read(header + 2, &command, 1);
    CdAudio::Disc *disc = CdAudio::At(letter);
    auto byte_at = [&](unsigned address) {
        unsigned char value = 0;
        Read(address, &value, 1);
        return unsigned(value);
    };
    auto put_byte = [&](unsigned address, unsigned value) {
        const unsigned char b = (unsigned char)value;
        Store(address, &b, 1);
    };
    // The table of contents: the disc's, or one data track over a mounted
    // directory.
    CdAudio::Track tracks[100];
    unsigned track_count = 0, lead_out = 0;
    if (disc)
    {
        for (unsigned i = 0; i < disc->Tracks() && track_count < 99; ++i)
            if (disc->TrackAt(i, tracks[track_count]))
                ++track_count;
        lead_out = disc->LeadOut();
    }
    else if (Volume::Backend *volume = Volume::Mounted(letter))
    {
        unsigned long long total = 0, available = 0;
        volume->Space(total, available);
        lead_out = unsigned(total / 2048);
        tracks[0] = {1, false, 0, lead_out};
        track_count = 1;
    }
    unsigned status = Done;
    switch (command)
    {
    case 3:  // IOCTL input
    case 12: // IOCTL output
    {
        const unsigned far = Read32(header + 0x0e);
        const unsigned block = (far >> 16) * 16 + (far & 0xffff);
        const unsigned code = byte_at(block);
        if (command == 12)
        {
            switch (code)
            {
            case 1: // lock or unlock the door
            case 2: // reset the drive
                break;
            case 3: // audio channel control: channel and volume, four pairs
                CdAudio::SetVolume(letter, byte_at(block + 2), byte_at(block + 4));
                break;
            default: // eject (0) and close tray (5) need a tray; an image has none
                status = ErrorBit | Done | UnknownCommand;
                break;
            }
            break;
        }
        switch (code)
        {
        case 4: // audio channel info
        {
            unsigned left = 0, right = 0;
            CdAudio::Volume(letter, left, right);
            const unsigned char info[8] = {0, (unsigned char)left, 1, (unsigned char)right, 2, 0, 3, 0};
            Store(block + 1, info, sizeof info);
            break;
        }
        case 6: // device status: door unlocked, cooked and raw, data read and
                // audio play, audio channel manipulation, HSG and Red Book;
                // bit 11 when the drive holds no disc
            Write32(block + 1, 0x0002 | 0x0004 | 0x0010 | 0x0100 | 0x0200 |
                                   (disc || Volume::Mounted(letter) ? 0 : 0x0800));
            break;
        case 8: // volume size
            Write32(block + 1, lead_out);
            break;
        case 9: // media changed: FFh once after a change of the disc, else 1
            put_byte(block + 1, CdAudio::TakeChanged(letter) ? 0xff : 1);
            break;
        case 10: // audio disk info
            put_byte(block + 1, track_count ? tracks[0].number : 1);
            put_byte(block + 2, track_count ? tracks[track_count - 1].number : 1);
            Write32(block + 3, RedBook(lead_out));
            break;
        case 11: // audio track info: the track in, its start and control out
        {
            const unsigned wanted = byte_at(block + 1);
            unsigned found = track_count;
            for (unsigned i = 0; i < track_count; ++i)
                if (tracks[i].number == wanted)
                    found = i;
            if (found == track_count)
            {
                status = ErrorBit | Done | GeneralFailure;
                break;
            }
            Write32(block + 2, RedBook(tracks[found].start));
            put_byte(block + 6, tracks[found].audio ? 0x01 : 0x41);
            break;
        }
        case 12: // audio Q-channel info: control/ADR, track and index in BCD,
                 // the time in the track and on the disc
        {
            const auto state = CdAudio::State(letter);
            const unsigned at = state.position;
            unsigned index = 0;
            for (unsigned i = 0; i < track_count; ++i)
                if (tracks[i].start <= at)
                    index = i;
            const auto &track = tracks[index];
            const unsigned relative = at - (at >= track.start ? track.start : at);
            const unsigned absolute = RedBook(at);
            const unsigned char info[10] = {
                (unsigned char)(track.audio ? 0x01 : 0x41),
                (unsigned char)Bcd(track_count ? track.number : 1),
                1,
                (unsigned char)(relative / (60 * 75)),
                (unsigned char)(relative / 75 % 60),
                (unsigned char)(relative % 75),
                0,
                (unsigned char)(absolute >> 16),
                (unsigned char)(absolute >> 8),
                (unsigned char)absolute};
            Store(block + 1, info, sizeof info);
            break;
        }
        case 15: // audio status info: paused bit, resume start, end
        {
            const auto state = CdAudio::State(letter);
            Win16Word(block + 1, state.paused ? 1 : 0);
            Write32(block + 3, RedBook(state.paused ? state.position : state.start));
            Write32(block + 7, RedBook(state.end));
            break;
        }
        default: // the device header address (0), the drive bytes (5), read
                 // subchannels (13) and the UPC code (14) are not answered by
                 // this drive; a driver reports the code as unknown
            status = ErrorBit | Done | UnknownCommand;
            break;
        }
        break;
    }
    case 131: // SEEK: the head goes to the start, playing stops
    {
        const unsigned start = Logical(byte_at(header + 0x0d), Read32(header + 0x14));
        if (!disc || !CdAudio::Play(letter, start, 0))
            status = ErrorBit | Done | (disc ? GeneralFailure : NotReady);
        break;
    }
    case 132: // PLAY AUDIO: addressing mode, start, number of sectors
    {
        const unsigned start = Logical(byte_at(header + 0x0d), Read32(header + 0x0e));
        const unsigned count = Read32(header + 0x12);
        if (!disc || !CdAudio::Play(letter, start, count))
            status = ErrorBit | Done | (disc ? GeneralFailure : NotReady);
        break;
    }
    case 133: // STOP AUDIO
        CdAudio::Stop(letter);
        break;
    case 136: // RESUME AUDIO
        if (!CdAudio::Resume(letter))
            status = ErrorBit | Done | GeneralFailure;
        break;
    default:
        status = ErrorBit | Done | UnknownCommand;
        break;
    }
    if (CdAudio::State(letter).playing)
        status |= Busy;
    Win16Word(header + 3, status);
    if (log && trace_calls)
    {
        char text[160];
        snprintf(text, sizeof text, "MSCDEX drive=%c command=%u status=%04x", letter, command, status);
        log(text);
    }
}
} // namespace X86
