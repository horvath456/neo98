// SPDX-License-Identifier: GPL-2.0-or-later
// The host's side of the Dock icons of the guest's programs; see
// tools/host_dock.h.
#include "host_dock.h"
#import <AppKit/AppKit.h>

#include <cerrno>
#include <deque>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

namespace
{
struct Helper
{
    unsigned process = 0;
    pid_t pid = 0;
    int channel = -1;
    std::string name, pending;
};
std::vector<Helper> helpers;
std::deque<HostDock::Event> events;

NSString *Text(const std::string &text) { return [NSString stringWithUTF8String:text.c_str()] ?: @""; }

NSData *Png(int width, int height, const std::vector<unsigned> &argb, int size)
{
    NSBitmapImageRep *rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr
                                                                    pixelsWide:size
                                                                    pixelsHigh:size
                                                                 bitsPerSample:8
                                                               samplesPerPixel:4
                                                                      hasAlpha:YES
                                                                      isPlanar:NO
                                                                colorSpaceName:NSDeviceRGBColorSpace
                                                                  bitmapFormat:NSBitmapFormatAlphaNonpremultiplied
                                                                   bytesPerRow:size * 4
                                                                  bitsPerPixel:32];
    unsigned char *bytes = rep.bitmapData;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            // The nearest pixel of the guest's icon: whole factors keep a
            // 32x32 icon sharp.
            const unsigned p = argb[size_t(y * height / size) * width + size_t(x * width / size)];
            unsigned char *q = bytes + (size_t(y) * size + x) * 4;
            q[0] = (unsigned char)(p >> 16);
            q[1] = (unsigned char)(p >> 8);
            q[2] = (unsigned char)p;
            q[3] = (unsigned char)(p >> 24);
        }
    return [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
}

void Big(NSMutableData *data, unsigned value)
{
    const unsigned char bytes[4] = {(unsigned char)(value >> 24), (unsigned char)(value >> 16), (unsigned char)(value >> 8),
                                    (unsigned char)value};
    [data appendBytes:bytes length:4];
}

std::string BundleIdentifier(const std::string &name)
{
    unsigned long long hash = 1469598103934665603ull;
    for (unsigned char c : name) { hash ^= (unsigned char)tolower(c); hash *= 1099511628211ull; }
    char text[80];
    snprintf(text, sizeof text, "at.andreas-horvath.neo98.app.%012llx", hash & 0xffffffffffffull);
    return text;
}

// <directory>/<Name>.app with the helper, the icon and an Info.plist of its
// own. Written anew for every start of a program: the icon may have changed.
bool WriteBundle(const std::string &bundle, const HostDock::Program &program, const std::string &helper,
                 const std::string &fallback_icon, std::string &executable)
{
    NSFileManager *files = NSFileManager.defaultManager;
    NSString *root = Text(bundle), *macos = [root stringByAppendingPathComponent:@"Contents/MacOS"],
             *resources = [root stringByAppendingPathComponent:@"Contents/Resources"];
    if (![files createDirectoryAtPath:macos withIntermediateDirectories:YES attributes:nil error:nil] ||
        ![files createDirectoryAtPath:resources withIntermediateDirectories:YES attributes:nil error:nil])
        return false;
    NSString *program_path = [macos stringByAppendingPathComponent:@"neo98-dock-helper"];
    executable = program_path.UTF8String;
    // A helper of the same program may be running from this file: put the
    // new one beside it and rename.
    NSString *fresh = [program_path stringByAppendingString:@".neu"];
    [files removeItemAtPath:fresh error:nil];
    if (![files copyItemAtPath:Text(helper) toPath:fresh error:nil]) return false;
    if (rename(fresh.UTF8String, program_path.UTF8String) != 0) return false;
    chmod(program_path.UTF8String, 0755);
    NSString *icon = [resources stringByAppendingPathComponent:@"icon.icns"];
    bool has_icon = HostDock::WriteIcon(icon.UTF8String, program.width, program.height, program.argb);
    if (!has_icon && !fallback_icon.empty())
    {
        [files removeItemAtPath:icon error:nil];
        has_icon = [files copyItemAtPath:Text(fallback_icon) toPath:icon error:nil];
    }
    const std::string name = HostDock::DisplayName(program.name);
    NSMutableDictionary *info = [@{
        @"CFBundleIdentifier" : Text(BundleIdentifier(program.name)),
        @"CFBundleName" : Text(name),
        @"CFBundleDisplayName" : Text(name),
        @"CFBundleExecutable" : @"neo98-dock-helper",
        @"CFBundlePackageType" : @"APPL",
        @"CFBundleInfoDictionaryVersion" : @"6.0",
        @"CFBundleShortVersionString" : @"1.0",
        @"NSHighResolutionCapable" : @YES,
        // What the bundle stands for, for whoever finds it.
        @"NEO98Program" : Text(program.name),
    } mutableCopy];
    if (has_icon) info[@"CFBundleIconFile"] = @"icon";
    return [info writeToFile:[root stringByAppendingPathComponent:@"Contents/Info.plist"] atomically:YES];
}

