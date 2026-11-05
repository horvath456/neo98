/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Guest side of tools/test_dsound_stream_guest.py: a streaming buffer of
   DirectSound, refilled by GetCurrentPosition, over the original DSOUND.DLL of
   the tree.

   This is what a game does that mixes its own sound: one looping secondary
   buffer of a second, a thread that asks for the cursors every few
   milliseconds and writes up to a tenth of a second ahead of the write
   cursor. The contract it depends on is the one of the DirectSound
   documentation: the write cursor is ahead of the play cursor by the stretch
   that can no longer be changed, and what is written behind it is heard.

   Four passes, three seconds each: the first with the flags of a program that
   knows only the first DirectSound (DSBCAPS_GLOBALFOCUS), the second with
   DSBCAPS_GETCURRENTPOSITION2 added, the third with DSBCAPS_LOCSOFTWARE on top,
   which keeps the buffer in the software mixer of DirectSound, and the fourth
   on the primary buffer itself with DSSCL_WRITEPRIMARY. Each pass reports how the cursors moved;
   what it writes is a ramp, 16 a frame and 2000 frames long, so the runner
   finds every frame again in what the mixer rendered: a gap in the ramp is an
   underrun, and the place of a frame against the time it was written is the
   latency. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U LoadLibraryA(const char *);
API U GetProcAddress(U, const char *);
API void OutputDebugStringA(const char *);
API void ExitProcess(U);
API void Sleep(U);
API U timeGetTime(void);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U ShowWindow(U, U);
API U SetForegroundWindow(U);

typedef U(__stdcall *Create)(void *, void **, void *);
typedef U(__stdcall *Buffer2)(void *, void *, void **, void *);
typedef U(__stdcall *One)(void *, void *);
typedef U(__stdcall *Level)(void *, U, U);
typedef U(__stdcall *Lock)(void *, U, U, void **, U *, void **, U *, U);
typedef U(__stdcall *Unlock)(void *, void *, U, void *, U);
typedef U(__stdcall *Play)(void *, U, U, U);
typedef U(__stdcall *Cursor)(void *, U *, U *);
typedef U(__stdcall *Nothing)(void *);
typedef U(__stdcall *Format)(void *, void *);

static char line[256];
static U put(U at, const char *text)
{
    while (*text) line[at++] = *text++;
    return at;
}
static U hex(U at, U value)
{
    static const char digits[] = "0123456789abcdef";
    U i;
    for (i = 0; i < 8; ++i) line[at++] = digits[(value >> (28 - i * 4)) & 15];
    return at;
}
static void number(const char *prefix, const char *label, U value)
{
    U at = put(0, "DSTREAM ");
    at = put(at, prefix);
    at = put(at, label);
    line[at++] = '=';
    at = hex(at, value);
    line[at] = 0;
    OutputDebugStringA(line);
}
static void *entry(void *object, U offset)
{
    U *vtable = *(U **)object;
    return (void *)vtable[offset / 4];
}
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
static U description[5];
static U capabilities[24];
static U window_class[10];
static U __stdcall procedure(U window, U message, U first, U second)
{
    return DefWindowProcA(window, message, first, second);
}

/* The secondary buffers are a second of 16-bit mono at 44.1 kHz, and a tenth
   of a second is the stretch this program keeps written behind the write
   cursor. */
#define RATE 44100u
#define RAMP 2000u
#define READINGS 1024u

static U moves[READINGS], gaps[READINGS];
static void sort(U *values, U count)
{
    U i, j;
    for (i = 1; i < count; ++i)
    {
        const U value = values[i];
        for (j = i; j && values[j - 1] > value; --j) values[j] = values[j - 1];
        values[j] = value;
    }
}

/* One pass over `buffer`, `total` long with frames of `frame_bytes`, kept
   written `ahead` bytes behind the write cursor. */
