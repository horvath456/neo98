// SPDX-License-Identifier: GPL-2.0-or-later
// Runs tools/dsound_3d_fixture.c over the original DSOUND.DLL of the tree and
// reports, for every phase of the 3D buffer, what the mixer rendered: the
// level of each channel, the delay between the channels and the pitch.
//
// The runner checks nothing but that the guest got through; one line per
// phase goes out, and tools/test_dsound_3d_guest.py compares the run over the
// DirectSound driver with the run over the wave device (NEO98_DSOUND_WAVE=1).
#define private public
#include "../src/x86_app.h"
#undef private
#include "../src/sound.h"
#include "system_tree_host.h"
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
static std::vector<unsigned char> load(const char *p)
{
    FILE *f = fopen(p, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    auto n = ftell(f);
    rewind(f);
    std::vector<unsigned char> b(n);
    assert(fread(b.data(), 1, n, f) == size_t(n));
    fclose(f);
    return b;
}
static short capture[44100 * 30 * 2];
static std::string debug_output;
static double seconds()
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}
static double capture_start = 1e300;
static void sample_clock()
{
    const unsigned frames = Sound::Captured();
    if (!frames) return;
    const double start = seconds() - frames / 44100.0;
    if (start < capture_start) capture_start = start;
}
struct Mark
{
    std::string pass;
    unsigned phase;
    double time;
};
static std::vector<Mark> marks;
static void collect(const char *text)
{
    sample_clock();
    if (strstr(text, "DSOUNDVXD")) { fprintf(stderr, "%s\n", text); return; }
    debug_output += text;
    debug_output += '\n';
    const char *line = strstr(text, "D3D ");
    const char *at = line ? strstr(line, "mark=") : nullptr;
    if (!at) return;
    marks.push_back(Mark{std::string(line + 4, at), (unsigned)strtoul(at + 5, nullptr, 16), seconds()});
}
int main(int argc, char **argv)
{
    assert(argc >= 3);
    auto exe = load(argv[1]), font = load(argv[2]);
    assert(SystemTreeHost::Mount());
    Sound::SetCapture(capture, sizeof capture / (2 * sizeof(short)));
    X86::Application app(nullptr);
    app.log = collect;
    assert(app.Start(exe.data(), exe.size(), font.data(), font.size(), "D3D.EXE"));
    for (unsigned n = 0; n < 16000000 && app.Status() != X86::Application::Exited &&
                         app.Status() != X86::Application::Failed;
         ++n)
    {
        app.Pump(32);
        sample_clock();
        if (app.Status() == X86::Application::Idle)
            std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
    if (app.Status() != X86::Application::Exited || app.ExitCode())
        fprintf(stderr, "D3D status=%u code=%u error=%s\n%s\n", app.Status(), app.ExitCode(), app.Error(),
                debug_output.c_str());
    assert(app.Status() == X86::Application::Exited && !app.ExitCode());
    assert(debug_output.find("D3D done=00000001") != std::string::npos);
    const unsigned frames = Sound::Captured();
    for (size_t m = 0; m + 1 < marks.size(); ++m)
    {
        const Mark &mark = marks[m];
        if (mark.phase > 4 || marks[m + 1].pass != mark.pass) continue;
        // The last 350 ms of the phase: whatever either path has mixed ahead
        // when the position changes is through by then.
        const double base = marks[m + 1].time - capture_start;
        const long first = (long)((base - 0.40) * 44100), count = (long)(0.35 * 44100);
        if (first < 100 || first + count + 100 > (long)frames) { printf("D3D %s%u outside the capture\n", mark.pass.c_str(), mark.phase); continue; }
        double left = 0, right = 0;
        unsigned crossings = 0;
        for (long i = first; i < first + count; ++i)
        {
            const double l = capture[i * 2], r = capture[i * 2 + 1];
            left += l * l;
            right += r * r;
            const int now = capture[i * 2] + capture[i * 2 + 1], before = capture[(i - 1) * 2] + capture[(i - 1) * 2 + 1];
            if (before < 0 && now >= 0) ++crossings;
        }
        left = std::sqrt(left / count);
        right = std::sqrt(right / count);
        // The delay of the right channel against the left one: the shift that
        // makes the two agree best, within half a period of the sine.
        int best = 0;
        double best_sum = -1e300;
        for (int lag = -45; lag <= 45; ++lag)
        {
            double sum = 0;
            for (long i = first; i < first + count; ++i) sum += (double)capture[i * 2] * capture[(i + lag) * 2 + 1];
            if (sum > best_sum) { best_sum = sum; best = lag; }
        }
        printf("D3D %s phase=%u left=%.0f right=%.0f delay=%d hertz=%.1f\n", mark.pass.c_str(), mark.phase, left,
               right, left > 20 && right > 20 ? best : 0, crossings / 0.35);
    }
    printf("D3D MEASURED calls=%u\n", app.ApiCalls());
    return 0;
}
