/* SPDX-License-Identifier: GPL-2.0-or-later
 * An x86 guest over the public waveOut API and PlaySound. It plays a sine of
 * a known frequency in 8-bit mono at 11.025 kHz; the runner checks what came
 * out of the mixer at 44.1 kHz in 16-bit stereo, which is the sample side of
 * the pixel check a UI test does.
 *
 * Every check carries its own exit code, so a failure names itself.
 * */
typedef unsigned U;
#define API __declspec(dllimport)

API void __stdcall ExitProcess(U);
API void __stdcall Sleep(U);
API U __stdcall GetTickCount(void);
API U __stdcall GetCurrentThread(void);
API U __stdcall SetThreadPriority(U, int);

API U __stdcall waveOutGetNumDevs(void);
API U __stdcall waveOutGetDevCapsA(U, void *, U);
API U __stdcall waveOutOpen(U *, U, const void *, U, U, U);
API U __stdcall waveOutClose(U);
API U __stdcall waveOutPrepareHeader(U, void *, U);
API U __stdcall waveOutUnprepareHeader(U, void *, U);
API U __stdcall waveOutWrite(U, void *, U);
API U __stdcall waveOutPause(U);
API U __stdcall waveOutRestart(U);
API U __stdcall waveOutReset(U);
API U __stdcall waveOutBreakLoop(U);
API U __stdcall waveOutGetPosition(U, void *, U);
API U __stdcall waveOutGetVolume(U, U *);
API U __stdcall waveOutSetVolume(U, U);
API U __stdcall waveOutGetID(U, U *);
API U __stdcall waveOutGetPitch(U, U *);
API U __stdcall waveOutSetPlaybackRate(U, U);
API U __stdcall waveOutMessage(U, U, U, U);
API U __stdcall PlaySoundA(const char *, U, U);

struct WaveFormat
{
    unsigned short tag, channels;
    U rate, average;
    unsigned short align, bits;
    unsigned short extra;
};
struct WaveHeader
{
    char *data;
    U length, recorded, user, flags, loops;
    void *next;
    U reserved;
};

static void check(int ok, U code)
{
    if (!ok) ExitProcess(code);
}

/* One period of 441 Hz at 11.025 kHz is exactly 25 samples. */
static const unsigned char period[25] = {128, 153, 176, 196, 212, 223, 228, 226, 218,
                                         205, 187, 165, 141, 115, 91,  69,  51,  38,
                                         30,  28,  33,  44,  60,  80,  103};
#define BLOCK 2205 /* a fifth of a second */
static char first[BLOCK], second[BLOCK];

/* What the callback saw, in the order it saw it. */
static U seen[8], seen_count, opened, closed;
static void __stdcall Callback(U handle, U message, U instance, U first_parameter, U second)
{
    (void)handle;
    (void)second;
    if (instance != 0x5ea50000) return;
    if (message == 0x3bb) ++opened;
    if (message == 0x3bc) ++closed;
    if (message == 0x3bd && seen_count < 8) seen[seen_count++] = first_parameter;
}

