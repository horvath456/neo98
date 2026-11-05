/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Guest side of tools/test_dsound_guest.py: DirectSound over the original
   DSOUND.DLL of the tree.

   The fixture loads the module by hand and walks the public path a game walks:
   DirectSoundCreate, GetCaps, SetCooperativeLevel on a window of its own, the
   primary buffer with a format, a secondary buffer with a sine in it, volume,
   pan, frequency, Play and Stop. Every interface is called through its vtable,
   because this fixture has no Windows headers; the offsets are the ones of
   IDirectSound and IDirectSoundBuffer.

   Every observation goes out through OutputDebugStringA, so the runner reads
   it out of the log; the exit code names the first step that did not get far
   enough to observe anything. The samples that come out the other end are
   checked in tools/test_dsound_guest.cpp. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U LoadLibraryA(const char *);
API U GetProcAddress(U, const char *);
API void OutputDebugStringA(const char *);
API void ExitProcess(U);
API void Sleep(U);
API U GetTickCount(void);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U ShowWindow(U, U);
API U SetForegroundWindow(U);

typedef U(__stdcall *Create)(void *, void **, void *);
/* IDirectSound: QueryInterface, AddRef, Release, CreateSoundBuffer, GetCaps,
   DuplicateSoundBuffer, SetCooperativeLevel, Compact, GetSpeakerConfig,
   SetSpeakerConfig, Initialize. */
typedef U(__stdcall *Buffer2)(void *, void *, void **, void *);
typedef U(__stdcall *One)(void *, void *);
typedef U(__stdcall *Level)(void *, U, U);
/* IDirectSoundBuffer: the entries this fixture uses. */
typedef U(__stdcall *Lock)(void *, U, U, void **, U *, void **, U *, U);
typedef U(__stdcall *Unlock)(void *, void *, U, void *, U);
typedef U(__stdcall *Play)(void *, U, U, U);
typedef U(__stdcall *Word)(void *, U);
typedef U(__stdcall *Signed)(void *, int);
typedef U(__stdcall *Out)(void *, U *);
typedef U(__stdcall *Cursor)(void *, U *, U *);
typedef U(__stdcall *Nothing)(void *);
typedef U(__stdcall *Format)(void *, void *);

static char line[256];
static void number(const char *label, U value)
{
    static const char digits[] = "0123456789abcdef";
    U at = 0, i = 0;
    while (label[i]) line[at++] = label[i++];
    line[at++] = '=';
    for (i = 0; i < 8; ++i) line[at++] = digits[(value >> (28 - i * 4)) & 15];
    line[at] = 0;
    OutputDebugStringA(line);
}
static void *entry(void *object, U offset)
{
    U *vtable = *(U **)object;
    return (void *)vtable[offset / 4];
}
/* WAVEFORMATEX: wFormatTag, nChannels, nSamplesPerSec, nAvgBytesPerSec,
   nBlockAlign, wBitsPerSample, cbSize. */
static short format[9];
static void wave(U rate, U bits, U channels)
{
    const U align = channels * (bits / 8);
    format[0] = 1;
    format[1] = (short)channels;
    *(U *)(format + 2) = rate;
    *(U *)(format + 4) = rate * align;
    format[6] = (short)align;
    format[7] = (short)bits;
    format[8] = 0;
}
/* DSBUFFERDESC: dwSize, dwFlags, dwBufferBytes, dwReserved, lpwfxFormat. */
static U description[5];
/* DSCAPS is 24 dwords; only dwSize and dwFlags are read here. */
static U capabilities[24];
/* One period of a sine, 25 samples, which is 441 Hz at 11.025 kHz. The same
   frequency the wave output fixture plays, so both tests measure the same
   thing at the other end of the mixer. */
static const int period[25] = {0,   25,  48,  68,  84,  95,  100, 98,  90,  77,  59, 37, 13,
                               -13, -37, -59, -77, -90, -98, -100, -95, -84, -68, -48, -25};
/* WNDCLASSA: style, lpfnWndProc, cbClsExtra, cbWndExtra, hInstance, hIcon,
   hCursor, hbrBackground, lpszMenuName, lpszClassName. */
static U window_class[10];
static U __stdcall procedure(U window, U message, U first, U second)
{
    return DefWindowProcA(window, message, first, second);
}

