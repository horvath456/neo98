// SPDX-License-Identifier: GPL-2.0-or-later
// Host side of the crash report: it creates build/crash-reports/<stamp>-<grund>/
// and writes report.md, report.json and screenshot.png into it. The report core
// in src/crash_report.* stays free of file access; this header is the only
// place that opens one. tools/neo98_host.cpp, tools/probe_application.cpp and
// the crash-report test all include it.
#pragma once
#include "../src/crash_report.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/stat.h>

namespace CrashFiles
{
class FileSink final : public CrashReport::Sink
{
  public:
    explicit FileSink(FILE *f) : file(f) {}
    bool Write(const void *data, unsigned length) override
    {
        return !length || fwrite(data, 1, length, file) == length;
    }

  private:
    FILE *file;
};

// Creates every missing level of a relative or absolute path.
inline bool MakePath(const char *path)
{
    char buffer[1024];
    snprintf(buffer, sizeof buffer, "%s", path);
    for (char *p = buffer + 1; *p; ++p)
    {
        if (*p != '/') continue;
        *p = 0;
        mkdir(buffer, 0755);
        *p = '/';
    }
    return !mkdir(buffer, 0755) || errno == EEXIST;
}

// Where the NEO98 build revision comes from: the build passes it in, otherwise
// the report says so instead of inventing one.
inline const char *Revision()
{
#ifdef NEO98_REVISION
    return NEO98_REVISION;
#else
    return "unknown (NEO98_REVISION is not set)";
#endif
}
inline const char *BuildFlags()
{
#ifdef NEO98_BUILD_FLAGS
    return NEO98_BUILD_FLAGS;
#else
    return "unknown";
#endif
}

// Fills the fields the report core cannot know: time, revision, command line.
inline void Describe(CrashReport::Report &report, const char *reason, const char *host,
                     int argc, char **argv)
{
    snprintf(report.reason, sizeof report.reason, "%s", reason ? reason : "unknown");
    snprintf(report.host, sizeof report.host, "%s", host ? host : "neo98");
    snprintf(report.revision, sizeof report.revision, "%s", Revision());
    snprintf(report.build, sizeof report.build, "%s", BuildFlags());
    const time_t now = time(nullptr);
    struct tm parts;
    localtime_r(&now, &parts);
    strftime(report.timestamp, sizeof report.timestamp, "%Y%m%d-%H%M%S", &parts);
    unsigned used = 0;
    for (int i = 0; i < argc && used + 1 < sizeof report.command; ++i)
        used += (unsigned)snprintf(report.command + used, sizeof report.command - used, "%s%s",
                                   i ? " " : "", argv[i]);
}

// Writes the whole directory and returns its path in `out`. `pixels` may be
// null; then no screenshot is written. Prints the one CRASHREPORT line on
// stderr that scripts and agents look for.
inline bool Write(const CrashReport::Report &report, const char *root, const unsigned char *pixels,
                  unsigned width, unsigned height, char *out, unsigned capacity)
{
    char slug[64];
    CrashReport::ReasonSlug(report.reason, slug, sizeof slug);
    char directory[1024];
    snprintf(directory, sizeof directory, "%s/%s-%s", root && *root ? root : "build/crash-reports",
             report.timestamp[0] ? report.timestamp : "00000000-000000", slug);
    // A second report in the same second gets its own directory.
    struct stat info;
    if (!stat(directory, &info))
        for (unsigned n = 2; n < 100; ++n)
        {
            char candidate[1024];
            snprintf(candidate, sizeof candidate, "%s-%u", directory, n);
            if (stat(candidate, &info)) { snprintf(directory, sizeof directory, "%s", candidate); break; }
        }
    if (!MakePath(directory)) return false;
    char path[1200];
    bool okay = true;
    snprintf(path, sizeof path, "%s/report.md", directory);
    if (FILE *file = fopen(path, "wb"))
    {
        FileSink sink(file);
        okay = CrashReport::FormatMarkdown(report, sink) && okay;
        okay = !fclose(file) && okay;
    }
    else okay = false;
    snprintf(path, sizeof path, "%s/report.json", directory);
    if (FILE *file = fopen(path, "wb"))
    {
        FileSink sink(file);
        okay = CrashReport::FormatJson(report, sink) && okay;
        okay = !fclose(file) && okay;
    }
    else okay = false;
    if (pixels)
    {
        snprintf(path, sizeof path, "%s/screenshot.png", directory);
        if (FILE *file = fopen(path, "wb"))
        {
            FileSink sink(file);
            okay = CrashReport::WritePng(sink, pixels, width, height) && okay;
            okay = !fclose(file) && okay;
        }
        else okay = false;
    }
    if (out && capacity) snprintf(out, capacity, "%s", directory);
    fprintf(stderr, "CRASHREPORT %s\n", directory);
    fflush(stderr);
    return okay;
}
} // namespace CrashFiles
