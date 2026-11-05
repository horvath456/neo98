/* SPDX-License-Identifier: GPL-2.0-or-later
 * An x86 guest over the public MCI API: mciSendCommandA, mciSendStringA and
 * mciGetDeviceIDA, for waveaudio over a wave file of the tree and for
 * sequencer over a standard MIDI file the guest lays down itself. The runner
 * checks the samples both of them produced.
 *
 * Every check carries its own exit code, so a failure names itself.
 * */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall

API void ExitProcess(U);
API void Sleep(U);
API U GetTickCount(void);
API U CreateFileA(const char *, U, U, void *, U, U, U);
API U WriteFile(U, const void *, U, U *, void *);
API U CloseHandle(U);
API U DeleteFileA(const char *);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
API U DefWindowProcA(U, U, U, U);
API U DestroyWindow(U);
API U PeekMessageA(void *, U, U, U, U);
API U DispatchMessageA(const void *);
API U mciSendCommandA(U, U, U, void *);
API U mciSendStringA(const char *, char *, U, U);
API U mciGetDeviceIDA(const char *);
API U mciGetErrorStringA(U, char *, U);

struct Open
{
    U callback, device;
    const char *type, *element, *alias;
};
struct PlayTo
{
    U callback, from, to;
};
struct Generic
{
    U callback;
};
struct StatusBlock
{
    U callback, value, item, track;
};
struct SeekBlock
{
    U callback, to;
};
struct SetBlock
{
    U callback, format, audio;
};

static void check(int ok, U code)
{
    if (!ok) ExitProcess(code);
}
static int same(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { ++a; ++b; }
    return *a == *b;
}

/* A standard MIDI file of format 0: tempo 500.000, program 0, volume 100,
 * note 69 on at tick 0 and off at tick 96, which is one quarter note at a
 * division of 96 and therefore half a second. */
static const unsigned char sequence[48] = {
    'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, 0x60,
    'M', 'T', 'r', 'k', 0, 0, 0, 0x1a,
    0x00, 0xff, 0x51, 0x03, 0x07, 0xa1, 0x20,
    0x00, 0xc0, 0x00,
    0x00, 0xb0, 0x07, 0x64,
    0x00, 0x90, 0x45, 0x7f,
    0x60, 0x80, 0x45, 0x00,
    0x00, 0xff, 0x2f, 0x00};
static const char sequence_path[] = "C:\\NEO98SEQ.MID";
static const char wave_path[] = "C:\\WINDOWS\\MEDIA\\DING.WAV";

static U notified, notify_code, notify_device, window;
static U __stdcall procedure(U h, U m, U w, U l)
{
    if (m == 0x3b9) { ++notified; notify_code = w; notify_device = l; }
    return DefWindowProcA(h, m, w, l);
}
static void pump(U milliseconds)
{
    U start = GetTickCount(), message[7];
    for (;;)
    {
        while (PeekMessageA(message, 0, 0, 0, 1)) DispatchMessageA(message);
        if (GetTickCount() - start >= milliseconds) return;
        Sleep(5);
    }
}