static void stream(void *buffer, const char *prefix, U total, U frame_bytes, U ahead)
{
    void *first = 0, *second = 0;
    U first_bytes = 0, second_bytes = 0, value, i;
    U play = 0, write = 0, previous = 0, started, now, moved_at;
    U ours = 0, frame = 0, rounds = 0, count = 0, resyncs = 0, failures = 0;
    U lead_low = 0xffffffff, lead_high = 0, marked = 0, read_at = 0, jump = 0, late = 0;
    const U rate_bytes = RATE * frame_bytes;
    int have_previous = 0, streaming = 0;

    value = ((Lock)entry(buffer, 0x2c))(buffer, 0, total, &first, &first_bytes, &second, &second_bytes, 0);
    if (value || !first) ExitProcess(21);
    for (i = 0; i < first_bytes; ++i) ((char *)first)[i] = 0;
    ((Unlock)entry(buffer, 0x4c))(buffer, first, first_bytes, second, second_bytes);
    value = ((Play)entry(buffer, 0x30))(buffer, 0, 0, 1); /* DSBPLAY_LOOPING */
    number(prefix, "play", value);
    if (value) ExitProcess(22);

    started = timeGetTime();
    moved_at = started;
    for (;;)
    {
        U lead, target, bytes;
        now = timeGetTime();
        if (now - started >= 3000) break;
        ++rounds;
        if (((Cursor)entry(buffer, 0x10))(buffer, &play, &write) || play >= total || write >= total)
        {
            ++failures;
            Sleep(5);
            continue;
        }
        /* A round that comes later than half of what is kept written: the
           machine held this thread up, and no cursor can make up for that. */
        if (have_previous && (now - read_at) * (rate_bytes / 100) / 10 > ahead / 2) ++late;
        if (have_previous && play != previous)
        {
            /* How far the cursor went beyond the time that passed since the
               reading before: a cursor that follows the sound moves by what
               was played, one that jumps a block at a time moves by a block
               within one round. 88.2 bytes are a millisecond. */
            const U moved = (play + total - previous) % total;
            const U due = (now - read_at) * (rate_bytes / 100) / 10;
            if (moved > due && moved - due > jump) jump = moved - due;
            if (count < READINGS)
            {
                moves[count] = moved;
                gaps[count] = now - moved_at;
                ++count;
            }
            moved_at = now;
        }
        previous = play;
        read_at = now;
        have_previous = 1;
        lead = (write + total - play) % total;
        if (lead < lead_low) lead_low = lead;
        if (lead > lead_high) lead_high = lead;

        /* Write from where this program stopped up to a tenth of a second
           behind the write cursor. A lead beyond three quarters of the buffer means the
           cursor has overtaken the data, and the stream starts again behind
           it. */
        if (!streaming) { ours = write; streaming = 1; }
        if ((ours + total - write) % total > total / 4 * 3) { ours = write; ++resyncs; }
        target = (write + ahead) % total;
        bytes = (target + total - ours) % total;
        bytes -= bytes % frame_bytes;
        if (bytes && bytes <= total / 4 * 3)
        {
            first = second = 0;
            first_bytes = second_bytes = 0;
            if (!((Lock)entry(buffer, 0x2c))(buffer, ours, bytes, &first, &first_bytes, &second, &second_bytes, 0))
            {
                /* Every channel of a frame carries the value of the frame. */
                const U lanes = frame_bytes / 2;
                short *out = (short *)first;
                for (i = 0; i < first_bytes / 2; ++i)
                {
                    out[i] = (short)(16 + 16 * (frame % RAMP));
                    if (i % lanes == lanes - 1) ++frame;
                }
                out = (short *)second;
                for (i = 0; out && i < second_bytes / 2; ++i)
                {
                    out[i] = (short)(16 + 16 * (frame % RAMP));
                    if (i % lanes == lanes - 1) ++frame;
                }
                ((Unlock)entry(buffer, 0x4c))(buffer, first, first_bytes, second, second_bytes);
                /* One mark every fifth of a second: the number of the first
                   frame this round wrote and how many frames it lies behind
                   the write cursor. The line goes out the moment the data is
                   in the buffer; the runner looks the frame up in what the
                   mixer rendered and has the latency. */
                if (now - marked >= 200)
                {
                    U at = put(0, "DSTREAM ");
                    at = put(at, prefix);
                    at = put(at, "mark=");
                    at = hex(at, frame - bytes / frame_bytes);
                    line[at++] = ',';
                    at = hex(at, ((ours + total - write) % total) / frame_bytes);
                    line[at] = 0;
                    OutputDebugStringA(line);
                    marked = now;
                }
                ours = (ours + bytes) % total;
            }
            else ++failures;
        }
        Sleep(5);
    }
    ((Nothing)entry(buffer, 0x48))(buffer); /* Stop */

    number(prefix, "bytes", total);
    number(prefix, "rounds", rounds);
    number(prefix, "failures", failures);
    number(prefix, "resyncs", resyncs);
    number(prefix, "late", late);
    number(prefix, "frames", frame);
    number(prefix, "moves", count);
    sort(moves, count);
    sort(gaps, count);
    number(prefix, "step_median", count ? moves[count / 2] : 0);
    number(prefix, "step_max", count ? moves[count - 1] : 0);
    number(prefix, "jump_max", jump);
    number(prefix, "gap_median", count ? gaps[count / 2] : 0);
    number(prefix, "gap_max", count ? gaps[count - 1] : 0);
    number(prefix, "lead_min", lead_low);
    number(prefix, "lead_max", lead_high);
    /* Half a second of silence, so the runner sees the passes apart. */
    Sleep(500);
}

