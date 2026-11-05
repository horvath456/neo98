// SPDX-License-Identifier: GPL-2.0-or-later
// The CD audio player. See src/cd_audio.h.
#include "cd_audio.h"
#include "sound.h"
namespace CdAudio
{
namespace
{
struct Drive
{
    Disc *disc = nullptr;
    unsigned source = 0;
    // Written by the guest side under Sound::Lock, read by the generator.
    bool playing = false, paused = false;
    U start = 0, end = 0, position = 0;
    U frame = 0; // frame inside the sector at `position`
    U left = 255, right = 255;
    // The sector the generator has in hand.
    U cached = ~0u;
    unsigned char sector[SectorBytes] = {};
    bool changed = false;
};
Drive drives[26];
Drive *Of(char letter)
{
    if (letter >= 'a' && letter <= 'z')
        letter = char(letter - 32);
    return letter >= 'A' && letter <= 'Z' ? &drives[letter - 'A'] : nullptr;
}
// Runs inside the mixer's render, on whichever thread renders.
void Render(void *owner, short *out, unsigned frames)
{
    auto &drive = *static_cast<Drive *>(owner);
    for (unsigned i = 0; i < frames; ++i)
    {
        if (!drive.playing || drive.position >= drive.end || !drive.disc)
        {
            if (drive.playing && drive.position >= drive.end)
                drive.playing = false;
            out[i * 2] = out[i * 2 + 1] = 0;
            continue;
        }
        if (drive.cached != drive.position)
        {
            if (drive.disc->ReadAudio(drive.position, 1, drive.sector) != 1)
                for (auto &b : drive.sector)
                    b = 0;
            drive.cached = drive.position;
        }
        const unsigned char *p = drive.sector + drive.frame * 4;
        const int l = short(p[0] | p[1] << 8), r = short(p[2] | p[3] << 8);
        out[i * 2] = short(l * int(drive.left) / 255);
        out[i * 2 + 1] = short(r * int(drive.right) / 255);
        if (++drive.frame == FramesPerSector)
        {
            drive.frame = 0;
            ++drive.position;
        }
    }
}
} // namespace

void Insert(char letter, Disc *disc)
{
    if (Drive *drive = Of(letter))
    {
        Sound::Lock();
        drive->disc = disc;
        drive->playing = drive->paused = false;
        drive->cached = ~0u;
        Sound::Unlock();
    }
}
void Changed(char letter)
{
    if (Drive *drive = Of(letter))
        drive->changed = true;
}
bool TakeChanged(char letter)
{
    Drive *drive = Of(letter);
    if (!drive || !drive->changed)
        return false;
    drive->changed = false;
    return true;
}
Disc *At(char letter)
{
    Drive *drive = Of(letter);
    return drive ? drive->disc : nullptr;
}
bool Play(char letter, U start, U count)
{
    Drive *drive = Of(letter);
    if (!drive || !drive->disc || start >= drive->disc->LeadOut() || count > drive->disc->LeadOut() - start)
        return false;
    if (!drive->source)
    {
        drive->source = Sound::Shared().OpenGenerator(Render, drive);
        if (!drive->source)
            return false;
    }
    Sound::Lock();
    drive->start = drive->position = start;
    drive->end = start + count;
    drive->frame = 0;
    drive->paused = false;
    drive->playing = count != 0;
    Sound::Unlock();
    return true;
}
void Stop(char letter)
{
    Drive *drive = Of(letter);
    if (!drive)
        return;
    Sound::Lock();
    if (drive->playing)
    {
        drive->playing = false;
        drive->paused = true;
    }
    else
        drive->paused = false;
    Sound::Unlock();
}
bool Resume(char letter)
{
    Drive *drive = Of(letter);
    if (!drive || !drive->paused)
        return false;
    Sound::Lock();
    drive->paused = false;
    drive->playing = drive->position < drive->end;
    Sound::Unlock();
    return true;
}
Status State(char letter)
{
    Status status;
    if (Drive *drive = Of(letter))
    {
        Sound::Lock();
        if (drive->playing && drive->position >= drive->end)
            drive->playing = false;
        status.playing = drive->playing;
        status.paused = drive->paused;
        status.position = drive->position;
        status.start = drive->start;
        status.end = drive->end;
        Sound::Unlock();
    }
    return status;
}
void SetVolume(char letter, U left, U right)
{
    if (Drive *drive = Of(letter))
    {
        Sound::Lock();
        drive->left = left > 255 ? 255 : left;
        drive->right = right > 255 ? 255 : right;
        Sound::Unlock();
    }
}
void Volume(char letter, U &left, U &right)
{
    Drive *drive = Of(letter);
    left = drive ? drive->left : 0;
    right = drive ? drive->right : 0;
}
bool Clocked()
{
    for (const auto &drive : drives)
        if (drive.playing)
            return true;
    return false;
}
void ResetAll()
{
    for (auto &drive : drives)
    {
        if (drive.source)
            Sound::Shared().Close(drive.source);
        Disc *disc = drive.disc;
        drive = Drive{};
        drive.disc = disc;
    }
}
} // namespace CdAudio
