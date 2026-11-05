/* SPDX-License-Identifier: GPL-2.0-or-later
 * An x86 guest over the public waveIn API. The runner sets the test
 * microphone of src/sound_microphone.cpp (NEO98_TEST_MICROPHONE) and passes
 * its kind as the last word of the command line: "sine" is 441 Hz with the
 * peak 25.600, "silence" and "denied" record zeros, "none" is a host without
 * a microphone, "toggle" waits until the host switch takes the device away.
 * The guest records half a second in 8 and 16 bit, mono and
 * stereo, at 11.025 and 44.100 Hz and measures the peak and the zero
 * crossings itself; it goes through the callback kinds, the WHDR flags,
 * Stop, Reset and the position.
 *
 * Every check carries its own exit code, so a failure names itself.
 * */
typedef unsigned U;
#define API __declspec(dllimport)

API void __stdcall ExitProcess(U);
API void __stdcall Sleep(U);
API U __stdcall GetTickCount(void);
API const char *__stdcall GetCommandLineA(void);
API U __stdcall CreateEventA(void *, U, U, const char *);
API U __stdcall WaitForSingleObject(U, U);
API U __stdcall CloseHandle(U);
API U __stdcall GetCurrentThreadId(void);
API void __stdcall OutputDebugStringA(const char *);

struct Message
{
    U window, message, wparam, lparam, time, x, y;
};
API U __stdcall PeekMessageA(struct Message *, U, U, U, U);
API U __stdcall CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
API U __stdcall DestroyWindow(U);

API U __stdcall waveInGetNumDevs(void);
API U __stdcall waveInGetDevCapsA(U, void *, U);
API U __stdcall waveInOpen(U *, U, const void *, U, U, U);
API U __stdcall waveInClose(U);
API U __stdcall waveInPrepareHeader(U, void *, U);
API U __stdcall waveInUnprepareHeader(U, void *, U);
API U __stdcall waveInAddBuffer(U, void *, U);
API U __stdcall waveInStart(U);
API U __stdcall waveInStop(U);
API U __stdcall waveInReset(U);
API U __stdcall waveInGetPosition(U, void *, U);
API U __stdcall waveInGetID(U, U *);
API U __stdcall waveInMessage(U, U, U, U);
API U __stdcall mixerGetDevCapsA(U, void *, U);
API U __stdcall mixerOpen(U *, U, U, U, U);
API U __stdcall mixerClose(U);
API U __stdcall mixerGetID(U, U *, U);
API U __stdcall mixerGetLineInfoA(U, void *, U);
API U __stdcall mixerGetLineControlsA(U, void *, U);
API U __stdcall mixerSetControlDetails(U, void *, U);
API U __stdcall mixerGetControlDetailsA(U, void *, U);

struct WaveFormat
{
    unsigned short tag, channels;
    U rate, average;
    unsigned short align, bits;
    unsigned short extra;
};
struct WaveHeader
{
    unsigned char *data;
    U length, recorded, user, flags, loops;
    void *next;
    U reserved;
};

static void check(int ok, U code)
{
    if (!ok) ExitProcess(code);
}

/* Half a second at 44.100 Hz in 16-bit stereo is the largest recording. */
static unsigned char buffer[2][44100 * 4 / 4];
static unsigned char big[44100 * 2 * 2];

static U seen[16], seen_count, opened, closed;
static void __stdcall Callback(U handle, U message, U instance, U first, U second)
{
    (void)handle;
    (void)second;
    if (instance != 0x5ea50000) return;
    if (message == 0x3be) ++opened;
    if (message == 0x3bf) ++closed;
    if (message == 0x3c0 && seen_count < 16) seen[seen_count++] = first;
}

static void format_of(struct WaveFormat *format, U rate, U bits, U channels)
{
    format->tag = 1;
    format->channels = (unsigned short)channels;
    format->rate = rate;
    format->align = (unsigned short)(channels * bits / 8);
    format->average = rate * format->align;
    format->bits = (unsigned short)bits;
    format->extra = 0;
}
static void header_of(struct WaveHeader *header, unsigned char *data, U length, U user)
{
    header->data = data;
    header->length = length;
    header->recorded = 0;
    header->user = user;
    header->flags = 0;
    header->loops = 0;
    header->next = 0;
    header->reserved = 0;
}
static int sample(const unsigned char *frame, U bits, U lane)
{
    if (bits == 8) return ((int)frame[lane] - 128) * 256;
    return (short)(frame[lane * 2] | (frame[lane * 2 + 1] << 8));
}