void __stdcall mainCRTStartup(void)
{
    U library = 0, value = 0, window = 0, i = 0, bytes = 0;
    U play_cursor = 0, write_cursor = 0, previous = 0;
    U advances = 0, wraps = 0, highest = 0, steps = 0;
    U stall = 0, longest = 0, travelled = 0, started = 0, elapsed = 0;
    void *sound = 0, *primary = 0, *secondary = 0, *looping = 0;
    void *block = 0, *wrap = 0;
    U block_bytes = 0, wrap_bytes = 0;
    unsigned char *samples = 0;

    library = LoadLibraryA("DSOUND.DLL");
    number("DSOUND loaded", library ? 1 : 0);
    if (!library) ExitProcess(1);
    Create create = (Create)GetProcAddress(library, "DirectSoundCreate");
    number("DSOUND create_found", create ? 1 : 0);
    if (!create) ExitProcess(2);

    value = create(0, &sound, 0);
    number("DSOUND create", value);
    number("DSOUND object", sound ? 1 : 0);
    if (value || !sound) ExitProcess(3);

    /* GetCaps: DSCAPS_EMULDRIVER (20h) would say that the card has no
       DirectSound driver and DirectSound goes through the wave device. */
    capabilities[0] = sizeof capabilities;
    value = ((One)entry(sound, 0x10))(sound, capabilities);
    number("DSOUND getcaps", value);
    number("DSOUND caps_flags", capabilities[1]);
    number("DSOUND caps_emulated", (capabilities[1] & 0x20) ? 1 : 0);

    window_class[1] = (U)procedure;
    window_class[9] = (U) "NEO98DirectSound";
    number("DSOUND class", RegisterClassA(window_class) ? 1 : 0);
    window = CreateWindowExA(0, "NEO98DirectSound", "NEO98 DirectSound", 0x10000000,
                             0, 0, 320, 200, 0, 0, 0, 0);
    number("DSOUND window", window ? 1 : 0);
    if (!window) ExitProcess(4);
    ShowWindow(window, 1);
    SetForegroundWindow(window);

    /* DSSCL_PRIORITY, the level a game takes to set the primary format. */
    value = ((Level)entry(sound, 0x18))(sound, window, 2);
    number("DSOUND cooperative", value);
    if (value) ExitProcess(5);

    /* The primary buffer: DSBCAPS_PRIMARYBUFFER, no bytes and no format. */
    description[0] = sizeof description;
    description[1] = 1;
    description[2] = 0;
    description[3] = 0;
    description[4] = 0;
    value = ((Buffer2)entry(sound, 0x0c))(sound, description, &primary, 0);
    number("DSOUND primary", value);
    number("DSOUND primary_object", primary ? 1 : 0);
    if (value || !primary) ExitProcess(6);

    wave(44100, 16, 2);
    value = ((Format)entry(primary, 0x38))(primary, format);
    number("DSOUND primary_format", value);
    value = ((Play)entry(primary, 0x30))(primary, 0, 0, 1);
    number("DSOUND primary_play", value);

    /* The secondary buffer: two fifths of a second of 8-bit mono at 11.025 kHz,
       the same shape the wave output fixture writes. */
    bytes = 4410;
    wave(11025, 8, 1);
    description[0] = sizeof description;
    description[1] = 0x20 | 0x40 | 0x80 | 0x8000; /* frequency, pan, volume, global focus */
    description[2] = bytes;
    description[3] = 0;
    description[4] = (U)format;
    value = ((Buffer2)entry(sound, 0x0c))(sound, description, &secondary, 0);
    number("DSOUND secondary", value);
    number("DSOUND secondary_object", secondary ? 1 : 0);
    if (value || !secondary) ExitProcess(7);

    value = ((Lock)entry(secondary, 0x2c))(secondary, 0, bytes, &block, &block_bytes,
                                           &wrap, &wrap_bytes, 0);
    number("DSOUND lock", value);
    number("DSOUND lock_bytes", block_bytes);
    if (value || !block || block_bytes != bytes) ExitProcess(8);
    samples = (unsigned char *)block;
    for (i = 0; i < bytes; ++i)
        samples[i] = (unsigned char)(128 + period[i % 25]);
    value = ((Unlock)entry(secondary, 0x4c))(secondary, block, block_bytes, wrap, wrap_bytes);
    number("DSOUND unlock", value);

    /* Full volume, centre pan, the rate of the format. */
    value = ((Signed)entry(secondary, 0x3c))(secondary, 0);
    number("DSOUND volume", value);
    value = ((Signed)entry(secondary, 0x40))(secondary, 0);
    number("DSOUND pan", value);
    value = ((Word)entry(secondary, 0x44))(secondary, 11025);
    number("DSOUND frequency", value);
    value = 0;
    ((Out)entry(secondary, 0x18))(secondary, &value);
    number("DSOUND volume_back", value);

    value = ((Play)entry(secondary, 0x30))(secondary, 0, 0, 0);
    number("DSOUND play", value);
    if (value) ExitProcess(9);
    /* Two fifths of a second of sound, and a little more so the mixer of
       DSOUND has written the tail out through waveOut. */
    for (i = 0; i < 40; ++i)
        Sleep(20);
    value = 0;
    ((Out)entry(secondary, 0x24))(secondary, &value);
    number("DSOUND status", value);
    value = ((Nothing)entry(secondary, 0x48))(secondary);
    number("DSOUND stop", value);

    /* The play cursor of a looping buffer. SDL 1.2 and every streaming engine
       of the collection wait here: the audio thread asks
       IDirectSoundBuffer::GetCurrentPosition over and over and only writes the
       next chunk once the cursor has left the chunk it holds. A cursor that
       stands still or leaves its buffer turns that wait into an endless loop,
       so this is the contract the wait depends on and it gets measured, not
       assumed. The buffer is two fifths of a second of silence (128 is the
       middle of unsigned eight bit), so the samples the run checks further
       down stay the ones of the sine above. */
    bytes = 4410;
    wave(11025, 8, 1);
    description[0] = sizeof description;
    description[1] = 0x8000; /* DSBCAPS_GLOBALFOCUS */
    description[2] = bytes;
    description[3] = 0;
    description[4] = (U)format;
    value = ((Buffer2)entry(sound, 0x0c))(sound, description, &looping, 0);
    number("DSOUND loop_buffer", value);
    number("DSOUND loop_object", looping ? 1 : 0);
    if (value || !looping) ExitProcess(10);
    block = 0;
    block_bytes = 0;
    value = ((Lock)entry(looping, 0x2c))(looping, 0, bytes, &block, &block_bytes,
                                         &wrap, &wrap_bytes, 0);
    number("DSOUND loop_lock", value);
    if (value || !block || block_bytes != bytes) ExitProcess(11);
    samples = (unsigned char *)block;
    for (i = 0; i < bytes; ++i) samples[i] = 128;
    ((Unlock)entry(looping, 0x4c))(looping, block, block_bytes, wrap, wrap_bytes);

    value = ((Word)entry(looping, 0x34))(looping, 0); /* SetCurrentPosition */
    number("DSOUND loop_rewind", value);
    /* DSBPLAY_LOOPING. */
    value = ((Play)entry(looping, 0x30))(looping, 0, 0, 1);
    number("DSOUND loop_play", value);
    if (value) ExitProcess(12);
    value = 0;
    ((Out)entry(looping, 0x24))(looping, &value);
    /* DSBSTATUS_PLAYING 1 and DSBSTATUS_LOOPING 4. */
    number("DSOUND loop_status", value);

    value = ((Cursor)entry(looping, 0x10))(looping, &play_cursor, &write_cursor);
    number("DSOUND loop_position", value);
    previous = play_cursor;
    highest = play_cursor;
    started = GetTickCount();
    /* Fifty readings forty milliseconds apart: two seconds, which is five turns
       of this buffer. `travelled` adds up the distance the cursor covered, so
       the run can compare it with the time that passed on the clock of the
       guest; `longest` is the longest run of readings that saw no movement at
       all, which is what a waiting thread would sit through. */
    for (steps = 0; steps < 50; ++steps)
    {
        Sleep(40);
        play_cursor = 0xffffffff;
        if (((Cursor)entry(looping, 0x10))(looping, &play_cursor, &write_cursor)) break;
        if (play_cursor >= bytes) { highest = play_cursor; break; }
        if (play_cursor > highest) highest = play_cursor;
        if (play_cursor == previous) { if (++stall > longest) longest = stall; }
        else
        {
            stall = 0;
            ++advances;
            if (play_cursor < previous) { ++wraps; travelled += bytes - previous + play_cursor; }
            else travelled += play_cursor - previous;
        }
        previous = play_cursor;
    }
    elapsed = GetTickCount() - started;
    number("DSOUND loop_steps", steps);
    number("DSOUND loop_advances", advances);
    number("DSOUND loop_wraps", wraps);
    number("DSOUND loop_highest", highest);
    number("DSOUND loop_longest_stall", longest);
    number("DSOUND loop_travelled", travelled);
    number("DSOUND loop_elapsed", elapsed);
    /* The bytes the buffer should have played in that time: eight bit mono at
       11.025 kHz is 11.025 bytes a millisecond. */
    number("DSOUND loop_expected", elapsed * 11025 / 1000);
    value = 0;
    ((Out)entry(looping, 0x24))(looping, &value);
    number("DSOUND loop_status_end", value);
    value = ((Nothing)entry(looping, 0x48))(looping);
    number("DSOUND loop_stop", value);
    ((Nothing)entry(looping, 8))(looping);

    ((Nothing)entry(secondary, 8))(secondary);
    ((Nothing)entry(primary, 8))(primary);
    ((Nothing)entry(sound, 8))(sound);
    number("DSOUND done", 1);
    ExitProcess(0);
}