int mainCRTStartup(void)
{
    struct Open open;
    struct PlayTo play;
    struct Generic generic;
    struct StatusBlock status;
    struct SeekBlock seek;
    struct SetBlock set;
    U device = 0, written = 0, file = 0, length = 0;
    char answer[128], text[128];

    /* The sequence file of this fixture. */
    file = CreateFileA(sequence_path, 0x40000000, 0, 0, 2, 0x80, 0);
    check(file != 0xffffffff, 2);
    check(WriteFile(file, sequence, sizeof sequence, &written, 0) && written == sizeof sequence, 3);
    check(CloseHandle(file), 4);

    {
        U c[10] = {0, (U)procedure, 0, 0, 0, 0, 0, 0, 0, (U) "MciFixture"};
        check(RegisterClassA(c), 5);
        window = CreateWindowExA(0, "MciFixture", "", 0x80000000, 0, 0, 40, 30, 0, 0, 0, 0);
        check(window, 6);
    }

    /* A message below 800h reaches the installable driver layer, and without
     * a driver there is no device behind the identifier. */
    check(mciSendCommandA(0, 3, 0, 0) == 257, 7);
    /* A command on an identifier that was never opened. */
    generic.callback = 0;
    check(mciSendCommandA(77, 0x808, 0, &generic) == 257, 8);

    /* waveaudio over a wave file of the tree. */
    open.callback = 0;
    open.device = 0;
    open.type = "waveaudio";
    open.element = wave_path;
    open.alias = "ding";
    check(mciSendCommandA(0, 0x803, 0x2600, &open) == 0, 9);
    device = open.device;
    check(device != 0, 10);
    check(mciGetDeviceIDA("ding") == device, 11);
    check(mciGetDeviceIDA("nothing") == 0, 12);

    status.callback = 0;
    status.item = 1; /* MCI_STATUS_LENGTH */
    check(mciSendCommandA(device, 0x814, 0x100, &status) == 0, 13);
    length = status.value;
    check(length > 100 && length < 60000, 14);
    status.item = 4; /* MCI_STATUS_MODE */
    check(mciSendCommandA(device, 0x814, 0x100, &status) == 0, 15);
    check(status.value == 525, 16); /* stopped */
    check(mciSendCommandA(device, 0x814, 0, &status) == 273, 17);

    /* The time format of a waveaudio device: milliseconds, bytes, samples. */
    set.callback = 0;
    set.format = 8; /* MCI_FORMAT_BYTES */
    check(mciSendCommandA(device, 0x80d, 0x400, &set) == 0, 18);
    status.item = 1;
    check(mciSendCommandA(device, 0x814, 0x100, &status) == 0, 19);
    check(status.value > length, 20);
    set.format = 7;
    check(mciSendCommandA(device, 0x80d, 0x400, &set) == 293, 21);
    set.format = 0;
    check(mciSendCommandA(device, 0x80d, 0x400, &set) == 0, 22);

    /* Play a stretch and wait for it. */
    play.callback = 0;
    play.from = 0;
    play.to = 300;
    check(mciSendCommandA(device, 0x806, 0x4 | 0x8 | 0x2, &play) == 0, 23);
    status.item = 2; /* MCI_STATUS_POSITION */
    check(mciSendCommandA(device, 0x814, 0x100, &status) == 0, 24);
    check(status.value >= 280 && status.value <= 320, 25);

    /* Seek back and play with a notification to the window. */
    seek.callback = 0;
    check(mciSendCommandA(device, 0x807, 0x100, &seek) == 0, 26); /* MCI_SEEK_TO_START */
    check(mciSendCommandA(device, 0x814, 0x100, &status) == 0, 27);
    check(status.value == 0, 28);
    notified = 0;
    play.callback = window;
    play.from = 0;
    play.to = 200;
    check(mciSendCommandA(device, 0x806, 0x4 | 0x8 | 0x1, &play) == 0, 29);
    status.item = 4;
    check(mciSendCommandA(device, 0x814, 0x100, &status) == 0, 30);
    check(status.value == 526, 31); /* playing */
    pump(600);
    check(notified == 1, 32);
    check(notify_code == 1, 33); /* MCI_NOTIFY_SUCCESSFUL */
    check(notify_device == device, 34);
    check(mciSendCommandA(device, 0x814, 0x100, &status) == 0, 35);
    check(status.value == 525, 36);

    /* Pause and resume. */
    play.callback = 0;
    play.from = 0;
    play.to = length;
    check(mciSendCommandA(device, 0x806, 0x4 | 0x8, &play) == 0, 37);
    check(mciSendCommandA(device, 0x809, 0, &generic) == 0, 38);
    status.item = 4;
    check(mciSendCommandA(device, 0x814, 0x100, &status) == 0, 39);
    check(status.value == 529, 40); /* paused */
    check(mciSendCommandA(device, 0x855, 0, &generic) == 0, 41);
    check(mciSendCommandA(device, 0x808, 0, &generic) == 0, 42);
    check(mciSendCommandA(device, 0x814, 0x100, &status) == 0, 43);
    check(status.value == 525, 44);
    check(mciSendCommandA(device, 0x804, 0, &generic) == 0, 45);
    check(mciSendCommandA(device, 0x808, 0, &generic) == 257, 46);
    check(mciGetDeviceIDA("ding") == 0, 47);

    /* A device type without a device under it. */
    open.callback = 0;
    open.device = 0;
    open.type = "cdaudio";
    open.element = 0;
    open.alias = 0;
    check(mciSendCommandA(0, 0x803, 0x2000, &open) == 306, 48);
    check(mciGetErrorStringA(306, text, sizeof text) == 1, 49);
    check(text[0] != 0, 50);
    open.type = "nosuchdevice";
    check(mciSendCommandA(0, 0x803, 0x2000, &open) == 263, 51);

    /* The same file over the string interface. */
    check(mciSendStringA("", answer, sizeof answer, 0) == 267, 52);
    check(mciSendStringA("play nothing", answer, sizeof answer, 0) == 287, 53);
    check(mciSendStringA("open C:\\WINDOWS\\MEDIA\\DING.WAV type waveaudio alias tune",
                         answer, sizeof answer, 0) == 0, 54);
    check(mciGetDeviceIDA("tune") != 0, 55);
    check(mciSendStringA("status tune length", answer, sizeof answer, 0) == 0, 56);
    check(answer[0] >= '0' && answer[0] <= '9', 57);
    check(mciSendStringA("status tune mode", answer, sizeof answer, 0) == 0, 58);
    check(same(answer, "stopped"), 59);
    check(mciSendStringA("play tune from 0 to 200 wait", answer, sizeof answer, 0) == 0, 60);
    check(mciSendStringA("status tune position", answer, sizeof answer, 0) == 0, 61);
    check(mciSendStringA("seek tune to start", answer, sizeof answer, 0) == 0, 62);
    check(mciSendStringA("status tune position", answer, sizeof answer, 0) == 0, 63);
    check(same(answer, "0"), 64);
    check(mciSendStringA("status tune nonsense", answer, sizeof answer, 0) == 259, 65);
    check(mciSendStringA("fly tune", answer, sizeof answer, 0) == 261, 66);
    check(mciSendStringA("close tune", answer, sizeof answer, 0) == 0, 67);

    /* Silence, so the runner can tell the two stretches apart. */
    pump(500);

    /* sequencer over the MIDI file the guest laid down. */
    open.callback = 0;
    open.device = 0;
    open.type = 0;
    open.element = sequence_path;
    open.alias = "song";
    check(mciSendCommandA(0, 0x803, 0x200 | 0x400, &open) == 0, 68);
    device = open.device;
    check(device != 0, 69);
    status.callback = 0;
    status.item = 1;
    check(mciSendCommandA(device, 0x814, 0x100, &status) == 0, 70);
    check(status.value >= 400 && status.value <= 700, 71);
    notified = 0;
    play.callback = window;
    play.from = 0;
    play.to = status.value;
    check(mciSendCommandA(device, 0x806, 0x4 | 0x8 | 0x1, &play) == 0, 72);
    status.item = 4;
    check(mciSendCommandA(device, 0x814, 0x100, &status) == 0, 73);
    check(status.value == 526, 74);
    pump(1200);
    check(notified == 1, 75);
    check(notify_code == 1, 76);
    check(mciSendCommandA(device, 0x814, 0x100, &status) == 0, 77);
    check(status.value == 525, 78);
    check(mciSendCommandA(device, 0x80d, 0x400, &set) == 0, 79);
    set.format = 8;
    check(mciSendCommandA(device, 0x80d, 0x400, &set) == 293, 80);
    check(mciSendCommandA(device, 0x804, 0, &generic) == 0, 81);

    /* a compound device opens without an element, as MCISEQ.DRV of
     * the tree does; Shanghai II offers music only when this open succeeds.
     * The device is known by its type and has nothing to play. */
    open.callback = 0;
    open.device = 0;
    open.type = "sequencer";
    open.element = 0;
    open.alias = 0;
    check(mciSendCommandA(0, 0x803, 0x2000 | 0x2, &open) == 0, 84);
    device = open.device;
    check(device != 0 && mciGetDeviceIDA("sequencer") == device, 85);
    play.callback = 0;
    check(mciSendCommandA(device, 0x806, 0, &play) != 0, 86);
    check(mciSendCommandA(device, 0x804, 0, &generic) == 0, 87);
    open.device = 0;
    open.type = "waveaudio";
    check(mciSendCommandA(0, 0x803, 0x2000, &open) == 0 && open.device != 0, 88);
    check(mciSendCommandA(open.device, 0x804, 0, &generic) == 0, 89);
    check(mciSendStringA("open sequencer alias tune", answer, sizeof answer, 0) == 0, 90);
    check(mciSendStringA("close tune", answer, sizeof answer, 0) == 0, 91);

    pump(300);
    check(DestroyWindow(window), 82);
    check(DeleteFileA(sequence_path), 83);
    ExitProcess(0);
    return 0;
}
