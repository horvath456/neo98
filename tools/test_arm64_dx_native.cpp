// SPDX-License-Identifier: GPL-2.0-or-later
// Runs one ARM64 program that uses DirectDraw or DirectSound to its end,
// without a screen and without a sound device, and keeps what a test
// compares: the rows of its console, pictures of the host image at the
// moments the program asks for them, and the frames the mixer rendered.
// tools/test_arm64_dx_native.py.
// A program asks for a picture by writing a console row "SHOT name" and
// sleeping; the runner writes <NEO98_DX_OUT>/name.ppm from the host image, the
// 32-bit picture the display driver publishes, before the program goes on.
// With NEO98_DX_SOUND=seconds the silent provider of the mixer records that
// many seconds into <NEO98_DX_OUT>/sound.raw (44.100 Hz, 16 bit, stereo).
#define private public
#include <chrono>
#include <csignal>
#include <unistd.h>
#include "../src/sound.h"
#include "../src/user.h"
#include "../src/x86_app.h"
#undef private
#include "system_tree_host.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static std::vector<unsigned char> Load(const char *path)
{
    std::vector<unsigned char> bytes;
    FILE *file = fopen(path, "rb");
    if (!file) return bytes;
    for (int c; (c = fgetc(file)) != EOF;) bytes.push_back((unsigned char)c);
    fclose(file);
    return bytes;
}

static volatile sig_atomic_t asked_to_end = 0;
static bool verbose = false;
static void Log(const char *text)
{
    if (verbose) fprintf(stderr, "LOG %s\n", text);
    else if (!strncmp(text, "ARM64", 5) || !strncmp(text, "MISSING", 7) || !strncmp(text, "EXCEPTION", 9) ||
             !strncmp(text, "arm64 ", 6) || !strncmp(text, "DXDRV", 5))
        printf("LOG %s\n", text);
}

static bool WritePicture(const std::string &path)
{
    User::Render();
    int width = 0, height = 0;
    User::PixelsSize(width, height);
    const unsigned char *pixels = User::Pixels();
    if (!pixels || width <= 0 || height <= 0) return false;
    FILE *file = fopen(path.c_str(), "wb");
    if (!file) return false;
    fprintf(file, "P6\n%d %d\n255\n", width, height);
    for (int i = 0; i < width * height; ++i)
    {
        const unsigned char rgb[] = {pixels[i * 4 + 2], pixels[i * 4 + 1], pixels[i * 4]};
        fwrite(rgb, 1, 3, file);
    }
    return fclose(file) == 0;
}

int main(int argc, char **argv)
{
    if (argc < 4)
    {
        fprintf(stderr, "usage: test-arm64-dx program.exe font.fnt GUEST.EXE [command line]\n");
        return 2;
    }
    verbose = getenv("NEO98_ARM64_VERBOSE") != nullptr;
    const std::string out = getenv("NEO98_DX_OUT") ? getenv("NEO98_DX_OUT") : ".";
    auto exe = Load(argv[1]), font = Load(argv[2]);
    if (exe.empty() || font.empty()) return 2;
    if (!SystemTreeHost::Mount()) return 2;
    std::vector<short> sound;
    if (const char *seconds = getenv("NEO98_DX_SOUND"))
    {
        sound.resize(size_t(atoi(seconds)) * Sound::OutputRate * Sound::OutputChannels);
        Sound::SetCapture(sound.data(), unsigned(sound.size() / Sound::OutputChannels));
    }
    X86::Application app(Log);
    if (getenv("NEO98_DX_TRACE")) app.trace_calls = true;
    const char *command = argc > 4 ? argv[4] : nullptr;
    if (!app.Start(exe.data(), (unsigned)exe.size(), font.data(), (unsigned)font.size(), argv[3], command))
    {
        printf("START FAILED %s\n", app.Error());
        return 1;
    }
    const char *limit = getenv("NEO98_ARM64_SECONDS");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(limit ? atoi(limit) : 120);
    // SIGTERM ends the run in order: the process leaves through main, so the
    // machine of the ARM64 program is taken down before it goes.
    signal(SIGTERM, [](int) { asked_to_end = 1; });
    unsigned shots = 0;
    std::vector<std::string> taken;
    const bool focus_test = getenv("NEO98_DX_FOCUS") != nullptr;
    unsigned foreground_queue = 0;
    Win98Activation::Queue foreground_state{};
    std::vector<std::string> focus_rows;
    while (app.Status() != X86::Application::Exited && app.Status() != X86::Application::Failed && !asked_to_end &&
           std::chrono::steady_clock::now() < deadline)
    {
        const unsigned before = app.ApiCalls();
        app.Pump(32);
        // A new console row "SHOT name": the picture of this moment.
        for (unsigned row = 0; row < 50; ++row)
        {
            char text[256] = {};
            if (!app.ConsoleText(row, text, sizeof text)) break;
            if (focus_test && !strncmp(text, "FOCUS ", 6))
            {
                unsigned mode;
                char phase[32] = {};
                if (sscanf(text, "FOCUS %u %31s", &mode, phase) != 2) continue;
                const std::string key = std::to_string(mode) + " " + phase;
                bool known = false;
                for (const auto &old : focus_rows) known |= old == key;
                if (known) continue;
                focus_rows.push_back(key);
                if (!foreground_queue) foreground_queue = User::ForegroundQueue();
                if (!strcmp(phase, "back"))
                {
                    foreground_state = User::ActivationQueue();
                    User::SetForegroundQueue(0);
                    User::WriteActivationQueue({});
                }
                if (!strcmp(phase, "return"))
                {
                    User::SetForegroundQueue(foreground_queue);
                    User::WriteActivationQueue(foreground_state);
                }
                printf("FOCUS_FRAME %u %s %u\n", mode, phase, Sound::Captured());
            }
            if (strncmp(text, "SHOT ", 5)) continue;
            std::string name = text + 5;
            while (!name.empty() && name.back() == ' ') name.pop_back();
            bool known = false;
            for (const auto &old : taken) known = known || old == name;
            if (known || name.empty()) continue;
            taken.push_back(name);
            if (WritePicture(out + "/" + name + ".ppm")) ++shots;
        }
        if (app.ApiCalls() == before) usleep(200);
    }
    for (unsigned row = 0; row < 50; ++row)
    {
        char text[256] = {};
        if (!app.ConsoleText(row, text, sizeof text)) break;
        size_t length = strlen(text);
        while (length && text[length - 1] == ' ') text[--length] = 0;
        if (length) printf("CONSOLE %u %s\n", row, text);
    }
    if (!sound.empty())
    {
        const unsigned frames = Sound::Captured();
        FILE *file = fopen((out + "/sound.raw").c_str(), "wb");
        if (file)
        {
            fwrite(sound.data(), sizeof(short) * Sound::OutputChannels, frames, file);
            fclose(file);
        }
        printf("SOUND frames=%u\n", frames);
        Sound::SetCapture(nullptr, 0);
    }
    printf("SHOTS %u presents=%u\n", shots, app.HalPresents());
    if (app.Status() == X86::Application::Exited)
        printf("EXITED code=%u calls=%u callbacks=%u\n", app.ExitCode(), app.ApiCalls(), app.Callbacks());
    else if (app.Status() == X86::Application::Failed)
        printf("FAILED %s\n", app.Error());
    else
        printf("RUNNING status=%u calls=%u\n", (unsigned)app.Status(), app.ApiCalls());
    return 0;
}
