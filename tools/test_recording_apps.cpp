// SPDX-License-Identifier: GPL-2.0-or-later
// The recording side of three original programs of Windows 98, without a
// screen, on the test microphone (NEO98_TEST_MICROPHONE=sine, the 441 Hz of
// src/sound_microphone.cpp). The mode is the third argument:
// sndrec SNDREC32.EXE records three seconds, plays them back and saves
// them with Save As. The runner drives it the way its buttons do:
// WM_COMMAND with the identifiers of its dialog template (205 to the
// start, 207 Play, 208 Stop, 209 Record) and of its menu (14 Save
// As), and measures the playback in the frames of the mixer, the
// file the program saved, and that the microphone was opened only
// while it recorded.
// sndvol SNDVOL32.EXE /R, the recording view of the volume control: its
// columns name the recording destination and the microphone, and
// moving the microphone fader changes the level the recording
// streams read.
// mmsys RUNDLL32.EXE shell32.dll,Control_RunDLL mmsys.cpl: the Audio page
// of Multimedia Properties names "NEO98 Wave In" as the preferred
// recording device.
//
#define main original_probe_main
#include "probe_application.cpp"
#undef main
using App = X86::Application;
static App *recorder;
static short playback[44100 * 30 * 2];
static void require(bool ok, const char *why)
{
    if (ok) return;
    fprintf(stderr, "SNDREC FAILED: %s\n", why);
    snapshot_windows();
    fflush(stdout);
    exit(1);
}
static void pump(unsigned milliseconds)
{
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    do {
        recorder->Pump(32);
        App::PumpChildren(8);
        require(recorder->Status() != App::Failed, recorder->Error());
        if (recorder->Status() == App::Idle) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < end);
}
template <class F> static void until(F condition, const char *why, unsigned limit = 30000)
{
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(limit);
    while (!condition() && std::chrono::steady_clock::now() < end) pump(20);
    require(condition(), why);
}
static unsigned top_level(const char *klass_wanted)
{
    unsigned nodes[User::WindowCapacity], n = 0;
    User::WindowSnapshot(User::GetDesktopWindow(), nodes, User::WindowCapacity, n);
    for (unsigned i = 0; i < n; ++i) {
        char klass[128];
        User::GetClassName(nodes[i], klass, sizeof klass);
        if (User::IsVisible(nodes[i]) && !strcmp(klass, klass_wanted)) return nodes[i];
    }
    return 0;
}
static void command(unsigned window, unsigned id)
{
    require(User::PostMessage(window, 0x111, id, User::GetDlgItem(window, id)), "post WM_COMMAND");
}
// Peak and zero crossings of `frames` samples of one lane.
struct Measure
{
    int peak = 0;
    unsigned crossings = 0, frames = 0;
};
template <class Sample> static Measure measure(Sample sample, unsigned frames)
{
    Measure m;
    m.frames = frames;
    int previous = frames ? sample(0) : 0;
    for (unsigned i = 0; i < frames; ++i) {
        const int value = sample(i);
        if (value > m.peak) m.peak = value;
        if ((previous < 0) != (value < 0)) ++m.crossings;
        previous = value;
    }
    return m;
}
static unsigned word(const unsigned char *p) { return p[0] | p[1] << 8; }
static unsigned dword(const unsigned char *p) { return word(p) | word(p + 2) << 16; }
// Every window below `root`, depth first.
static void descendants(unsigned root, std::vector<unsigned> &out)
{
    for (unsigned child = User::GetWindow(root, 5); child; child = User::GetWindow(child, 2)) {
        out.push_back(child);
        descendants(child, out);
    }
}
static unsigned descendant_with_id(unsigned root, unsigned id)
{
    std::vector<unsigned> all;
    descendants(root, all);
    for (unsigned hwnd : all)
        if (User::ControlID(hwnd) == id) return hwnd;
    return 0;
}
static int sndrec(const char *saved_host);
static int sndvol();
static int mmsys();
int main(int argc, char **argv)
{
    if (argc < 5) return 2;
    const char *mode = argv[3];
    const auto tree_options = SystemTreeHost::Scan(argc, argv);
    require(tree_options.tree && SystemTreeHost::Mount(tree_options.root), "mount the imported Win98 tree");
    auto drive_options = HostVolume::Scan(argc, argv);
    require(drive_options.drive_c != nullptr, "--drive-c names the writable C:");
    require(HostVolume::Mount(drive_options), "mount the writable C:");
    const char *kind = getenv("NEO98_TEST_MICROPHONE");
    require(kind && !strcmp(kind, "sine"), "NEO98_TEST_MICROPHONE=sine");
    auto exe = load(argv[1]), font = load(argv[2]);
    Sound::SetCapture(playback, sizeof playback / (2 * sizeof(short)));
    recorder = new App(nullptr);
    const char *program = !strcmp(mode, "sndrec") ? "SNDREC32.EXE" : !strcmp(mode, "sndvol") ? "SNDVOL32.EXE" : "RUNDLL32.EXE";
    const char *line = !strcmp(mode, "sndrec") ? nullptr
                       : !strcmp(mode, "sndvol") ? "SNDVOL32.EXE /R"
                                                 : "RUNDLL32.EXE shell32.dll,Control_RunDLL mmsys.cpl";
    require(recorder->Start(exe.data(), exe.size(), font.data(), font.size(), program, line), "start the program");
    if (!strcmp(mode, "sndrec")) return sndrec(argv[4]);
    if (!strcmp(mode, "sndvol")) return sndvol();
    return mmsys();
}
static int sndrec(const char *saved_host)
{
    unsigned window = 0;
    until([&] { window = top_level("SoundRec"); return window != 0; }, "the Sound Recorder window", 90000);
    pump(500);
    // Nothing records yet: the microphone stays closed while the program
    // only looks at the device.
    require(Sound::Microphone::State() == Sound::InputOff, "the microphone is closed before Record");
    const unsigned long long before = Sound::Microphone::Received();
    command(window, 209);   // Record
    until([] { return Sound::Microphone::State() == Sound::InputRunning; }, "Record opens the microphone", 10000);
    pump(3000);
    command(window, 208);   // Stop
    pump(500);
    const unsigned long long recorded = Sound::Microphone::Received() - before;
    char length[32] = {};
    User::GetWindowText(User::GetDlgItem(window, 202), length, sizeof length);
    printf("SNDREC recorded frames=%llu length=%s\n", recorded, length);
    require(recorded > 44100 * 2, "three seconds went through the microphone");
    // Play it back from the start.
    const unsigned played_before = Sound::Captured();
    command(window, 205);
    pump(200);
    command(window, 207);   // Play
    pump(4000);
    command(window, 208);
    pump(300);
    const unsigned played_after = Sound::Captured();
    unsigned first = played_before;
    while (first < played_after && !playback[first * 2]) ++first;
    unsigned last = first;
    for (unsigned i = first; i < played_after; ++i)
        if (playback[i * 2]) last = i;
    require(first < played_after, "the playback reached the mixer");
    // A second in the middle of what was played.
    const unsigned middle = first + (last - first) / 2 > first + 22050 ? first + (last - first) / 2 - 22050 : first;
    const Measure heard = measure([&](unsigned i) { return (int)playback[(middle + i) * 2]; },
                                  last - middle > 44100 ? 44100 : last - middle);
    printf("SNDREC playback frames=%u span=%u peak=%d crossings=%u over=%u\n", played_after - played_before,
           last - first + 1, heard.peak, heard.crossings, heard.frames);
    require(last - first > 44100 * 2, "more than two seconds played");
    require(heard.frames == 44100 && heard.crossings >= 876 && heard.crossings <= 888,
            "the playback is 441 Hz");
    require(heard.peak > 24000, "the playback carries the level of the recording");
    // Save As, into the file the runner names.
    command(window, 14);
    unsigned dialog = 0;
    until([&] { dialog = visible_dialog("Save As"); return dialog != 0; }, "the Save As dialog", 30000);
    pump(300);
    unsigned name = User::GetDlgItem(dialog, 1152);   // edt1
    require(name != 0, "the file name field of Save As");
    require(User::SetWindowText(name, "C:\\MICTEST.WAV"), "type the file name");
    require(User::PostMessage(dialog, 0x111, 1, User::GetDlgItem(dialog, 1)), "press Save");
    until([&] { return !visible_dialog("Save As"); }, "Save As closes", 30000);
    pump(1000);
    char title[128] = {};
    User::GetWindowText(window, title, sizeof title);
    printf("SNDREC title=%s\n", title);
    // Exit closes the device; the microphone is shut once nothing records.
    require(User::PostMessage(window, 0x10, 0, 0), "close the Sound Recorder");
    until([] { return recorder->Status() == App::Exited; }, "SNDREC32 ends", 30000);
    require(Sound::Microphone::State() == Sound::InputOff && !Sound::Microphone::OpenStreams(),
            "the microphone is closed after the program ended");
    // The file, as the program wrote it.
    FILE *file = fopen(saved_host, "rb");
    require(file != nullptr, "the saved file is on the drive");
    std::vector<unsigned char> wave;
    {
        unsigned char chunk[65536];
        size_t got;
        while ((got = fread(chunk, 1, sizeof chunk, file)) > 0) wave.insert(wave.end(), chunk, chunk + got);
        fclose(file);
    }
    require(wave.size() > 44 && !memcmp(wave.data(), "RIFF", 4) && !memcmp(wave.data() + 8, "WAVE", 4), "a RIFF WAVE file");
    unsigned tag = 0, channels = 0, rate = 0, bits = 0, data = 0, bytes = 0;
    for (unsigned at = 12; at + 8 <= wave.size();) {
        const unsigned size = dword(&wave[at + 4]);
        if (!memcmp(&wave[at], "fmt ", 4) && size >= 16) {
            tag = word(&wave[at + 8]); channels = word(&wave[at + 10]);
            rate = dword(&wave[at + 12]); bits = word(&wave[at + 22]);
        }
        if (!memcmp(&wave[at], "data", 4)) { data = at + 8; bytes = size; }
        at += 8 + size + (size & 1);
    }
    printf("SNDREC file bytes=%u format=%u channels=%u rate=%u bits=%u data=%u\n", (unsigned)wave.size(), tag,
           channels, rate, bits, bytes);
    require(tag == 1 && (bits == 8 || bits == 16) && (channels == 1 || channels == 2) && rate, "PCM in the file");
    require(data && data + bytes <= wave.size(), "a data chunk");
    const unsigned frame = channels * bits / 8, frames = bytes / frame;
    require(frames > rate * 2, "more than two seconds in the file");
    const unsigned start = frames / 2 - rate / 2;
    const Measure stored = measure([&](unsigned i) {
        const unsigned char *p = &wave[data + (start + i) * frame];
        return bits == 8 ? ((int)p[0] - 128) * 256 : (int)(short)(p[0] | p[1] << 8);
    }, rate);
    printf("SNDREC file second peak=%d crossings=%u\n", stored.peak, stored.crossings);
    require(stored.crossings >= 876 && stored.crossings <= 888, "the file holds 441 Hz");
    require(stored.peak > 24000, "the file holds the level of the test microphone");
    puts("SNDREC PASS");
    return 0;
}

