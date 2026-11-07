// SPDX-License-Identifier: GPL-2.0-or-later
// Functional check of the DOS process,  a Win32
// program (NOTEPAD.EXE, the root of the session) calls CreateProcessA on a
// DOS program on drive D:, the DOS process opens its `tty` window, the window
// shows what the program printed, a key reaches the program, the program's
// return code arrives through GetExitCodeProcess. Three scenarios, one after
// the other in one session: a.COM, a.BAT that runs it, and a.PIF with a
// title of its own and "Close on exit" off, whose window stays with
// "Finished - " until it is closed.
// probe-dos-vm NOTEPAD.EXE FONT --system-tree --drive D=DIR --output DIR
// [--scenario NAME]... [--time-limit SECONDS]
#define private public
#include "../src/x86_app.h"
#include "../src/sound.h"
#undef private
#include "host_dos_vm.h"
#include "host_comm.h"
#include "host_gpu.h"
#include "host_volume.h"
#include "system_tree_host.h"
#include <atomic>
#include <cassert>
#include <cmath>
#include <vector>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

static std::vector<unsigned char> load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return {};
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    if (n <= 0 || n > 8 * 1024 * 1024) { fclose(f); return {}; }
    std::vector<unsigned char> b(n);
    if (fread(b.data(), 1, b.size(), f) != b.size()) b.clear();
    fclose(f);
    return b;
}
static const char *leaf(const char *s)
{
    const auto *p = strrchr(s, '/');
    return p ? p + 1 : s;
}
static void hex_text(const char *text)
{
    for (const auto *p = reinterpret_cast<const unsigned char *>(text); *p; ++p) printf("%02x", *p);
}
// Sessions whose machine has started, counted from the DOS process's log.
static std::atomic<unsigned> sessions_started{0};
static void logger(const char *s)
{
    if (!strncmp(s, "DOSVM session process=", 22) && !strstr(s, "session=0x0")) ++sessions_started;
    puts(s);
    fflush(stdout);
}
static bool screenshot(const char *directory, const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.ppm", directory, name);
    FILE *file = fopen(path, "wb");
    if (!file) return false;
    fprintf(file, "P6\n800 600\n255\n");
    User::Render();
    const auto *pixels = User::Pixels();
    for (unsigned i = 0; i < 800 * 600; ++i)
    {
        unsigned char rgb[] = {pixels[i * 4 + 2], pixels[i * 4 + 1], pixels[i * 4]};
        if (fwrite(rgb, 1, 3, file) != 3) { fclose(file); return false; }
    }
    if (fclose(file)) return false;
    printf("DOSVM_SHOT name=%s\n", name);
    return true;
}
// How many pixels of the client area of the window are not black, on the
// composed screen, and how many are. Text on the DOS screen lights a few
// hundred; the rest of the DOS screen is black once the machine's picture is
// in the window, while a freshly created window is not painted yet.
static unsigned lit_client_pixels(unsigned hwnd, Gdi::RECT &client, unsigned &black)
{
    client = {};
    black = 0;
    if (!User::ClientScreenRect(hwnd, &client)) return 0;
    User::Render();
    const auto *pixels = User::Pixels();
    unsigned lit = 0;
    // A client area a little taller than the DOS picture carries the toolbar
    // strip above it, whose grey must not count as the program's text. The
    // full screen is 800x600.
    // The text mode in its raster font is 300 pixels high (8x12), the VGA
    // picture 400; either with the 28 pixels of the strip above it.
    const int height = client.bottom - client.top, strip = height % 100 == 28 ? 28 : height > 400 && height <= 450 ? height - 400 : 0;
    for (int y = (client.top < 0 ? 0 : client.top) + strip; y < client.bottom && y < 600; ++y)
        for (int x = client.left < 0 ? 0 : client.left; x < client.right && x < 800; ++x)
        {
            const auto *p = pixels + (y * 800 + x) * 4;
            if (p[0] | p[1] | p[2]) ++lit;
            else ++black;
        }
    return lit;
}
static unsigned find_tty()
{
    unsigned nodes[192], count = 0;
    if (!User::WindowSnapshot(0, nodes, 192, count)) return 0;
    for (unsigned i = 0; i < count; ++i)
    {
        char klass[64];
        User::GetClassName(nodes[i], klass, sizeof klass);
        if (!strcmp(klass, "tty") && User::IsVisible(nodes[i])) return nodes[i];
    }
    return 0;
}
struct Session
{
    X86::Application *app;
    std::chrono::steady_clock::time_point deadline;
    bool pump()
    {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        app->Pump();
        // In the host the audio device clocks the mixer; here the loop does,
        // so the machine's sound reaches the capture buffer.
        Sound::Shared().Advance((unsigned long long)std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        if (!app->SystemRunnable()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        return true;
    }
    // CreateProcessA with the command line, as the shell would call it.
    unsigned create_process(const char *command, unsigned &process_handle)
    {
        process_handle = 0;
        const unsigned line = app->String(command);
        const unsigned startup = app->Allocate(68), information = app->Allocate(16);
        if (!line || !startup || !information) return 0;
        unsigned char zero[68] = {};
        app->Store(startup, zero, sizeof zero);
        app->Write32(startup, 68);
        app->Store(information, zero, 16);
        const unsigned arguments[] = {0, line, 0, 0, 0, 0, 0, 0, startup, information};
        const unsigned result = app->ProcessApi("CreateProcessA", arguments);
        process_handle = app->Read32(information);
        printf("DOSVM_CREATE command=%s result=%u error=%u process=%08x pid=%u\n", command, result,
               app->last_error, process_handle, app->Read32(information + 8));
        fflush(stdout);
        // The thread handle is not needed; left open it would hold the
        // child's slot after the end, as it would under Win98.
        if (const unsigned thread = result ? app->Read32(information + 4) : 0)
        {
            const unsigned close[] = {thread};
            app->ThreadApi("CloseHandle", close);
        }
        app->Free(line);
        app->Free(startup);
        app->Free(information);
        return result;
    }
    unsigned exit_code(unsigned process_handle)
    {
        const unsigned out = app->Allocate(4);
        const unsigned arguments[] = {process_handle, out};
        const unsigned ok = app->ProcessApi("GetExitCodeProcess", arguments);
        const unsigned code = app->Read32(out);
        app->Free(out);
        return ok ? code : 0xffffffffu;
    }
};

static unsigned find_class(const char *wanted, unsigned not_process)
{
    unsigned nodes[192], count = 0;
    if (!User::WindowSnapshot(0, nodes, 192, count)) return 0;
    for (unsigned i = 0; i < count; ++i)
    {
        char klass[64];
        User::GetClassName(nodes[i], klass, sizeof klass);
        if (!strcmp(klass, wanted) && User::IsVisible(nodes[i]) && User::WindowProcess(nodes[i]) != not_process) return nodes[i];
    }
    return 0;
}
// Typed keys: letters and digits by their virtual key and PC scancode, '\r'
// is Enter. The DOS side wants the key, not the character.
static void type_keys(Session &s, const char *keys)
{
    static const struct { char c; unsigned scan; } scans[] = {
        {'a', 0x1e}, {'e', 0x12}, {'i', 0x17}, {'n', 0x31}, {'t', 0x14}, {'x', 0x2d}, {'\r', 0x1c}, {'\t', 0x0f}};
    for (const char *k = keys; *k; ++k)
    {
        unsigned scan = 0;
        for (const auto &entry : scans) if (entry.c == *k) scan = entry.scan;
        const unsigned vk = *k == '\r' ? 0x0d : *k == '\t' ? 0x09 : (unsigned)(*k & 0x5f);
        s.app->RawKey(vk, (unsigned char)*k, true, scan, false);
        for (unsigned i = 0; i < 4; ++i) s.app->Pump();
        s.app->RawKey(vk, 0, false, scan, false);
        for (unsigned i = 0; i < 8; ++i) s.pump();
    }
}
// Alt+Enter: the left Alt held while Enter goes down and up.
static void alt_enter(Session &s)
{
    s.app->KeyboardModifiers(1u << 2);
    for (unsigned i = 0; i < 2; ++i) s.app->Pump();
    s.app->RawKey(0x0d, '\r', true, 0x1c, false);
    for (unsigned i = 0; i < 4; ++i) s.app->Pump();
    s.app->RawKey(0x0d, 0, false, 0x1c, false);
    for (unsigned i = 0; i < 2; ++i) s.app->Pump();
    s.app->KeyboardModifiers(0);
    for (unsigned i = 0; i < 8; ++i) s.pump();
}

struct Scenario
{
    const char *name, *command, *expected_title;
    bool stays;             // the window keeps "Finished - " until it is closed
    unsigned expected_code; // GetExitCodeProcess after the end
    const char *keys;       // typed once the picture is there
    bool full_screen;       // Alt+Enter before the keys, and back
    const char *child_class;// a window of this class must appear: a Windows program DOS started
    unsigned minimum_lit;   // lit pixels of the client area the program's text has to reach
    bool close_question;    // WM_CLOSE while the program runs has to ask, and No keeps it
    bool toolbar_click;     // click the Full screen button of the toolbar, then Alt+Enter back
    const char *action;     // "paste": the clipboard's "a" through the Paste button; "copy": Mark, drag, Copy
};

// Two sessions at once: two HELLO.COM windows, each on an image
// of the core of its own. Both draw their line; the key goes to the newer
// one, which ends with 42 while the older one keeps waiting; then the older
// one takes its key from a click on its caption and ends as well.
static unsigned find_other_tty(unsigned other)
{
    unsigned nodes[192], count = 0;
    if (!User::WindowSnapshot(0, nodes, 192, count)) return 0;
    for (unsigned i = 0; i < count; ++i)
    {
        char klass[64];
        User::GetClassName(nodes[i], klass, sizeof klass);
        if (!strcmp(klass, "tty") && User::IsVisible(nodes[i]) && nodes[i] != other) return nodes[i];
    }
    return 0;
}
static bool twin(Session &s, const char *out)
{
    unsigned first = 0, second = 0, hwnd1 = 0, hwnd2 = 0, black = 0;
    Gdi::RECT client = {};
    if (!s.create_process("D:\\HELLO.COM", first) || !first) { printf("DOSVM_RESULT FAIL twin create\n"); return false; }
    while (s.pump() && !(hwnd1 = find_tty())) {}
    // The machine's picture is there when its text lights the black screen.
    unsigned lit1 = 0;
    while (s.pump() && !((lit1 = lit_client_pixels(hwnd1, client, black)) >= 200 && black > lit1)) {}
    const unsigned started = sessions_started;
    if (!s.create_process("D:\\HELLO.COM", second) || !second) { printf("DOSVM_RESULT FAIL twin create-second\n"); return false; }
    while (s.pump() && !(hwnd2 = find_other_tty(hwnd1))) {}
    // The second window shows what lay under it until its own machine
    // draws, so the start of its session is what the wait looks for.
    unsigned lit2 = 0;
    while (s.pump() && sessions_started == started) {}
    for (unsigned i = 0; i < 20; ++i) s.pump();
    while (s.pump() && !((lit2 = lit_client_pixels(hwnd2, client, black)) >= 200 && black > lit2 &&
                         User::GetForegroundWindow() == hwnd2)) {}
    char title[256] = {};
    User::GetWindowText(hwnd2, title, sizeof title);
    printf("DOSVM_WINDOW scenario=twin hwnd=%u class=tty foreground=%u focus=1 client=%d,%d,%d,%d lit=%u black=%u title=", hwnd2,
           User::GetForegroundWindow() == hwnd2, client.left, client.top, client.right, client.bottom, lit2, black);
    hex_text(title);
    putchar('\n');
    Gdi::RECT first_rect = {}, second_rect = {};
    User::GetWindowRect(hwnd1, &first_rect);
    User::GetWindowRect(hwnd2, &second_rect);
    printf("DOSVM_TWIN windows=%u,%u first=%d,%d second=%d,%d lit_first=%u lit_second=%u\n", hwnd1, hwnd2, first_rect.left,
           first_rect.top, second_rect.left, second_rect.top, lit1, lit2);
    fflush(stdout);
    screenshot(out, "twin-both");
    // The newer window has the keyboard: its program ends, the older runs on.
    type_keys(s, "a");
    unsigned code2 = 259;
    while (s.pump() && (code2 = s.exit_code(second)) == 259)
        if (User::IsWindow(hwnd2))
        {
            char text[256] = {};
            User::GetWindowText(hwnd2, text, sizeof text);
            if (!strncmp(text, "Finished - ", 11)) User::PostMessage(hwnd2, 0x0010, 0, 0);
        }
    for (unsigned i = 0; i < 50; ++i) s.pump();
    const unsigned code1_meanwhile = s.exit_code(first);
    printf("DOSVM_TWIN second_code=%u first_meanwhile=%u\n", code2, code1_meanwhile);
    // The older window: a click on its caption brings it to the front.
    User::GetWindowRect(hwnd1, &first_rect);
    s.app->MouseButtons(first_rect.left + 60, first_rect.top + 10, 0);
    s.app->MouseButtons(first_rect.left + 60, first_rect.top + 10, 1);
    for (unsigned i = 0; i < 4; ++i) s.app->Pump();
    s.app->MouseButtons(first_rect.left + 60, first_rect.top + 10, 0);
    while (s.pump() && User::GetForegroundWindow() != hwnd1) {}
    type_keys(s, "a");
    unsigned code1 = 259;
    while (s.pump() && (code1 = s.exit_code(first)) == 259)
        if (User::IsWindow(hwnd1))
        {
            char text[256] = {};
            User::GetWindowText(hwnd1, text, sizeof text);
            if (!strncmp(text, "Finished - ", 11)) User::PostMessage(hwnd1, 0x0010, 0, 0);
        }
    while (s.pump() && (User::IsWindow(hwnd1) || User::IsWindow(hwnd2))) {}
    const bool gone = !User::IsWindow(hwnd1) && !User::IsWindow(hwnd2);
    printf("DOSVM_EXIT scenario=twin code=%u window_gone=%u windows=%u children=%u\n", code1, gone, User::LiveWindows(),
           X86::Application::LiveChildren());
    for (unsigned process : {first, second})
    {
        const unsigned close[] = {process};
        s.app->ThreadApi("CloseHandle", close);
    }
    const bool okay = code2 == 42 && code1_meanwhile == 259 && code1 == 42 && gone && lit1 >= 200 && lit2 >= 200 &&
                      (first_rect.left != second_rect.left || first_rect.top != second_rect.top);
    printf("DOSVM_RESULT %s twin\n", okay ? "PASS" : "FAIL");
    fflush(stdout);
    return okay;
}

// The sound of a DOS program at the mixer: the capture buffer of the
// silent provider takes the mixed frames while the program plays; its peak
// and loudness say whether the machine's sound arrived.
static std::vector<short> sound_capture(44100 * 2 * 120);
static void sound_measure(Session &s, const char *name)
{
    const unsigned before = Sound::Captured();
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (s.pump() && std::chrono::steady_clock::now() < until) {}
    const unsigned after = Sound::Captured();
    int peak = 0;
    double sum = 0;
    for (unsigned i = before * 2; i < after * 2 && i < sound_capture.size(); ++i)
    {
        const int v = sound_capture[i] < 0 ? -sound_capture[i] : sound_capture[i];
        if (v > peak) peak = v;
        sum += (double)sound_capture[i] * sound_capture[i];
    }
    const unsigned samples = after > before ? (after - before) * 2 : 1;
    printf("DOSVM_SOUND scenario=%s frames=%u peak=%d rms=%u\n", name, after - before, peak, (unsigned)std::sqrt(sum / samples));
    fflush(stdout);
}

// A named key of a game script: virtual key, PC scancode, extended bit.
struct NamedKey { const char *name; unsigned vk, scan; bool extended; };
static const NamedKey named_keys[] = {
    {"enter", 0x0d, 0x1c, false}, {"esc", 0x1b, 0x01, false}, {"space", 0x20, 0x39, false}, {"tab", 0x09, 0x0f, false},
    {"left", 0x25, 0x4b, true}, {"right", 0x27, 0x4d, true}, {"up", 0x26, 0x48, true}, {"down", 0x28, 0x50, true},
    {"f1", 0x70, 0x3b, false}, {"f2", 0x71, 0x3c, false}, {"y", 'Y', 0x15, false}, {"n", 'N', 0x31, false},
    {"1", '1', 0x02, false}, {"2", '2', 0x03, false}, {"s", 'S', 0x1f, false}, {"j", 'J', 0x24, false},
};
// The host's modifier mask bits (tools/neo98_host.cpp modifier_mask).
static unsigned modifier_bit(const char *name)
{
    return !strcmp(name, "ctrl") ? 1u : !strcmp(name, "shift") ? 2u : !strcmp(name, "alt") ? 4u : 0u;
}
// A DOS game from the collection, driven by a script of steps separated by
// ';': wait:SECONDS, shot:NAME, key:NAME (down and up), down:NAME, up:NAME
// (a modifier ctrl, shift, alt goes through the host's modifier mask as the
// host sends it), move:X,Y and click:X,Y in client coordinates below the
// toolbar strip, altenter. Every shot prints DOSVM_GAME lines with the
// title, the client area, the lit pixels and the machine's frame count.
static bool game(Session &s, const char *command, const char *script, const char *out)
{
    unsigned process = 0;
    if (!s.create_process(command, process) || !process) { printf("DOSVM_RESULT FAIL game create\n"); return false; }
    unsigned hwnd = 0;
    while (s.pump() && !(hwnd = find_tty())) {}
    if (!hwnd) { printf("DOSVM_RESULT FAIL game no-window\n"); return false; }
    unsigned modifiers = 0;
    auto run_for = [&](double seconds) {
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds((long)(seconds * 1000));
        while (s.pump() && std::chrono::steady_clock::now() < until) {}
    };
    auto client_point = [&](const char *at, int &x, int &y) {
        Gdi::RECT client = {};
        User::ClientScreenRect(hwnd, &client);
        int cx = 0, cy = 0;
        sscanf(at, "%d,%d", &cx, &cy);
        const int height = client.bottom - client.top, width = client.right - client.left;
        const bool toolbar = height % 100 == 28 || (height - 28) * 4 == width * 3;
        const int strip = (User::Style(hwnd) & 0x80000000u) ? 0 : toolbar ? 28 : 0;
        x = client.left + cx;
        y = client.top + strip + cy;
    };
    std::string steps = script;
    size_t at = 0;
    while (at <= steps.size())
    {
        size_t end = steps.find(';', at);
        if (end == std::string::npos) end = steps.size();
        const std::string step = steps.substr(at, end - at);
        at = end + 1;
        if (step.empty()) continue;
        const size_t colon = step.find(':');
        const std::string verb = step.substr(0, colon), arg = colon == std::string::npos ? "" : step.substr(colon + 1);
        if (verb == "wait") run_for(atof(arg.c_str()));
        else if (verb == "shot")
        {
            Gdi::RECT client = {};
            unsigned black = 0;
            const unsigned lit = User::IsWindow(hwnd) ? lit_client_pixels(hwnd, client, black) : 0;
            char title[256] = {};
            if (User::IsWindow(hwnd)) User::GetWindowText(hwnd, title, sizeof title);
            printf("DOSVM_GAME shot=%s window=%u foreground=%u client=%d,%d,%d,%d lit=%u black=%u style=%08x exstyle=%08x cursor=%u code=%u title=",
                   arg.c_str(), User::IsWindow(hwnd), User::GetForegroundWindow() == hwnd, client.left, client.top, client.right,
                   client.bottom, lit, black, User::IsWindow(hwnd) ? User::Style(hwnd) : 0u,
                   User::IsWindow(hwnd) ? User::ExtendedStyle(hwnd) : 0u, User::PointerShape(), s.exit_code(process));
            hex_text(title);
            putchar('\n');
            fflush(stdout);
            screenshot(out, arg.c_str());
        }
        else if (verb == "key" || verb == "down" || verb == "up")
        {
            if (const unsigned bit = modifier_bit(arg.c_str()))
            {
                if (verb != "up") { modifiers |= bit; s.app->KeyboardModifiers(modifiers); for (unsigned i = 0; i < 4; ++i) s.pump(); }
                if (verb != "down") { modifiers &= ~bit; s.app->KeyboardModifiers(modifiers); for (unsigned i = 0; i < 4; ++i) s.pump(); }
                continue;
            }
            const NamedKey *key = nullptr;
            for (const auto &k : named_keys) if (arg == k.name) key = &k;
            if (!key) { printf("DOSVM_RESULT FAIL game key %s\n", arg.c_str()); return false; }
            if (verb != "up") { s.app->RawKey(key->vk, 0, true, key->scan, key->extended); run_for(verb == "key" ? 0.15 : 0.02); }
            if (verb != "down") { s.app->RawKey(key->vk, 0, false, key->scan, key->extended); run_for(0.05); }
        }
        else if (verb == "move" || verb == "click")
        {
            int x = 0, y = 0;
            client_point(arg.c_str(), x, y);
            s.app->MouseButtons(x, y, 0);
            run_for(0.1);
            if (verb == "click")
            {
                s.app->MouseButtons(x, y, 1);
                run_for(0.1);
                s.app->MouseButtons(x, y, 0);
                run_for(0.1);
            }
        }
        else if (verb == "altenter") alt_enter(s);
        else if (verb == "close")
        {
            if (User::IsWindow(hwnd)) User::PostMessage(hwnd, 0x0010, 0, 0);
            run_for(0.5);
        }
        else { printf("DOSVM_RESULT FAIL game step %s\n", step.c_str()); return false; }
    }
    // The end: close the window if it is still there, and wait for the process.
    const auto end_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    unsigned code = 259;
    while (s.pump() && std::chrono::steady_clock::now() < end_deadline && (code = s.exit_code(process)) == 259)
    {
        if (User::IsWindow(hwnd)) User::PostMessage(hwnd, 0x0010, 0, 0);
        run_for(0.3);
        if (unsigned box = find_class("#32770", s.app->process_id)) { (void)box; type_keys(s, "\r"); }
    }
    printf("DOSVM_GAME end code=%u window_gone=%u\n", code, !User::IsWindow(hwnd));
    printf("DOSVM_RESULT PASS game\n");
    fflush(stdout);
    const unsigned close[] = {process};
    s.app->ThreadApi("CloseHandle", close);
    return true;
}

// One scenario. Answers true when everything matched.
static bool scenario(Session &s, const Scenario &sc, const char *out)
{
    const char *name = sc.name;
    unsigned process = 0;
    if (!strcmp(name, "oldpath"))
    {
        // A path of another machine is not found: CreateProcess fails with
        // ERROR_FILE_NOT_FOUND, as under Win98.
        const unsigned result = s.create_process(sc.command, process);
        const unsigned error = s.app->last_error;
        printf("DOSVM_EXIT scenario=%s code=%u window_gone=1 windows=%u children=%u\n", name, error, User::LiveWindows(),
               X86::Application::LiveChildren());
        const bool okay = !result && error == 2;
        printf("DOSVM_RESULT %s %s\n", okay ? "PASS" : "FAIL", name);
        fflush(stdout);
        return okay;
    }
    if (!s.create_process(sc.command, process) || !process)
    {
        printf("DOSVM_RESULT FAIL %s create\n", name);
        return false;
    }
    unsigned hwnd = 0;
    while (s.pump() && !(hwnd = find_tty())) {}
    if (!hwnd) { printf("DOSVM_RESULT FAIL %s no-window\n", name); return false; }
    // The picture: wait until the machine's screen is in the window (mostly
    // black) and the program's text lights enough of it, and until the
    // window is the foreground window that takes the keyboard.
    Gdi::RECT client = {};
    unsigned lit = 0, black = 0;
    const auto picture_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (s.pump() && std::chrono::steady_clock::now() < picture_deadline)
    {
        lit = lit_client_pixels(hwnd, client, black);
        if (lit >= sc.minimum_lit && black > lit && User::GetForegroundWindow() == hwnd) break;
    }
    char klass[64] = {}, title[256] = {};
    User::GetClassName(hwnd, klass, sizeof klass);
    User::GetWindowText(hwnd, title, sizeof title);
    printf("DOSVM_WINDOW scenario=%s hwnd=%u class=%s foreground=%u focus=%u client=%d,%d,%d,%d lit=%u black=%u title=", name,
           hwnd, klass, User::GetForegroundWindow() == hwnd, User::WindowProcess(User::GetFocus()) == User::WindowProcess(hwnd),
           client.left, client.top, client.right, client.bottom, lit, black);
    hex_text(title);
    putchar('\n');
    fflush(stdout);
    char shot[64];
    snprintf(shot, sizeof shot, "%s-running", name);
    screenshot(out, shot);
    const Gdi::RECT windowed = client;
    if (!strcmp(name, "beep") || !strcmp(name, "adlib") || !strcmp(name, "midi") || !strcmp(name, "sblaster")) sound_measure(s, name);
    bool sheet_failed = false;
    if (!strcmp(name, "sheet"))
    {
        // The Properties button of the strip opens the window's sheet; the
        // machine plays on meanwhile, then Cancel closes the sheet.
        const int bx = client.left + 184 + 11, by = client.top + 14;
        s.app->MouseButtons(bx, by, 0); s.app->MouseButtons(bx, by, 1);
        for (unsigned i = 0; i < 4; ++i) s.app->Pump();
        s.app->MouseButtons(bx, by, 0);
        unsigned sheet = 0;
        const auto sheet_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (s.pump() && std::chrono::steady_clock::now() < sheet_deadline && !(sheet = find_class("#32770", 0))) {}
        printf("DOSVM_SHEET scenario=%s sheet=%u\n", name, sheet);
        fflush(stdout);
        if (!sheet) sheet_failed = true;
        for (unsigned i = 0; i < 20; ++i) s.pump();
        sound_measure(s, name);
        screenshot(out, "sheet-open");
        if (sheet) User::PostMessage(sheet, 0x0111, 2, User::GetDlgItem(sheet, 2));
        const auto close_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (s.pump() && std::chrono::steady_clock::now() < close_deadline && sheet && User::IsWindow(sheet)) {}
        while (s.pump() && std::chrono::steady_clock::now() < close_deadline && User::GetForegroundWindow() != hwnd) {}
    }
    // A short program may already have ended when the picture is there; its
    // window then says "Finished - " before the title.
    char finished_title[300];
    snprintf(finished_title, sizeof finished_title, "Finished - %s", sc.expected_title);
    bool okay = !strcmp(klass, "tty") && lit >= sc.minimum_lit &&
                (!sc.expected_title[0] || !strcmp(title, sc.expected_title) || (sc.stays && !strcmp(title, finished_title)));
    if (sheet_failed) okay = false;
    if (sc.full_screen)
    {
        // Alt+Enter: the client area has to cover the screen, the picture
        // stretched into it; Alt+Enter again brings the window back.
        alt_enter(s);
        const auto full_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        unsigned full_lit = 0, full_black = 0;
        Gdi::RECT full = {};
        while (s.pump() && std::chrono::steady_clock::now() < full_deadline)
        {
            full_lit = lit_client_pixels(hwnd, full, full_black);
            if (full.left == 0 && full.top == 0 && full.right == 800 && full.bottom == 600 && full_lit >= sc.minimum_lit) break;
        }
        printf("DOSVM_FULLSCREEN scenario=%s client=%d,%d,%d,%d lit=%u black=%u style=%08x\n", name, full.left, full.top, full.right,
               full.bottom, full_lit, full_black, User::Style(hwnd));
        snprintf(shot, sizeof shot, "%s-fullscreen", name);
        screenshot(out, shot);
        // The window's own lit count includes the toolbar strip, so the full
        // screen only has to light the program's text.
        const bool covered = full.left == 0 && full.top == 0 && full.right == 800 && full.bottom == 600 && full_lit >= sc.minimum_lit;
        alt_enter(s);
        const auto back_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        Gdi::RECT back = {};
        unsigned back_lit = 0, back_black = 0;
        while (s.pump() && std::chrono::steady_clock::now() < back_deadline)
        {
            back_lit = lit_client_pixels(hwnd, back, back_black);
            if (back.left == windowed.left && back.top == windowed.top && back.right == windowed.right && back.bottom == windowed.bottom) break;
        }
        printf("DOSVM_WINDOWED scenario=%s client=%d,%d,%d,%d lit=%u style=%08x\n", name, back.left, back.top, back.right, back.bottom,
               back_lit, User::Style(hwnd));
        const bool restored = back.left == windowed.left && back.top == windowed.top && back.right == windowed.right &&
                              back.bottom == windowed.bottom && back_lit >= sc.minimum_lit;
        if (!covered || !restored) okay = false;
    }
    unsigned child = 0;
    if (sc.child_class)
    {
        // A Windows program the DOS side started: its window belongs to
        // another process than the root of the session.
        const auto child_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (s.pump() && std::chrono::steady_clock::now() < child_deadline && !(child = find_class(sc.child_class, s.app->process_id))) {}
        printf("DOSVM_CHILD scenario=%s class=%s hwnd=%u process=%u\n", name, sc.child_class, child, child ? User::WindowProcess(child) : 0);
        snprintf(shot, sizeof shot, "%s-child", name);
        screenshot(out, shot);
        if (!child) okay = false;
        // Win98 activates the program the batch file started; the keys of
        // the scenario belong to the DOS window, so the user closes the
        // program first and Windows gives the activation back to the DOS
        // window. Typing into the covered DOS window only worked while the
        // program had not been activated yet.
        if (child)
        {
            User::PostMessage(child, 0x0010, 0, 0); // WM_CLOSE
            const auto closed_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            while (s.pump() && std::chrono::steady_clock::now() < closed_deadline &&
                   (User::IsWindow(child) || User::GetForegroundWindow() != hwnd)) {}
            printf("DOSVM_CHILD_BACK scenario=%s gone=%u foreground=%u\n", name, !User::IsWindow(child),
                   User::GetForegroundWindow() == hwnd);
            fflush(stdout);
            if (User::IsWindow(child) || User::GetForegroundWindow() != hwnd) okay = false;
        }
    }
    if (sc.toolbar_click)
    {
        // The toolbar: 28 pixels above the DOS screen, the Full screen
        // button is the fourth, after the gap. Its click covers the screen.
        const int bx = client.left + 4 + 62 + 8 + 3 * 23 + 8 + 11, by = client.top + 3 + 11;
        s.app->MouseButtons(bx, by, 0);
        s.app->MouseButtons(bx, by, 1);
        for (unsigned i = 0; i < 4; ++i) s.app->Pump();
        s.app->MouseButtons(bx, by, 0);
        const auto full_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        Gdi::RECT full = {};
        unsigned full_lit = 0, full_black = 0;
        while (s.pump() && std::chrono::steady_clock::now() < full_deadline)
        {
            full_lit = lit_client_pixels(hwnd, full, full_black);
            if (full.left == 0 && full.top == 0 && full.right == 800 && full.bottom == 600) break;
        }
        // The strip above a text picture of 300 (8x12 cells) or a VGA picture of 400.
        const int client_height = client.bottom - client.top;
        const int measured_strip = client_height % 100 == 28 ? client_height - (client_height / 100) * 100 : client_height - 400;
        printf("DOSVM_TOOLBAR scenario=%s strip=%d client=%d,%d,%d,%d lit=%u\n", name, measured_strip, full.left, full.top,
               full.right, full.bottom, full_lit);
        if (!(full.left == 0 && full.top == 0 && full.right == 800 && full.bottom == 600)) okay = false;
        alt_enter(s);
        for (unsigned i = 0; i < 20; ++i) s.pump();
    }
    if (sc.action && !strcmp(sc.action, "paste"))
    {
        // The clipboard of the session holds "a"; the Paste button types it.
        const unsigned open[] = {0};
        assert(s.app->Api("OpenClipboard", open));
        assert(s.app->Api("EmptyClipboard", open));
        const unsigned alloc[] = {0x2042, 2};
        const unsigned handle = s.app->Api("GlobalAlloc", alloc);
        assert(handle);
        const unsigned lock[] = {handle};
        const unsigned pointer = s.app->Api("GlobalLock", lock);
        assert(pointer && s.app->Store(pointer, "a", 2));
        s.app->Api("GlobalUnlock", lock);
        const unsigned set[] = {1, handle};
        assert(s.app->Api("SetClipboardData", set) == handle);
        assert(s.app->Api("CloseClipboard", open));
        const int bx = client.left + 4 + 62 + 8 + 2 * 23 + 11, by = client.top + 3 + 11;
        s.app->MouseButtons(bx, by, 0);
        s.app->MouseButtons(bx, by, 1);
        for (unsigned i = 0; i < 4; ++i) s.app->Pump();
        s.app->MouseButtons(bx, by, 0);
        printf("DOSVM_PASTE scenario=%s handle=%u\n", name, handle);
    }
    if (sc.action && !strcmp(sc.action, "copy"))
    {
        // Mark, drag over the first 24 cells of the top line, Copy: the
        // clipboard then carries the line the program printed.
        // The toolbar strip is 28 pixels; the text rows start below it.
        const int strip = 28;
        const int mark_x = client.left + 4 + 62 + 8 + 11, copy_x = mark_x + 23, by = client.top + 3 + 11;
        s.app->MouseButtons(mark_x, by, 0); s.app->MouseButtons(mark_x, by, 1);
        for (unsigned i = 0; i < 4; ++i) s.app->Pump();
        s.app->MouseButtons(mark_x, by, 0);
        for (unsigned i = 0; i < 4; ++i) s.pump();
        const int x0 = client.left + 3, y0 = client.top + strip + 8, x1 = client.left + 23 * 8 + 3;
        s.app->MouseButtons(x0, y0, 0); s.app->MouseButtons(x0, y0, 1);
        for (unsigned i = 0; i < 4; ++i) s.pump();
        s.app->MouseButtons((x0 + x1) / 2, y0, 1);
        for (unsigned i = 0; i < 4; ++i) s.pump();
        s.app->MouseButtons(x1, y0, 1);
        for (unsigned i = 0; i < 4; ++i) s.pump();
        s.app->MouseButtons(x1, y0, 0);
        for (unsigned i = 0; i < 40; ++i) s.pump();
        snprintf(shot, sizeof shot, "%s-marked", name);
        screenshot(out, shot);
        {
            // The marked block is drawn inverted: white cells on the black screen.
            Gdi::RECT c = {};
            unsigned b = 0;
            const unsigned lit_marked = lit_client_pixels(hwnd, c, b);
            printf("DOSVM_MARKED scenario=%s lit=%u\n", name, lit_marked);
            if (lit_marked < lit + 1000) okay = false;
        }
        s.app->MouseButtons(copy_x, by, 0); s.app->MouseButtons(copy_x, by, 1);
        for (unsigned i = 0; i < 4; ++i) s.app->Pump();
        s.app->MouseButtons(copy_x, by, 0);
        for (unsigned i = 0; i < 8; ++i) s.pump();
        const unsigned open[] = {0};
        char copied[512] = {};
        if (s.app->Api("OpenClipboard", open))
        {
            const unsigned format[] = {1};
            const unsigned handle = s.app->Api("GetClipboardData", format);
            if (handle)
            {
                const unsigned lock[] = {handle};
                const unsigned pointer = s.app->Api("GlobalLock", lock);
                snprintf(copied, sizeof copied, "%s", s.app->ReadString(pointer ? pointer : handle));
            }
            s.app->Api("CloseClipboard", open);
        }
        printf("DOSVM_COPY scenario=%s text=", name);
        hex_text(copied);
        putchar('\n');
        if (strcmp(copied, "NEO98 DOS VM press a key")) okay = false;
    }
    if (sc.close_question)
    {
        // The close box while the program runs: WINOLDAP's question, a
        // Yes/No box with the window's title; No keeps the program.
        User::PostMessage(hwnd, 0x0010, 0, 0);
        unsigned box = 0;
        const auto box_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (s.pump() && std::chrono::steady_clock::now() < box_deadline && !(box = find_class("#32770", s.app->process_id))) {}
        char box_title[256] = {};
        if (box) User::GetWindowText(box, box_title, sizeof box_title);
        printf("DOSVM_CLOSEQ scenario=%s box=%u title=", name, box);
        hex_text(box_title);
        putchar('\n');
        snprintf(shot, sizeof shot, "%s-question", name);
        screenshot(out, shot);
        if (!box || strcmp(box_title, title)) okay = false;
        if (box)
        {
            // Tab to the No button, Enter: the default button is Yes.
            type_keys(s, "\t\r");
            const auto gone_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (s.pump() && std::chrono::steady_clock::now() < gone_deadline && User::IsWindow(box)) {}
            if (User::IsWindow(box) || !User::IsWindow(hwnd)) okay = false;
        }
    }
    if (!strcmp(name, "fontbox"))
    {
        // The size box of the strip: a click opens its list, a click on
        // "8 x 8" (Auto, 4x6, 5x12, 6x8, 7x12, 10x18, 12x16, 8x8: the eighth
        // item, 16 pixels each) chooses it.
        unsigned box = 0, items = 0;
        {
            unsigned nodes[64], count = 0;
            if (User::WindowSnapshot(hwnd, nodes, 64, count))
                for (unsigned i = 0; i < count && !box; ++i)
                {
                    char klass[32] = {};
                    User::GetClassName(nodes[i], klass, sizeof klass);
                    if (!strcmp(klass, "ComboBox") || !strcmp(klass, "COMBOBOX")) box = nodes[i];
                }
        }
        Gdi::RECT box_rect = {};
        if (box && User::ClientScreenRect(box, &box_rect))
        {
            items = box; // the box exists; its list belongs to the DOS process, which a send from here would block
            const int ax = box_rect.right - 8, ay = (box_rect.top + box_rect.bottom) / 2;
            s.app->MouseButtons(ax, ay, 0); s.app->MouseButtons(ax, ay, 1);
            for (unsigned i = 0; i < 4; ++i) s.app->Pump();
            s.app->MouseButtons(ax, ay, 0);
            for (unsigned i = 0; i < 20; ++i) s.pump();
            screenshot(out, "fontbox-open");
            const int ix = box_rect.left + 20, iy = box_rect.bottom + 2 + 7 * 16 + 8;
            s.app->MouseButtons(ix, iy, 0); s.app->MouseButtons(ix, iy, 1);
            for (unsigned i = 0; i < 4; ++i) s.app->Pump();
            s.app->MouseButtons(ix, iy, 0);
        }
        Gdi::RECT now = {};
        const auto size_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (s.pump() && std::chrono::steady_clock::now() < size_deadline &&
               !(User::ClientScreenRect(hwnd, &now) && now.bottom - now.top == 228)) {}
        for (unsigned i = 0; i < 20; ++i) s.pump();
        User::ClientScreenRect(hwnd, &now);
        printf("DOSVM_FONTBOX items=%u client=%dx%d\n", items, now.right - now.left, now.bottom - now.top);
        fflush(stdout);
        screenshot(out, "fontbox-chosen");
        if (now.bottom - now.top != 228) okay = false;
        // The keyboard is the DOS window's again.
        while (s.pump() && std::chrono::steady_clock::now() < size_deadline && User::GetForegroundWindow() != hwnd) {}
    }
    if (!strcmp(name, "maxfont"))
    {
        // The maximized window and the size box. WINOLDAP's
        // maximized window is as large as its text, in the upper left corner
        // with the frame outside the screen; with Auto the font becomes the
        // largest that fits so (10 x 18 on 800x600). Another size from the
        // box makes it smaller, still maximized and still in the corner;
        // Restore brings the window back with that size.
        auto style = [&]() { return User::Style(hwnd); };
        auto settle = [&](auto done) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
            while (s.pump() && std::chrono::steady_clock::now() < deadline && !done()) {}
            for (unsigned i = 0; i < 20; ++i) s.pump();
        };
        Gdi::RECT before = {}, zoomed = {}, zoomed_client = {}, chosen = {}, chosen_client = {}, restored = {}, restored_client = {};
        User::GetWindowRect(hwnd, &before);
        User::PostMessage(hwnd, 0x112, 0xf030, 0); // SC_MAXIMIZE
        settle([&] { return (style() & 0x01000000u) && User::ClientScreenRect(hwnd, &zoomed_client) &&
                            zoomed_client.right - zoomed_client.left == 800; });
        User::GetWindowRect(hwnd, &zoomed);
        User::ClientScreenRect(hwnd, &zoomed_client);
        const unsigned zoomed_style = style();
        screenshot(out, "maxfont-maximized");
        unsigned box = 0;
        {
            unsigned nodes[64], count = 0;
            if (User::WindowSnapshot(hwnd, nodes, 64, count))
                for (unsigned i = 0; i < count && !box; ++i)
                {
                    char klass[32] = {};
                    User::GetClassName(nodes[i], klass, sizeof klass);
                    if (!strcmp(klass, "ComboBox") || !strcmp(klass, "COMBOBOX")) box = nodes[i];
                }
        }
        Gdi::RECT box_rect = {};
        if (box && User::ClientScreenRect(box, &box_rect))
        {
            const int ax = box_rect.right - 8, ay = (box_rect.top + box_rect.bottom) / 2;
            s.app->MouseButtons(ax, ay, 0); s.app->MouseButtons(ax, ay, 1);
            for (unsigned i = 0; i < 4; ++i) s.app->Pump();
            s.app->MouseButtons(ax, ay, 0);
            for (unsigned i = 0; i < 20; ++i) s.pump();
            const int ix = box_rect.left + 20, iy = box_rect.bottom + 2 + 7 * 16 + 8; // "8 x 8"
            s.app->MouseButtons(ix, iy, 0); s.app->MouseButtons(ix, iy, 1);
            for (unsigned i = 0; i < 4; ++i) s.app->Pump();
            s.app->MouseButtons(ix, iy, 0);
        }
        settle([&] { return User::ClientScreenRect(hwnd, &chosen_client) && chosen_client.bottom - chosen_client.top == 228; });
        User::GetWindowRect(hwnd, &chosen);
        User::ClientScreenRect(hwnd, &chosen_client);
        const unsigned chosen_style = style();
        screenshot(out, "maxfont-chosen");
        User::PostMessage(hwnd, 0x112, 0xf120, 0); // SC_RESTORE
        settle([&] { return !(style() & 0x01000000u); });
        User::GetWindowRect(hwnd, &restored);
        User::ClientScreenRect(hwnd, &restored_client);
        const unsigned restored_style = style();
        screenshot(out, "maxfont-restored");
        auto size = [](const Gdi::RECT &r, char *text, size_t capacity) {
            snprintf(text, capacity, "%d,%d,%dx%d", r.left, r.top, r.right - r.left, r.bottom - r.top);
            return text;
        };
        char a[7][48];
        printf("DOSVM_MAXFONT before=%s maximized=%s client=%s zoomed=%u chosen=%s client=%s zoomed=%u restored=%s client=%s zoomed=%u\n",
               size(before, a[0], 48), size(zoomed, a[1], 48), size(zoomed_client, a[2], 48), zoomed_style >> 24 & 1,
               size(chosen, a[3], 48), size(chosen_client, a[4], 48), chosen_style >> 24 & 1,
               size(restored, a[5], 48), size(restored_client, a[6], 48), restored_style >> 24 & 1);
        fflush(stdout);
        if (!(zoomed_style & 0x01000000u) || !(chosen_style & 0x01000000u) || (restored_style & 0x01000000u)) okay = false;
        const auto front = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (s.pump() && std::chrono::steady_clock::now() < front && User::GetForegroundWindow() != hwnd) {}
    }
    if (!strcmp(name, "hotkey"))
    {
        // The shortcut key of the PIF (Ctrl+Alt+H): Notepad below comes to
        // the front with a click on its uncovered part, the key brings the
        // MS-DOS window back.
        Gdi::RECT window_rect = {};
        User::GetWindowRect(hwnd, &window_rect);
        const int x = 400, y = window_rect.bottom + 20;
        s.app->MouseButtons(x, y, 0); s.app->MouseButtons(x, y, 1);
        for (unsigned i = 0; i < 4; ++i) s.app->Pump();
        s.app->MouseButtons(x, y, 0);
        const auto away_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (s.pump() && std::chrono::steady_clock::now() < away_deadline && User::GetForegroundWindow() == hwnd) {}
        const bool away = User::GetForegroundWindow() != hwnd;
        s.app->KeyboardModifiers(1u | 4u);
        for (unsigned i = 0; i < 2; ++i) s.app->Pump();
        s.app->RawKey('H', 0, true, 0x23, false);
        for (unsigned i = 0; i < 4; ++i) s.app->Pump();
        s.app->RawKey('H', 0, false, 0x23, false);
        s.app->KeyboardModifiers(0);
        const auto back_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (s.pump() && std::chrono::steady_clock::now() < back_deadline && User::GetForegroundWindow() != hwnd) {}
        const bool back = User::GetForegroundWindow() == hwnd;
        printf("DOSVM_HOTKEY away=%u back=%u\n", away ? 1u : 0u, back ? 1u : 0u);
        fflush(stdout);
        if (!away || !back) okay = false;
    }
    bool fullend_failed = false;
    if (!strcmp(name, "fullend"))
    {
        // The PIF starts in the full screen: the window covers the screen
        // above the topmost windows, the taskbar included.
        Gdi::RECT full = {};
        User::ClientScreenRect(hwnd, &full);
        const unsigned extended = User::ExtendedStyle(hwnd);
        printf("DOSVM_FULLEND running client=%d,%d,%d,%d topmost=%u\n", full.left, full.top, full.right, full.bottom,
               (extended & 8) ? 1u : 0u);
        if (!(full.left == 0 && full.top == 0 && full.right == 800 && full.bottom == 600) || !(extended & 8)) fullend_failed = true;
    }
    if (!strcmp(name, "mouseabs"))
    {
        const int height = client.bottom-client.top;
        const int strip = height%100==28 ? 28 : 0;
        s.app->MouseButtons(client.left + (client.right-client.left)/2,
                            client.top + strip + (height-strip)/2, 0);
        for(unsigned i=0;i<50;++i)s.pump();
    }
    if (!strcmp(name, "mouse"))
    {
        // The Windows pointer over the picture of mode 13h (320x200, shown
        // doubled below the toolbar strip): first somewhere else, then at
        // (200, 100) of the picture area, which is DOS pixel (100, 50) and
        // INT 33h place (200, 50).
        const int strip = (client.bottom - client.top) - 400;
        s.app->MouseButtons(client.left + 600, client.top + strip + 350, 0);
        for (unsigned i = 0; i < 20; ++i) s.pump();
        for (int step = 1; step <= 8; ++step)
        {
            s.app->MouseButtons(client.left + 600 - step * 50, client.top + strip + 350 - step * 250 / 8, 0);
            for (unsigned i = 0; i < 6; ++i) s.pump();
        }
        for (unsigned i = 0; i < 30; ++i) s.pump();
        printf("DOSVM_MOUSE strip=%d at=%d,%d\n", strip, 200, 100);
    }
    if (!strcmp(name, "keys"))
    {
        // Right held for a second, then Esc: the program's own INT 9
        // handler has to see the make code, typematic repeats and the break
        // code only at the release, the way Mario & Luigi reads the keys

        s.app->RawKey(0x27, 0, true, 0x4d, true);
        const auto held_until = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (s.pump() && std::chrono::steady_clock::now() < held_until) {}
        s.app->RawKey(0x27, 0, false, 0x4d, true);
        for (unsigned i = 0; i < 30; ++i) s.pump();
        s.app->RawKey(0x1b, 0, true, 0x01, false);
        for (unsigned i = 0; i < 8; ++i) s.pump();
        s.app->RawKey(0x1b, 0, false, 0x01, false);
    }
    if (sc.keys[0]) type_keys(s, sc.keys);
    unsigned code = 259;
    if (sc.stays)
    {
        // The window keeps the last screen and "Finished - " in the title.
        char finished[256] = {};
        while (s.pump() && User::IsWindow(hwnd) && (User::GetWindowText(hwnd, finished, sizeof finished), strncmp(finished, "Finished - ", 11))) {}
        code = s.exit_code(process);
        printf("DOSVM_FINISHED scenario=%s window=%u code=%u title=", name, User::IsWindow(hwnd), code);
        hex_text(finished);
        putchar('\n');
        snprintf(shot, sizeof shot, "%s-finished", name);
        screenshot(out, shot);
        if (!User::IsWindow(hwnd) || strncmp(finished, "Finished - ", 11)) { printf("DOSVM_RESULT FAIL %s not-finished\n", name); return false; }
        if (!strcmp(name, "fullend"))
        {
            // The end gives the display back: the window with its frame at
            // its place, no longer topmost.
            for (unsigned i = 0; i < 10; ++i) s.pump();
            Gdi::RECT back = {};
            User::ClientScreenRect(hwnd, &back);
            const unsigned style = User::Style(hwnd), extended = User::ExtendedStyle(hwnd);
            printf("DOSVM_FULLEND finished client=%d,%d,%d,%d style=%08x topmost=%u\n", back.left, back.top, back.right, back.bottom,
                   style, (extended & 8) ? 1u : 0u);
            if ((style & 0x80000000u) || !(style & 0x00c00000u) || (extended & 8) || back.right - back.left >= 800) fullend_failed = true;
        }
        User::PostMessage(hwnd, 0x0010, 0, 0); // WM_CLOSE
    }
    while (s.pump() && (code = s.exit_code(process)) == 259) {}
    const bool window_gone = !User::IsWindow(hwnd);
    printf("DOSVM_EXIT scenario=%s code=%u window_gone=%u windows=%u children=%u\n", name, code, window_gone,
           User::LiveWindows(), X86::Application::LiveChildren());
    fflush(stdout);
    const unsigned close[] = {process};
    s.app->ThreadApi("CloseHandle", close);
    if (child && User::IsWindow(child))
    {
        // The Windows program outlives the DOS box, as under Win98; close it
        // so the next scenario starts clean.
        User::PostMessage(child, 0x0010, 0, 0);
        const auto gone_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (s.pump() && std::chrono::steady_clock::now() < gone_deadline && User::IsWindow(child)) {}
        printf("DOSVM_CHILD_CLOSED scenario=%s gone=%u children=%u\n", name, !User::IsWindow(child), X86::Application::LiveChildren());
    }
    okay = okay && code == sc.expected_code && window_gone && !fullend_failed;
    printf("DOSVM_RESULT %s %s\n", okay ? "PASS" : "FAIL", name);
    fflush(stdout);
    return okay;
}

int main(int argc, char **argv)
{
    if (argc < 3) return 2;
    const char *out = nullptr;
    int limit = 120;
    std::vector<std::string> scenarios;
    const char *game_command = nullptr, *game_script = nullptr;
    const auto tree_options = SystemTreeHost::Scan(argc, argv);
    if (!tree_options.tree || !SystemTreeHost::Mount(tree_options.root))
    {
        fprintf(stderr, "the imported Win98 tree is needed\n");
        return 2;
    }
    const auto drive_options = HostVolume::Scan(argc, argv);
    if (!HostVolume::Mount(drive_options)) { fprintf(stderr, "cannot mount the host drives\n"); return 13; }
    for (int i = 3; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--system-tree")) { if (i + 1 < argc && argv[i + 1][0] != '-') ++i; continue; }
        if (!strcmp(argv[i], "--drive") || !strcmp(argv[i], "--drive-c") || !strcmp(argv[i], "--drive-readonly")) { ++i; continue; }
        if (!strcmp(argv[i], "--ram-drive")) continue;
        if (!strcmp(argv[i], "--output") && i + 1 < argc) { out = argv[++i]; continue; }
        if (!strcmp(argv[i], "--time-limit") && i + 1 < argc) { limit = atoi(argv[++i]); continue; }
        if (!strcmp(argv[i], "--scenario") && i + 1 < argc) { scenarios.push_back(argv[++i]); continue; }
        if (!strcmp(argv[i], "--game") && i + 2 < argc) { game_command = argv[++i]; game_script = argv[++i]; continue; }
        fprintf(stderr, "unknown argument %s\n", argv[i]);
        return 2;
    }
    if (!out) return 2;
    // The drives of the DOS side: the Win98 tree as C: with a memory overlay,
    // the host directories of the Windows side under their letters.
    std::vector<HostDosVm::Drive> drives;
    const char *tree_root = tree_options.root ? tree_options.root : SystemTreeHost::DefaultRoot();
    drives.push_back({'C', tree_root, true, drive_options.drive_c});
    for (unsigned i = 0; i < drive_options.count; ++i)
        if (drive_options.extra[i].letter && drive_options.extra[i].letter != 'C' && !drive_options.extra[i].cdrom)
            drives.push_back({drive_options.extra[i].letter, drive_options.extra[i].directory, false});
    // The serial ports with an endpoint in NEO98_COM1..4.
    // NEO98_PROBE_COM_HELD=n: a Windows program holds COMn during the run.
    static HostComm::Ports serial_ports;
    serial_ports.Install();
    if (const char *held = getenv("NEO98_PROBE_COM_HELD"); held && *held >= '1' && *held <= '4')
        if (const CommHost::Provider *h = CommHost::Current()) h->Open(h->owner, unsigned(*held - '1'), CommHost::ProcessHolder(1));
    HostDosVm::Install(logger, drives.data(), (unsigned)drives.size());
    // NEO98_PROBE_GPU: the GPU backend, so the 3dfx Voodoo of a machine is
    // drawn by it; without, the machine draws the card in software.
    if (getenv("NEO98_PROBE_GPU")) X86::Application::SetGpuBackend(HostGpu::Install());
    // The mixer has no speaker here; its frames go into the capture buffer.
    Sound::SetCapture(sound_capture.data(), (unsigned)(sound_capture.size() / 2));
    auto exe = load(argv[1]), font = load(argv[2]);
    if (exe.empty() || font.empty()) return 3;
    {
        char guest[260];
        snprintf(guest, sizeof guest, "C:\\WINDOWS\\%s", leaf(argv[1]));
        Files::System().Seed(guest, exe.data(), exe.size());
    }
    auto *a = new X86::Application(logger);
    if (getenv("DOSVM_TRACE")) { a->trace_calls = true; a->trace_from = 1; }
    if (!a->Start(exe.data(), exe.size(), font.data(), font.size(), leaf(argv[1]), nullptr)) return 3;
    Session s{a, std::chrono::steady_clock::now() + std::chrono::seconds(limit)};
    while (s.pump() && !a->WaitingForInput()) {}
    if (!a->WaitingForInput()) { printf("DOSVM_RESULT FAIL root-not-idle\n"); return 4; }
    if (game_command)
    {
        const bool played = game(s, game_command, game_script, out);
        printf("DOSVM_SUMMARY %s windows=%u children=%u calls=%u\n", played ? "PASS" : "FAIL", User::LiveWindows(),
               X86::Application::LiveChildren(), a->ApiCalls());
        fflush(stdout);
        a->TerminateSelf(0);
        X86::Application::TerminateChildren(0);
        delete a;
        HostDosVm::Uninstall();
        return played ? 0 : 5;
    }
    if (scenarios.empty()) scenarios = {"com", "bat", "pif", "prompt", "winbat", "full", "closeq", "conbat", "toolbar", "overlay", "paste", "copy", "longdir", "dirlist", "rows", "mode", "low", "twin", "beep", "adlib", "midi", "sblaster", "sheet", "hotkey", "truetype", "fontbox", "maxfont", "oldpath", "keys", "fullend", "mouse", "mouseabs", "besidepif", "quoted", "quotwin", "emptybat"};
    static const Scenario table[] = {
        // A program without a PIF keeps its window with "Finished -" until it
        // is closed: that is the default PIF of PIFMGR, which sets
        // "Close on exit" only for the command interpreter.
        {"com", "D:\\HELLO.COM", "HELLO", true, 42, "a", false, nullptr, 200, false, false, nullptr},
        {"bat", "D:\\HELLO.BAT", "HELLO", true, 42, "a", false, nullptr, 200, false, false, nullptr},
        {"pif", "D:\\PROBE.PIF", "Probe Title", true, 42, "a", false, nullptr, 200, false, false, nullptr},
        // The MS-DOS prompt of the Start menu: the prompt, `exit` ends it.
        {"prompt", "C:\\WINDOWS\\DOSPRMPT.PIF", "MS-DOS Prompt", false, 0, "exit\r", false, nullptr, 150, false, false, nullptr},
        // A batch file that runs a Windows program: it opens as a Windows process.
        {"winbat", "D:\\WINBAT.BAT", "WINBAT", true, 0, "\r", false, "Notepad", 100, false, false, nullptr},
        // Alt+Enter to the full screen and back.
        {"full", "D:\\HELLO.COM", "HELLO", true, 42, "a", true, nullptr, 200, false, false, nullptr},
        // The close box while the program waits for its key.
        {"closeq", "D:\\HELLO.COM", "HELLO", true, 42, "a", false, nullptr, 200, true, false, nullptr},
        // A batch file that runs a console program: the DOS side waits for it
        // and takes its exit code as the error level.
        {"conbat", "D:\\WINCON.BAT", "WINCON", true, 7, "", false, nullptr, 100, false, false, nullptr},
        // The Full screen button of the toolbar.
        {"toolbar", "D:\\HELLO.COM", "HELLO", true, 42, "a", false, nullptr, 200, false, true, nullptr},
        // A batch file that writes to C:, the overlay directory of the Windows side.
        {"overlay", "D:\\OVERLAY.BAT", "OVERLAY", true, 0, "", false, nullptr, 100, false, false, nullptr},
        // Paste types the clipboard's "a"; Copy takes the marked line.
        {"paste", "D:\\HELLO.COM", "HELLO", true, 42, "", false, nullptr, 200, false, false, "paste"},
        {"copy", "D:\\HELLO.COM", "HELLO", true, 42, "a", false, nullptr, 200, false, false, "copy"},
        // A Windows program started from a batch file in a directory with a long name.
        {"longdir", "\"D:\\Long Name Dir\\LONG.BAT\"", "LONG", true, 0, "\r", false, "Notepad", 100, false, false, nullptr},
        // A directory listing of C: without the sidecar files of the overlay.
        {"dirlist", "D:\\DIRLIST.BAT", "DIRLIST", true, 0, "", false, nullptr, 100, false, false, nullptr},
        // The PIF in the machine : 50 rows of the Screen page
        // (the program ends with rows - 1), the Hercules card of the machine
        // options (mode 7), 256 KB conventional memory of the Memory page.
        // Sound: the PC speaker, an OPL2 note, a General MIDI note through the MPU-401.
        {"beep", "D:\\BEEP.COM", "BEEP", true, 0, "a", false, nullptr, 20, false, false, nullptr},
        {"adlib", "D:\\ADLIB.COM", "ADLIB", true, 0, "a", false, nullptr, 20, false, false, nullptr},
        {"midi", "D:\\MIDI.COM", "MIDI", true, 0, "a", false, nullptr, 20, false, false, nullptr},
        // Digital sound of the Sound Blaster: 8-bit auto-init DMA on channel 1.
        {"sblaster", "D:\\SBLASTER.COM", "SBLASTER", true, 0, "a", false, nullptr, 20, false, false, nullptr},
        // The window's property sheet open while the Sound Blaster plays.
        {"sheet", "D:\\SBLASTER.COM", "SBLASTER", true, 0, "a", false, nullptr, 20, false, false, nullptr},
        // The shortcut key of a PIF.
        {"hotkey", "D:\\HOTKEY.PIF", "Hot Key", true, 42, "a", false, nullptr, 200, false, false, nullptr},
        // A TrueType size of the Font page: Lucida Console in 8x14 cells.
        {"truetype", "D:\\TRUETYPE.PIF", "TrueType", true, 42, "a", false, nullptr, 200, false, false, nullptr},
        // The size box of the toolbar: 8 x 8, at once and into the PIF.
        {"fontbox", "D:\\FONT.COM", "FONT", true, 42, "a", false, nullptr, 200, false, false, nullptr},
        {"maxfont", "D:\\MAXFONT.COM", "MAXFONT", true, 42, "a", false, nullptr, 200, false, false, nullptr},
        // A PIF with the path of another machine does not start.
        {"oldpath", "D:\\OLDPATH.PIF", "", true, 2, "", false, nullptr, 0, false, false, nullptr},
        {"rows", "D:\\ROWS.PIF", "Rows", true, 49, "a", false, nullptr, 60, false, false, nullptr},
        {"mode", "D:\\MODE.PIF", "Mode", true, 7, "a", false, nullptr, 60, false, false, nullptr},
        {"low", "D:\\LOW.PIF", "Low", true, 1, "a", false, nullptr, 60, false, false, nullptr},
        // A program with its own keyboard handler: a held key stays down.
        // A PIF in the full screen whose program ends.
        {"fullend", "D:\\FULLEND.PIF", "Full End", true, 42, "a", false, nullptr, 200, false, false, nullptr},
        // The DOS mouse under the Windows pointer.
        {"mouse", "D:\\MOUSEPOS.COM", "MOUSEPOS", true, 0, "a", false, nullptr, 1000, false, false, nullptr},
        {"mouseabs", "D:\\MOUSEABS.COM", "MOUSEABS", true, 0, "a", false, nullptr, 100, false, false, nullptr},
        // A program with a PIF beside it that names the path of another
        // machine: the program runs, with the PIF's title.
        {"besidepif", "D:\\BESIDE\\HELLO.COM", "Beside", true, 42, "a", false, nullptr, 200, false, false, nullptr},
        {"keys", "D:\\KEYTEST.COM", "KEYTEST", true, 1, "", false, nullptr, 100, false, false, nullptr},
        // A program name in double quotes in a batch file, a DOS program and
        // a Windows program: COMMAND.COM drops the quotes.
        {"quoted", "D:\\QUOTED.BAT", "QUOTED - HELLO", true, 42, "a", false, nullptr, 200, false, false, nullptr},
        {"quotwin", "D:\\QUOTWIN.BAT", "QUOTWIN", true, 0, "\r", false, "Notepad", 100, false, false, nullptr},
        // An empty batch file runs in a DOS window that ends at once.
        {"emptybat", "D:\\EMPTY.BAT", "EMPTY", true, 0, "", false, nullptr, 0, false, false, nullptr},
    };
    bool okay = true;
    for (const auto &name : scenarios)
    {
        s.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(limit);
        if (name == "twin") { okay = twin(s, out) && okay; if (!okay) break; continue; }
        const Scenario *found = nullptr;
        for (const auto &entry : table) if (name == entry.name) found = &entry;
        if (!found) { printf("DOSVM_RESULT FAIL unknown scenario %s\n", name.c_str()); okay = false; break; }
        okay = scenario(s, *found, out) && okay;
        if (!okay) break;
    }
    printf("DOSVM_SUMMARY %s windows=%u children=%u calls=%u\n", okay ? "PASS" : "FAIL", User::LiveWindows(),
           X86::Application::LiveChildren(), a->ApiCalls());
    fflush(stdout);
    a->TerminateSelf(0);
    X86::Application::TerminateChildren(0);
    delete a;
    HostDosVm::Uninstall();
    return okay ? 0 : 5;
}
