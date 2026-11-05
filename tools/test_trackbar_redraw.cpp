// SPDX-License-Identifier: GPL-2.0-or-later
// After a drag, a click into the channel and the arrow keys, a trackbar must
// look exactly as it looks when it is drawn afresh at the same position.
// The runner starts an original program of the pristine C: and drives its
// trackbars with the mouse and the keyboard, the way a hand does:
//
// sndvol-r SNDVOL32.EXE /R, the recording view, on the test microphone:
// the balance and volume trackbars of every column.
// sndvol SNDVOL32.EXE, the playback view.
//
// After every step it copies the trackbar's pixels from the composed
// desktop, invalidates the whole control, lets it paint and copies again.
// Both copies must be equal. The fourth argument is the output directory
// for PPM pictures of each step (before, after, fresh).
// The trackbar record is read from guest memory (Read32).
#define private public
#define main original_probe_main
#include "probe_application.cpp"
#undef main
using App = X86::Application;
static App *app;
static std::string out;
static void require(bool ok, const char *why)
{
    if (ok) return;
    fprintf(stderr, "TRACKBAR FAILED: %s\n", why);
    snapshot_windows();
    fflush(stdout);
    exit(1);
}
static void pump(unsigned milliseconds)
{
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    do {
        app->Pump(32);
        App::PumpChildren(8);
        require(app->Status() != App::Failed, app->Error());
        if (app->Status() == App::Idle) std::this_thread::sleep_for(std::chrono::milliseconds(1));
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
static void descendants(unsigned root, std::vector<unsigned> &all)
{
    for (unsigned child = User::GetWindow(root, 5); child; child = User::GetWindow(child, 2)) {
        all.push_back(child);
        descendants(child, all);
    }
}
// Visible with every ancestor: the trackbars of a property sheet's hidden
// pages keep WS_VISIBLE.
static bool shown(unsigned hwnd)
{
    for (; hwnd; hwnd = User::Parent(hwnd))
        if (!User::IsVisible(hwnd)) return false;
    return true;
}
// The pixels of a window rectangle on the composed desktop.
struct Picture
{
    int w = 0, h = 0;
    std::vector<unsigned> rgb;
    bool operator==(const Picture &o) const { return w == o.w && h == o.h && rgb == o.rgb; }
};
static Picture grab(const Gdi::RECT &r)
{
    int width = 0, height = 0;
    User::PixelsSize(width, height);
    const unsigned char *pixels = User::Pixels();
    require(pixels != nullptr, "a composed desktop");
    Picture p;
    p.w = r.right - r.left;
    p.h = r.bottom - r.top;
    for (int y = r.top; y < r.bottom; ++y)
        for (int x = r.left; x < r.right; ++x) {
            const bool inside = x >= 0 && y >= 0 && x < width && y < height;
            const unsigned i = inside ? unsigned(y * width + x) * 4 : 0;
            p.rgb.push_back(inside ? pixels[i] | pixels[i + 1] << 8 | pixels[i + 2] << 16 : 0);
        }
    return p;
}
static void save(const Picture &p, const std::string &name)
{
    if (out.empty()) return;
    FILE *file = fopen((out + "/" + name + ".ppm").c_str(), "wb");
    if (!file) return;
    const int scale = 4;
    fprintf(file, "P6\n%d %d\n255\n", p.w * scale, p.h * scale);
    for (int y = 0; y < p.h * scale; ++y)
        for (int x = 0; x < p.w * scale; ++x) {
            const unsigned v = p.rgb[(y / scale) * p.w + x / scale];
            const unsigned char c[] = {(unsigned char)(v >> 16), (unsigned char)(v >> 8), (unsigned char)v};
            fwrite(c, 1, 3, file);
        }
    fclose(file);
}
// The runner reads the trackbar's state the way TBM_GETPOS, TBM_GETRANGEMAX
// and TBM_GETTHUMBRECT do, from COMCTL32's own record (window word 0): the
// position at +34h, the maximum at +30h, the thumb
// rectangle at +5Ch, held horizontally and turned for TBS_VERT.
// A send from the runner would enter the guest outside its message loop.
static unsigned field(unsigned slider, unsigned offset)
{
    unsigned record = 0;
    require(User::WindowLong(slider, 0, record, false) && record, "the trackbar's record");
    return app->Read32(record + offset);
}
static unsigned failures;
// The trackbar as it is now against the same trackbar drawn afresh.
// `ring`: leave out the outermost ring of the control. COMCTL32 draws its
// focus frame over the client border on every paint of a focused trackbar
// (: FrameRect, then DrawFocusRect) and its ticks only on a full
// paint, so after a paint of the whole client from its retained bitmap the
// ends of the ticks show the focus frame, in Win98 as here.
static void check(unsigned slider, const char *label, bool ring = false)
{
    Gdi::RECT r{};
    require(User::GetWindowRect(slider, &r), "the trackbar has a place");
    pump(400);
    const Picture now = grab(r);
    const unsigned position = field(slider, 0x34);
    // TBM_SETRANGEMAX with the same maximum and redraw: COMCTL32 marks every
    // part of the control for its next paint and invalidates it whole,
    // so this is the control drawn afresh.
    require(User::PostMessage(slider, 0x408, 1, field(slider, 0x30)), "TBM_SETRANGEMAX");
    pump(400);
    const Picture fresh = grab(r);
    unsigned differ = 0;
    for (size_t i = 0; i < now.rgb.size() && i < fresh.rgb.size(); ++i) {
        const int x = int(i % size_t(now.w)), y = int(i / size_t(now.w));
        if (ring && (x == 0 || y == 0 || x == now.w - 1 || y == now.h - 1)) continue;
        differ += now.rgb[i] != fresh.rgb[i];
    }
    printf("TRACKBAR %s pos=%u differing_pixels=%u %s\n", label, position, differ, differ ? "BAD" : "ok");
    if (differ) {
        ++failures;
        save(now, std::string(label) + "-after");
        save(fresh, std::string(label) + "-fresh");
    }
}
static void press(int x, int y) { app->MouseButtons(x, y, 0); pump(60); app->MouseButtons(x, y, 1); pump(120); }
static void release(int x, int y) { app->MouseButtons(x, y, 0); pump(200); }
static void key(unsigned code)
{
    app->RawKey(code, 0, true, 0, code >= 0x21 && code <= 0x28);
    pump(80);
    app->RawKey(code, 0, false, 0, code >= 0x21 && code <= 0x28);
    pump(150);
}
// The thumb's rectangle in screen coordinates (TBM_GETTHUMBRECT).
static Gdi::RECT thumb(unsigned slider)
{
    Gdi::RECT w{};
    User::GetWindowRect(slider, &w);
    int l = int(field(slider, 0x5c)), t = int(field(slider, 0x60)), r = int(field(slider, 0x64)), b = int(field(slider, 0x68));
    if (User::Style(slider) & 2) { std::swap(l, t); std::swap(r, b); }
    return {l + w.left, t + w.top, r + w.left, b + w.top};
}
static void exercise(unsigned slider, const char *name)
{
    Gdi::RECT w{};
    User::GetWindowRect(slider, &w);
    const bool vertical = User::Style(slider) & 2;
    std::string base = name;
    save(grab(w), base + "-before");
    // Drag the thumb a few pixels along the channel.
    Gdi::RECT t = thumb(slider);
    int x = (t.left + t.right) / 2, y = (t.top + t.bottom) / 2;
    press(x, y);
    for (int i = 1; i <= 6; ++i) { app->MouseButtons(vertical ? x : x - i * 2, vertical ? y + i * 3 : y, 1); pump(60); }
    release(vertical ? x : x - 12, vertical ? y + 18 : y);
    check(slider, (base + "-drag").c_str());
    // Click into the channel beside the thumb: a page.
    t = thumb(slider);
    x = (t.left + t.right) / 2;
    y = (t.top + t.bottom) / 2;
    if (vertical) y = t.top - 3 > w.top + 2 ? t.top - 3 : t.bottom + 3;
    else x = t.right + 3 < w.right - 2 ? t.right + 3 : t.left - 3;
    press(x, y);
    release(x, y);
    check(slider, (base + "-click").c_str());
    // Arrow keys move one line each.
    key(vertical ? 0x28 : 0x25);
    check(slider, (base + "-key1").c_str());
    key(vertical ? 0x26 : 0x27);
    key(vertical ? 0x26 : 0x27);
    check(slider, (base + "-key2").c_str());
}
// The programs and the window each of them shows.
struct Mode
{
    const char *name, *program, *line, *klass, *title;
};
static const Mode modes[] = {
    {"sndvol-r", "SNDVOL32.EXE", "SNDVOL32.EXE /R", "Volume Control", "Recording Control"},
    {"sndvol", "SNDVOL32.EXE", "SNDVOL32.EXE", "Volume Control", "Volume Control"},
    // Display Properties, Settings: the Screen area slider.
    {"desk", "RUNDLL32.EXE", "RUNDLL32.EXE shell32.dll,Control_RunDLL desk.cpl,,3", "#32770", "Display Properties"},
    // Mouse Properties, Motion: Pointer speed and the trail length.
    {"mouse", "RUNDLL32.EXE", "RUNDLL32.EXE shell32.dll,Control_RunDLL main.cpl,@0,2", "#32770", "Mouse Properties"},
};
static unsigned titled(const char *klass_wanted, const char *title_wanted)
{
    unsigned nodes[User::WindowCapacity], n = 0;
    User::WindowSnapshot(User::GetDesktopWindow(), nodes, User::WindowCapacity, n);
    for (unsigned i = 0; i < n; ++i) {
        char klass[128], title[128];
        User::GetClassName(nodes[i], klass, sizeof klass);
        User::GetWindowText(nodes[i], title, sizeof title);
        if (User::IsVisible(nodes[i]) && !User::Parent(nodes[i]) && !strcmp(klass, klass_wanted) && !strcmp(title, title_wanted))
            return nodes[i];
    }
    return 0;
}
static int run(const Mode &mode)
{
    unsigned window = 0;
    until([&] { window = titled(mode.klass, mode.title); return window != 0; }, "the program's window", 90000);
    pump(1500);
    printf("TRACKBAR title=%s\n", mode.title);
    std::vector<unsigned> all;
    descendants(window, all);
    std::vector<unsigned> sliders;
    for (unsigned hwnd : all) {
        char klass[64] = {};
        User::GetClassName(hwnd, klass, sizeof klass);
        if (!strcmp(klass, "msctls_trackbar32") && shown(hwnd) && User::IsEnabled(hwnd)) sliders.push_back(hwnd);
    }
    require(!sliders.empty(), "trackbars in the window");
    // Siblings that overlap a trackbar: their own paint lands on it.
    for (unsigned s : sliders) {
        Gdi::RECT r{};
        User::GetWindowRect(s, &r);
        for (unsigned hwnd : all) {
            Gdi::RECT o{};
            char klass[64] = {};
            User::GetClassName(hwnd, klass, sizeof klass);
            if (hwnd == s || User::Parent(hwnd) != User::Parent(s) || !shown(hwnd) || !User::GetWindowRect(hwnd, &o)) continue;
            if (o.left < r.right && r.left < o.right && o.top < r.bottom && r.top < o.bottom)
                printf("TRACKBAR overlap trackbar=%u (%d,%d,%d,%d) %s=%u (%d,%d,%d,%d) style=%08x\n", s, r.left, r.top, r.right,
                       r.bottom, klass, hwnd, o.left, o.top, o.right, o.bottom, User::Style(hwnd));
        }
    }
    if (!out.empty()) screenshot((out + "/window-before.ppm").c_str());
    // Every trackbar as the program drew it first, against a fresh paint.
    unsigned n = 0;
    for (unsigned s : sliders) {
        char label[32];
        snprintf(label, sizeof label, "t%u-%s-initial", n++, User::Style(s) & 2 ? "vert" : "horz");
        check(s, label);
    }
    n = 0;
    if (getenv("TRACKBAR_TRACE")) { app->trace_calls = true; app->trace_from = 0; puts("TRACKBAR trace on"); }
    for (unsigned s : sliders) {
        if (getenv("TRACKBAR_ONLY") && n != unsigned(atoi(getenv("TRACKBAR_ONLY")))) { ++n; continue; }
        char label[32];
        snprintf(label, sizeof label, "t%u-%s", n++, User::Style(s) & 2 ? "vert" : "horz");
        exercise(s, label);
    }
    // The parent erased and painted again, as when the window is uncovered:
    // the controls paint in the order of their list, each trackbar after the
    // icon in front of it.
    User::InvalidateRect(User::Parent(sliders[0]), nullptr, true);
    n = 0;
    for (unsigned s : sliders) {
        char label[32];
        snprintf(label, sizeof label, "t%u-%s-parent", n++, User::Style(s) & 2 ? "vert" : "horz");
        check(s, label, User::GetFocus() == s);
    }
    if (!out.empty()) screenshot((out + "/window-after.ppm").c_str());
    // A property sheet ends with Cancel, so nothing is applied.
    if (!strcmp(mode.klass, "#32770")) require(User::PostMessage(window, 0x111, 2, User::GetDlgItem(window, 2)), "Cancel");
    else require(User::PostMessage(window, 0x10, 0, 0), "close the window");
    until([] { return app->Status() == App::Exited; }, "the program ends", 30000);
    printf("TRACKBAR failures=%u\n", failures);
    if (failures) return 1;
    puts("TRACKBAR PASS");
    return 0;
}
int main(int argc, char **argv)
{
    if (argc < 4) return 2;
    setvbuf(stdout, nullptr, _IOLBF, 0);
    const char *mode = argv[3];
    if (argc > 4 && argv[4][0] != '-') out = argv[4];
    const auto tree_options = SystemTreeHost::Scan(argc, argv);
    require(tree_options.tree && SystemTreeHost::Mount(tree_options.root), "mount the imported Win98 tree");
    auto drive_options = HostVolume::Scan(argc, argv);
    require(drive_options.drive_c != nullptr, "--drive-c names the writable C:");
    require(HostVolume::Mount(drive_options), "mount the writable C:");
    auto exe = load(argv[1]), font = load(argv[2]);
    // TRACKBAR_TRACE=1 logs every API call to stdout; TRACKBAR_ONLY=n exercises only trackbar n.
    app = new App(getenv("TRACKBAR_TRACE") ? [](const char *line) { fprintf(stdout, "%s\n", line); } : nullptr);
    for (const auto &m : modes)
        if (!strcmp(m.name, mode)) {
            require(app->Start(exe.data(), exe.size(), font.data(), font.size(), m.program, m.line), "start the program");
            return run(m);
        }
    return 2;
}