int mainCRTStartup(void)
{
    struct WaveFormat format;
    struct WaveHeader header[2];
    unsigned char caps[52];
    U wave = 0, value = 0, i, deadline;
    U time[4];

    for (i = 0; i < BLOCK; ++i) first[i] = (char)period[i % 25];
    for (i = 0; i < BLOCK; ++i) second[i] = (char)period[i % 25];

    /* The device count is one, because the mixer is one device. */
    check(waveOutGetNumDevs() == 1, 44000);

    /* a size of zero is success before anything else, a bad buffer
     * is 11, a device out of range is 2, and device 0 answers. */
    check(waveOutGetDevCapsA(0, caps, 0) == 0, 44001);
    check(waveOutGetDevCapsA(0, 0, sizeof caps) == 11, 44002);
    check(waveOutGetDevCapsA(1, caps, sizeof caps) == 2, 44003);
    for (i = 0; i < sizeof caps; ++i) caps[i] = 0;
    check(waveOutGetDevCapsA(0, caps, sizeof caps) == 0, 44004);
    check(*(unsigned short *)(caps + 0x2c) == 2, 44005);         /* wChannels */
    check(*(U *)(caps + 0x28) != 0, 44006);                      /* dwFormats */
    check((*(U *)(caps + 0x30) & 4) != 0, 44007);                /* WAVECAPS_VOLUME */
    check((*(U *)(caps + 0x30) & 3) == 0, 44008);                /* no pitch, no rate */
    check(waveOutGetDevCapsA(~0u, caps, sizeof caps) == 0, 44009); /* the mapper */

    format.tag = 1;
    format.channels = 1;
    format.rate = 11025;
    format.average = 11025;
    format.align = 1;
    format.bits = 8;
    format.extra = 0;

    /* WAVE_FORMAT_QUERY answers without opening. A format the mixer cannot
     * convert is WAVERR_BADFORMAT and not a lie. */
    check(waveOutOpen(&wave, 0, &format, 0, 0, 1) == 0, 44010);
    check(wave == 0, 44011);
    format.channels = 3;
    check(waveOutOpen(&wave, 0, &format, 0, 0, 1) == 32, 44012);
    format.channels = 1;
    format.tag = 7; /* WAVE_FORMAT_MULAW, which this driver does not take */
    check(waveOutOpen(&wave, 0, &format, 0, 0, 1) == 32, 44013);
    format.tag = 1;
    check(waveOutOpen(&wave, 2, &format, 0, 0, 1) == 2, 44014);
    check(waveOutOpen(0, 0, 0, 0, 0, 1) == 11, 44015);
    /*  takes only the five callback kinds. */
    check(waveOutOpen(&wave, 0, &format, 0, 0, 0x40000) == 11, 44016);

    check(waveOutOpen(&wave, 0, &format, (U)Callback, 0x5ea50000, 0x30000) == 0, 44020);
    check(wave != 0, 44021);
    check(opened == 1, 44022);

    check(waveOutGetID(wave, &value) == 0, 44023);
    check(value == 0, 44024);
    check(waveOutGetPitch(wave, &value) == 8, 44025);
    check(waveOutSetPlaybackRate(wave, 0x10000) == 8, 44026);
    check(waveOutMessage(wave, 0x800, 0, 0) == 8, 44027);

    header[0].data = first;
    header[0].length = BLOCK;
    header[0].recorded = 0;
    header[0].user = 1;
    header[0].flags = 0;
    header[0].loops = 0;
    header[0].next = 0;
    header[0].reserved = 0;
    header[1] = header[0];
    header[1].data = second;
    header[1].user = 2;

    /* An unprepared header is WAVERR_UNPREPARED, which is 34. */
    check(waveOutWrite(wave, &header[0], sizeof header[0]) == 34, 44030);
    check(waveOutWrite(wave, &header[0], 8) == 11, 44031);
    check(waveOutPrepareHeader(wave, &header[0], sizeof header[0]) == 0, 44032);
    check(waveOutPrepareHeader(wave, &header[1], sizeof header[1]) == 0, 44033);
    check((header[0].flags & 2) != 0, 44034);
    check(header[0].reserved != 0, 44035);
    /* A second prepare of the same header is success and changes nothing. */
    check(waveOutPrepareHeader(wave, &header[0], sizeof header[0]) == 0, 44036);

    /* Both blocks go into the queue before the stream runs: on a loaded host
     * the guest can take longer than one block between two writes, and the
     * mixer would play silence in between. A paused device queues
     * writes, and waveOutRestart below lets them go together. */
    check(waveOutPause(wave) == 0, 44039);
    check(waveOutWrite(wave, &header[0], sizeof header[0]) == 0, 44040);
    check((header[0].flags & 0x10) != 0, 44041);
    check((header[0].flags & 1) == 0, 44042);
    /* A header already in the queue is WAVERR_STILLPLAYING, 33, and so is an
     * unprepare of it. */
    check(waveOutWrite(wave, &header[0], sizeof header[0]) == 33, 44043);
    check(waveOutUnprepareHeader(wave, &header[0], sizeof header[0]) == 33, 44044);
    check(waveOutWrite(wave, &header[1], sizeof header[1]) == 0, 44045);
    /* A close with blocks in the queue is WAVERR_STILLPLAYING too. */
    check(waveOutClose(wave) == 33, 44046);
    check(waveOutRestart(wave) == 0, 44047);

    /* The blocks come back in the order they were written. */
    deadline = GetTickCount() + 20000;
    while (seen_count < 2 && GetTickCount() < deadline) Sleep(5);
    check(seen_count == 2, 44050);
    check(seen[0] == (U)&header[0], 44051);
    check(seen[1] == (U)&header[1], 44052);
    check((header[0].flags & 1) != 0, 44053);
    check((header[0].flags & 0x10) == 0, 44054);
    check((header[1].flags & 1) != 0, 44055);

    /* Position, in the four formats a wave device answers. */
    time[0] = 4; /* TIME_BYTES */
    check(waveOutGetPosition(wave, time, sizeof time) == 0, 44060);
    check(time[0] == 4, 44061);
    check(time[1] == 2 * BLOCK, 44062);
    time[0] = 2; /* TIME_SAMPLES */
    check(waveOutGetPosition(wave, time, sizeof time) == 0, 44063);
    check(time[1] == 2 * BLOCK, 44064);
    time[0] = 1; /* TIME_MS */
    check(waveOutGetPosition(wave, time, sizeof time) == 0, 44065);
    check(time[1] == 400, 44066);
    time[0] = 32; /* TIME_TICKS, which a wave device answers as TIME_BYTES */
    check(waveOutGetPosition(wave, time, sizeof time) == 0, 44067);
    check(time[0] == 4, 44068);
    check(waveOutGetPosition(wave, time, 4) == 11, 44069);

    check(waveOutUnprepareHeader(wave, &header[0], sizeof header[0]) == 0, 44070);
    check((header[0].flags & 2) == 0, 44071);
    check(header[0].reserved == 0, 44072);
    check(waveOutUnprepareHeader(wave, &header[1], sizeof header[1]) == 0, 44073);

    /* Pause holds the stream, restart lets it go, reset hands every queued
     * block back at once. */
    header[0].flags = 0;
    header[1].flags = 0;
    check(waveOutPrepareHeader(wave, &header[0], sizeof header[0]) == 0, 44080);
    check(waveOutPrepareHeader(wave, &header[1], sizeof header[1]) == 0, 44081);
    check(waveOutPause(wave) == 0, 44082);
    check(waveOutWrite(wave, &header[0], sizeof header[0]) == 0, 44083);
    check(waveOutWrite(wave, &header[1], sizeof header[1]) == 0, 44084);
    Sleep(100);
    check(seen_count == 2, 44085); /* nothing plays while paused */
    check(waveOutReset(wave) == 0, 44086);
    /* The blocks are back before waveOutReset returns: WHDR_DONE is set and
     * WHDR_INQUEUE clear, without waiting for the notification. */
    check((header[0].flags & 0x11) == 1 && (header[1].flags & 0x11) == 1, 44092);
    deadline = GetTickCount() + 20000;
    while (seen_count < 4 && GetTickCount() < deadline) Sleep(5);
    check(seen_count == 4, 44087);
    check(seen[2] == (U)&header[0], 44088);
    check(seen[3] == (U)&header[1], 44089);
    /* Reset puts the position back to the start. */
    time[0] = 4;
    check(waveOutGetPosition(wave, time, sizeof time) == 0, 44090);
    check(time[1] == 0, 44091);

    /* WaveMix resets for every new sound and writes the same header again at
     * once; WAVERR_STILLPLAYING (33) there silenced Space Cadet. */
    check(waveOutPause(wave) == 0, 44093);
    check(waveOutWrite(wave, &header[0], sizeof header[0]) == 0, 44094);
    check(waveOutWrite(wave, &header[0], sizeof header[0]) == 33, 44095);
    check(waveOutReset(wave) == 0, 44096);
    check(waveOutPause(wave) == 0, 44097);
    check(waveOutWrite(wave, &header[0], sizeof header[0]) == 0, 44098);
    check(waveOutReset(wave) == 0, 44099);
    check((header[0].flags & 0x11) == 1, 44124);

    /* A program that raises its own priority and never waits still gets its
     * blocks back: the timer thread of WINMM runs at time-critical priority.
     * Space Cadet sets THREAD_PRIORITY_HIGHEST, polls PeekMessage
     * and looks at WHDR_DONE from WaveMixPump; without the priority no block
     * ever came back and every sound ended after three blocks. */
    check(SetThreadPriority(GetCurrentThread(), 2), 44125);
    check(waveOutWrite(wave, &header[0], sizeof header[0]) == 0, 44126);
    deadline = GetTickCount() + 10000;
    while (!(header[0].flags & 1) && GetTickCount() < deadline) { /* busy, no wait */ }
    check(header[0].flags & 1, 44127);
    check(SetThreadPriority(GetCurrentThread(), 0), 44128);

    check(waveOutGetVolume(wave, &value) == 0, 44100);
    check(value == ~0u, 44101);
    check(waveOutSetVolume(wave, 0x80008000) == 0, 44102);
    check(waveOutGetVolume(wave, &value) == 0, 44103);
    check(value == 0x80008000, 44104);
    check(waveOutBreakLoop(wave) == 0, 44105);

    check(waveOutUnprepareHeader(wave, &header[0], sizeof header[0]) == 0, 44110);
    check(waveOutUnprepareHeader(wave, &header[1], sizeof header[1]) == 0, 44111);
    check(waveOutClose(wave) == 0, 44112);
    check(closed == 1, 44113);
    /* A closed handle is no longer a handle. */
    check(waveOutPause(wave) == 5, 44114);

    /* PlaySound over a file of the tree, synchronously, so the runner finds
     * the samples in its capture buffer when this returns. */
    check(PlaySoundA("C:\\WINDOWS\\MEDIA\\DING.WAV", 0, 0x20000) == 1, 44120);
    /* SND_LOOP without SND_ASYNC is refused. */
    check(PlaySoundA("C:\\WINDOWS\\MEDIA\\DING.WAV", 0, 0x20008) == 0, 44121);
    /* A name that is neither alias nor file, with SND_NODEFAULT, is FALSE. */
    check(PlaySoundA("nosuchsound.wav", 0, 0x20002) == 0, 44122);
    /* A null name stops whatever plays and succeeds. */
    check(PlaySoundA(0, 0, 0) == 1, 44123);

    ExitProcess(0);
    return 0;
}
