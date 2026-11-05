/* SPDX-License-Identifier: GPL-2.0-or-later */
/* DirectSound for ARM64 programs, checked from inside one: the interfaces of
   DSOUND.DLL (sdk/dx/dsound) over the driver NEO98DX.DLL. The checks compare
   answers and cursors; what the buffers sound like is in the frames the mixer
   rendered, which tools/test_arm64_dx_native.py reads. The rows "MEASURE"
   carry the times the test reports. */
#define COBJMACROS
#include <windows.h>
#include <dsound.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int checks, failures;
/* With a file name as its argument the fixture writes its MEASURE rows and
   its summary there as well: the host shows no console to a test. */
static FILE *report;
static void Say(const char *format, ...)
{
    char text[200];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(text, sizeof text, format, arguments);
    va_end(arguments);
    printf("%s\n", text);
    if (report)
    {
        fprintf(report, "%s\n", text);
        fflush(report);
    }
}
/* A failed check is kept by its line and, for a result, its value; the rows
   "FAILS line:value..." at the end name them all (the console holds
   twenty-five rows). */
static int failed_lines[96];
static unsigned long failed_values[96];
static void Fail(int line, unsigned long value)
{
    if (failures < 96)
    {
        failed_lines[failures] = line;
        failed_values[failures] = value;
    }
    ++failures;
}
static void Report(void)
{
    char row[200];
    int i, used = 0;
    for (i = 0; i < failures && i < 96; ++i)
    {
        used += snprintf(row + used, sizeof row - (size_t)used, "%s%d:%lx", i % 8 ? " " : "FAILS ", failed_lines[i], failed_values[i]);
        if (i % 8 == 7 || i + 1 == failures)
        {
            Say("%s", row);
            used = 0;
        }
    }
    Say("DSOUND checks=%d failures=%d", checks, failures);
}
#define CHECK(condition, text) \
    do { ++checks; if (!(condition)) Fail(__LINE__, 0); } while (0)
#define CHECK_HR(expression, expected, text) \
    do { const HRESULT hr_ = (expression); ++checks; if (hr_ != (HRESULT)(expected)) Fail(__LINE__, (unsigned long)hr_); } while (0)

static HWND window;

static void Format(WAVEFORMATEX *format, DWORD rate, WORD channels, WORD bits)
{
    memset(format, 0, sizeof *format);
    format->wFormatTag = WAVE_FORMAT_PCM;
    format->nChannels = channels;
    format->nSamplesPerSec = rate;
    format->wBitsPerSample = bits;
    format->nBlockAlign = (WORD)(channels * bits / 8);
    format->nAvgBytesPerSec = rate * format->nBlockAlign;
}

static IDirectSoundBuffer *MakeBuffer(IDirectSound *ds, DWORD flags, DWORD bytes, DWORD rate, WORD channels, WORD bits)
{
    WAVEFORMATEX format;
    DSBUFFERDESC desc;
    IDirectSoundBuffer *buffer = NULL;
    Format(&format, rate, channels, bits);
    memset(&desc, 0, sizeof desc);
    desc.dwSize = sizeof desc;
    desc.dwFlags = flags;
    desc.dwBufferBytes = bytes;
    desc.lpwfxFormat = &format;
    return IDirectSound_CreateSoundBuffer(ds, &desc, &buffer, NULL) == DS_OK ? buffer : NULL;
}

/* Plays a buffer to its end and answers how long that took, in milliseconds. */
static DWORD PlayToEnd(IDirectSoundBuffer *buffer)
{
    DWORD status = DSBSTATUS_PLAYING, before = timeGetTime(), guard = 0;
    IDirectSoundBuffer_Play(buffer, 0, 0, 0);
    while ((status & DSBSTATUS_PLAYING) && guard++ < 2000)
    {
        Sleep(5);
        IDirectSoundBuffer_GetStatus(buffer, &status);
    }
    before = timeGetTime() - before;
    Sleep(250);   /* silence between two sounds */
    return before;
}

static BOOL CALLBACK CountDevices(LPGUID guid, LPCSTR description, LPCSTR module, LPVOID context)
{
    (void)module;
    if (!guid && description && description[0]) ++*(int *)context;
    return TRUE;
}

