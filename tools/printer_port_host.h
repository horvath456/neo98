// SPDX-License-Identifier: GPL-2.0-or-later
// The Mac end of the parallel port (src/printer_port.h). Every job the
// spooler sends to LPT1 becomes a file in one output directory: the bytes as
// they came over the port, and for PostScript a PDF made from them.
// The conversion is a host tool. macOS 14 and later dropped PostScript from
// cupsfilter and from CGPSConverter, so the sink tries, in this order,
// cupsfilter, ps2pdf and gs, and writes a line PRINTER_JOB with the result to
// standard error either way.
#pragma once
#include "../src/printer_port.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <spawn.h>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

namespace PrinterPortHost
{
class Directory final : public PrinterPort::Sink
{
  public:
    explicit Directory(std::string directory) : root(std::move(directory)) {}
    bool Open(unsigned port) override
    {
        mkdir(root.c_str(), 0755);
        char name[64];
        snprintf(name, sizeof name, "/lpt%u-job%03u.prn", port, ++jobs);
        path = root + name;
        file = fopen(path.c_str(), "wb");
        written = 0;
        return file != nullptr;
    }
    bool Write(unsigned, const unsigned char *bytes, unsigned count) override
    {
        if (!file || fwrite(bytes, 1, count, file) != count)
            return false;
        written += count;
        return true;
    }
    void Close(unsigned) override
    {
        if (!file)
            return;
        fclose(file);
        file = nullptr;
        // A program that opens and closes the port without writing leaves
        // no job behind.
        if (!written)
        {
            remove(path.c_str());
            return;
        }
        ++completed;
        std::string result = path;
        if (PostScript())
        {
            const std::string ps = path.substr(0, path.size() - 4) + ".ps";
            rename(path.c_str(), ps.c_str());
            result = ps;
            const std::string pdf = path.substr(0, path.size() - 4) + ".pdf";
            const char *tool = Convert(ps, pdf);
            fprintf(stderr, "PRINTER_JOB bytes=%lu file=%s pdf=%s tool=%s\n", written, ps.c_str(),
                    tool ? pdf.c_str() : "-", tool ? tool : "none");
            return;
        }
        fprintf(stderr, "PRINTER_JOB bytes=%lu file=%s pdf=- tool=none\n", written, result.c_str());
    }
    unsigned Jobs() const { return jobs; }
    // Jobs that reached the host with at least one byte.
    unsigned Completed() const { return completed; }

  private:
    std::string root, path;
    FILE *file = nullptr;
    unsigned long written = 0;
    unsigned jobs = 0, completed = 0;
    // A PostScript job starts with "%!", possibly behind the Ctrl-D and the
    // PJL header a driver may send first.
    bool PostScript() const
    {
        FILE *in = fopen(path.c_str(), "rb");
        if (!in)
            return false;
        char head[512] = {};
        const size_t n = fread(head, 1, sizeof head - 1, in);
        fclose(in);
        for (size_t i = 0; i + 1 < n; ++i)
            if (head[i] == '%' && head[i + 1] == '!')
                return true;
        return false;
    }
    static int Run(const char *const *argv, const char *output)
    {
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        if (output)
            posix_spawn_file_actions_addopen(&actions, 1, output, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
        pid_t pid = 0;
        const int spawned =
            posix_spawnp(&pid, argv[0], &actions, nullptr, const_cast<char *const *>(argv), environ);
        posix_spawn_file_actions_destroy(&actions);
        if (spawned)
            return -1;
        int status = 0;
        waitpid(pid, &status, 0);
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
    static bool NonEmpty(const std::string &file)
    {
        struct stat info;
        return !stat(file.c_str(), &info) && info.st_size > 0;
    }
    static const char *Convert(const std::string &ps, const std::string &pdf)
    {
        {
            const char *argv[] = {"cupsfilter", "-m", "application/pdf", ps.c_str(), nullptr};
            if (!Run(argv, pdf.c_str()) && NonEmpty(pdf))
                return "cupsfilter";
        }
        {
            const char *argv[] = {"ps2pdf", ps.c_str(), pdf.c_str(), nullptr};
            if (!Run(argv, nullptr) && NonEmpty(pdf))
                return "ps2pdf";
        }
        // Neo98.app started from the Finder has no Homebrew in its PATH, so
        // Ghostscript is also looked for where Homebrew puts it.
        for (const char *gs : {"gs", "/opt/homebrew/bin/gs", "/usr/local/bin/gs"})
        {
            const std::string out = "-sOutputFile=" + pdf;
            const char *argv[] = {gs, "-q", "-dSAFER", "-dBATCH", "-dNOPAUSE", "-sDEVICE=pdfwrite",
                                  out.c_str(), ps.c_str(), nullptr};
            if (!Run(argv, nullptr) && NonEmpty(pdf))
                return "gs";
        }
        remove(pdf.c_str());
        return nullptr;
    }
};
} // namespace PrinterPortHost