/* Peak and zero crossings of what came back; both lanes of a stereo frame
 * must be the same, because the microphone is mono. */
static void measure(const unsigned char *data, U bytes, U bits, U channels, int *peak, U *crossings, U code)
{
    const U frame_bytes = channels * bits / 8;
    U i;
    int previous = sample(data, bits, 0);
    *peak = 0;
    *crossings = 0;
    for (i = 0; i < bytes / frame_bytes; ++i)
    {
        const unsigned char *frame = data + i * frame_bytes;
        const int left = sample(frame, bits, 0);
        if (channels == 2) check(sample(frame, bits, 1) == left, code);
        if (left > *peak) *peak = left;
        if ((previous < 0) != (left < 0)) ++*crossings;
        previous = left;
    }
}

static char mode = 's';
static int last_peak;

/* "WAVEIN <rate> <bits> <channels> peak=<p> crossings=<c>" for the log. */
static char *number(char *at, U value)
{
    char digits[12];
    U n = 0;
    do digits[n++] = (char)('0' + value % 10), value /= 10;
    while (value);
    while (n) *at++ = digits[--n];
    return at;
}
static void tell(U rate, U bits, U channels, int peak, U crossings)
{
    char text[96], *at = text;
    const char *word;
    for (word = "WAVEIN "; *word; ++word) *at++ = *word;
    at = number(at, rate), *at++ = ' ';
    at = number(at, bits), *at++ = ' ';
    at = number(at, channels);
    for (word = " peak="; *word; ++word) *at++ = *word;
    at = number(at, (U)peak);
    for (word = " crossings="; *word; ++word) *at++ = *word;
    at = number(at, crossings);
    *at = 0;
    OutputDebugStringA(text);
}

/* One recording of half a second in two buffers of a quarter each, with a
 * function callback. `base` names the checks of this format. */
static void record(U rate, U bits, U channels, U base)
{
    struct WaveFormat format;
    struct WaveHeader header[2];
    U wave = 0, deadline, i, time[4], quarter;
    int peak = 0;
    U crossings = 0;
    format_of(&format, rate, bits, channels);
    quarter = rate / 4 * format.align;
    seen_count = opened = closed = 0;
    check(waveInOpen(&wave, ~0u, &format, (U)Callback, 0x5ea50000, 0x30000) == 0, base + 0);
    check(opened == 1, base + 1);
    header_of(&header[0], buffer[0], quarter, 1);
    header_of(&header[1], buffer[1], quarter, 2);
    for (i = 0; i < quarter; ++i) buffer[0][i] = buffer[1][i] = 0x55;
    check(waveInPrepareHeader(wave, &header[0], sizeof header[0]) == 0, base + 2);
    check(waveInPrepareHeader(wave, &header[1], sizeof header[1]) == 0, base + 3);
    check(waveInAddBuffer(wave, &header[0], sizeof header[0]) == 0, base + 4);
    check(waveInAddBuffer(wave, &header[1], sizeof header[1]) == 0, base + 5);
    check((header[0].flags & 0x13) == 0x12, base + 6);
    check(waveInStart(wave) == 0, base + 7);
    deadline = GetTickCount() + 20000;
    while (seen_count < 2 && GetTickCount() < deadline) Sleep(5);
    check(seen_count == 2, base + 8);
    check(seen[0] == (U)&header[0] && seen[1] == (U)&header[1], base + 9);
    check((header[0].flags & 0x13) == 3 && (header[1].flags & 0x13) == 3, base + 10);
    check(header[0].recorded == quarter && header[1].recorded == quarter, base + 11);
    time[0] = 4;
    check(waveInGetPosition(wave, time, sizeof time) == 0, base + 12);
    check(time[0] == 4 && time[1] >= 2 * quarter, base + 13);
    check(waveInStop(wave) == 0, base + 14);
    check(waveInReset(wave) == 0, base + 15);
    check(waveInUnprepareHeader(wave, &header[0], sizeof header[0]) == 0, base + 16);
    check(waveInUnprepareHeader(wave, &header[1], sizeof header[1]) == 0, base + 17);
    check(waveInClose(wave) == 0, base + 18);
    check(closed == 1, base + 19);
    /* The two buffers are one stretch of the microphone. */
    for (i = 0; i < quarter; ++i) big[i] = buffer[0][i], big[quarter + i] = buffer[1][i];
    measure(big, 2 * quarter, bits, channels, &peak, &crossings, base + 20);
    tell(rate, bits, channels, peak, crossings);
    last_peak = peak;
    if (mode == 's' && base < 47000)
    {
        /* 25.600 in sixteen bit, 99 or 100 of 128 in eight; the interpolation
         * of 11.025 Hz takes a little off the top. */
        check(peak > 25000 && peak <= 25600, base + 21);
        /* 441 Hz over half a second is 441 zero crossings. */
        check(crossings >= 438 && crossings <= 444, base + 22);
    }
    else if (mode != 's') check(peak == 0 && crossings == 0, base + 23);
}

