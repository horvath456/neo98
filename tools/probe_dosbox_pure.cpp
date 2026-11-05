// SPDX-License-Identifier: GPL-2.0-or-later
// The headless check of the DOSBox Pure core behind the provider of
// tools/host_dos_vm.cpp: a host directory as C:, one program, frames until
// the program has ended. No USER, no Unicorn, no SDL. The program is the
// hand-assembled HELLO.COM of tools/test_dosbox_pure_core.py: INT 21h/09h
// prints a line, INT 21h/4Ch ends with code 42. The picture has to carry the
// line, the exit code has to arrive. A second start of the same machine
// proves the core can be reused for the next DOS program of a session.
//
// probe-dosbox-pure --drive C=DIR [--drive D=DIR] --command LINE [--directory C:\X]
// [--seconds N] [--output PPM] [--twice] [--option NAME=VALUE]...
//
// --option passes a machine option the way a PIF's MACHINE OPTIONS section
// does (cpu_core=dynamic, cycles=max). Each run reports the wall time from
// the start of the machine to the end of the program and the processor time
// of the whole process in that span (tools/bench_dos_vm.py measures with it).
#include "../src/dos_vm.h"
#include "host_dos_vm.h"
#include <chrono>
#include <sys/resource.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <string>
#include <vector>

static void logger(const char *line)
{
    puts(line);
    fflush(stdout);
}

static bool write_ppm(const char *path, const DosVm::Frame &frame)
{
    FILE *file = fopen(path, "wb");
    if (!file) return false;
    fprintf(file, "P6\n%u %u\n255\n", frame.width, frame.height);
    for (unsigned i = 0; i < frame.width * frame.height; ++i)
    {
        const unsigned p = frame.pixels[i];
        unsigned char rgb[] = {(unsigned char)(p >> 16), (unsigned char)(p >> 8), (unsigned char)p};
        fwrite(rgb, 1, 3, file);
    }
    return fclose(file) == 0;
}

// User plus system time of the process, in milliseconds.
static double process_milliseconds()
{
    rusage usage = {};
    getrusage(RUSAGE_SELF, &usage);
    return (usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) * 1000.0 + (usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1000.0;
}

// One run of one program. Answers 0 on success.
static int run(const DosVm::Launch &launch, int seconds, const char *output, const char *label)
{
    const auto *provider = DosVm::Current();
    char error[256] = {};
    const auto started = std::chrono::steady_clock::now();
    const double cpu_started = process_milliseconds();
    auto *session = provider->Start(provider->owner, launch, error, sizeof error);
    if (!session)
    {
        printf("DOSBOX PROBE %s FAIL start: %s\n", label, error);
        return 3;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    DosVm::Frame frame, last;
    std::vector<unsigned> kept;
    unsigned frames = 0, exit_code = 0;
    bool crashed = false, finished = false;
    char message[256] = {};
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (provider->NewFrame(provider->owner, session, last.serial, frame))
        {
            ++frames;
            kept.assign(frame.pixels, frame.pixels + (size_t)frame.width * frame.height);
            last = frame;
            last.pixels = kept.data();
        }
        if (provider->Finished(provider->owner, session, exit_code, crashed, message, sizeof message))
        {
            finished = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    const double cpu = process_milliseconds() - cpu_started;
    // The picture after the end: the machine stopped, the last frame stays.
    if (provider->NewFrame(provider->owner, session, last.serial, frame))
    {
        ++frames;
        kept.assign(frame.pixels, frame.pixels + (size_t)frame.width * frame.height);
        last = frame;
        last.pixels = kept.data();
    }
    // Sound that arrived; the probe only counts it.
    short audio[4096];
    unsigned audio_frames = 0, got;
    while ((got = provider->Audio(provider->owner, session, audio, 2048)) != 0) audio_frames += got;
    unsigned columns=0,rows=0;unsigned short cells[80*50]={};
    if(provider->ScreenText && provider->ScreenText(provider->owner,session,columns,rows,cells,80*50)) {
        std::string text;
        for(unsigned i=0;i<columns*rows;++i)text.push_back((char)(cells[i]&255));
        printf("DOSBOX TEXT %s columns=%u rows=%u hello=%u\n",label,columns,rows,
               text.find("NEO98 DOS VM hello")!=std::string::npos?1u:0u);
    }
    provider->Stop(provider->owner, session);
    unsigned lit = 0, colors = 0;
    if (last.pixels)
    {
        std::vector<unsigned> seen;
        for (unsigned i = 0; i < last.width * last.height; ++i)
        {
            const unsigned p = last.pixels[i] & 0xffffff;
            if (p) ++lit;
            bool known = false;
            for (unsigned c : seen)
                if (c == p) known = true;
            if (!known && seen.size() < 64) seen.push_back(p);
        }
        colors = (unsigned)seen.size();
    }
    if (output && last.pixels && !write_ppm(output, last)) printf("DOSBOX PROBE %s cannot write %s\n", label, output);
    printf("DOSBOX PROBE %s frames=%u width=%u height=%u lit=%u colors=%u audio=%u finished=%u exit=%u crashed=%u message=%s\n",
           label, frames, last.width, last.height, lit, colors, audio_frames, finished ? 1u : 0u, exit_code,
           crashed ? 1u : 0u, message);
    printf("DOSBOX PROBE %s elapsed_ms=%.1f cpu_ms=%.1f\n", label, elapsed, cpu);
    fflush(stdout);
    return finished ? 0 : 4;
}

int main(int argc, char **argv)
{
    DosVm::Launch launch;
    std::vector<HostDosVm::Drive> drives;
    int seconds = 30;
    const char *output = nullptr;
    bool twice = false;
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--drive") && i + 1 < argc)
        {
            const char *text = argv[++i];
            if (strlen(text) < 3 || text[1] != '=') return 2;
            drives.push_back({(char)(text[0] & 0x5f), text + 2, false});
        }
        else if (!strcmp(argv[i], "--overlay") && i + 1 < argc)
        {
            const char *text = argv[++i];
            if (strlen(text) < 3 || text[1] != '=') return 2;
            drives.push_back({(char)(text[0] & 0x5f), text + 2, true});
        }
        else if (!strcmp(argv[i], "--command") && i + 1 < argc)
            snprintf(launch.command, sizeof launch.command, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--directory") && i + 1 < argc)
            snprintf(launch.directory, sizeof launch.directory, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--seconds") && i + 1 < argc)
            seconds = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--output") && i + 1 < argc)
            output = argv[++i];
        else if (!strcmp(argv[i], "--twice"))
            twice = true;
        else if (!strcmp(argv[i], "--option") && i + 1 < argc)
        {
            const size_t used = strlen(launch.options);
            snprintf(launch.options + used, sizeof launch.options - used, "%s\n", argv[++i]);
        }
        else
        {
            fprintf(stderr, "unknown argument %s\n", argv[i]);
            return 2;
        }
    }
    HostDosVm::Install(logger, drives.data(), (unsigned)drives.size());
    int result = run(launch, seconds, output, "first");
    if (!result && twice) result = run(launch, seconds, nullptr, "second");
    HostDosVm::Uninstall();
    printf("DOSBOX PROBE %s\n", result ? "FAIL" : "PASS");
    return result;
}
