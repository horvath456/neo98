#ifdef NEO98_RUNTIME_CONTENT
#include "host_windows_data.h"
#endif
// SPDX-License-Identifier: GPL-2.0-or-later
// Shared Linux launch, drive, registry, network, serial and printing providers.
#pragma once
#include "../src/x86_app.h"
#include "crash_report_files.h"
#include "host_volume.h"
#include "host_registry.h"
#include "registry_file.h"
#include "system_tree_host.h"
#include "host_comm.h"
#include "host_net.h"
#include "printer_port_host.h"
#include "host_single.h"
#ifdef NEO98_WITH_DOSBOX
#include "host_dos_vm.h"
#endif
#include <chrono>
#include <csignal>
#include <memory>
#include <map>
#include <functional>
#include <string>
#include <vector>

namespace LinuxHost
{
struct Options
{
    std::string exe = "C:\\WINDOWS\\EXPLORER.EXE", command, font, screenshot, game_machine, load_state;
    std::string reports = "build/crash-reports", printer = "build/print-output";
    std::string open_host, program_action, net_listen = "127.0.0.1";
    unsigned seconds = 0, scale = 1, desktop_width = 0, desktop_height = 0;
    bool quiet = true, offline = false, fullscreen = false, help = false, wifi = false, microphone = true;
    bool seamless = false, seamless_taskbar = false, seamless_windows = true, seamless_icons = false, seamless_dock = true;
    bool pause_unfocused = false, wheel_focus = false;
};
inline bool Parse(int argc, char **argv, Options &out)
{
    // Retain the EXE FONT invocation used by tools/host_run.py.
    unsigned positional = 0;
    for (int i = 1; i < argc; ++i)
    {
        std::string option = argv[i];
        if (option.empty()) return false;
        auto next = [&]() -> const char * { return ++i < argc ? argv[i] : nullptr; };
        if (option == "--help" || option == "-h") out.help = true;
        else if (option == "--quiet") out.quiet = true;
        else if (option == "--verbose") out.quiet = false;
        else if (option == "--offline") out.offline = true;
        else if (option == "--online") out.offline = false;
        else if (option == "--wifi") out.wifi = true;
        else if (option == "--no-wifi") out.wifi = false;
        else if (option == "--microphone") out.microphone = true;
        else if (option == "--no-microphone") out.microphone = false;
        else if (option == "--net-listen")
        {
            const char *value = next(); NetHost::U address;
            if (!value || !HostNet::PosixSockets::ParseListen(value, address)) return false;
            out.net_listen = value;
        }
        else if (option == "--fullscreen") out.fullscreen = true;
        else if (option == "--no-fullscreen") out.fullscreen = false;
        else if (option == "--seamless") out.seamless = true;
        else if (option == "--no-seamless") out.seamless = false;
        else if (option == "--seamless-taskbar") out.seamless_taskbar = true;
        else if (option == "--seamless-no-taskbar") out.seamless_taskbar = false;
        else if (option == "--seamless-layer") out.seamless_windows = false;
        else if (option == "--seamless-windows") out.seamless_windows = true;
        else if (option == "--seamless-icons") out.seamless_icons = true;
        else if (option == "--seamless-no-icons") out.seamless_icons = false;
        else if (option == "--seamless-no-dock") out.seamless_dock = false;
        else if (option == "--seamless-dock") out.seamless_dock = true;
        else if (option == "--pause-unfocused") out.pause_unfocused = true;
        else if (option == "--no-pause-unfocused") out.pause_unfocused = false;
        else if (option == "--wheel-focus") out.wheel_focus = true;
        else if (option == "--wheel-pointer") out.wheel_focus = false;
        else if (option == "--ram-drive" || option == "--no-registry-file" || option == "--setup-linux") {}
        else if (option == "--exe" || option == "--open-host" || option == "--activate-program" || option == "--quit-program" || option == "--command" || option == "--font" || option == "--time-limit" || option == "--game-machine" || option == "--load-state" ||
                 option == "--scale" || option == "--screenshot" || option == "--crash-report-dir" || option == "--printer-output")
        {
            const char *value = next(); if (!value) return false;
            if (option == "--exe") out.exe = value;
            else if (option == "--open-host") out.open_host = value;
            else if (option == "--activate-program" || option == "--quit-program")
            { out.program_action = option == "--activate-program" ? "activate" : "quit"; out.open_host = value; }
            else if (option == "--game-machine") out.game_machine = value;
            else if (option == "--load-state") out.load_state = value;
            else if (option == "--command") out.command = value;
            else if (option == "--font") out.font = value;
            else if (option == "--screenshot") out.screenshot = value;
            else if (option == "--crash-report-dir") out.reports = value;
            else if (option == "--printer-output") out.printer = value;
            else
            {
                char *end = nullptr; unsigned long number = strtoul(value, &end, 10);
                if (!*value || *end || number > 86400) return false;
                if (option == "--scale") { if (!number || number > 4) return false; out.scale = number; }
                else out.seconds = number;
            }
        }
        else if (option == "--desktop")
        {
            const char *value = next(); if (!value) return false;
            unsigned width = 0, height = 0; char tail = 0;
            if (sscanf(value, "%ux%u%c", &width, &height, &tail) != 2 || width < 320 || height < 200 || width > 4096 || height > 4096) return false;
            out.desktop_width = width; out.desktop_height = height;
        }
        else if (option == "--system-tree") { if (i + 1 < argc && argv[i + 1][0] != '-') ++i; }
        else if (option == "--drive-c" || option == "--drive" || option == "--drive-readonly" || option == "--drive-below" || option == "--cdrom" || option == "--registry")
        { if (!next()) return false; }
        else if (option[0] != '-' || option == "-")
        {
            if (positional == 0) { if (option != "-") out.exe = option; }
            else if (positional == 1) out.font = option;
            else return false;
            ++positional;
        }
        else { fprintf(stderr, "Unknown option: %s\n", option.c_str()); return false; }
    }
    return true;
}
inline void Help(bool window)
{
    printf("Neo98 on Linux x64, native x86 execution\n"
           "Usage: %s --exe 'C:\\WINDOWS\\NOTEPAD.EXE' [options]\n"
           "  --drive-c FOLDER       Drive C:; defaults to NEO98_WIN98_ROOT or build/win98-c\n"
           "  --game-machine FOLDER  Start a game machine\n"
           "  --load-state ID        Load a game-machine state (graphical host)\n"
           "  --setup-linux          Open Linux setup (graphical host)\n"
           "  --drive D=FOLDER       Additional drives\n"
           "  --cdrom IMAGE         Mount an ISO, CUE or folder as CD-ROM\n"
           "  --command LINE        Guest command line\n"
           "  --time-limit SECONDS  Stop after this interval; otherwise wait for guest exit\n"
           "  --offline             Disconnect guest networking\n"
           "  --wifi                Expose host Wi-Fi in Windows\n"
           "  --no-microphone       Disable the Windows microphone\n"
           "  --net-listen ADDRESS   127.0.0.1, any or an IPv4 address for game servers\n"
           "  --desktop WIDTHxHEIGHT  Guest desktop size\n"
           "  --screenshot FILE    Save the screen as PNG\n"
           "  --verbose             Log API calls\n", window ? "neo98-host" : "neo98-cli");
    if (window) puts("  --seamless             Show guest windows on the Linux desktop\n"
                     "  --no-seamless          Show the guest desktop in one window\n"
                     "  --game-machine FOLDER  Game machine with separate write layers\n"
                     "  Ctrl+Shift+M opens the machine menu; Ctrl+Shift+S toggles seamless view.\n"
                     "  F11 toggles full screen.");
}
inline bool verbose = false;
// --quiet leaves out the line of every API call, as on macOS; what the
// runtime has to say beyond those stays in the log.
inline void Log(const char *line)
{
    if (!verbose && !strncmp(line, "APP ", 4)) return;
    fprintf(stderr, "%s\n", line); fflush(stderr);
}
inline std::vector<unsigned char> ReadFile(const char *path)
{
    FILE *file = fopen(path, "rb"); if (!file) return {};
    fseek(file, 0, SEEK_END); long length = ftell(file); rewind(file);
    std::vector<unsigned char> bytes;
    if (length > 0 && static_cast<unsigned long long>(length) <= 0xffffffffu)
    { bytes.resize(length); if (fread(bytes.data(), 1, bytes.size(), file) != bytes.size()) bytes.clear(); }
    fclose(file); return bytes;
}
class Session
{
    int argc; char **argv;
    std::vector<unsigned char> exe, font;
    std::string locked_root;
  public:
    Options options;
    HostNet::PosixSockets network;
    HostComm::Ports serial;
    std::unique_ptr<PrinterPortHost::Directory> printer;
    std::unique_ptr<X86::Application> app;
    bool forwarded = false;
#ifdef NEO98_WITH_DOSBOX
    void RefreshDos(const std::map<char, std::string> *roots = nullptr)
    {
        std::vector<HostDosVm::Drive> dos;
        const auto mounted = HostVolume::Scan(argc, argv);
        const char *root = mounted.drive_c ? mounted.drive_c : HostVolume::DefaultDriveC();
        if (auto *layer = HostVolume::Layer('C'); layer && Volume::Mounted('C') == layer)
            dos.push_back({'C', layer->BelowRoot().c_str(), false, layer->Root().c_str(), layer->Foreign()});
        else if (SystemTree::Mounted()) dos.push_back({'C', SystemTreeHost::MountedRoot(), true, mounted.ram ? nullptr : root});
        else dos.push_back({'C', root, false});
        auto add = [&](char letter, const char *path, bool readonly) {
            if (auto *layer = HostVolume::Layer(letter); layer && Volume::Mounted(letter) == layer)
                dos.push_back({letter, layer->BelowRoot().c_str(), false, layer->Root().c_str(), layer->Foreign()});
            else dos.push_back({letter, path, readonly});
        };
        if (roots)
        {
            for (const auto &entry : *roots)
                if (entry.first != 'C' && Volume::Mounted(entry.first) && Volume::Type(entry.first) != Volume::Cdrom)
                    add(entry.first, entry.second.c_str(), !Volume::Writable(entry.first));
        }
        else for (unsigned i = 0; i < mounted.count; ++i)
            if (!mounted.extra[i].cdrom)
                add(mounted.extra[i].letter, mounted.extra[i].directory, mounted.extra[i].readonly);
        HostDosVm::Install(Log, dos.data(), unsigned(dos.size()));
    }
#endif
    Session(int count, char **arguments, Options settings) : argc(count), argv(arguments), options(std::move(settings)) {}
    ~Session()
    {
        Stop();
    }
    void Stop()
    {
        CdAudio::ResetAll();
        if (app) { X86::Application::TerminateChildren(0); app.reset(); }
#ifdef NEO98_WITH_DOSBOX
        HostDosVm::Uninstall();
#endif
        Volume::Clear();
        if (!locked_root.empty()) { HostSingle::Release(locked_root); locked_root.clear(); }
        PrinterPort::Install(nullptr); NetHost::Install(nullptr);
    }
    bool Start(bool allow_forward = false, std::function<bool()> before_mount = {}, std::function<int(X86::Application &, const unsigned short *)> before_start = {})
    {
        verbose = !options.quiet;
        auto tree = SystemTreeHost::Scan(argc, argv);
        const auto drives = HostVolume::Scan(argc, argv);
        const char *root = tree.root ? tree.root : drives.drive_c ? drives.drive_c : HostVolume::DefaultDriveC();
        if (!drives.ram)
        {
            const char *drive = drives.drive_c ? drives.drive_c : root;
            long owner = 0;
            const auto lock = HostSingle::Acquire(drive, owner);
            if (lock != HostSingle::Result::Owner)
            {
                if (lock == HostSingle::Result::Running && allow_forward && options.command.empty())
                {
                    char process[64], executable[PATH_MAX] = {}, own[PATH_MAX] = {};
                    snprintf(process, sizeof process, "/proc/%ld/exe", owner);
                    const ssize_t count = readlink(process, executable, sizeof executable - 1);
                    if (count > 0 && realpath(argv[0], own) && !strcmp(executable, own) &&
                        HostSingle::Hand(drive, !options.open_host.empty() ? options.open_host :
                            !options.game_machine.empty() || !strcasecmp(options.exe.c_str(), "C:\\WINDOWS\\EXPLORER.EXE") ? "" : options.exe,
                            options.program_action.empty() ? "open" : options.program_action.c_str()))
                    { forwarded = true; return false; }
                }
                if (lock == HostSingle::Result::Running) fprintf(stderr, "Drive C: is already in use by process %ld.\n", owner);
                else fprintf(stderr, "Could not lock drive C: %s\n", drive);
                return false;
            }
            locked_root = drive;
        }
        if (before_mount && !before_mount()) return false;
        if (!SystemTreeHost::Mount(root) || !HostVolume::Mount(drives, root))
        { fprintf(stderr, "Could not open drive C: %s\n", root); return false; }
#ifdef NEO98_RUNTIME_CONTENT
        {
            Files::Session files(Files::System()); std::string error;
            if (!HostWindowsData::Load([&](const char *path, HostWindowsData::Bytes &bytes) {
                return HostRegistry::ReadGuestFile(files, path, bytes);
            }, error)) { fprintf(stderr, "Neo98: %s\n", error.c_str()); return false; }
        }
#endif
        RegistryFile::Attach(argc, argv, drives.drive_c ? drives.drive_c : root);
        if (!HostRegistry::Start(stderr)) return false;
#ifdef NEO98_WITH_DOSBOX
        RefreshDos();
#endif
        if (!network.ConfigureListen(options.net_listen))
        { fprintf(stderr, "Invalid guest-network listen address.\n"); return false; }
        network.SetOnline(!options.offline); NetHost::Install(&network); serial.Install();
        printer = std::make_unique<PrinterPortHost::Directory>(options.printer); PrinterPort::Install(printer.get());
        const time_t now = time(nullptr); struct tm clock; localtime_r(&now, &clock);
        unsigned short fields[] = {static_cast<unsigned short>(1900 + clock.tm_year), static_cast<unsigned short>(clock.tm_mon + 1),
            static_cast<unsigned short>(clock.tm_wday), static_cast<unsigned short>(clock.tm_mday), static_cast<unsigned short>(clock.tm_hour),
            static_cast<unsigned short>(clock.tm_min), static_cast<unsigned short>(clock.tm_sec), 0};
        X86::Application::StartGuestClock(fields);
        if (options.desktop_width) X86::Application::SetHostDisplay(options.desktop_width, options.desktop_height, false);
        app = std::make_unique<X86::Application>(Log);
        if (before_start) { const int result = before_start(*app, fields); if (result) return result > 0; }
        if (SystemTreeHost::GuestPath(options.exe.c_str()))
        { if (!SystemTreeHost::ReadGuest(options.exe.c_str(), exe)) { fprintf(stderr,"Could not read program: %s\n",options.exe.c_str()); return false; } }
        else exe = ReadFile(options.exe.c_str());
        if (options.font.empty())
        { if (!HostSystemFont::Load("C:\\WINDOWS\\FONTS\\VGAFIX.FON", font)) return false; }
        else if (HostSystemFont::GuestPath(options.font.c_str()))
        { if (!HostSystemFont::Load(options.font.c_str(), font)) return false; }
        else font = ReadFile(options.font.c_str());
        if (exe.empty() || font.empty()) { fprintf(stderr, "Program or system font is missing.\n"); return false; }
        const bool started = app->Start(exe.data(), exe.size(), font.data(), font.size(), options.exe.c_str(),
                                      options.command.empty() ? nullptr : options.command.c_str());
        if (!started) fprintf(stderr, "Guest startup failed: %s\n", app->Error());
        return started;
    }
    bool Finished() const
    {
        return (app->Status() == X86::Application::Exited || app->Status() == X86::Application::Failed) && !X86::Application::LiveChildren();
    }
    int Result() const
    {
        if (app->Status() == X86::Application::Failed || X86::Application::FailedChildren()) return 1;
        return app->ExitCode() & 255;
    }
    void Describe() const
    {
        fprintf(stderr, "NEO98 Linux state=%u calls=%u callbacks=%u windows=%u children=%u error=%s\n",
                app->Status(), app->ApiCalls(), app->Callbacks(), User::LiveWindows(), X86::Application::LiveChildren(), app->Error());
        for (unsigned i = 0; const char *failure = X86::Application::FailedChild(i); ++i)
            fprintf(stderr, "NEO98 Linux child failed: %s\n", failure);
    }
    // A successful exec keeps the descriptor. The next process reclaims it.
    void PrepareRestart()
    {
        HostSingle::KeepAcrossExec(); locked_root.clear();
    }
    bool Screenshot(const std::string &path)
    {
        User::Render(); int width, height; User::PixelsSize(width, height);
        const auto *pixels = app ? reinterpret_cast<const unsigned char *>(app->ConsolePixels()) : nullptr;
        if (pixels) { width = 800; height = 600; } else pixels = User::Pixels();
        FILE *file = fopen(path.c_str(), "wb"); if (!file) return false;
        // A .ppm path gets binary PPM (P6), as the macOS host writes it;
        // every other path a PNG.
        if (path.size() > 4 && path.compare(path.size() - 4, 4, ".ppm") == 0)
        {
            fprintf(file, "P6\n%d %d\n255\n", width, height);
            for (size_t i = 0; i < size_t(width) * size_t(height); ++i)
            {
                const unsigned char rgb[] = {pixels[i * 4 + 2], pixels[i * 4 + 1], pixels[i * 4]};
                fwrite(rgb, 1, 3, file);
            }
            return fclose(file) == 0;
        }
        CrashFiles::FileSink sink(file);
        bool okay = CrashReport::WritePng(sink, pixels, width, height);
        fclose(file); return okay;
    }
    void Report(const char *reason)
    {
        auto report = std::make_unique<CrashReport::Report>();
        CrashFiles::Describe(*report, reason, "linux-x64-native", argc, argv);
        app->CollectCrashReport(*report); int width, height; User::PixelsSize(width, height); char path[1024];
        CrashFiles::Write(*report, options.reports.c_str(), User::Pixels(), width, height, path, sizeof path);
    }
};
}