static int sndvol()
{
    unsigned window = 0;
    until([&] { window = top_level("Volume Control"); return window != 0; }, "the volume control window", 90000);
    pump(1500);
    char title[128] = {};
    User::GetWindowText(window, title, sizeof title);
    std::vector<unsigned> all;
    descendants(window, all);
    unsigned microphone = 0, destination = 0;
    for (unsigned hwnd : all) {
        char text[128] = {};
        User::GetWindowText(hwnd, text, sizeof text);
        if (!strcmp(text, "Microphone")) microphone = hwnd;
        if (!strcmp(text, "Recording Control") || !strcmp(text, "Recording")) destination = hwnd;
    }
    printf("SNDVOL title=%s windows=%u microphone=%u destination=%u\n", title, (unsigned)all.size(), microphone,
           destination);
    require(strstr(title, "Recording") != nullptr, "the title names the recording view");
    require(microphone != 0, "a column names the microphone");
    // The volume fader of the microphone column: the tall trackbar below its
    // name.
    Gdi::RECT name{};
    require(User::GetWindowRect(microphone, &name), "the microphone column has a place");
    unsigned fader = 0;
    for (unsigned hwnd : all) {
        char klass[64] = {};
        User::GetClassName(hwnd, klass, sizeof klass);
        Gdi::RECT r{};
        if (strcmp(klass, "msctls_trackbar32") || !User::GetWindowRect(hwnd, &r)) continue;
        const int centre = (r.left + r.right) / 2;
        if (centre >= name.left && centre <= name.right && r.bottom - r.top > r.right - r.left) fader = hwnd;
    }
    require(fader != 0, "the microphone column has a volume fader");
    const unsigned before = Sound::Microphone::Level();
    // What a drag does: the trackbar moves and tells its parent.
    require(User::PostMessage(fader, 0x400 + 5, 1, 40), "TBM_SETPOS");
    require(User::PostMessage(User::Parent(fader), 0x115, 4 | (40u << 16), fader), "WM_VSCROLL");
    require(User::PostMessage(User::Parent(fader), 0x115, 8, fader), "SB_ENDSCROLL");
    until([&] { return Sound::Microphone::Level() != before; }, "the fader sets the microphone level", 10000);
    printf("SNDVOL level before=%08x after=%08x\n", before, Sound::Microphone::Level());
    require(User::PostMessage(window, 0x10, 0, 0), "close the volume control");
    until([] { return recorder->Status() == App::Exited; }, "SNDVOL32 ends", 30000);
    puts("SNDVOL PASS");
    return 0;
}
static int mmsys()
{
    unsigned sheet = 0;
    until([&] { sheet = visible_dialog("Multimedia Properties"); return sheet != 0; }, "Multimedia Properties", 90000);
    pump(1500);
    const unsigned playback_box = descendant_with_id(sheet, 5140), recording_box = descendant_with_id(sheet, 5141);
    char playback_name[64] = {}, recording_name[64] = {};
    if (playback_box) User::GetWindowText(playback_box, playback_name, sizeof playback_name);
    if (recording_box) User::GetWindowText(recording_box, recording_name, sizeof recording_name);
    printf("MMSYS playback=%s recording=%s\n", playback_name, recording_name);
    require(recording_box != 0, "the Audio page has the recording device box");
    require(!strcmp(recording_name, "NEO98 Wave In"), "the preferred recording device is NEO98 Wave In");
    require(User::PostMessage(sheet, 0x111, 2, User::GetDlgItem(sheet, 2)), "Cancel");
    until([] { return recorder->Status() == App::Exited; }, "the control panel ends", 30000);
    puts("MMSYS PASS");
    return 0;
}