/* A secondary buffer of a second, 16-bit mono, with `flags`. */
static void pass(void *sound, const char *prefix, U flags)
{
    void *buffer = 0;
    U value;
    wave(RATE, 16, 1);
    description[0] = sizeof description;
    description[1] = flags;
    description[2] = RATE * 2;
    description[3] = 0;
    description[4] = (U)format;
    value = ((Buffer2)entry(sound, 0x0c))(sound, description, &buffer, 0);
    number(prefix, "buffer", value);
    if (value || !buffer) ExitProcess(20);
    stream(buffer, prefix, RATE * 2, 2, RATE / 10 * 2);
    ((Nothing)entry(buffer, 8))(buffer);
}

void __stdcall mainCRTStartup(void)
{
    U library, value, window;
    void *sound = 0, *primary = 0;
    Create create;

    library = LoadLibraryA("DSOUND.DLL");
    if (!library) ExitProcess(1);
    create = (Create)GetProcAddress(library, "DirectSoundCreate");
    if (!create) ExitProcess(2);
    value = create(0, &sound, 0);
    number("", "create", value);
    if (value || !sound) ExitProcess(3);
    capabilities[0] = sizeof capabilities;
    value = ((One)entry(sound, 0x10))(sound, capabilities);
    number("", "caps_flags", capabilities[1]);

    window_class[1] = (U)procedure;
    window_class[9] = (U) "NEO98DirectSoundStream";
    RegisterClassA(window_class);
    window = CreateWindowExA(0, "NEO98DirectSoundStream", "NEO98 DirectSound stream", 0x10000000,
                             0, 0, 320, 200, 0, 0, 0, 0);
    if (!window) ExitProcess(4);
    ShowWindow(window, 1);
    SetForegroundWindow(window);
    value = ((Level)entry(sound, 0x18))(sound, window, 2); /* DSSCL_PRIORITY */
    number("", "cooperative", value);
    if (value) ExitProcess(5);

    /* The primary buffer in the format of the stream, as a game sets it. */
    description[0] = sizeof description;
    description[1] = 1;
    value = ((Buffer2)entry(sound, 0x0c))(sound, description, &primary, 0);
    if (value || !primary) ExitProcess(6);
    wave(RATE, 16, 2);
    value = ((Format)entry(primary, 0x38))(primary, format);
    number("", "primary_format", value);
    ((Nothing)entry(primary, 8))(primary);

    pass(sound, "old_", 0x8000);            /* DSBCAPS_GLOBALFOCUS */
    pass(sound, "new_", 0x8000 | 0x10000);  /* and DSBCAPS_GETCURRENTPOSITION2 */
    /* DSBCAPS_LOCSOFTWARE: the buffer is mixed by DirectSound itself, also on
       a card that could play it. */
    pass(sound, "soft_", 0x8000 | 0x10000 | 8);

    /* DSSCL_WRITEPRIMARY: the program writes the primary buffer itself, the
       way a program does that brings its whole mixer along. The buffer is as
       long as the card makes it; three eighths of it are kept written. */
    value = ((Level)entry(sound, 0x18))(sound, window, 4);
    number("", "write_primary", value);
    if (!value)
    {
        U caps[5] = {sizeof caps, 0, 0, 0, 0};
        primary = 0;
        description[0] = sizeof description;
        description[1] = 1;
        description[2] = 0;
        description[3] = 0;
        description[4] = 0;
        value = ((Buffer2)entry(sound, 0x0c))(sound, description, &primary, 0);
        if (value || !primary) ExitProcess(30);
        wave(RATE, 16, 2);
        value = ((Format)entry(primary, 0x38))(primary, format);
        number("primary_", "format", value);
        value = ((One)entry(primary, 0x0c))(primary, caps); /* GetCaps */
        if (value || caps[2] < 4096) ExitProcess(31);
        stream(primary, "primary_", caps[2], 4, caps[2] / 8 * 3);
        ((Nothing)entry(primary, 8))(primary);
    }

    ((Nothing)entry(sound, 8))(sound);
    number("", "done", 1);
    ExitProcess(0);
}
