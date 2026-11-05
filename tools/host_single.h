// SPDX-License-Identifier: GPL-2.0-or-later
// One host per drive C:; docs/BUILDING.md.
// Win98 does not start twice from the same disk. Two hosts on the same folder
// both wrote the registry journal and the.neo98-index sidecars of the host
// file system. The host that mounts a folder as drive C: therefore holds a
// lock (flock) on a file beside it:
// <folder>.host-lock the lock; its text is the process id of the owner
// <folder>.host-open what later starts hand to the owner, one line each
// For a machine folder these are `C.host-lock` and `C.host-open` beside `C`
// and `C.registry`. A second start on the same folder appends a line to.host-open (`front`, or `open` and a path it was asked to start, or `game`
// and a program that is to run as a game machine), brings the
// owner's window to the front and ends. The owner reads the file a few times a
// second from its main loop. Both sides take a lock on.host-open itself while
// they read or write it.
// The lock is closed for children (FD_CLOEXEC); the restart of the machine
// keeps it across its execv (KeepAcrossExec), so no other start gets in
// between.
#pragma once
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace HostSingle
{
inline constexpr const char *KeptEnv = "NEO98_HOST_LOCK_KEPT";

struct Held
{
    std::string base; // the folder, links resolved, no trailing slash
    int descriptor = -1;
};
inline std::vector<Held> &HeldLocks()
{
    static std::vector<Held> held;
    return held;
}

// The folder with its links resolved; a folder that does not exist yet (the
// host creates an empty drive C:) by its parent.
inline std::string Base(const std::string &folder)
{
    std::string path = folder;
    while (path.size() > 1 && path.back() == '/')
        path.pop_back();
    char real[PATH_MAX];
    if (realpath(path.c_str(), real))
        return real;
    const auto slash = path.rfind('/');
    const std::string parent = slash == std::string::npos ? std::string(".") : slash == 0 ? std::string("/") : path.substr(0, slash);
    const std::string leaf = slash == std::string::npos ? path : path.substr(slash + 1);
    if (realpath(parent.c_str(), real))
        return std::string(real) + "/" + leaf;
    return path;
}
inline std::string LockPath(const std::string &base) { return base + ".host-lock"; }
inline std::string OpenPath(const std::string &base) { return base + ".host-open"; }

enum class Result
{
    Owner,   // this process holds the folder now (or already did)
    Running, // another host holds it; `owner` is its process id (0 unknown)
    NoLock,  // the lock file cannot be made (read-only place); nothing is held
};

inline Result Acquire(const std::string &folder, long &owner)
{
    owner = 0;
    const std::string base = Base(folder);
    for (const auto &held : HeldLocks())
        if (held.base == base)
            return Result::Owner;
    // The lock the image before the execv of a restart left open.
    if (const char *kept = getenv(KeptEnv); kept && *kept)
    {
        char *end = nullptr;
        const long descriptor = strtol(kept, &end, 10);
        if (end && *end == ':' && base == end + 1 && fcntl(int(descriptor), F_GETFD) != -1)
        {
            fcntl(int(descriptor), F_SETFD, FD_CLOEXEC);
            unsetenv(KeptEnv);
            HeldLocks().push_back({base, int(descriptor)});
            return Result::Owner;
        }
    }
    const int descriptor = open(LockPath(base).c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (descriptor < 0)
        return Result::NoLock;
    if (flock(descriptor, LOCK_EX | LOCK_NB))
    {
        const bool busy = errno == EWOULDBLOCK;
        char text[32] = {};
        if (busy && pread(descriptor, text, sizeof text - 1, 0) > 0)
            owner = atol(text);
        close(descriptor);
        return busy ? Result::Running : Result::NoLock;
    }
    char text[32];
    const int length = snprintf(text, sizeof text, "%ld\n", long(getpid()));
    if (ftruncate(descriptor, 0) || pwrite(descriptor, text, size_t(length), 0) != length)
    {
        // The lock holds without the number.
    }
    // What a start handed to a host that ended before it read it.
    unlink(OpenPath(base).c_str());
    HeldLocks().push_back({base, descriptor});
    return Result::Owner;
}

// Gives a folder up again: a start that held it only while it copied from it
// (a game machine made while the machine does not run).
inline void Release(const std::string &folder)
{
    const std::string base = Base(folder);
    auto &held = HeldLocks();
    for (size_t i = 0; i < held.size(); ++i)
        if (held[i].base == base)
        {
            flock(held[i].descriptor, LOCK_UN);
            close(held[i].descriptor);
            held.erase(held.begin() + long(i));
            return;
        }
}

// Before the execv of a restart: the new image takes the first lock over.
inline void KeepAcrossExec()
{
    if (HeldLocks().empty())
        return;
    const auto &held = HeldLocks().front();
    const int flags = fcntl(held.descriptor, F_GETFD);
    if (flags == -1 || fcntl(held.descriptor, F_SETFD, flags & ~FD_CLOEXEC) == -1)
        return;
    setenv(KeptEnv, (std::to_string(held.descriptor) + ":" + held.base).c_str(), 1);
}

// The second start: one line for the owner. `path` empty asks for the window
// only. `verb` is `open`, or `game` for a program that is to run as a game
// machine.
inline bool Hand(const std::string &folder, const std::string &path, const char *verb = "open")
{
    const std::string base = Base(folder);
    const int descriptor = open(OpenPath(base).c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (descriptor < 0)
        return false;
    flock(descriptor, LOCK_EX);
    std::string line = path.empty() ? std::string("front\n") : std::string(verb) + " " + path + "\n";
    const bool written = write(descriptor, line.data(), line.size()) == ssize_t(line.size());
    flock(descriptor, LOCK_UN);
    close(descriptor);
    return written;
}

// The owner: the lines handed over since the last call. Each is `front`,
// `open PATH` or `game PATH`.
inline bool Take(std::vector<std::string> &lines)
{
    lines.clear();
    for (const auto &held : HeldLocks())
    {
        const std::string path = OpenPath(held.base);
        struct stat info;
        if (stat(path.c_str(), &info) || !info.st_size)
            continue;
        const int descriptor = open(path.c_str(), O_RDWR | O_CLOEXEC);
        if (descriptor < 0)
            continue;
        flock(descriptor, LOCK_EX);
        std::string text;
        char block[4096];
        ssize_t got;
        while ((got = read(descriptor, block, sizeof block)) > 0)
            text.append(block, size_t(got));
        if (ftruncate(descriptor, 0))
        {
            // Read again next time; the lines are served twice then.
        }
        flock(descriptor, LOCK_UN);
        close(descriptor);
        for (size_t from = 0; from < text.size();)
        {
            size_t to = text.find('\n', from);
            if (to == std::string::npos)
                to = text.size();
            if (to > from)
                lines.push_back(text.substr(from, to - from));
            from = to + 1;
        }
    }
    return !lines.empty();
}
} // namespace HostSingle
