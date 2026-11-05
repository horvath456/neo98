// SPDX-License-Identifier: GPL-2.0-or-later
// The host side of the sound system: one SDL audio device that pulls frames
// out of the mixer of src/sound.h.
// src/ knows nothing about SDL, exactly as with the screen and the crash
// report. The mixer asks for four things and this header answers them: open
// the device when the first source opens it, close it, and keep Render out of
// the mixer while a guest thread changes a source. SDL_LockAudioDevice is the
// lock the audio callback already respects, so the two Lock entries are it.
// The device is opened at the one output format of the mixer, 44.100 frames a
// second in 16-bit stereo, and never reopened: a program that plays two
// sounds at two rates finds one device, because the mixer converts.
//
#pragma once
#include "../src/sound.h"
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
// Only macOS asks for a permission before the microphone opens; Linux opens
// it at once (PulseAudio, PipeWire or ALSA through SDL). Defining
// NEO98_MICROPHONE_WITHOUT_PERMISSION compiles the Linux path on a Mac, for a
// check of the branch that the Mac never runs.
#if defined(__APPLE__) && !defined(NEO98_MICROPHONE_WITHOUT_PERMISSION)
#define NEO98_MICROPHONE_PERMISSION 1
#include "host_microphone_mac.h"
#endif

namespace HostAudio
{
// The microphone of the host, the wave input of the sound card
// (src/sound.h, Sound::Input). It is an SDL capture device at the one input
// format of the ring, mono 16 bit at 44.100 Hz; SDL converts from whatever
// the hardware samples. The device is opened at the first waveInStart of a
// program and closed with the last waveInClose, never before, so macOS asks
// for the microphone only when a program records. On macOS the permission is
// asked first (tools/host_microphone_mac.mm); a refusal leaves the device
// to Windows and it records silence.
// NEO98_TEST_MICROPHONE puts the test device of src/sound_microphone.cpp in
// its place, and then this class never touches SDL: tests do not open the
// real microphone.
class Microphone
{
  public:
    void Install()
    {
        if (const char *test = getenv("NEO98_TEST_MICROPHONE"); test && *test)
        {
            Sound::SetInput(Sound::Input{});
            return;
        }
        Sound::Input input;
        input.owner = this;
        input.Present = [](void *owner) { return ((Microphone *)owner)->Present(); };
        input.Start = [](void *owner) { return ((Microphone *)owner)->Start(); };
        input.Poll = [](void *owner) { return ((Microphone *)owner)->Poll(); };
        input.Stop = [](void *owner) { ((Microphone *)owner)->Stop(); };
        input.Lock = [](void *owner) { ((Microphone *)owner)->Hold(true); };
        input.Unlock = [](void *owner) { ((Microphone *)owner)->Hold(false); };
        Sound::SetInput(input);
    }
    ~Microphone() { Stop(); }

  private:
    static constexpr unsigned BlockFrames = 512;
    SDL_AudioDeviceID device = 0;
    // The device count of SDL, asked at most every two seconds: Windows asks
    // waveInGetNumDevs often, and a microphone that is plugged in counts at
    // the next question after that.
    int count = 0;
    unsigned counted = 0;
    bool counted_once = false;

