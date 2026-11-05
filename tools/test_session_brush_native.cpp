// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/x86_app.h"
#include <cassert>
#include <cstdio>
#include <vector>

static void log(const char *message) { puts(message); }

static std::vector<unsigned char> load(const char *path) {
    FILE *file = fopen(path, "rb");
    assert(file);
    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    rewind(file);
    std::vector<unsigned char> contents(size);
    assert(fread(contents.data(), 1, size, file) == size_t(size));
    fclose(file);
    return contents;
}

int main(int argc, char **argv) {
    assert(argc == 3);
    const auto executable = load(argv[1]);
    const auto font = load(argv[2]);
    assert(!Files::System().Seed("C:\\SESSIONBRUSH.EXE", executable.data(), executable.size()));

    for (unsigned run = 0; run != 2; ++run) {
        X86::Application app(log);
        assert(app.Start(executable.data(), executable.size(), font.data(), font.size(),
                         "SESSIONBRUSH.EXE", "PARENT"));
        for (unsigned i = 0; i != 2000 && app.Status() != X86::Application::Exited &&
                             app.Status() != X86::Application::Failed; ++i) {
            app.Pump(4);
        }
        if (app.Status() != X86::Application::Exited || app.ExitCode()) {
            fprintf(stderr, "FAIL run=%u status=%u exit=%u error=%s\n", run,
                    app.Status(), app.ExitCode(), app.Error());
            return 1;
        }
        assert(!X86::Application::LiveChildren());
        printf("SESSION BRUSH RUN PASS run=%u calls=%u\n", run, app.ApiCalls());
    }
    puts("SESSION BRUSH PASS");
}
