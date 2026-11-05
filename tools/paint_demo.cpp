// SPDX-License-Identifier: GPL-2.0-or-later
// Repeatable original Paint mouse demo. Only the native RAM volume is modified.
#define private public
#include "x86_app.h"
#undef private
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>
static std::vector<unsigned char> load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return {};
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    if (n <= 0 || n > 8 * 1024 * 1024) {
        fclose(f);
        return {};
    }
    std::vector<unsigned char> b(n);
    if (fread(b.data(), 1, b.size(), f) != b.size())
        b.clear();
    fclose(f);
    return b;
}
static void logger(const char *s) {
    puts(s);
    fflush(stdout);
}
static const char *leaf(const char *s) {
    const auto *p = strrchr(s, '/');
    return p ? p + 1 : s;
}
static void hex_text(const char *text) {
    for (const auto *p = reinterpret_cast<const unsigned char *>(text); *p; ++p)
        printf("%02x", *p);
}
static void snapshot_windows() {
    unsigned handles[192], count = 0;
    if (!User::WindowSnapshot(User::GetDesktopWindow(), handles, 192, count))
        return;
    for (unsigned i = 0; i < count; ++i) {
        const unsigned hwnd = handles[i];
        char klass[64];
        std::vector<char> text(User::TextLength(hwnd) + 1);
        User::GetClassName(hwnd, klass, sizeof klass);
        User::GetWindowText(hwnd, text.data(), text.size());
        printf("PROBE_WINDOW hwnd=%u parent=%u owner=%u visible=%u class=", hwnd, User::Parent(hwnd),
               User::Owner(hwnd), User::IsVisible(hwnd));
        hex_text(klass);
        printf(" text=");
        hex_text(text.data());
        putchar('\n');
    }
}
struct Document {
    unsigned handle;
    std::vector<unsigned char> bytes;
};
static bool output_ok = true;
static std::vector<Document> documents(X86::Application &app, const char *stage) {
    std::vector<Document> snapshots;
    struct Query : Gdi::BitmapIO {
        unsigned char data[84]{};
        bool Read(unsigned, void *, unsigned) override { return false; }
        bool Write(unsigned p, const void *v, unsigned n) override {
            if (p < 0x1000 || p - 0x1000 + n > 84)
                return false;
            memcpy(data + p - 0x1000, v, n);
            return true;
        }
        bool Probe(unsigned, bool) override { return true; }
        void Error(unsigned) override {}
        void Fault(const char *) override {}
    } query;
    for (const auto &o : app.gdi_objects)
        if (o.handle && Gdi::HasDibBitmap(o.handle)) {
            if (Gdi::DibGetObject(o.handle, 84, 0x1000, query) != 84)
                continue;
            unsigned values[21];
            memcpy(values, query.data, 84);
            unsigned width = values[1], height = values[2], depth = values[4] >> 16, pointer = values[5];
            if (width != 400 || height != 300 || depth != 24)
                continue;
            const unsigned stride = ((width * depth + 31) / 32) * 4;
            std::vector<unsigned char> bytes(stride * height);
            if (!app.Read(pointer, bytes.data(), bytes.size()))
                continue;
            char name[80];
            snprintf(name, sizeof name, "document-%x-%s.bin", o.handle, stage);
            FILE *f = fopen(name, "wb");
            if (f) {
                output_ok = fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size() && output_ok;
                output_ok = fclose(f) == 0 && output_ok;
            } else
                output_ok = false;
            snapshots.push_back({o.handle, bytes});
            printf("DOCUMENT stage=%s bitmap=%x width=%u height=%u depth=%u guest=%x bytes=%zu "
                   "orientation=%d file=%s\n",
                   stage, o.handle, width, height, depth, pointer, bytes.size(), int(values[8]), name);
        }
    return snapshots;
}
int main(int argc, char **argv) {
    if (argc < 3)
        return 2;
    const char *command = nullptr;
    for (int i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "--command")) {
            if (++i == argc)
                return 2;
            command = argv[i];
            continue;
        }
        auto bytes = load(argv[i]);
        char guest[260];
        snprintf(guest, sizeof guest, "C:\\WINDOWS\\SYSTEM\\%s", leaf(argv[i]));
        if (bytes.empty() || Files::System().Seed(guest, bytes.data(), bytes.size()))
            return 3;
    }
    auto exe = load(argv[1]), font = load(argv[2]);
    auto *a = new X86::Application(logger);
    const bool started = a->Start(exe.data(), exe.size(), font.data(), font.size(), leaf(argv[1]), command);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    if (started)
        while (a->Status() != X86::Application::Exited && a->Status() != X86::Application::Failed &&
               std::chrono::steady_clock::now() < deadline) {
            a->Pump();
            if (a->WaitingForInput())
                break;
            if (a->Status() == X86::Application::Idle)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

    auto pump = [&]() {
        for (unsigned n = 0;
             n < 1000 && a->Status() != X86::Application::Failed && a->Status() != X86::Application::Exited;
             ++n) {
            a->Pump(32);
            if (a->WaitingForInput())
                break;
        }
    };
    auto image = [&](const char *path) {
        User::Render();
        std::vector<unsigned char> rgb(800 * 600 * 3);
        auto *p = User::Pixels();
        for (unsigned n = 0; n < 800 * 600; ++n) {
            rgb[n * 3] = p[n * 4 + 2];
            rgb[n * 3 + 1] = p[n * 4 + 1];
            rgb[n * 3 + 2] = p[n * 4];
        }
        FILE *f = fopen(path, "wb");
        if (f) {
            output_ok = fprintf(f, "P6\n800 600\n255\n") > 0 && output_ok;
            output_ok = fwrite(rgb.data(), 1, rgb.size(), f) == rgb.size() && output_ok;
            output_ok = fclose(f) == 0 && output_ok;
        } else
            output_ok = false;
        return rgb;
    };
    unsigned handles[192], count = 0, canvas = 0;
    User::WindowSnapshot(User::GetDesktopWindow(), handles, 192, count);
    for (unsigned i = 0; i < count; ++i) {
        char klass[64];
        User::GetClassName(handles[i], klass, sizeof klass);
        Gdi::RECT r{}, c{};
        User::GetWindowRect(handles[i], &r);
        User::GetClientRect(handles[i], &c);
        printf("GEOMETRY hwnd=%u class=%s window=%d,%d,%d,%d client=%d,%d,%d,%d\n", handles[i], klass, r.left,
               r.top, r.right, r.bottom, c.left, c.top, c.right, c.bottom);
        if (!strcmp(klass, "Afx:1000000:8"))
            canvas = handles[i];
    }
    if (!started || a->Status() == X86::Application::Failed || a->Status() == X86::Application::Exited ||
        !a->WaitingForInput()) {
        fprintf(stderr, "Paint startup failed: %s\n", a->Error());
        delete a;
        return 4;
    }
    Gdi::RECT client{};
    Gdi::POINT zero{};
    if (!canvas || !User::GetClientRect(canvas, &client) || !User::ScreenToClient(canvas, &zero) ||
        client.right < 216 || client.bottom < 106) {
        fprintf(stderr, "Paint canvas missing or too small\n");
        delete a;
        return 5;
    }
    const int origin_x = -zero.x, origin_y = -zero.y;
    printf("CANVAS hwnd=%u left=%d top=%d right=%d bottom=%d\n", canvas, origin_x, origin_y,
           origin_x + client.right, origin_y + client.bottom);
    const auto before = image("before.ppm");
    const auto document_before = documents(*a, "before");
    unsigned packets = 0, completed = 0;
    bool released = true;
    if (canvas) {
        // Reference points are translated to the actual canvas client origin.
        const std::vector<std::vector<Gdi::POINT>> strokes = {
            {{105, 205}, {127, 140}, {149, 205}},
            {{114, 180}, {141, 180}},
            {{167, 205}, {167, 140}, {195, 140}, {210, 153}, {210, 164}, {196, 176}, {167, 176}},
            {{190, 176}, {214, 205}},
            {{235, 205}, {235, 140}, {260, 177}, {285, 140}, {285, 205}}};
        auto packet = [&](int x, int y, unsigned buttons) {
            x += origin_x - 70;
            y += origin_y - 100;
            printf("INPUT n=%u x=%d y=%d buttons=%u\n", packets++, x, y, buttons);
            a->MouseButtons(x, y, buttons);
            pump();
            return a->Status() != X86::Application::Failed && a->Status() != X86::Application::Exited;
        };
        for (unsigned stroke = 0; stroke < strokes.size(); ++stroke) {
            const auto &points = strokes[stroke];
            bool ok = packet(points[0].x, points[0].y, 0) && packet(points[0].x, points[0].y, 1);
            for (unsigned segment = 1; ok && segment < points.size(); ++segment) {
                auto from = points[segment - 1], to = points[segment];
                int dx = to.x - from.x, dy = to.y - from.y, ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
                unsigned steps = ((ax > ay ? ax : ay) + 3) / 4;
                for (unsigned n = 1; ok && n <= steps; ++n)
                    ok = packet(from.x + dx * int(n) / int(steps), from.y + dy * int(n) / int(steps), 1);
            }
            if (ok)
                ok = packet(points.back().x, points.back().y, 0);
            if (!ok)
                break;
            ++completed;
            released = released && !User::CaptureOperation(0);
            char path[40];
            snprintf(path, sizeof path, "stroke-%u.ppm", completed);
            image(path);
            printf("STROKE_COMPLETE n=%u state=%u capture=%u calls=%u callbacks=%u error=%s\n", completed,
                   a->Status(), User::CaptureOperation(0), a->ApiCalls(), a->Callbacks(), a->Error());
        }
    }
    printf("DEMO completed=%u packets=%u\n", completed, packets);
    const auto document_after = documents(*a, "after");
    const auto after = image("after.ppm");
    unsigned changed = 0;
    for (unsigned n = 0; n < 800 * 600; ++n)
        if (memcmp(&before[n * 3], &after[n * 3], 3))
            ++changed;
    unsigned calls = a->ApiCalls(), callbacks = a->Callbacks();
    for (unsigned i = 0;
         i < 32 && a->Status() != X86::Application::Failed && a->Status() != X86::Application::Exited; ++i)
        a->Pump(32);
    const auto idle = image("idle.ppm");
    unsigned idle_changed = 0;
    for (unsigned n = 0; n < 800 * 600; ++n)
        if (memcmp(&idle[n * 3], &after[n * 3], 3))
            ++idle_changed;
    printf("INTERACTION state=%u code=%u canvas=%u changed_pixels=%u idle_changed_pixels=%u idle_calls=%u "
           "idle_callbacks=%u error=%s\n",
           a->Status(), a->ExitCode(), canvas, changed, idle_changed, a->ApiCalls() - calls,
           a->Callbacks() - callbacks, a->Error());
    snapshot_windows();
    unsigned canvas_changed = 0, document_changed = 0, changed_documents = 0;
    for (int y = origin_y; y < origin_y + client.bottom; ++y)
        for (int x = origin_x; x < origin_x + client.right; ++x)
            if (x >= 0 && x < 800 && y >= 0 && y < 600 &&
                memcmp(&before[(y * 800 + x) * 3], &after[(y * 800 + x) * 3], 3))
                ++canvas_changed;
    for (const auto &old : document_before)
        for (const auto &now : document_after)
            if (old.handle == now.handle && old.bytes.size() == now.bytes.size()) {
                unsigned bytes = 0;
                for (unsigned i = 0; i < old.bytes.size(); ++i)
                    if (old.bytes[i] != now.bytes[i])
                        ++bytes;
                if (bytes) {
                    ++changed_documents;
                    document_changed += bytes;
                }
            }
    const bool ok = started && completed == 5 && canvas_changed && document_changed && released &&
                    !User::CaptureOperation(0) && a->Status() == X86::Application::Idle &&
                    a->WaitingForInput() && !idle_changed && a->ApiCalls() == calls &&
                    a->Callbacks() == callbacks && output_ok;
    printf("DEMO_RESULT success=%u completed=%u packets=%u calls=%u callbacks=%u state=%u capture=%u "
           "released=%u canvas_pixels=%u document_bytes=%u changed_documents=%u idle_pixels=%u idle_calls=%u "
           "idle_callbacks=%u output_ok=%u\n",
           ok, completed, packets, a->ApiCalls(), a->Callbacks(), a->Status(), User::CaptureOperation(0),
           released, canvas_changed, document_changed, changed_documents, idle_changed, a->ApiCalls() - calls,
           a->Callbacks() - callbacks, output_ok);
    delete a;
    return ok ? 0 : 6;
}