    bool Audio()
    {
        if (SDL_WasInit(SDL_INIT_AUDIO)) return true;
        if (SDL_InitSubSystem(SDL_INIT_AUDIO))
        {
            fprintf(stderr, "SDL audio: %s\n", SDL_GetError());
            return false;
        }
        return true;
    }
    bool Present()
    {
        if (device) return true;
        const unsigned now = SDL_GetTicks();
        if (!counted_once || now - counted > 2000)
        {
            counted_once = true;
            counted = now;
            // Counting reads the device list of the system; it opens nothing,
            // so macOS does not ask for the permission here. A driver that
            // cannot list its devices answers -1 and may still have one.
            count = Audio() ? SDL_GetNumAudioDevices(1) : 0;
        }
        return count != 0;
    }
    unsigned Start()
    {
        if (device) return Sound::InputRunning;
#ifdef NEO98_MICROPHONE_PERMISSION
        // The dummy driver of the tests (SDL_AUDIODRIVER=dummy) has no
        // hardware behind it and needs no permission, so a test never makes
        // macOS ask.
        if (!Dummy())
            switch (HostMicrophone::Current())
            {
            case HostMicrophone::Granted: break;
            case HostMicrophone::Undetermined: HostMicrophone::Ask(); return Sound::InputWaiting;
            default: return Sound::InputDenied;
            }
#endif
        return Open();
    }
    bool Dummy()
    {
        if (!Audio()) return false;
        const char *driver = SDL_GetCurrentAudioDriver();
        return driver && !strcmp(driver, "dummy");
    }
    unsigned Poll()
    {
#ifdef NEO98_MICROPHONE_PERMISSION
        switch (HostMicrophone::Answer())
        {
        case HostMicrophone::Granted: return Open();
        case HostMicrophone::Undetermined: return Sound::InputWaiting;
        default: return Sound::InputDenied;
        }
#else
        return Open();
#endif
    }
    unsigned Open()
    {
        if (device) return Sound::InputRunning;
        if (!Audio()) return Sound::InputFailed;
        SDL_AudioSpec want{}, have{};
        want.freq = Sound::InputRate;
        want.format = AUDIO_S16SYS;
        want.channels = 1;
        want.samples = BlockFrames;
        want.callback = [](void *, unsigned char *stream, int bytes) {
            Sound::Microphone::Push((const short *)stream, (unsigned)bytes / 2);
        };
        device = SDL_OpenAudioDevice(nullptr, 1, &want, &have, 0);
        if (!device)
        {
            fprintf(stderr, "SDL microphone: %s\n", SDL_GetError());
            return Sound::InputFailed;
        }
        SDL_PauseAudioDevice(device, 0);
        fprintf(stderr, "NEO98 MICROPHONE driver=%s rate=%d channels=%u frames=%u\n",
                SDL_GetCurrentAudioDriver(), have.freq, have.channels, have.samples);
        return Sound::InputRunning;
    }
    void Stop()
    {
        if (!device) return;
        SDL_CloseAudioDevice(device);
        device = 0;
    }
    void Hold(bool lock)
    {
        if (!device) return;
        if (lock) SDL_LockAudioDevice(device);
        else SDL_UnlockAudioDevice(device);
    }
};

class Device
{
  public:
    // Registers this device with the mixer. Nothing is opened yet; the first
    // source that wants to play opens it. The microphone comes with it.
    void Install()
    {
        static Microphone microphone;
        microphone.Install();
        Sound::Output output;
        output.owner = this;
        output.Start = [](void *owner) { return ((Device *)owner)->Start(); };
        output.Stop = [](void *owner) { ((Device *)owner)->Stop(); };
        output.Lock = [](void *owner) { ((Device *)owner)->Hold(true); };
        output.Unlock = [](void *owner) { ((Device *)owner)->Hold(false); };
        output.block = BlockFrames;
        Sound::SetOutput(output);
    }
    ~Device() { Stop(); }

  private:
    // What the device takes out of the mixer per callback: 1024 frames, 23
    // milliseconds. The play cursor of a DirectSound buffer moves in these
    // steps and its write cursor lies this far ahead (src/x86_dsound_vxd.cpp).
    static constexpr unsigned BlockFrames = 1024;
    SDL_AudioDeviceID device = 0;
    bool refused = false;
    FILE *tap = nullptr;

    bool Start()
    {
        if (device) return true;
        // One failure is enough: a machine without an audio device should not
        // be asked again at every waveOutOpen.
        if (refused) return false;
        if (SDL_InitSubSystem(SDL_INIT_AUDIO))
        {
            fprintf(stderr, "SDL audio: %s\n", SDL_GetError());
            refused = true;
            return false;
        }
        SDL_AudioSpec want{}, have{};
        want.freq = Sound::OutputRate;
        want.format = AUDIO_S16SYS;
        want.channels = Sound::OutputChannels;
        want.samples = BlockFrames;
        want.userdata = this;
        want.callback = [](void *self, unsigned char *stream, int bytes) {
            Sound::Shared().Render((short *)stream,
                                   (unsigned)bytes / (2 * Sound::OutputChannels));
            if (auto *tap = ((Device *)self)->tap) fwrite(stream, 1, (size_t)bytes, tap);
        };
        // NEO98_AUDIO_TAP=<file> keeps a copy of every frame the device plays,
        // 16-bit stereo at 44.100 Hz without a header: the proof of what a
        // program sounded like in the host.
        if (const char *path = getenv("NEO98_AUDIO_TAP"))
            if ((tap = fopen(path, "wb"))) setvbuf(tap, nullptr, _IONBF, 0);
        device = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
        if (!device)
        {
            fprintf(stderr, "SDL audio: %s\n", SDL_GetError());
            refused = true;
            return false;
        }
        SDL_PauseAudioDevice(device, 0);
        if (getenv("NEO98_AUDIO_TAP") || getenv("NEO98_TRACE_AUDIO"))
            fprintf(stderr, "NEO98 AUDIO driver=%s rate=%d channels=%u frames=%u\n",
                    SDL_GetCurrentAudioDriver(), have.freq, have.channels, have.samples);
        return true;
    }
    void Stop()
    {
        if (!device) return;
        SDL_CloseAudioDevice(device);
        device = 0;
        if (tap) fclose(tap);
        tap = nullptr;
    }
    void Hold(bool lock)
    {
        if (!device) return;
        if (lock) SDL_LockAudioDevice(device);
        else SDL_UnlockAudioDevice(device);
    }
};
} // namespace HostAudio
