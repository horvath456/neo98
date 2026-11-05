// SPDX-License-Identifier: GPL-2.0-or-later
// A capture takes only the events of its own queue. The mode
// "queue" runs tools/capture_queue_fixture.c: a capture held by a thread in
// the background takes only the clicks on that thread's windows. The mode
// "foreground" runs tools/capture_foreground_fixture.c: the foreground
// thread's capture leaves a click on another thread's window to that thread
// and gets USER's extra press and release, and a drag that starts on the
// capture's window still ends there.
#define private public
#include "../src/x86_app.h"
#undef private
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>
static void log(const char *text) { puts(text); }
static void click(X86::Application &app, unsigned w, unsigned buttons = 1)
{
    Gdi::RECT bounds{};
    assert(User::IsWindow(w) && User::GetWindowRect(w, &bounds));
    const int x = (bounds.left + bounds.right) / 2, y = (bounds.top + bounds.bottom) / 2;
    app.MouseButtons(x, y, 0);
    app.MouseButtons(x, y, buttons);
    app.MouseButtons(x, y, 0);
}
static void drag(X86::Application &app, unsigned from, unsigned to)
{
    Gdi::RECT a{}, b{};
    assert(User::IsWindow(from) && User::GetWindowRect(from, &a));
    assert(User::IsWindow(to) && User::GetWindowRect(to, &b));
    const int x = (a.left + a.right) / 2, y = (a.top + a.bottom) / 2;
    const int tx = (b.left + b.right) / 2, ty = (b.top + b.bottom) / 2;
    app.MouseButtons(x, y, 0);
    app.MouseButtons(x, y, 1);
    app.MouseButtons((x + tx) / 2, (y + ty) / 2, 1);
    app.MouseButtons(tx, ty, 1);
    app.MouseButtons(tx, ty, 0);
}
template <class F> static bool handles(F &files, const char *path, unsigned both[2])
{
    unsigned node = 0;
    if (files.Lookup(path, node)) return false;
    const auto *file = files.Get(node);
    if (!file || file->size != 8) return false;
    memcpy(both, file->data, 8);
    return true;
}
static std::vector<unsigned char> load(const char *path)
{
    FILE *f = fopen(path, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    std::vector<unsigned char> bytes(n);
    assert(fread(bytes.data(), 1, n, f) == size_t(n));
    fclose(f);
    return bytes;
}
int main(int argc, char **argv)
{
    assert(argc == 4);
    const bool foreground = !strcmp(argv[3], "foreground");
    auto exe = load(argv[1]), font = load(argv[2]);
    auto &files = Files::System();
    files.MakeDirectory("C:\\WINDOWS\\TEMP");
    X86::Application app(log);
    assert(app.Start(exe.data(), exe.size(), font.data(), font.size(), "CAPTUREQUEUE.EXE",
                     "CAPTUREQUEUE.EXE"));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    bool sent = false, dragging = false;
    unsigned window = 0;
    while (app.Status() != X86::Application::Exited && app.Status() != X86::Application::Failed &&
           std::chrono::steady_clock::now() < deadline)
    {
        app.Pump(8);
        unsigned both[2] = {};
        if (!sent && handles(files, foreground ? "C:\\WINDOWS\\TEMP\\CAPTURE-FOREGROUND.DAT"
                                               : "C:\\WINDOWS\\TEMP\\CAPTURE-QUEUE.DAT", both))
        {
            window = both[1];
            if (foreground)
                click(app, both[1]); // on the other thread's window; thread 1 holds the mouse
            else
                for (unsigned w : both) click(app, w); // the foreground window, then the captured one
            sent = true;
        }
        // Then a drag from the foreground window, which takes the capture on
        // the press, to the other thread's window.
        if (foreground && sent && !dragging && app.Status() != X86::Application::Exited && handles(files, "C:\\WINDOWS\\TEMP\\CAPTURE-DRAG.DAT", both))
        {
            drag(app, both[0], both[1]);
            dragging = true;
        }
        if (app.Status() == X86::Application::Idle)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (app.Status() != X86::Application::Exited || app.ExitCode())
    {
        fprintf(stderr, "CAPTURE QUEUE status=%u code=%u sent=%u dragging=%u window=%u error=%s\n", app.Status(),
                app.ExitCode(), sent ? 1u : 0u, dragging ? 1u : 0u, window, app.Error());
        assert(false);
    }
    assert(sent && (!foreground || dragging));
    printf("CAPTURE QUEUE PASS calls=%u callbacks=%u\n", app.ApiCalls(), app.Callbacks());
    return 0;
}