void End(Helper &helper)
{
    if (helper.channel >= 0)
    {
        (void)!write(helper.channel, "bye\n", 4);
        close(helper.channel);
        helper.channel = -1;
    }
}
} // namespace

namespace HostDock
{
std::string DisplayName(const std::string &program)
{
    std::string name = program;
    const auto slash = name.find_last_of("\\/");
    if (slash != std::string::npos) name = name.substr(slash + 1);
    const auto dot = name.rfind('.');
    if (dot != std::string::npos && dot > 0) name = name.substr(0, dot);
    bool lower = false;
    for (char c : name) lower = lower || (c >= 'a' && c <= 'z');
    // "CALC" reads better as "Calc"; a name with small letters is the
    // program's own spelling.
    if (!lower)
        for (size_t i = 1; i < name.size(); ++i)
            if (name[i] >= 'A' && name[i] <= 'Z') name[i] = char(name[i] - 'A' + 'a');
    for (char &c : name)
        if (c == '/' || c == ':' || (unsigned char)c < 0x20) c = '-';
    return name.empty() ? std::string("Program") : name;
}

bool WriteIcon(const std::string &path, int width, int height, const std::vector<unsigned> &argb)
{
    @autoreleasepool
    {
        if (width <= 0 || height <= 0 || argb.size() < size_t(width) * size_t(height)) return false;
        static const struct { const char *type; int size; } entries[] = {
            {"icp5", 32}, {"ic07", 128}, {"ic08", 256}, {"ic09", 512}};
        NSMutableData *body = [NSMutableData data];
        for (const auto &entry : entries)
        {
            NSData *png = Png(width, height, argb, entry.size);
            if (!png) return false;
            [body appendBytes:entry.type length:4];
            Big(body, unsigned(png.length) + 8);
            [body appendData:png];
        }
        NSMutableData *file = [NSMutableData data];
        [file appendBytes:"icns" length:4];
        Big(file, unsigned(body.length) + 8);
        [file appendData:body];
        return [file writeToFile:Text(path) atomically:YES];
    }
}

std::vector<std::string> Sync(const std::vector<Program> &programs, const std::string &directory,
                              const std::string &helper_path, const std::string &fallback_icon, Mode mode)
{
    std::vector<std::string> log;
    if (mode == Mode::Off) return log;
    @autoreleasepool
    {
        char line[1200];
        // Helpers that ended by themselves, and what the others say.
        for (auto &helper : helpers)
        {
            if (helper.pid > 0)
            {
                int status = 0;
                if (waitpid(helper.pid, &status, WNOHANG) == helper.pid) helper.pid = 0;
            }
            if (helper.channel < 0) continue;
            char buffer[256];
            for (;;)
            {
                const ssize_t n = read(helper.channel, buffer, sizeof buffer);
                if (n > 0) { helper.pending.append(buffer, size_t(n)); continue; }
                if (n == 0) { close(helper.channel); helper.channel = -1; }
                break;
            }
            for (size_t end; (end = helper.pending.find('\n')) != std::string::npos;)
            {
                const std::string message = helper.pending.substr(0, end);
                helper.pending.erase(0, end + 1);
                Event event;
                event.process = helper.process;
                if (message == "activate") event.kind = Event::Activate;
                else if (message == "reopen") event.kind = Event::Reopen;
                else if (message == "quit") event.kind = Event::Quit;
                else
                {
                    snprintf(line, sizeof line, "dock helper pid=%u says %s", helper.process, message.c_str());
                    log.push_back(line);
                    continue;
                }
                events.push_back(event);
            }
        }
        // Programs that are gone.
        for (size_t i = 0; i < helpers.size();)
        {
            bool runs = false;
            for (const auto &program : programs) runs = runs || program.process == helpers[i].process;
            if (runs) { ++i; continue; }
            End(helpers[i]);
            snprintf(line, sizeof line, "dock program pid=%u %s ended", helpers[i].process, helpers[i].name.c_str());
            log.push_back(line);
            if (helpers[i].pid > 0)
            {
                // It ends on "bye"; it is reaped on a later turn or with the host.
                int status = 0;
                waitpid(helpers[i].pid, &status, WNOHANG);
            }
            helpers.erase(helpers.begin() + long(i));
        }
        // New programs.
        for (const auto &program : programs)
        {
            bool known = false;
            for (const auto &helper : helpers) known = known || helper.process == program.process;
            if (known) continue;
            Helper helper;
            helper.process = program.process;
            helper.name = DisplayName(program.name);
            const std::string bundle = directory + "/" + helper.name + ".app";
            std::string executable;
            const bool written = WriteBundle(bundle, program, helper_path, fallback_icon, executable);
            if (written && (mode == Mode::Dock || mode == Mode::Silent))
            {
                int pair[2] = {-1, -1};
                if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0)
                {
                    posix_spawn_file_actions_t actions;
                    posix_spawn_file_actions_init(&actions);
                    posix_spawn_file_actions_adddup2(&actions, pair[1], 3);
                    posix_spawnattr_t attributes;
                    posix_spawnattr_init(&attributes);
                    // Nothing of the host's files but its output and the channel.
                    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_CLOEXEC_DEFAULT);
                    for (int fd = 0; fd < 3; ++fd) posix_spawn_file_actions_addinherit_np(&actions, fd);
                    const char *arguments[] = {executable.c_str(), "--channel", "3",
                                               mode == Mode::Silent ? "--silent" : nullptr, nullptr};
                    pid_t pid = 0;
                    if (posix_spawn(&pid, executable.c_str(), &actions, &attributes, const_cast<char **>(arguments),
                                    environ) == 0)
                    {
                        helper.pid = pid;
                        helper.channel = pair[0];
                        fcntl(helper.channel, F_SETFL, fcntl(helper.channel, F_GETFL) | O_NONBLOCK);
                        fcntl(helper.channel, F_SETFD, FD_CLOEXEC);
                    }
                    else close(pair[0]);
                    close(pair[1]);
                    posix_spawn_file_actions_destroy(&actions);
                    posix_spawnattr_destroy(&attributes);
                }
            }
            snprintf(line, sizeof line, "dock program pid=%u name=%s icon=%dx%d helper=%d program=%s bundle=%s",
                     program.process, helper.name.c_str(), program.width, program.height, int(helper.pid),
                     program.name.c_str(), written ? bundle.c_str() : "not written");
            log.push_back(line);
            helpers.push_back(helper);
        }
    }
    return log;
}

bool Next(Event &event)
{
    if (events.empty()) return false;
    event = events.front();
    events.pop_front();
    return true;
}

void Shutdown()
{
    for (auto &helper : helpers) End(helper);
    helpers.clear();
    events.clear();
}
} // namespace HostDock
