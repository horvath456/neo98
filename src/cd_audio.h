// SPDX-License-Identifier: GPL-2.0-or-later
// The audio side of a CD-ROM drive: the table of contents of the disc in it and
// a player that streams its audio tracks into the mixer.
// A drive letter that Volume::Mount registered as Volume::Cdrom can carry a
// Disc. The disc is a host provider (tools/host_cd_image.h reads an image
// described by a CUE sheet); nothing here touches a host. The player is what
// the CD-ROM device driver of Windows 98 does behind the MSCDEX requests PLAY
// AUDIO, STOP AUDIO and RESUME AUDIO: it reads raw sectors of 2352 bytes,
// which are 588 frames of 44100 Hz 16-bit stereo, exactly the output format
// of the mixer, and hands them to it as a generator source.
// src/x86_mscdex.cpp answers the MSCDEX interface on top of this.
//
#pragma once
namespace CdAudio
{
using U = unsigned;
constexpr U SectorBytes = 2352, FramesPerSector = 588, SectorsPerSecond = 75;
// The pregap of the first track: a Red Book address is two seconds ahead of the
// logical block address (MSF 00:02:00 is block 0).
constexpr U Pregap = 150;
struct Track
{
    U number = 0;
    bool audio = false;
    U start = 0;  // logical block address
    U length = 0; // sectors
};
// What a host provider offers for one disc.
struct Disc
{
    virtual ~Disc() {}
    virtual U Tracks() const = 0;
    virtual bool TrackAt(U index, Track &out) const = 0;
    // The logical block address of the lead-out, one behind the last sector.
    virtual U LeadOut() const = 0;
    // Raw audio sectors, 2352 bytes each. May run on the host audio thread, so
    // an implementation keeps no state between two calls. Answers the number
    // of sectors read.
    virtual U ReadAudio(U lba, U sectors, unsigned char *out) = 0;
};
// The disc of a drive letter, or none. A drive without a disc of its own (a
// mounted directory) is a data disc with no audio.
void Insert(char letter, Disc *disc);
Disc *At(char letter);
// A medium change while the machine runs. The next MSCDEX "media
// changed" query of the drive answers FFh (changed) once, then 1 again.
void Changed(char letter);
bool TakeChanged(char letter);

// The player of one drive.
struct Status
{
    bool playing = false, paused = false;
    U position = 0;         // logical block address being played
    U start = 0, end = 0;   // the range of the last PLAY AUDIO, end exclusive
};
// PLAY AUDIO from `start` for `count` sectors. False when the range leaves the
// disc.
bool Play(char letter, U start, U count);
// STOP AUDIO: a playing drive pauses and keeps its position; a paused drive
// forgets it.
void Stop(char letter);
// RESUME AUDIO: false when nothing is paused.
bool Resume(char letter);
Status State(char letter);
// Audio channel control: the volume of the left and right output, 0 to 255.
void SetVolume(char letter, U left, U right);
void Volume(char letter, U &left, U &right);
// Whether a drive is playing: without a host audio device the mixer then
// needs its own clock (Application::Pump).
bool Clocked();
// Stops every drive and closes its mixer source; a runner calls this between
// two guests.
void ResetAll();
} // namespace CdAudio
