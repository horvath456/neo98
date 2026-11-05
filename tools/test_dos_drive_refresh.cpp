// SPDX-License-Identifier: GPL-2.0-or-later
#include "host_dos_vm.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

static DosVm::Session *start(const char *directory, const char *command)
{
    const auto *p = DosVm::Current();
    DosVm::Launch launch;
    snprintf(launch.directory, sizeof launch.directory, "%s", directory);
    snprintf(launch.command, sizeof launch.command, "%s", command);
    char error[512]{};
    auto *session = p->Start(p->owner, launch, error, sizeof error);
    if (!session) fprintf(stderr, "%s\n", error);
    assert(session);
    return session;
}

static void ended(DosVm::Session *session, unsigned expected)
{
    const auto *p = DosVm::Current();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    unsigned code = 0;
    bool crashed = false, finished = false;
    char message[512]{};
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (p->Finished(p->owner, session, code, crashed, message, sizeof message)) { finished = true; break; }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    fprintf(stderr, "DOS DRIVE exit=%u expected=%u finished=%u crashed=%u %s\n", code, expected, finished, crashed, message);
    assert(finished && !crashed && code == expected);
    p->Stop(p->owner, session);
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    HostDosVm::Drive drives[26]{};
    drives[0] = {'G', argv[1], false};
    HostDosVm::Install(nullptr, drives, 1);
    auto *waiting = start("G:\\", "WAIT.COM");
    const auto *p = DosVm::Current();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    char program[16]{};
    while (std::chrono::steady_clock::now() < deadline)
    {
        p->Program(p->owner, waiting, program, sizeof program);
        if (!strcmp(program, "WAIT")) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(!strcmp(program, "WAIT"));
    // A second Finder-open mounts H: while the first DOS game still runs.
    drives[1] = {'H', argv[1], false};
    HostDosVm::Install(nullptr, drives, 2);
    ended(start("H:\\", "DONE.COM"), 42);
    // A reused core slot takes the new directory, including clearing an
    // overlay that belonged to the previous configuration.
    drives[1] = {'H', argv[2], false};
    HostDosVm::Install(nullptr, drives, 2);
    ended(start("H:\\", "DONE.COM"), 43);
    for (unsigned i = 0; i < 26; ++i) drives[i] = {char('A' + i), argv[2], false};
    HostDosVm::Install(nullptr, drives, 26);
    ended(start("Y:\\", "DONE.COM"), 43);
    // Updating later sessions did not disturb the first session's machine.
    p->Key(p->owner, waiting, 0x0d, 0x1c, false, true);
    p->Key(p->owner, waiting, 0x0d, 0x1c, false, false);
    ended(waiting, 7);
    HostDosVm::Uninstall();
    puts("DOS DRIVE REFRESH PASS");
}