int main(int argc, char **argv)
{
    WNDCLASSA klass;
    IDirectSound *ds = NULL;
    IDirectSoundBuffer *primary = NULL, *again = NULL, *tone = NULL, *copy = NULL, *plain = NULL, *stream = NULL;
    DSCAPS caps;
    DSBCAPS bcaps;
    DSBUFFERDESC desc;
    WAVEFORMATEX format, read;
    void *first = NULL, *second = NULL, *other = NULL;
    DWORD first_bytes = 0, second_bytes = 0, play = 0, write = 0, status = 0, value = 0, took;
    LONG level = 1;
    int i, devices = 0;
    static const GUID stranger = {0x12345678, 1, 2, {3, 4, 5, 6, 7, 8, 9, 10}};

    if (argc > 1) report = fopen(argv[1], "w");
    memset(&klass, 0, sizeof klass);
    klass.lpfnWndProc = DefWindowProcA;
    klass.hInstance = GetModuleHandleA(NULL);
    klass.lpszClassName = "Arm64DirectSoundFixture";
    RegisterClassA(&klass);
    window = CreateWindowExA(0, klass.lpszClassName, "DirectSound", WS_POPUP, 10, 10, 100, 100, NULL, NULL, klass.hInstance, NULL);
    /* DirectSound gives the sound to the program whose window has the focus. */
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    {
        MSG message;
        while (PeekMessageA(&message, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&message);
    }

    CHECK_HR(DirectSoundCreate(&stranger, &ds, NULL), DSERR_NODRIVER, "DirectSoundCreate with a strange device");
    CHECK_HR(DirectSoundEnumerateA(CountDevices, &devices), DS_OK, "DirectSoundEnumerate");
    CHECK(devices == 1, "one device");
    CHECK_HR(DirectSoundCreate(NULL, &ds, NULL), DS_OK, "DirectSoundCreate");
    if (!ds)
    {
        Fail(__LINE__, 0);
        Report();
        return 1;
    }
    memset(&desc, 0, sizeof desc);
    desc.dwSize = sizeof desc;
    desc.dwFlags = DSBCAPS_PRIMARYBUFFER;
    /* DirectSound hands out the primary buffer before a cooperative level is set. */
    CHECK_HR(IDirectSound_CreateSoundBuffer(ds, &desc, &primary, NULL), DS_OK, "the primary buffer before SetCooperativeLevel");
    if (primary) IDirectSoundBuffer_Release(primary);
    primary = NULL;
    CHECK_HR(IDirectSound_SetCooperativeLevel(ds, NULL, DSSCL_NORMAL), DSERR_INVALIDPARAM, "SetCooperativeLevel without a window");
    CHECK_HR(IDirectSound_SetCooperativeLevel(ds, window, 9), DSERR_INVALIDPARAM, "a level that does not exist");
    CHECK_HR(IDirectSound_SetCooperativeLevel(ds, window, DSSCL_NORMAL), DS_OK, "SetCooperativeLevel");
    memset(&caps, 0, sizeof caps);
    caps.dwSize = sizeof caps;
    CHECK_HR(IDirectSound_GetCaps(ds, &caps), DS_OK, "GetCaps");
    CHECK((caps.dwFlags & DSCAPS_SECONDARY16BIT) && (caps.dwFlags & DSCAPS_CONTINUOUSRATE) && !(caps.dwFlags & DSCAPS_EMULDRIVER) &&
          caps.dwPrimaryBuffers == 1 && caps.dwMaxHwMixingAllBuffers >= 16 && caps.dwFreeHwMixingAllBuffers == caps.dwMaxHwMixingAllBuffers,
          "a card with a driver that mixes many buffers");
    caps.dwSize = 12;
    CHECK_HR(IDirectSound_GetCaps(ds, &caps), DSERR_INVALIDPARAM, "GetCaps with a wrong size");
    CHECK_HR(IDirectSound_GetSpeakerConfig(ds, &value), DS_OK, "GetSpeakerConfig");
    CHECK(value == (DSSPEAKER_STEREO | 20u << 16), "stereo speakers, twenty degrees apart");
    CHECK_HR(IDirectSound_Compact(ds), DSERR_PRIOLEVELNEEDED, "Compact at the normal level");

    /* The primary buffer: its format is a wish of a program with priority. */
    CHECK_HR(IDirectSound_CreateSoundBuffer(ds, &desc, &primary, NULL), DS_OK, "the primary buffer");
    CHECK_HR(IDirectSound_CreateSoundBuffer(ds, &desc, &again, NULL), DS_OK, "the primary buffer again");
    CHECK(primary && again == primary, "there is one primary buffer");
    if (again) IDirectSoundBuffer_Release(again);
    Format(&format, 22050, 1, 8);
    if (primary)
    {
        CHECK_HR(IDirectSoundBuffer_SetFormat(primary, &format), DSERR_PRIOLEVELNEEDED, "SetFormat at the normal level");
        CHECK_HR(IDirectSound_SetCooperativeLevel(ds, window, DSSCL_PRIORITY), DS_OK, "the priority level");
        CHECK_HR(IDirectSoundBuffer_SetFormat(primary, &format), DS_OK, "SetFormat with priority");
        memset(&read, 0, sizeof read);
        CHECK_HR(IDirectSoundBuffer_GetFormat(primary, &read, sizeof read, &value), DS_OK, "GetFormat of the primary buffer");
        CHECK(read.nSamplesPerSec == 22050 && read.nChannels == 1 && read.wBitsPerSample == 8, "the format that was set");
        format.wFormatTag = 2;
        CHECK_HR(IDirectSoundBuffer_SetFormat(primary, &format), DSERR_BADFORMAT, "a format that is not PCM");
        CHECK_HR(IDirectSoundBuffer_Lock(primary, 0, 16, &first, &first_bytes, NULL, NULL, 0), DSERR_PRIOLEVELNEEDED,
                 "Lock of the primary buffer without DSSCL_WRITEPRIMARY");
        CHECK_HR(IDirectSoundBuffer_Play(primary, 0, 0, 0), DSERR_INVALIDPARAM, "the primary buffer only loops");
        CHECK_HR(IDirectSoundBuffer_Play(primary, 0, 0, DSBPLAY_LOOPING), DS_OK, "Play of the primary buffer");
        CHECK_HR(IDirectSoundBuffer_GetStatus(primary, &status), DS_OK, "GetStatus of the primary buffer");
        CHECK(status == (DSBSTATUS_PLAYING | DSBSTATUS_LOOPING), "it plays what the card mixes");
        CHECK_HR(IDirectSoundBuffer_SetCurrentPosition(primary, 0), DSERR_INVALIDCALL, "SetCurrentPosition of the primary buffer");
    }

    /* What CreateSoundBuffer refuses. */
    Format(&format, 44100, 2, 16);
    memset(&desc, 0, sizeof desc);
    desc.dwSize = sizeof desc;
    desc.dwBufferBytes = 44100;
    CHECK_HR(IDirectSound_CreateSoundBuffer(ds, &desc, &tone, NULL), DSERR_INVALIDPARAM, "a buffer without a format");
    desc.lpwfxFormat = &format;
    desc.dwBufferBytes = 44101;
    CHECK_HR(IDirectSound_CreateSoundBuffer(ds, &desc, &tone, NULL), DS_OK, "a size that is no whole frame");
    if (tone)
    {
        memset(&bcaps, 0, sizeof bcaps);
        bcaps.dwSize = sizeof bcaps;
        IDirectSoundBuffer_GetCaps(tone, &bcaps);
        CHECK(bcaps.dwBufferBytes == 44104, "it grows to the next frame");
        IDirectSoundBuffer_Release(tone);
        tone = NULL;
    }
    desc.dwBufferBytes = 44100;
    desc.dwFlags = DSBCAPS_CTRL3D;
    CHECK_HR(IDirectSound_CreateSoundBuffer(ds, &desc, &tone, NULL), DS_OK, "a 3D buffer");
    if (tone) { IDirectSoundBuffer_Release(tone); tone = NULL; }
    desc.dwFlags = DSBCAPS_CTRLPOSITIONNOTIFY;
    CHECK_HR(IDirectSound_CreateSoundBuffer(ds, &desc, &tone, NULL), DS_OK, "a buffer with position notifications");
    if (tone) { IDirectSoundBuffer_Release(tone); tone = NULL; }
    desc.dwFlags = 0;
    desc.dwSize = sizeof(DSBUFFERDESC1);
    CHECK_HR(IDirectSound_CreateSoundBuffer(ds, &desc, &plain, NULL), DS_OK, "a buffer by the description before DirectX 7");
    if (plain)
    {
        CHECK_HR(IDirectSoundBuffer_SetVolume(plain, -100), DSERR_CONTROLUNAVAIL, "SetVolume without DSBCAPS_CTRLVOLUME");
        CHECK_HR(IDirectSoundBuffer_GetPan(plain, &level), DSERR_CONTROLUNAVAIL, "GetPan without DSBCAPS_CTRLPAN");
        CHECK_HR(IDirectSoundBuffer_SetFrequency(plain, 22050), DSERR_CONTROLUNAVAIL, "SetFrequency without DSBCAPS_CTRLFREQUENCY");
        CHECK_HR(IDirectSoundBuffer_SetFormat(plain, &format), DSERR_INVALIDCALL, "SetFormat of a secondary buffer");
        CHECK(IDirectSoundBuffer_Release(plain) == 0, "the buffer goes");
    }

    /* 1 to 4: a static buffer, a quarter of a second of a square wave. */
    tone = MakeBuffer(ds, DSBCAPS_STATIC | DSBCAPS_CTRLPAN | DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLFREQUENCY | DSBCAPS_GLOBALFOCUS, 44100,
                      44100, 2, 16);
    CHECK(tone != NULL, "a static buffer");
    if (tone)
    {
        memset(&bcaps, 0, sizeof bcaps);
        bcaps.dwSize = sizeof bcaps;
        CHECK_HR(IDirectSoundBuffer_GetCaps(tone, &bcaps), DS_OK, "caps of the buffer");
        CHECK(bcaps.dwBufferBytes == 44100 && (bcaps.dwFlags & DSBCAPS_STATIC) && (bcaps.dwFlags & DSBCAPS_LOCHARDWARE), "its size and place");
        value = 0;
        CHECK_HR(IDirectSoundBuffer_GetFormat(tone, NULL, 0, &value), DS_OK, "the size of the format");
        CHECK(value == sizeof(WAVEFORMATEX), "eighteen bytes");
        CHECK_HR(IDirectSoundBuffer_Lock(tone, 0, 0, &first, &first_bytes, NULL, NULL, DSBLOCK_ENTIREBUFFER), DS_OK, "Lock of the whole buffer");
        CHECK(first && first_bytes == 44100, "one stretch of the whole size");
        if (first)
        {
            short *samples = (short *)first;
            for (i = 0; i < 11025; ++i) samples[i * 2] = samples[i * 2 + 1] = (i / 50) & 1 ? -30000 : 30000;
            CHECK_HR(IDirectSoundBuffer_Unlock(tone, first, first_bytes, NULL, 0), DS_OK, "Unlock");
        }
        CHECK_HR(IDirectSoundBuffer_Unlock(tone, (BYTE *)first - 4, 4, NULL, 0), DS_OK, "Unlock does not look at what it is given");
        /* A stretch across the end comes in two parts. */
        CHECK_HR(IDirectSoundBuffer_Lock(tone, 44000, 400, &other, &first_bytes, &second, &second_bytes, 0), DS_OK, "Lock across the end");
        CHECK(other == (BYTE *)first + 44000 && first_bytes == 100 && second == first && second_bytes == 300, "the two parts of the ring");
        IDirectSoundBuffer_Unlock(tone, other, first_bytes, second, second_bytes);
        CHECK_HR(IDirectSoundBuffer_Lock(tone, 44100, 4, &other, &first_bytes, NULL, NULL, 0), DSERR_INVALIDPARAM, "Lock beyond the end");
        CHECK_HR(IDirectSoundBuffer_GetStatus(tone, &status), DS_OK, "GetStatus");
        CHECK(status == 0, "it does not play yet");
        CHECK_HR(IDirectSoundBuffer_GetCurrentPosition(tone, &play, &write), DS_OK, "GetCurrentPosition");
        CHECK(play == 0 && write == 0, "it stands at its begin");
        CHECK_HR(IDirectSoundBuffer_Play(tone, 0, 0, 2), DSERR_INVALIDPARAM, "Play with an unknown flag");

        /* How long after Play does the cursor move? The device has been
           running since DirectSoundCreate. */
        Sleep(300);
        took = timeGetTime();
        CHECK_HR(IDirectSoundBuffer_Play(tone, 0, 0, 0), DS_OK, "Play");
        value = timeGetTime() - took;
        for (i = 0; i < 2000; ++i)
        {
            IDirectSoundBuffer_GetCurrentPosition(tone, &play, &write);
            if (play) break;
            Sleep(1);
        }
        Say("MEASURE play_call_ms=%lu play_to_cursor_ms=%lu polls=%d", (unsigned long)value, (unsigned long)(timeGetTime() - took), i);
        IDirectSoundBuffer_GetStatus(tone, &status);
        CHECK(status == DSBSTATUS_PLAYING && play > 0 && play % 4 == 0, "it plays, the cursor on a frame");
        {
            DWORD last = play, backwards = 0, guard = 0;
            while ((status & DSBSTATUS_PLAYING) && guard++ < 2000)
            {
                Sleep(5);
                IDirectSoundBuffer_GetCurrentPosition(tone, &play, &write);
                IDirectSoundBuffer_GetStatus(tone, &status);
                if ((status & DSBSTATUS_PLAYING) && play < last) ++backwards;
                last = play;
            }
            CHECK(backwards == 0, "the cursor only goes forward");
        }
        took = timeGetTime() - took;
        CHECK(took >= 200 && took < 600, "a quarter of a second of sound takes that long");
        IDirectSoundBuffer_GetCurrentPosition(tone, &play, &write);
        CHECK(play == 0, "at its end the buffer stands at its begin again");
        Sleep(250);

        CHECK_HR(IDirectSoundBuffer_SetPan(tone, 10001), DSERR_INVALIDPARAM, "a pan beyond the right");
        CHECK_HR(IDirectSoundBuffer_SetPan(tone, DSBPAN_LEFT), DS_OK, "SetPan");
        CHECK_HR(IDirectSoundBuffer_GetPan(tone, &level), DS_OK, "GetPan");
        CHECK(level == DSBPAN_LEFT, "the pan that was set");
        PlayToEnd(tone);
        IDirectSoundBuffer_SetPan(tone, DSBPAN_CENTER);
        CHECK_HR(IDirectSoundBuffer_SetVolume(tone, 1), DSERR_INVALIDPARAM, "a volume above full");
        CHECK_HR(IDirectSoundBuffer_SetVolume(tone, -600), DS_OK, "SetVolume");
        CHECK_HR(IDirectSoundBuffer_GetVolume(tone, &level), DS_OK, "GetVolume");
        CHECK(level == -600, "the volume that was set");
        PlayToEnd(tone);
        IDirectSoundBuffer_SetVolume(tone, DSBVOLUME_MAX);
        CHECK_HR(IDirectSoundBuffer_SetFrequency(tone, 50), DSERR_INVALIDPARAM, "a frequency below the lowest");
        CHECK_HR(IDirectSoundBuffer_SetFrequency(tone, 88200), DS_OK, "SetFrequency");
        CHECK_HR(IDirectSoundBuffer_GetFrequency(tone, &value), DS_OK, "GetFrequency");
        CHECK(value == 88200, "the frequency that was set");
        took = PlayToEnd(tone);
        CHECK(took >= 90 && took < 350, "at double frequency it is over in half the time");
        CHECK_HR(IDirectSoundBuffer_SetFrequency(tone, DSBFREQUENCY_ORIGINAL), DS_OK, "the frequency of the format");
        IDirectSoundBuffer_GetFrequency(tone, &value);
        CHECK(value == 44100, "44.100 Hz again");
        /* SetCurrentPosition and Stop: the cursor stays where it was stopped. */
        CHECK_HR(IDirectSoundBuffer_SetCurrentPosition(tone, 44100), DSERR_INVALIDPARAM, "a position beyond the end");
        CHECK_HR(IDirectSoundBuffer_SetCurrentPosition(tone, 20000), DS_OK, "SetCurrentPosition");
        IDirectSoundBuffer_GetCurrentPosition(tone, &play, NULL);
        CHECK(play == 20000, "the position that was set");
        IDirectSoundBuffer_SetVolume(tone, DSBVOLUME_MIN);
        IDirectSoundBuffer_Play(tone, 0, 0, DSBPLAY_LOOPING);
        Sleep(60);
        IDirectSoundBuffer_GetStatus(tone, &status);
        CHECK(status == (DSBSTATUS_PLAYING | DSBSTATUS_LOOPING), "a looping buffer says so");
        CHECK_HR(IDirectSoundBuffer_Stop(tone), DS_OK, "Stop");
        IDirectSoundBuffer_GetCurrentPosition(tone, &play, NULL);
        IDirectSoundBuffer_GetStatus(tone, &status);
        CHECK(status == 0 && play > 20000 && play < 44100, "it stopped where it was");
        Sleep(30);
        IDirectSoundBuffer_GetCurrentPosition(tone, &value, NULL);
        CHECK(value == play, "and stays there");
        IDirectSoundBuffer_SetVolume(tone, DSBVOLUME_MAX);
        IDirectSoundBuffer_SetCurrentPosition(tone, 0);
        Sleep(250);
    }

    /* 5: a stream. A second of 16 bit mono at 22.050 Hz, looping, kept
       written a tenth of a second beyond the write cursor, the way a game
       feeds its mixer into DirectSound. The values are a ramp, so the test
       finds every frame in what the mixer rendered. */
    stream = MakeBuffer(ds, DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_GLOBALFOCUS, 44100, 22050, 1, 16);
    CHECK(stream != NULL, "a streaming buffer");
    if (stream)
    {
        const DWORD bytes = 44100, ahead = 4410;
        DWORD position = 0, frame = 0, begun, overtaken = 0, rounds = 0;
        DWORD lead_low = ~0u, lead_high = 0, step_high = 0, last_play = 0, margin_low = ~0u;
        CHECK_HR(IDirectSoundBuffer_Play(stream, 0, 0, DSBPLAY_LOOPING), DS_OK, "Play of the stream");
        begun = timeGetTime();
        while (timeGetTime() - begun < 2500)
        {
            DWORD target, count, lead, step;
            if (IDirectSoundBuffer_GetCurrentPosition(stream, &play, &write) != DS_OK) break;
            ++rounds;
            lead = (write + bytes - play) % bytes;
            if (lead < lead_low) lead_low = lead;
            if (lead > lead_high) lead_high = lead;
            step = (play + bytes - last_play) % bytes;
            if (rounds > 1 && step > step_high) step_high = step;
            last_play = play;
            /* What is written and not yet played, less the stretch the card may take any moment. */
            if (rounds > 1)
            {
                const DWORD written = (position + bytes - play) % bytes;
                if (written < lead) ++overtaken;
                else if (written - lead < margin_low) margin_low = written - lead;
            }
            if ((position + bytes - write) % bytes > ahead * 4) position = write;   /* fell behind: start anew */
            target = (write + ahead) % bytes;
            count = (target + bytes - position) % bytes;
            count -= count % 2;
            if (count && IDirectSoundBuffer_Lock(stream, position, count, &first, &first_bytes, &second, &second_bytes, 0) == DS_OK)
            {
                short *out = (short *)first;
                DWORD n;
                for (n = 0; n < first_bytes / 2; ++n, ++frame) out[n] = (short)((frame % 6000) * 4);
                out = (short *)second;
                for (n = 0; n < second_bytes / 2; ++n, ++frame) out[n] = (short)((frame % 6000) * 4);
                IDirectSoundBuffer_Unlock(stream, first, first_bytes, second, second_bytes);
                position = (position + count) % bytes;
            }
            Sleep(5);
        }
        CHECK_HR(IDirectSoundBuffer_Stop(stream), DS_OK, "Stop of the stream");
        CHECK(overtaken == 0, "the play cursor never reached what was not yet written");
        CHECK(frame > 44100 && frame < 70000, "about two and a half seconds were written");
        /* Bytes of 16 bit mono at 22.050 Hz: 44,1 a millisecond. */
        Say("MEASURE stream rounds=%lu frames=%lu overtaken=%lu", (unsigned long)rounds, (unsigned long)frame, (unsigned long)overtaken);
        Say("MEASURE write_lead_ms=%lu..%lu cursor_step_ms=%lu margin_ms=%lu", (unsigned long)(lead_low * 10 / 441),
            (unsigned long)(lead_high * 10 / 441), (unsigned long)(step_high * 10 / 441), (unsigned long)(margin_low * 10 / 441));
        Sleep(250);
    }

    /* 6: a duplicate plays the same bytes with a volume of its own. */
    if (tone)
    {
        CHECK_HR(IDirectSound_DuplicateSoundBuffer(ds, tone, &copy), DS_OK, "DuplicateSoundBuffer");
        if (copy)
        {
            CHECK_HR(IDirectSoundBuffer_Lock(copy, 0, 16, &other, &first_bytes, NULL, NULL, 0), DS_OK, "Lock of the duplicate");
            CHECK(other != NULL, "it has memory");
            {
                void *original = NULL;
                DWORD n = 0;
                IDirectSoundBuffer_Lock(tone, 0, 16, &original, &n, NULL, NULL, 0);
                CHECK(original == other, "the duplicate shares the memory of the original");
                IDirectSoundBuffer_Unlock(tone, original, n, NULL, 0);
            }
            IDirectSoundBuffer_Unlock(copy, other, first_bytes, NULL, 0);
            IDirectSoundBuffer_SetVolume(copy, -1200);
            IDirectSoundBuffer_GetVolume(tone, &level);
            CHECK(level == DSBVOLUME_MAX, "the volume of the original is its own");
            /* Both at once: two voices of the mixer. */
            IDirectSoundBuffer_Play(tone, 0, 0, 0);
            IDirectSoundBuffer_Play(copy, 0, 0, 0);
            memset(&caps, 0, sizeof caps);
            caps.dwSize = sizeof caps;
            IDirectSound_GetCaps(ds, &caps);
            CHECK(caps.dwFreeHwMixingAllBuffers + 2 == caps.dwMaxHwMixingAllBuffers, "two voices sound");
            Sleep(400);
            CHECK(IDirectSoundBuffer_Release(copy) == 0, "the duplicate goes");
        }
    }

    /* A program that writes the primary buffer itself. */
    if (primary)
    {
        CHECK_HR(IDirectSound_SetCooperativeLevel(ds, window, DSSCL_WRITEPRIMARY), DS_OK, "DSSCL_WRITEPRIMARY");
        first = NULL;
        CHECK_HR(IDirectSoundBuffer_Lock(primary, 0, 0, &first, &first_bytes, NULL, NULL, DSBLOCK_ENTIREBUFFER), DS_OK,
                 "Lock of the primary buffer by its writer");
        CHECK(first != NULL && first_bytes >= 4096, "it has memory of the card");
        if (first)
        {
            memset(first, 0x80, first_bytes);   /* silence at eight bits */
            IDirectSoundBuffer_Unlock(primary, first, first_bytes, NULL, 0);
        }
        CHECK_HR(IDirectSoundBuffer_Play(primary, 0, 0, DSBPLAY_LOOPING), DS_OK, "Play of the written primary buffer");
        Sleep(60);
        CHECK_HR(IDirectSoundBuffer_GetCurrentPosition(primary, &play, &write), DS_OK, "its cursors");
        CHECK(play != write, "the write cursor leads");
        CHECK_HR(IDirectSoundBuffer_Stop(primary), DS_OK, "Stop of the primary buffer");
    }

    if (stream) IDirectSoundBuffer_Release(stream);
    if (primary) IDirectSoundBuffer_Release(primary);
    CHECK(IDirectSound_Release(ds) == 0, "the DirectSound object goes");
    /* Its buffers went with it; under Windows the pointer `tone` is no
       object any more, so nothing is called on it. */
    DestroyWindow(window);
    Report();
    if (report) fclose(report);
    return failures ? 1 : 0;
}