int mainCRTStartup(void)
{
    const char *line = GetCommandLineA(), *last = line;
    unsigned char caps[0x30];
    struct WaveFormat format;
    struct WaveHeader header[2];
    struct Message message;
    U wave = 0, value = 0, flags = 0, i, deadline, event, window, time[4];

    for (; *line; ++line)
        if (*line == ' ') last = line + 1;
    /* s sine, z silence, d denied, n none */
    mode = last[0] == 's' && last[1] == 'i' && last[2] == 'l' ? 'z' : *last;

    if (mode == 't')
    {
        /* The host switches "Mikrofon in Windows" off while this runs
           (tools/test_microphone_host.py): the device goes away. */
        check(waveInGetNumDevs() == 1, 45008);
        deadline = GetTickCount() + 30000;
        while (waveInGetNumDevs() && GetTickCount() < deadline) Sleep(50);
        check(waveInGetNumDevs() == 0, 45009);
        ExitProcess(0);
    }
    if (mode == 'n')
    {
        /* A host without a microphone: the contract of a machine without a
         * recording device, as before this device existed. */
        check(waveInGetNumDevs() == 0, 45000);
        check(waveInGetDevCapsA(0, caps, sizeof caps) == 2, 45001);
        check(waveInGetDevCapsA(~0u, caps, sizeof caps) == 6, 45002);
        format_of(&format, 11025, 8, 1);
        check(waveInOpen(&wave, 0, &format, 0, 0, 0) == 2, 45003);
        check(waveInOpen(&wave, ~0u, &format, 0, 0, 0) == 6, 45004);
        check(waveInMessage(~0u, 0x2015, (U)&value, (U)&flags) == 6, 45005);
        /* No recording destination either. */
        check(mixerGetDevCapsA(0, caps, 0x30) == 0 && *(U *)(caps + 8 + 32 + 4) == 1, 45006);
        check(mixerGetID(0, &value, 0x20000000u) == 2, 45007);
        ExitProcess(0);
    }

    check(waveInGetNumDevs() == 1, 45010);
    /* size zero first, then the buffer, then the device. */
    check(waveInGetDevCapsA(0, caps, 0) == 0, 45011);
    check(waveInGetDevCapsA(0, 0, sizeof caps) == 11, 45012);
    check(waveInGetDevCapsA(1, caps, sizeof caps) == 2, 45013);
    for (i = 0; i < sizeof caps; ++i) caps[i] = 0xcc;
    check(waveInGetDevCapsA(0, caps, sizeof caps) == 0, 45014);
    check(caps[8] == 'A' && caps[14] == 'W' && caps[19] == 'I' && caps[20] == 'n' && caps[21] == 0, 45015);
    check(*(U *)(caps + 0x28) == 0xfff, 45016);
    check(*(unsigned short *)(caps + 0x2c) == 2, 45017);
    check(*(unsigned short *)(caps + 0x2e) == 0, 45018);
    check(waveInGetDevCapsA(~0u, caps, sizeof caps) == 0, 45019);

    /* The mapper names its preferred device: the "SB16 Wave In [220]" of the
     * image is no device here, so device zero and no PreferredOnly. */
    value = flags = 0x77;
    check(waveInMessage(~0u, 0x2015, (U)&value, (U)&flags) == 0, 45020);
    check(value == 0 && flags == 0, 45021);
    check(waveInMessage(0, 0x805, 0, 0) == 0, 45022);
    check(waveInMessage(0, 0x800, 0, 0) == 8, 45023);

    /* WAVE_FORMAT_QUERY answers without opening; what the driver cannot
     * convert is WAVERR_BADFORMAT. */
    format_of(&format, 11025, 8, 1);
    check(waveInOpen(&wave, 0, &format, 0, 0, 1) == 0, 45030);
    check(wave == 0, 45031);
    format.channels = 3;
    check(waveInOpen(&wave, 0, &format, 0, 0, 1) == 32, 45032);
    format.channels = 1;
    format.tag = 7;
    check(waveInOpen(&wave, 0, &format, 0, 0, 1) == 32, 45033);
    format.tag = 1;
    check(waveInOpen(&wave, 2, &format, 0, 0, 1) == 2, 45034);
    check(waveInOpen(0, 0, 0, 0, 0, 1) == 11, 45035);
    check(waveInOpen(&wave, 0, &format, 0, 0, 0x40000) == 11, 45036);

    /* The eight shapes. */
    record(11025, 8, 1, 46000);
    record(11025, 8, 2, 46100);
    record(11025, 16, 1, 46200);
    record(11025, 16, 2, 46300);
    record(44100, 8, 1, 46400);
    record(44100, 8, 2, 46500);
    record(44100, 16, 1, 46600);
    record(44100, 16, 2, 46700);

    /* The WHDR flags and their errors, on a stream without a callback. */
    format_of(&format, 22050, 16, 1);
    check(waveInOpen(&wave, 0, &format, 0, 0, 0) == 0, 45040);
    check(waveInGetID(wave, &value) == 0 && value == 0, 45041);
    header_of(&header[0], big, sizeof big, 1);
    header_of(&header[1], buffer[0], 4410, 2);
    check(waveInAddBuffer(wave, &header[0], sizeof header[0]) == 34, 45042);
    check(waveInAddBuffer(wave, &header[0], 8) == 11, 45043);
    check(waveInPrepareHeader(wave, &header[0], sizeof header[0]) == 0, 45044);
    check(waveInPrepareHeader(wave, &header[1], sizeof header[1]) == 0, 45045);
    check((header[0].flags & 2) && header[0].reserved, 45046);
    check(waveInAddBuffer(wave, &header[0], sizeof header[0]) == 0, 45047);
    check(waveInAddBuffer(wave, &header[0], sizeof header[0]) == 33, 45048);
    check(waveInUnprepareHeader(wave, &header[0], sizeof header[0]) == 33, 45049);
    check(waveInAddBuffer(wave, &header[1], sizeof header[1]) == 0, 45050);
    check(waveInClose(wave) == 33, 45051);
    /* Stop hands back the buffer it was filling with what it holds; the one
     * behind it stays queued. */
    check(waveInStart(wave) == 0, 45052);
    check(waveInStart(wave) == 0, 45053);
    Sleep(200);
    check(waveInStop(wave) == 0, 45054);
    check((header[0].flags & 0x11) == 1, 45055);
    check(header[0].recorded > 0 && header[0].recorded < header[0].length, 45056);
    check(header[0].recorded % 2 == 0, 45057);
    check((header[1].flags & 0x11) == 0x10, 45058);
    time[0] = 2; /* TIME_SAMPLES */
    check(waveInGetPosition(wave, time, sizeof time) == 0, 45059);
    check(time[0] == 2 && time[1] == header[0].recorded / 2, 45060);
    check(waveInGetPosition(wave, time, 4) == 11, 45061);
    /* Reset hands back the rest at once and starts the position at zero. */
    check(waveInReset(wave) == 0, 45062);
    check((header[1].flags & 0x11) == 1 && header[1].recorded == 0, 45063);
    time[0] = 4;
    check(waveInGetPosition(wave, time, sizeof time) == 0 && time[1] == 0, 45064);
    check(waveInUnprepareHeader(wave, &header[0], sizeof header[0]) == 0, 45065);
    check((header[0].flags & 2) == 0 && header[0].reserved == 0, 45066);
    check(waveInUnprepareHeader(wave, &header[1], sizeof header[1]) == 0, 45067);
    check(waveInClose(wave) == 0, 45068);
    check(waveInStart(wave) == 5, 45069);

    /* An event callback is set for every message. */
    event = CreateEventA(0, 0, 0, 0);
    check(event != 0, 45070);
    check(waveInOpen(&wave, 0, &format, event, 0, 0x50000) == 0, 45071);
    check(WaitForSingleObject(event, 0) == 0, 45072); /* WIM_OPEN */
    header_of(&header[0], buffer[0], 4410, 1);
    check(waveInPrepareHeader(wave, &header[0], sizeof header[0]) == 0, 45073);
    check(waveInAddBuffer(wave, &header[0], sizeof header[0]) == 0, 45074);
    check(waveInStart(wave) == 0, 45075);
    check(WaitForSingleObject(event, 10000) == 0, 45076); /* WIM_DATA */
    check((header[0].flags & 1) && header[0].recorded == 4410, 45077);
    check(waveInReset(wave) == 0, 45078);
    check(waveInUnprepareHeader(wave, &header[0], sizeof header[0]) == 0, 45079);
    check(waveInClose(wave) == 0, 45080);
    check(WaitForSingleObject(event, 0) == 0, 45081); /* WIM_CLOSE */
    CloseHandle(event);

    /* A thread gets the messages posted, with the handle in wParam. */
    check(waveInOpen(&wave, 0, &format, GetCurrentThreadId(), 0x77, 0x20000) == 0, 45090);
    check(PeekMessageA(&message, 0, 0x3be, 0x3c0, 1) && message.message == 0x3be && message.wparam == wave,
          45091);
    header_of(&header[0], buffer[0], 4410, 1);
    check(waveInPrepareHeader(wave, &header[0], sizeof header[0]) == 0, 45092);
    check(waveInAddBuffer(wave, &header[0], sizeof header[0]) == 0, 45093);
    check(waveInStart(wave) == 0, 45094);
    deadline = GetTickCount() + 10000;
    value = 0;
    while (!value && GetTickCount() < deadline)
    {
        if (PeekMessageA(&message, 0, 0x3c0, 0x3c0, 1)) value = message.lparam;
        else Sleep(5);
    }
    check(value == (U)&header[0], 45095);
    check(waveInReset(wave) == 0, 45096);
    check(waveInUnprepareHeader(wave, &header[0], sizeof header[0]) == 0, 45097);
    check(waveInClose(wave) == 0, 45098);
    check(PeekMessageA(&message, 0, 0x3bf, 0x3bf, 1) && message.wparam == wave, 45099);

    /* A window gets them posted the same way. */
    window = CreateWindowExA(0, "STATIC", "wave in", 0, 0, 0, 10, 10, 0, 0, 0, 0);
    check(window != 0, 45100);
    check(waveInOpen(&wave, 0, &format, window, 0, 0x10000) == 0, 45101);
    check(PeekMessageA(&message, window, 0x3be, 0x3be, 1) && message.wparam == wave, 45102);
    header_of(&header[0], buffer[0], 4410, 1);
    check(waveInPrepareHeader(wave, &header[0], sizeof header[0]) == 0, 45103);
    check(waveInAddBuffer(wave, &header[0], sizeof header[0]) == 0, 45104);
    check(waveInStart(wave) == 0, 45105);
    deadline = GetTickCount() + 10000;
    value = 0;
    while (!value && GetTickCount() < deadline)
    {
        if (PeekMessageA(&message, window, 0x3c0, 0x3c0, 1)) value = message.lparam;
        else Sleep(5);
    }
    check(value == (U)&header[0], 45106);
    check(waveInReset(wave) == 0, 45107);
    check(waveInUnprepareHeader(wave, &header[0], sizeof header[0]) == 0, 45108);
    check(waveInClose(wave) == 0, 45109);
    DestroyWindow(window);

    /* The recording destination of the mixer device and its microphone
       line; their volumes scale what is recorded. */
    {
        static U caps_mixer[12], line[42], line_controls[6], controls[3][37], details[6], values[2];
        U mixer = 0, id = 0xffff, microphone = 0, mute = 0, volume = 0;
        check(mixerGetDevCapsA(0, caps_mixer, 0x30) == 0, 45200);
        check(caps_mixer[(8 + 32 + 4) / 4] == 2, 45201);              /* cDestinations */
        check(mixerGetID(0, &id, 0x20000000u) == 0 && id == 0, 45202); /* MIXER_OBJECTF_WAVEIN */
        check(mixerOpen(&mixer, 0, 0, 0, 0) == 0 && mixer, 45203);
        for (i = 0; i < 42; ++i) line[i] = 0;
        line[0] = 0xa8;
        line[1] = 1;                                                    /* dwDestination */
        check(mixerGetLineInfoA(mixer, line, 0) == 0, 45204);
        check(line[6] == 7 && line[8] == 1, 45205);                     /* DST_WAVEIN, one source */
        check(line[0x78 / 4] == 2, 45206);                              /* TARGETTYPE_WAVEIN */
        check(((char *)line)[0x88] == 'A' && ((char *)line)[0x88 + 6] == 'W' && ((char *)line)[0x88 + 11] == 'I',
              45207);
        for (i = 0; i < 42; ++i) line[i] = 0;
        line[0] = 0xa8;
        line[0x78 / 4] = 2;
        check(mixerGetLineInfoA(mixer, line, 4) == 0 && line[6] == 7, 45208); /* by target type */
        for (i = 0; i < 42; ++i) line[i] = 0;
        line[0] = 0xa8;
        line[1] = 1;
        line[2] = 0;
        check(mixerGetLineInfoA(mixer, line, 1) == 0, 45209);
        check(line[6] == 0x1003 && (line[4] & 0x80000000u) && line[1] == 1, 45210); /* SRC_MICROPHONE */
        microphone = line[3];
        line_controls[0] = 0x18;
        line_controls[1] = microphone;
        line_controls[2] = 0;
        line_controls[3] = 2;
        line_controls[4] = 0x94;
        line_controls[5] = (U)controls;
        check(mixerGetLineControlsA(mixer, line_controls, 0) == 0, 45211);
        check(controls[0][2] == 0x50030001 && controls[1][2] == 0x20010002, 45212);
        volume = controls[0][1];
        mute = controls[1][1];
        /* A quarter of the microphone volume records a quarter of the sine. */
        details[0] = 0x18;
        details[1] = volume;
        details[2] = 2;
        details[3] = 0;
        details[4] = 4;
        details[5] = (U)values;
        values[0] = values[1] = 0x4000;
        check(mixerSetControlDetails(mixer, details, 0) == 0, 45213);
        values[0] = values[1] = 0;
        check(mixerGetControlDetailsA(mixer, details, 0) == 0 && values[0] == 0x4000, 45214);
        record(44100, 16, 1, 47000);
        if (mode == 's') check(last_peak > 6000 && last_peak < 6500, 45215);
        values[0] = values[1] = 0xffff;
        check(mixerSetControlDetails(mixer, details, 0) == 0, 45216);
        /* Mute records silence. */
        details[1] = mute;
        details[2] = 1;
        values[0] = 1;
        check(mixerSetControlDetails(mixer, details, 0) == 0, 45217);
        record(44100, 16, 1, 47100);
        check(last_peak == 0, 45218);
        values[0] = 0;
        check(mixerSetControlDetails(mixer, details, 0) == 0, 45219);
        record(44100, 16, 1, 47200);
        if (mode == 's') check(last_peak > 25000, 45220);
        check(mixerClose(mixer) == 0, 45221);
    }

    OutputDebugStringA("WAVEIN PASS");
    ExitProcess(0);
    return 0;
}
