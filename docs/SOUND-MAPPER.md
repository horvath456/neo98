# The wave mapper's devices and the mixer's levels

## The preferred devices

The wave mapper (MSACM.DRV of Windows) keeps the preferred devices under
`HKCU\Software\Microsoft\Multimedia\Sound Mapper`: "Playback" and "Record"
hold the `szPname` of a device, "PreferredOnly" whether the mapper may take
another one. When the mapper is loaded at the start of Windows and a name is
not there, it walks the devices and writes the name of the first one that
answers its query, has a mixer (`mixerGetID`) and capabilities. A name that
is there is left alone, also one of a device this machine does not have.

The port has the mapper itself (`src/x86_wave_out.cpp`,
`PreferredWaveOutDevice`). The start of the session does what the mapper's
start does (`Application::LoadSoundMapperSettings`, from
`LoadUserStartSettings`): "Playback" is the port's output device, "Record"
its input device while there is one (`Sound::Microphone::Present`: the host
has a microphone and the switch for it is on, or the test microphone
`NEO98_TEST_MICROPHONE`).

The Audio page of Multimedia of Windows 95 fills its device boxes and
compares each name with "Playback" and "Record"; for the device that matches
it opens the mixer (`mixerGetID` with `MIXER_OBJECTF_WAVEOUT`, `mixerOpen`)
and shows the volume of the wave line under the speakers' destination, and
its slider sets that volume (`mixerSetControlDetails`). Without the names it
opened no mixer: the slider stood at Low and moved nothing. The page of
Windows 98 has no slider; its volume is a button that starts the Volume
Control.

## The levels of the mixer

A sound card's driver keeps the levels of its mixer over a restart. The
port's levels are those of `Sound::Mixer` (master, wave, synthesizer) and of
the microphone (`Sound::Microphone`). Every change through
`mixerSetControlDetails` stores all controls of the mixer, identifier and
value pair, in the binary value "Mixer" of the sound card's software key
`HKLM\System\CurrentControlSet\Services\Class\MEDIA\0000`, and the start of
the session sets them again (`Application::SaveMixerLevels`,
`LoadMixerLevels`, `src/x86_mixer.cpp`).

## Tests

`tools/test_sound_mapper_host.py` with `tools/sound_mapper_fixture.c`: the
names written with and without a microphone, a foreign name left alone, a
wave volume kept over a restart, and on Windows 95 the page's slider moved
with the keyboard sets the wave volume, also for the next session.
