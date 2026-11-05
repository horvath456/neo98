// SPDX-License-Identifier: GPL-2.0-or-later
// Runs tools/dsound_stream_fixture.c over the original DSOUND.DLL of the tree
// and measures what a streaming program lives on: how the cursors of a looping
// buffer move, whether the stream ever runs dry and how long a frame takes
// from the Unlock that wrote it to the mixer that renders it.
// The guest writes a ramp, 16 a frame and 2000 frames long, a tenth of a
// second ahead of the write cursor. The capture buffer of the silent provider
// holds what the mixer rendered, frame by frame on the clock of the mixer, so
// a gap in the ramp is an underrun, and the index of a frame against the
// moment its mark arrived is the latency.
// With "measure" as third argument the runner only reports; without it the
// numbers are checked against the expected streaming behavior.
#define private public
#include "../src/x86_app.h"
#undef private
#include "../src/sound.h"
#include "system_tree_host.h"
#include <algorithm>
#include <cassert>
#include <chrono>
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
static short capture[44100 * 24 * 2];
static std::string debug_output;
static double seconds()
{
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}
// The moment the first frame of the capture buffer was rendered. The silent
// provider renders what has come due at every tick, so the frames rendered so
// far never run ahead of the clock: the smallest difference seen is the start.
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
    unsigned frame, lead;
    double time;
};
static std::vector<Mark> marks;
static void collect(const char *text)
{
    sample_clock();
    // NEO98_DSOUND_TRACE=1 shows every call into the native DSOUND.VXD.
    if (strstr(text, "DSOUNDVXD")) { fprintf(stderr, "%.3f %s\n", seconds(), text); return; }
    debug_output += text;
    debug_output += '\n';
    // The line may carry a prefix of the log; the observation starts at its name.
    const char *line = strstr(text, "DSTREAM ");
    const char *at = line ? strstr(line, "mark=") : nullptr;
    if (!at) return;
    Mark mark;
    mark.pass.assign(line + 8, at);
    char *end = nullptr;
    mark.frame = (unsigned)strtoul(at + 5, &end, 16);
    mark.lead = *end == ',' ? (unsigned)strtoul(end + 1, nullptr, 16) : 0;
    mark.time = seconds();
    marks.push_back(mark);
}
static unsigned observation(const std::string &name)
{
    const std::string key = "DSTREAM " + name + "=";
    const auto at = debug_output.find(key);
    if (at == std::string::npos)
    {
        fprintf(stderr, "DSTREAM %s missing\n%s\n", name.c_str(), debug_output.c_str());
        assert(false);
    }
    return (unsigned)strtoul(debug_output.c_str() + at + key.size(), nullptr, 16);
}
struct Segment
{
    unsigned first = 0, last = 0, breaks = 0;
};
int main(int argc, char **argv)
{
    assert(argc >= 3);
    const bool measure = argc > 3 && !strcmp(argv[3], "measure");
    // "ring3": the VxD leaves the mixer of DSOUND 4.06 to 4.08 in DDHELP
    // (NEO98_DSOUND_RING3=1), where it mixes the software buffer ahead into
    // the primary buffer of the driver in pieces of its own choosing.
    const bool ring3 = argc > 3 && !strcmp(argv[3], "ring3");
    auto exe = load(argv[1]), font = load(argv[2]);
    assert(SystemTreeHost::Mount());
    Sound::SetCapture(capture, sizeof capture / (2 * sizeof(short)));
    X86::Application app(nullptr);
    app.log = collect;

    assert(app.Start(exe.data(), exe.size(), font.data(), font.size(), "DSTREAM.EXE"));
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
        fprintf(stderr, "DSTREAM status=%u code=%u error=%s\n%s\n", app.Status(), app.ExitCode(),
                app.Error(), debug_output.c_str());
    assert(app.Status() == X86::Application::Exited && !app.ExitCode());
    assert(observation("create") == 0 && observation("cooperative") == 0);
    assert(observation("primary_format") == 0 && observation("done") == 1);

    // The ramp in the capture buffer. A frame of silence ends a segment; inside
    // one, every frame is 16 above the one before or the ramp starts again.
    const unsigned frames = Sound::Captured();
    std::vector<Segment> segments;
    for (unsigned i = 0; i < frames; ++i)
    {
        const int left = capture[i * 2];
        if (!left) continue;
        if (segments.empty() || segments.back().last + 1 != i)
        {
            segments.push_back(Segment{i, i, 0});
            continue;
        }
        const int step = left - capture[(i - 1) * 2];
        const bool ramp = step >= 12 && step <= 20;
        const bool turn = step <= -16 * 1999 + 40 && step >= -16 * 1999 - 40;
        if (!ramp && !turn) ++segments.back().breaks;
        segments.back().last = i;
    }
    // The passes lie half a second of silence apart. Segments closer than a
    // third of a second belong to one pass, and every place where an underrun
    // cut a pass in two counts as a gap of it. `last` then is the first frame
    // plus the frames that sounded, so the length of a pass is what it played.
    std::vector<Segment> passes;
    unsigned pieces = 0;
    for (const auto &segment : segments)
    {
        const unsigned frames_played = segment.last - segment.first + 1;
        static unsigned end = 0;
        if (passes.empty() || segment.first - end > 44100 / 3)
            passes.push_back(Segment{segment.first, segment.first + frames_played - 1, segment.breaks});
        else
        {
            passes.back().last += frames_played;
            passes.back().breaks += segment.breaks + 1;
            ++pieces;
        }
        end = segment.last;
    }
    const unsigned caps = observation("caps_flags");
    printf("DirectSound stream caps=%08x emulated=%u frames=%u segments=%zu\n", caps,
           (caps & 0x20) ? 1 : 0, frames, segments.size());
    if (getenv("NEO98_DSTREAM_SEGMENTS"))
        for (const auto &segment : segments)
            printf("  segment %u..%u (%u frames) breaks=%u first=%d\n", segment.first, segment.last,
                   segment.last - segment.first + 1, segment.breaks, capture[segment.first * 2]);
    // The pass on the primary buffer needs DSSCL_WRITEPRIMARY.
    assert(observation("write_primary") == 0 && observation("primary_format") == 0);
    bool good = passes.size() == 4;
    const char *names[] = {"old_", "new_", "soft_", "primary_"};
    for (unsigned p = 0; p < 4; ++p)
    {
        const std::string name = names[p];
        // Bytes a millisecond: 16-bit mono for the secondary buffers, 16-bit
        // stereo for the primary one.
        const double rate = p == 3 ? 176.4 : 88.2;
        const unsigned ahead = p == 3 ? observation(name + "bytes") / 8 * 3 / 4 : 4410;
        const unsigned step_median = observation(name + "step_median");
        const unsigned step_max = observation(name + "step_max");
        const unsigned jump_max = observation(name + "jump_max");
        const unsigned gap_median = observation(name + "gap_median");
        const unsigned gap_max = observation(name + "gap_max");
        const unsigned lead_min = observation(name + "lead_min");
        const unsigned lead_max = observation(name + "lead_max");
        const unsigned written = observation(name + "frames");
        const unsigned resyncs = observation(name + "resyncs");
        const unsigned failures = observation(name + "failures");
        const unsigned late = observation(name + "late");
        unsigned breaks = 0, played = 0;
        double total_low = 1e9, total_high = 0, system_low = 1e9, system_high = 0;
        std::vector<double> system_all;
        if (p < passes.size())
        {
            const Segment &segment = passes[p];
            breaks = segment.breaks;
            played = segment.last - segment.first + 1;
            for (const auto &mark : marks)
            {
                if (mark.pass != name) continue;
                // The first chunk goes to the write cursor itself while the
                // buffer starts; the stream is what follows it.
                if (mark.frame >= played || !mark.frame) continue;
                // Frame n of the pass is the n-th frame of its segment.
                const double rendered = capture_start + (segment.first + mark.frame) / 44100.0;
                const double total = (rendered - mark.time) * 1000.0;
                const double system = total - mark.lead / 44.1;
                total_low = std::min(total_low, total);
                total_high = std::max(total_high, total);
                system_low = std::min(system_low, system);
                system_high = std::max(system_high, system);
                system_all.push_back(system);
            }
        }
        std::sort(system_all.begin(), system_all.end());
        const double system_median = system_all.empty() ? 0 : system_all[system_all.size() / 2];
        // Bytes of 16-bit mono at 44.1 kHz: 88.2 a millisecond.
        printf("DirectSound stream %s step median=%u max=%u bytes (%.1f / %.1f ms), beyond the time passed "
               "max=%u bytes (%.1f ms), time between moves median=%u max=%u ms, write lead %u..%u bytes "
               "(%.1f..%.1f ms)\n",
               names[p], step_median, step_max, step_median / rate, step_max / rate, jump_max, jump_max / rate,
               gap_median, gap_max, lead_min, lead_max, lead_min / rate, lead_max / rate);
        printf("DirectSound stream %s written=%u played=%u breaks=%u resyncs=%u failures=%u late rounds=%u, "
               "unlock to mixer %.0f..%.0f ms, write cursor to mixer %.0f..%.0f ms (median %.0f)\n",
               names[p], written, played, breaks, resyncs, failures, late, total_low, total_high,
               system_low, system_high, system_median);
        if (measure) continue;
        // The contract. Either the play cursor follows the sound in steps of
        // at most about 25 ms, or the write cursor is ahead of it by at least
        // the step, so a program that writes behind the write cursor is never
        // overtaken. A step is measured against the time that passed since
        // the reading before it, because a guest thread that comes late sees
        // a large step of a cursor that moved evenly.
        const bool fine_steps = jump_max <= 25 * rate && step_median <= 25 * rate;
        const bool honest_lead = lead_min >= step_max;
        if (!fine_steps && !honest_lead) { printf("FAIL %s cursor\n", names[p]); good = false; }
        // What is kept written behind the write cursor never runs dry.
        // A round the machine held up for longer than half that stretch is
        // not the stream's fault; each one may cost a gap at either end.
        // The mixer of DSOUND in DDHELP mixes up to 200 ms ahead and may
        // overtake a stream that keeps only 100 ms written; what the pass
        // proves there is that the buffer sounds and its cursor moves.
        const unsigned allowed = ring3 && p == 2 ? 8 : 0;
        if (breaks > 2 * late + allowed || resyncs > late + allowed || failures)
        { printf("FAIL %s underrun\n", names[p]); good = false; }
        if (played < written / 2) { printf("FAIL %s silent\n", names[p]); good = false; }
        // What is still ahead when the pass stops its buffer is not played:
        // the tenth of a second it keeps written and the lead of the cursor.
        if (!late && played + ahead + 2205 < written) { printf("FAIL %s played\n", names[p]); good = false; }
        // What lies at the write cursor reaches the mixer within a tenth of a
        // second.
        if (system_all.empty() || system_median > 100.0 || (!late && system_high > 100.0))
        { printf("FAIL %s latency\n", names[p]); good = false; }
    }
    if (measure) { printf("DirectSound stream MEASURED\n"); return 0; }
    if (!good) { printf("DirectSound stream FAIL passes=%zu pieces=%u\n", passes.size(), pieces); return 1; }
    printf("DirectSound stream PASS calls=%u\n", app.ApiCalls());
    return 0;
}
