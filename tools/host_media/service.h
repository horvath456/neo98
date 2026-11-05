// SPDX-License-Identifier: GPL-2.0-or-later
// The host media provider (src/host_media.h): Kodi's catalogue, small
// pictures, the player and Kodi's dialogs for Media Center 98, and the
// screen for Kodi while it plays. The guest gets lists, pictures, states and
// the words of a dialog; the video stays in Kodi.
// NEO98_TEST_KODI replaces the search for Kodi: "none" is a computer without
// Kodi, a number the port of a running test server
// (tools/test_kodi_server.py). NEO98_KODI names Kodi's program and
// NEO98_KODI_PORT its JSON-RPC port (9090). The dialogs come from NEO98's
// add-on service.neo98 in Kodi, which the provider puts into Kodi's folder.
#pragma once
#include "../../src/host_media.h"
#include "../host_settings.h"
#include "kodi.h"
#include "picture.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>
namespace HostMediaService
{
using HostMediaJson::Value;

template <size_t N> void Text(char (&out)[N], const std::string &in)
{
    // Cut at a character boundary of UTF-8, the guest converts it to ANSI.
    size_t n = std::min(in.size(), N - 1);
    while (n && n < in.size() && (in[n] & 0xc0) == 0x80) --n;
    memcpy(out, in.data(), n); out[n] = 0;
}

constexpr char NetflixAddon[] = "plugin.video.netflix";
constexpr char NetflixRoot[] = "plugin://plugin.video.netflix/";
constexpr char HelperAddon[] = "service.neo98";

inline bool ReadFile(const std::string &path, std::string &out)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    out.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return true;
}
// The add-on as the package brings it (tools/build_host_media.py), beside
// the host program; empty if it is not there.
inline std::string HelperSource()
{
    const std::string exe = HostSettings::Executable();
    const std::string folder = exe.substr(0, exe.rfind('/') + 1);
    for (const char *place : {"../share/neo98/", "../Resources/", "../host-media/"})
        if (HostMediaKodi::Folder(folder + place + HelperAddon)) return folder + place + HelperAddon + "/";
    return "";
}

inline std::string NoKodi()
{
#ifdef __APPLE__
    return "Kodi is not installed on this Mac. Install Kodi from kodi.tv/download, start it once, and then open Media Center 98 again.";
#else
    return "Kodi is not installed on this computer. Install Kodi (the package kodi, or the Flatpak tv.kodi.Kodi from kodi.tv/download), start it once, and then open Media Center 98 again.";
#endif
}
inline std::string NoAnswer(unsigned port)
{
    return "Kodi does not answer on port " + std::to_string(port) + ". In Kodi, open Settings > Services > Control and turn on "
           "\"Allow remote control from applications on this system\". Then click Retry.";
}

// Fits a picture into a box of the given size, keeping its proportions, on
// the background colour; an area average when it shrinks.
inline void Fit(const std::vector<unsigned> &in, unsigned w, unsigned h, unsigned box_w, unsigned box_h, unsigned background, std::vector<unsigned> &out)
{
    out.assign(size_t(box_w) * box_h, background & 0xffffff);
    if (!w || !h) return;
    unsigned tw = box_w, th = unsigned(uint64_t(h) * box_w / w);
    if (th > box_h) { th = box_h; tw = std::max(1u, unsigned(uint64_t(w) * box_h / h)); }
    th = std::max(1u, th);
    const unsigned ox = (box_w - tw) / 2, oy = (box_h - th) / 2;
    const unsigned br = background >> 16 & 255, bg = background >> 8 & 255, bb = background & 255;
    for (unsigned y = 0; y < th; ++y)
    {
        const unsigned y0 = unsigned(uint64_t(y) * h / th), y1 = std::max(y0 + 1, unsigned(uint64_t(y + 1) * h / th));
        for (unsigned x = 0; x < tw; ++x)
        {
            const unsigned x0 = unsigned(uint64_t(x) * w / tw), x1 = std::max(x0 + 1, unsigned(uint64_t(x + 1) * w / tw));
            uint64_t a = 0, r = 0, g = 0, b = 0, n = 0;
            for (unsigned sy = y0; sy < y1 && sy < h; ++sy)
                for (unsigned sx = x0; sx < x1 && sx < w; ++sx)
                {
                    const unsigned p = in[size_t(sy) * w + sx];
                    a += p >> 24; r += p >> 16 & 255; g += p >> 8 & 255; b += p & 255; ++n;
                }
            if (!n) continue;
            const unsigned pa = unsigned(a / n);
            const unsigned pr = unsigned(r / n + br * (255 - pa) / 255), pg = unsigned(g / n + bg * (255 - pa) / 255), pb = unsigned(b / n + bb * (255 - pa) / 255);
            out[size_t(oy + y) * box_w + ox + x] = std::min(pr, 255u) << 16 | std::min(pg, 255u) << 8 | std::min(pb, 255u);
        }
    }
}

class Service final : public HostMedia::Provider
{
    struct Folder
    {
        unsigned state = HM_LIST_LOADING;
        std::string title, message;
        std::vector<HM_Entry> entries;
    };
    struct Art
    {
        unsigned state = HM_PICTURE_LOADING, width = 0, height = 0;
        std::vector<unsigned> pixels;
    };
    struct ArtJob { std::string key, art; unsigned width, height, background; };

    std::mutex lock;
    std::condition_variable wake, art_wake;
    std::thread worker, painter;
    // wanted: the program asked for Kodi. asked: the first question, which
    // starts Kodi by itself; after that only Retry (HM_START) does.
    bool stopping = false, wanted = false, asked = false, reconnect = false;
    HM_Status status{};
    HostMediaKodi::Connection kodi;
    unsigned port = 9090;
    bool test = false, launched = false, raise_after_start = false;
    std::map<std::string, Folder> folders;
    std::deque<std::string> folder_jobs;
    std::map<std::string, Art> arts;
    std::deque<ArtJob> art_jobs;
    std::deque<HM_Request> controls;
    unsigned player_id = 1;
    // Kodi's dialog, read through service.neo98 on a thread of its own, and
    // the answers of the program still to send.
    std::thread watcher;
    std::condition_variable answer_wake;
    HostMediaKodi::Helper helper;
    HM_Dialog dialog{};
    struct Answer { unsigned serial, kind; int value; std::string text; };
    std::deque<Answer> answers;
    // Streams of YouTube videos looked up ahead (HM_PREPARE): the address
    // for a catalogue path, and when it was found; the path being looked up.
    std::thread preparer;
    std::condition_variable prepare_wake;
    struct Found { std::string stream; std::chrono::steady_clock::time_point when; };
    std::map<std::string, Found> streams;
    std::string preparing, prepare_next;
    // The screen: what the provider wants, what the host shows.
    std::atomic<unsigned> screen_wanted{HM_SCREEN_NEO98};
    unsigned screen_shown = HM_SCREEN_NEO98;
    std::atomic<bool> raise_neo98{false};
    // The compositor of the Linux hosts Kodi draws into
    // (tools/host_wayland/compositor.h), empty without one.
    std::string wayland_socket, wayland_runtime;
public:
    // Where the guest shows Kodi's picture, in screen pixels: Media Center
    // 98's video area.
    // fill: the program shows the video on its whole screen, so the host
    // draws it on the whole of its own.
    struct Area { int x = 0, y = 0, width = 0, height = 0; bool shown = false, fill = false; };
private:
    Area area;
    std::atomic<bool> full_wanted{false};
    bool full_shown = false;
    HostMediaKodi::Events events;
    // Playback this program asked for: when it started, whether Kodi's
    // fullscreen video was seen, and how many polls found it gone.
    std::chrono::steady_clock::time_point opened{};
    // The video's width / height from Kodi (0 unknown); poll_soon asks
    // for it right after the first picture.
    double aspect = 0;
    bool poll_soon = false;
    // Kodi's first picture came (Player.OnAVStart) at av_at; the program
    // still shows its animation until Kodi's clock runs.
    bool av_started = false;
    std::chrono::steady_clock::time_point av_at{};
    bool seen_fullscreen = false;
    unsigned away = 0;

    void Changed() { ++status.generation; }
    void Say(unsigned state, const std::string &message)
    {
        status.state = state; Text(status.message, message); Changed();
        fprintf(stderr, "NEO98 HOST media state=%u %s\n", state, message.c_str());
    }
    void Stopped()
    {
        status.player = HM_STOPPED; status.time = status.total = 0; status.title[0] = 0; aspect = 0; av_started = false;
        screen_wanted = HM_SCREEN_NEO98; seen_fullscreen = false; away = 0; Changed();
    }

    bool Call(const char *method, Value params, Value &result, std::string &error, unsigned timeout_ms = 15000)
    {
        return kodi.Call(method, std::move(params), result, error, timeout_ms);
    }
    bool Has(const char *addon)
    {
        Value params = Value::MakeObject(), result; std::string error;
        params.Set("addonid", addon);
        Value properties = Value::MakeArray(); properties.Add("enabled"); params.Set("properties", std::move(properties));
        return Call("Addons.GetAddonDetails", std::move(params), result, error) && result["addon"]["enabled"].Truth();
    }

    void Notify(const std::string &method, const Value &params)
    {
        fprintf(stderr, "NEO98 HOST media notification %s\n", method.c_str());
        std::lock_guard<std::mutex> guard(lock);
        const auto &item = params["data"]["item"];
        if (method == "Player.OnPlay" || method == "Player.OnAVStart" || method == "Player.OnResume")
        {
            if (status.player == HM_STOPPED) return;  // Not started from Media Center 98.
            // Opening until the first picture and a running clock: the
            // program shows its own animation, not Kodi's spinner (Poll).
            if (method == "Player.OnAVStart" && status.player == HM_OPENING && !av_started) { av_started = true; av_at = std::chrono::steady_clock::now(); }
            if (status.player != HM_OPENING) status.player = HM_PLAYING;
            if (method == "Player.OnAVStart" || method == "Player.OnAVChange") poll_soon = true;
            // The catalogue's name stays (a yt-dlp address has none worth showing).
            if (!status.title[0] && item["title"].type == Value::String && !item["title"].text.empty()) Text(status.title, HostMediaKodi::Plain(item["title"].text));
            if (params["data"]["player"]["playerid"].type == Value::Number) player_id = unsigned(params["data"]["player"]["playerid"].Num());
            Changed();
        }
        else if (method == "Player.OnPause") { if (status.player != HM_STOPPED) { status.player = HM_PAUSED; Changed(); } }
        else if (method == "Player.OnStop") { if (status.player != HM_STOPPED) Stopped(); }
        else if (method == "Player.OnAVChange") poll_soon = true;
        else if (method == "Application.OnVolumeChanged")
        {
            status.volume = unsigned(std::min(std::max(params["data"]["volume"].Num(status.volume), 0.0), 100.0));
            status.muted = params["data"]["muted"].Truth();
            Changed();
        }
        else if (method == "System.OnQuit" || method == "System.OnRestart") reconnect = true;
        wake.notify_all();
    }
    void Lost()
    {
        std::lock_guard<std::mutex> guard(lock);
        reconnect = true; wake.notify_all();
    }

    // Connects to Kodi, starting it when it does not run. Without the lock.
    void Connect()
    {
        const char *chosen = getenv("NEO98_TEST_KODI");
        {
            std::lock_guard<std::mutex> guard(lock);
            folders.clear(); folder_jobs.clear(); Stopped();
            if (chosen && !strcmp(chosen, "none")) { Say(HM_NO_KODI, NoKodi()); wanted = false; return; }
            test = chosen && *chosen;
            port = test ? unsigned(atoi(chosen)) : getenv("NEO98_KODI_PORT") ? unsigned(atoi(getenv("NEO98_KODI_PORT"))) : 9090u;
            status.state = HM_STARTING; Changed();
        }
        bool open = kodi.Open(port, 500);
        if (!open && !test)
        {
            HostMediaKodi::Install install;
            if (!HostMediaKodi::Find(install))
            {
                std::lock_guard<std::mutex> guard(lock);
                Say(HM_NO_KODI, NoKodi()); wanted = false; return;
            }
            std::vector<std::string> command = install.command, variables;
            std::string socket, runtime;
            { std::lock_guard<std::mutex> guard(lock); socket = wayland_socket; runtime = wayland_runtime; }
            if (!socket.empty())
            {
                // Kodi draws into NEO98's compositor, not onto the screen.
                command.push_back("--windowing=wayland");
                variables = {"WAYLAND_DISPLAY=" + socket, "XDG_RUNTIME_DIR=" + runtime};
            }
            fprintf(stderr, "NEO98 HOST media starting Kodi: %s%s\n", install.where.c_str(), socket.empty() ? "" : " under NEO98's compositor");
            if (!HostMediaKodi::Spawn(command, -1, variables))
            {
                std::lock_guard<std::mutex> guard(lock);
                Say(HM_FAILED, "Kodi (" + install.where + ") could not be started."); wanted = false; return;
            }
            launched = true;
            // Kodi needs a few seconds before its port opens.
            for (unsigned i = 0; i < 90 && !open; ++i)
            {
                {
                    std::unique_lock<std::mutex> guard(lock);
                    if (wake.wait_for(guard, std::chrono::milliseconds(500), [&] { return stopping; })) return;
                }
                open = kodi.Open(port, 500);
            }
        }
        if (!open)
        {
            std::lock_guard<std::mutex> guard(lock);
            Say(HM_FAILED, test ? "The test Kodi does not answer on port " + std::to_string(port) + "." : NoAnswer(port)); wanted = false;
            return;
        }
        Value result; std::string error, name = "Kodi";
        Value properties = Value::MakeArray(); properties.Add("name"); properties.Add("version"); properties.Add("volume"); properties.Add("muted");
        Value params = Value::MakeObject(); params.Set("properties", std::move(properties));
        if (!Call("Application.GetProperties", std::move(params), result, error))
        {
            std::lock_guard<std::mutex> guard(lock);
            Say(HM_FAILED, "Kodi does not answer: " + error); wanted = false; kodi.Close(); return;
        }
        if (result["name"].type == Value::String) name = result["name"].text;
        const unsigned volume = unsigned(std::min(std::max(result["volume"].Num(100), 0.0), 100.0)), muted = result["muted"].Truth();
        if (result["version"]["major"].type == Value::Number)
            name += " " + std::to_string(int(result["version"]["major"].Num())) + "." + std::to_string(int(result["version"]["minor"].Num()));
        // What streaming needs; NEO98 brings none of it.
        unsigned features = 0;
        if (Has(NetflixAddon)) features |= HM_HAS_NETFLIX;
        if (Has("inputstream.adaptive")) features |= HM_HAS_INPUTSTREAM;
        if (Has("script.module.inputstreamhelper")) features |= HM_HAS_HELPER;
        Value cdm = Value::MakeObject(); cdm.Set("directory", "special://home/cdm/"); cdm.Set("media", "files");
        if (Call("Files.GetDirectory", std::move(cdm), result, error))
            for (const auto &file : result["files"].items)
            {
                std::string lower = file["label"].Text();
                for (auto &c : lower) c = char(tolower((unsigned char)c));
                if (lower.find("widevine") != std::string::npos) features |= HM_HAS_WIDEVINE;
            }
        if (!test) InstallHelper();
        std::lock_guard<std::mutex> guard(lock);
        Text(status.kodi, name);
        status.volume = volume; status.muted = muted;
        status.features = (status.features & HM_HAS_DIALOGS) | features;
        raise_after_start = launched;
        if (launched) raise_neo98 = true;
        Say(HM_READY, "");
        fprintf(stderr, "NEO98 HOST media connected %s port=%u features=%u\n", name.c_str(), port, features);
    }

    // service.neo98 into Kodi's folder, when it is missing or older, and
    // turned on. Kodi finds a new add-on after UpdateLocalAddons, which only
    // its EventServer runs.
    void InstallHelper()
    {
        const std::string source = HelperSource(), home = HostMediaKodi::Home();
        if (source.empty() || home.empty())
        {
            fprintf(stderr, "NEO98 HOST media: no %s %s\n", HelperAddon, source.empty() ? "beside the host program" : "folder of Kodi");
            return;
        }
        const std::string target = home + "addons/" + HelperAddon + "/";
        bool changed = false;
        for (const char *file : {"addon.xml", "service.py"})
        {
            std::string want, have;
            if (!ReadFile(source + file, want)) return;
            if (ReadFile(target + file, have) && have == want) continue;
            mkdir((home + "addons").c_str(), 0755); mkdir(target.c_str(), 0755);
            std::ofstream out(target + file, std::ios::binary);
            out << want;
            if (!out) { fprintf(stderr, "NEO98 HOST media: cannot write %s%s\n", target.c_str(), file); return; }
            changed = true;
        }
        Value params = Value::MakeObject(), result; std::string error;
        params.Set("addonid", HelperAddon);
        Value properties = Value::MakeArray(); properties.Add("enabled"); properties.Add("version"); params.Set("properties", properties);
        bool known = Call("Addons.GetAddonDetails", params, result, error, 3000);
        if (!known || changed)
        {
            events.Builtin("UpdateLocalAddons");
            for (unsigned i = 0; i < 20 && !(known = Call("Addons.GetAddonDetails", params, result, error, 3000)); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        if (!known) { fprintf(stderr, "NEO98 HOST media: Kodi did not find %s: %s\n", HelperAddon, error.c_str()); return; }
        // A changed add-on starts again with its new code.
        auto enable = [&](bool on) {
            Value change = Value::MakeObject(), done;
            change.Set("addonid", HelperAddon); change.Set("enabled", on);
            return Call("Addons.SetAddonEnabled", std::move(change), done, error, 5000);
        };
        const bool running = result["addon"]["enabled"].Truth();
        if (running && changed) enable(false);
        if (!running || changed)
            fprintf(stderr, "NEO98 HOST media %s %s\n", enable(true) ? "turned on" : "could not turn on", HelperAddon);
    }

    static unsigned Kind(const std::string &type, bool folder)
    {
        if (type == "tvshow") return HM_KIND_SHOW;
        if (type == "season") return HM_KIND_SEASON;
        if (type == "movie") return HM_KIND_MOVIE;
        if (type == "episode") return HM_KIND_EPISODE;
        return folder ? HM_KIND_FOLDER : HM_KIND_VIDEO;
    }
    static std::string Art(const Value &item)
    {
        const auto &art = item["art"];
        for (const char *key : {"poster", "tvshow.poster", "season.poster", "thumb", "icon", "fanart"})
            if (art[key].type == Value::String && !art[key].text.empty()) return art[key].text;
        return item["thumbnail"].Text();
    }

    // One folder of the catalogue. "" is the top: Kodi's video add-ons and
    // video sources.
    void Load(const std::string &path)
    {
        Folder folder;
        std::string error;
        Value result;
        unsigned features;
        { std::lock_guard<std::mutex> guard(lock); features = status.features; }
        auto add = [&](unsigned flags, unsigned kind, const std::string &label, const std::string &where, const std::string &art, const std::string &detail) {
            HM_Entry e{};
            e.flags = flags; e.kind = kind;
            Text(e.label, HostMediaKodi::Plain(label)); Text(e.path, where); Text(e.art, art); Text(e.detail, HostMediaKodi::Plain(detail));
            if (where.size() < HM_PATH) folder.entries.push_back(e);
        };
        if (path.empty())
        {
            folder.title = "Kodi";
            // Netflix stands in the catalogue even without its add-on, and
            // says what is missing when it opens.
            if (!(features & HM_HAS_NETFLIX)) add(HM_ENTRY_FOLDER, HM_KIND_ADDON, "Netflix", NetflixRoot, "", "Not installed in Kodi");
            Value params = Value::MakeObject();
            params.Set("type", "xbmc.addon.video"); params.Set("content", "video"); params.Set("enabled", true);
            Value properties = Value::MakeArray(); properties.Add("name"); properties.Add("thumbnail"); properties.Add("summary");
            params.Set("properties", std::move(properties));
            if (Call("Addons.GetAddons", std::move(params), result, error))
                for (const auto &addon : result["addons"].items)
                    add(HM_ENTRY_FOLDER, HM_KIND_ADDON, addon["name"].Text(addon["addonid"].Text().c_str()), "plugin://" + addon["addonid"].Text() + "/",
                        addon["thumbnail"].Text(), addon["summary"].Text());
            Value sources = Value::MakeObject(); sources.Set("media", "video");
            if (Call("Files.GetSources", std::move(sources), result, error))
                for (const auto &source : result["sources"].items)
                    add(HM_ENTRY_FOLDER, HM_KIND_SOURCE, source["label"].Text(), source["file"].Text(), "", "");
            folder.state = HM_LIST_READY;
        }
        else
        {
            const bool plugin = path.compare(0, 9, "plugin://") == 0;
            const bool netflix = path.compare(0, sizeof NetflixRoot - 1, NetflixRoot) == 0;
            // Asked again each time: the user may have installed it since (Retry).
            if (netflix && !(features & HM_HAS_NETFLIX) && Has(NetflixAddon))
            {
                std::lock_guard<std::mutex> guard(lock);
                status.features |= HM_HAS_NETFLIX; features = status.features; Changed();
            }
            if (netflix && !(features & HM_HAS_NETFLIX))
            {
                folder.state = HM_LIST_FAILED;
                folder.title = "Netflix";
                folder.message = "The Netflix add-on is not installed in Kodi. NEO98 does not include it. In Kodi, add the repository of "
                                 "CastagnaIT (github.com/CastagnaIT/plugin.video.netflix), install the add-on Netflix from it, and sign in there.";
            }
            else
            {
                Value params = Value::MakeObject();
                params.Set("directory", path); params.Set("media", "video");
                Value properties = Value::MakeArray();
                for (const char *p : {"title", "thumbnail", "art", "plot", "showtitle", "season", "episode", "year"}) properties.Add(p);
                params.Set("properties", std::move(properties));
                // Add-ons fetch their lists from the internet and may be slow.
                if (Call("Files.GetDirectory", std::move(params), result, error, 90000))
                {
                    folder.state = HM_LIST_READY;
                    for (const auto &item : result["files"].items)
                    {
                        const bool is_folder = item["filetype"].Text() == "directory";
                        std::string label = item["label"].Text(item["title"].Text().c_str());
                        add(is_folder ? HM_ENTRY_FOLDER : HM_ENTRY_PLAYABLE, Kind(item["type"].Text(), is_folder), label, item["file"].Text(), Art(item),
                            item["plot"].Text());
                    }
                    // The add-on's next page (YouTube: "Next page", a page or
                    // page_token in its address) goes on at the end.
                    for (auto &e : folder.entries)
                    {
                        std::string label = e.label, where = e.path;
                        for (auto &c : label) c = char(tolower((unsigned char)c));
                        if ((e.flags & HM_ENTRY_FOLDER) && (where.find("page_token=") != std::string::npos || where.find("page=") != std::string::npos) &&
                            (label.find("next") != std::string::npos || label.find("chste seite") != std::string::npos || label.find("weiter") != std::string::npos))
                            e.flags |= HM_ENTRY_NEXT;
                    }
                    if (folder.entries.empty() && plugin)
                        folder.message = "The add-on returned nothing here. Open Kodi to sign in to the service or to update the add-on.";
                }
                else
                {
                    folder.state = HM_LIST_FAILED;
                    folder.message = error == "Kodi did not answer in time."
                                         ? "The add-on did not answer within 90 seconds. Open Kodi to see whether it waits for a sign-in."
                                         : "Kodi could not open this folder: " + error;
                }
            }
        }
        fprintf(stderr, "NEO98 HOST media folder %s state=%u entries=%zu %s\n", path.c_str(), folder.state, folder.entries.size(), folder.message.c_str());
        std::lock_guard<std::mutex> guard(lock);
        folders[path] = std::move(folder);
        Changed();
    }

    void Play(const HM_Request &request)
    {
        Value params = Value::MakeObject(), item = Value::MakeObject(), result;
        std::string error;
        item.Set("file", request.path);
        params.Set("item", std::move(item));
        {
            std::lock_guard<std::mutex> guard(lock);
            status.player = HM_OPENING; status.message[0] = 0; status.title[0] = 0;
            // The name the catalogue gave the video.
            for (const auto &folder : folders)
                for (const auto &entry : folder.second.entries)
                    if (!status.title[0] && !strcmp(entry.path, request.path)) Text(status.title, entry.label);
            seen_fullscreen = false; away = 0; opened = std::chrono::steady_clock::now();
            // The screen goes to Kodi as soon as it opens the video, or the
            // video into the program's window where the host can show it.
            screen_wanted = Embeds() ? HM_SCREEN_WINDOW : HM_SCREEN_KODI; Changed();
        }
        fprintf(stderr, "NEO98 HOST media play %s\n", request.path);
        if (const std::string stream = Stream(request.path); !stream.empty())
        {
            unsigned features;
            { std::lock_guard<std::mutex> guard(lock); features = status.features; }
            const std::string adaptive = features & HM_HAS_INPUTSTREAM
                                             ? HostMediaKodi::AdaptiveFile("youtube-" + HostMediaKodi::YouTubeId(request.path), stream) : "";
            fprintf(stderr, "NEO98 HOST media play through yt-dlp%s\n", adaptive.empty() ? "" : " and inputstream.adaptive");
            Value direct = Value::MakeObject(); direct.Set("file", adaptive.empty() ? stream : adaptive);
            params.Set("item", std::move(direct));
        }
        if (!Call("Player.Open", std::move(params), result, error, 30000))
        {
            std::lock_guard<std::mutex> guard(lock);
            Stopped(); Text(status.message, "Kodi could not play this video: " + error);
        }
    }
    // The stream of a YouTube video: looked up ahead, being looked up (then
    // waited for), or looked up now. yt-dlp's addresses hold for hours.
    std::string Stream(const std::string &path)
    {
        if (HostMediaKodi::YouTubeId(path).empty()) return "";
        const auto start = std::chrono::steady_clock::now();
        {
            std::unique_lock<std::mutex> guard(lock);
            prepare_wake.wait_for(guard, std::chrono::seconds(45), [&] { return stopping || preparing != path; });
            auto found = streams.find(path);
            if (found != streams.end() && !found->second.stream.empty() && start - found->second.when < std::chrono::hours(1))
            {
                fprintf(stderr, "NEO98 HOST media stream ready ahead\n");
                return found->second.stream;
            }
        }
        std::string stream = HostMediaKodi::YouTubeStream(path);
        fprintf(stderr, "NEO98 HOST media yt-dlp took %.1f s\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
        std::lock_guard<std::mutex> guard(lock);
        if (!stream.empty()) streams[path] = {stream, std::chrono::steady_clock::now()};
        return stream;
    }
    // The video the program selected, looked up in the background; only the
    // latest selection counts.
    void Prepare()
    {
        for (;;)
        {
            std::string path;
            {
                std::unique_lock<std::mutex> guard(lock);
                prepare_wake.wait(guard, [&] { return stopping || !prepare_next.empty(); });
                if (stopping) return;
                path.swap(prepare_next);
                auto found = streams.find(path);
                if (found != streams.end() && std::chrono::steady_clock::now() - found->second.when < std::chrono::hours(1)) continue;
                preparing = path;
            }
            const auto start = std::chrono::steady_clock::now();
            std::string stream = HostMediaKodi::YouTubeStream(path);
            fprintf(stderr, "NEO98 HOST media looked ahead in %.1f s: %s\n",
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(), stream.empty() ? "nothing" : "a stream");
            std::lock_guard<std::mutex> guard(lock);
            if (streams.size() > 200) streams.clear();
            streams[path] = {stream, std::chrono::steady_clock::now()};
            preparing.clear();
            prepare_wake.notify_all();
        }
    }
    void Player(const char *method, Value params)
    {
        unsigned id;
        { std::lock_guard<std::mutex> guard(lock); id = player_id; }
        params.Set("playerid", id);
        Value result; std::string error;
        if (!Call(method, std::move(params), result, error))
            fprintf(stderr, "NEO98 HOST media %s failed: %s\n", method, error.c_str());
    }
    void Control(const HM_Request &request, bool)
    {
        switch (request.action)
        {
        case HM_PLAY: Play(request); break;
        case HM_PAUSE: Player("Player.PlayPause", Value::MakeObject()); break;
        case HM_SEEK:
        {
            Value params = Value::MakeObject(), value = Value::MakeObject();
            value.Set("seconds", request.value); params.Set("value", std::move(value));
            Player("Player.Seek", std::move(params));
            break;
        }
        case HM_SEEK_TO:
        {
            const unsigned s = unsigned(std::max(request.value, 0));
            Value params = Value::MakeObject(), value = Value::MakeObject(), time = Value::MakeObject();
            time.Set("hours", s / 3600); time.Set("minutes", s / 60 % 60); time.Set("seconds", s % 60); time.Set("milliseconds", 0u);
            value.Set("time", std::move(time)); params.Set("value", std::move(value));
            fprintf(stderr, "NEO98 HOST media seek to %u s\n", s);
            Player("Player.Seek", std::move(params));
            break;
        }
        case HM_STOP: Player("Player.Stop", Value::MakeObject()); break;
        case HM_VOLUME: case HM_MUTE:
        {
            Value params = Value::MakeObject(), result; std::string error;
            if (request.action == HM_VOLUME) params.Set("volume", std::min(std::max(request.value, 0), 100));
            else params.Set("mute", request.value != 0);
            if (!Call(request.action == HM_VOLUME ? "Application.SetVolume" : "Application.SetMute", std::move(params), result, error))
                fprintf(stderr, "NEO98 HOST media volume failed: %s\n", error.c_str());
            break;
        }
        case HM_SHOW_VIDEO:
        {
            Value params = Value::MakeObject(), result; std::string error;
            params.Set("fullscreen", true);
            Call("GUI.SetFullscreen", std::move(params), result, error);
            std::lock_guard<std::mutex> guard(lock);
            if (status.player == HM_PLAYING || status.player == HM_PAUSED) { screen_wanted = HM_SCREEN_KODI; seen_fullscreen = false; away = 0; Changed(); }
            break;
        }
        }
    }

    // While Kodi has the screen: the position, and whether the user left the
    // video in Kodi (Back): then the video pauses and NEO98 takes the screen.
    void Poll()
    {
        Value params = Value::MakeObject(), properties = Value::MakeArray(), result;
        std::string error;
        unsigned id, player;
        { std::lock_guard<std::mutex> guard(lock); id = player_id; player = status.player; }
        properties.Add("time"); properties.Add("totaltime"); properties.Add("speed"); properties.Add("currentvideostream");
        params.Set("playerid", id); params.Set("properties", std::move(properties));
        unsigned time = 0, total = 0; double speed = -1, shape = 0;
        bool started;
        { std::lock_guard<std::mutex> guard(lock); started = av_started; }
        double ran = 0;
        if ((player != HM_OPENING || started) && Call("Player.GetProperties", std::move(params), result, error, 3000))
        {
            auto seconds = [](const Value &t) { return unsigned(t["hours"].Num() * 3600 + t["minutes"].Num() * 60 + t["seconds"].Num()); };
            time = seconds(result["time"]); total = seconds(result["totaltime"]); speed = result["speed"].Num(-1);
            ran = time + result["time"]["milliseconds"].Num() / 1000;
            const double vw = result["currentvideostream"]["width"].Num(), vh = result["currentvideostream"]["height"].Num();
            if (vw > 0 && vh > 0) shape = vw / vh;
        }
        Value gui = Value::MakeObject(), wanted_gui = Value::MakeArray();
        wanted_gui.Add("fullscreen"); gui.Set("properties", std::move(wanted_gui));
        int fullscreen = -1;
        if (Call("GUI.GetProperties", std::move(gui), result, error, 3000)) fullscreen = result["fullscreen"].Truth() ? 1 : 0;
        bool pause = false, refullscreen = false;
        {
            std::lock_guard<std::mutex> guard(lock);
            if (status.player == HM_STOPPED) return;
            // The first picture is up and the clock runs (or Kodi was slow
            // to say so): from now on Kodi's picture shows.
            if (status.player == HM_OPENING && av_started && (ran >= 0.4 || std::chrono::steady_clock::now() - av_at > std::chrono::seconds(4)))
            {
                status.player = HM_PLAYING; Changed();
                fprintf(stderr, "NEO98 HOST media picture after %.1f s\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - opened).count());
            }
            if (status.time != time || status.total != total) { status.time = time; status.total = total; Changed(); }
            if (shape > 0 && std::abs(shape - aspect) > 0.005) { aspect = shape; fprintf(stderr, "NEO98 HOST media video %.3f\n", aspect); }
            if (fullscreen == 1) { seen_fullscreen = true; away = 0; }
            else if (fullscreen == 0 && seen_fullscreen && screen_wanted == HM_SCREEN_KODI && ++away >= 2)
            {
                fprintf(stderr, "NEO98 HOST media back from Kodi\n");
                // Where the window can show it, the video goes on there.
                const bool embeds = Embeds();
                screen_wanted = embeds ? HM_SCREEN_WINDOW : HM_SCREEN_NEO98; seen_fullscreen = false; away = 0;
                pause = !embeds && status.player == HM_PLAYING && speed != 0;
                // In the window Kodi's picture is the video, not its menu.
                refullscreen = embeds;
                Changed();
            }
            if (status.player == HM_OPENING && std::chrono::steady_clock::now() - opened > std::chrono::seconds(60))
            { Stopped(); Text(status.message, "The video did not start within a minute."); }
        }
        if (pause) Player("Player.PlayPause", Value::MakeObject());
        if (refullscreen)
        {
            Value full = Value::MakeObject(), done; std::string failed;
            full.Set("fullscreen", true);
            Call("GUI.SetFullscreen", std::move(full), done, failed);
        }
    }

    void Work()
    {
        auto last_poll = std::chrono::steady_clock::now();
        for (;;)
        {
            std::string path; HM_Request request{}; bool control = false, connect = false, poll = false, load = false;
            {
                std::unique_lock<std::mutex> guard(lock);
                wake.wait_for(guard, std::chrono::milliseconds(500), [&] {
                    return stopping || (wanted && (reconnect || status.state == HM_IDLE || !controls.empty() || !folder_jobs.empty() || poll_soon));
                });
                if (stopping) return;
                if (reconnect)
                {
                    // Kodi quit: the program says so and offers Retry
                    // instead of starting Kodi again behind the user's back.
                    reconnect = false;
                    if (status.state == HM_READY || status.state == HM_STARTING)
                    { Say(HM_FAILED, "Kodi was closed. Click Retry to start it again."); wanted = false; }
                    Stopped(); folders.clear(); folder_jobs.clear(); controls.clear();
                    guard.unlock(); kodi.Close(); guard.lock();
                    continue;
                }
                if (!wanted) continue;
                if (status.state == HM_IDLE) connect = true;
                else if (status.state != HM_READY) { controls.clear(); continue; }
                else if (!controls.empty()) { request = controls.front(); controls.pop_front(); control = true; }
                else if (!folder_jobs.empty()) { path = folder_jobs.front(); folder_jobs.pop_front(); load = true; }
                const auto now = std::chrono::steady_clock::now();
                // Quicker while the first picture waits for Kodi's clock.
                const auto every = std::chrono::milliseconds(status.player == HM_OPENING && av_started ? 200 : 900);
                if (!connect && !control && !load && status.player != HM_STOPPED && (poll_soon || now - last_poll >= every))
                { poll = true; last_poll = now; poll_soon = false; }
                if (!connect && !control && !load && !poll) continue;
            }
            if (connect) Connect();
            else if (control) Control(request, true);
            else if (poll) Poll();
            else Load(path);
        }
    }

    // The dialog Kodi shows, four times a second while Kodi is connected,
    // and the program's answers to it.
    void Watch()
    {
        for (;;)
        {
            std::deque<Answer> pending;
            bool ready;
            {
                std::unique_lock<std::mutex> guard(lock);
                answer_wake.wait_for(guard, std::chrono::milliseconds(250), [&] { return stopping || !answers.empty(); });
                if (stopping) return;
                ready = status.state == HM_READY;
                pending.swap(answers);
            }
            HM_Dialog now{};
            bool reached = false;
            if (ready)
            {
                for (auto &answer : pending)
                {
                    Value body = Value::MakeObject(), request = Value::MakeObject(), reply;
                    body.Set("serial", answer.serial);
                    if (answer.value == HM_ANSWER_CANCEL) body.Set("cancel", true);
                    else if (answer.kind == HM_DIALOG_INPUT) body.Set("text", answer.text);
                    else if (answer.kind == HM_DIALOG_SELECT) body.Set("item", answer.value);
                    else body.Set("button", answer.value);
                    request.Set("answer", std::move(body));
                    const bool taken = helper.Ask(request, reply) && reply["ok"].Truth();
                    fprintf(stderr, "NEO98 HOST media answer %u %s\n", answer.serial, taken ? "taken" : "refused");
                    std::fill(answer.text.begin(), answer.text.end(), '\0');
                }
                Value get = Value::MakeObject(), reply;
                get.Set("get", 1);
                if ((reached = helper.Ask(get, reply)) && reply["serial"].Num() > 0) Read(reply, now);
            }
            else helper.Close();
            std::lock_guard<std::mutex> guard(lock);
            const unsigned features = reached ? status.features | HM_HAS_DIALOGS : status.features & ~HM_HAS_DIALOGS;
            if (features != status.features) { status.features = features; Changed(); }
            if (memcmp(&now, &dialog, sizeof now))
            {
                if (now.serial != dialog.serial && now.serial)
                    fprintf(stderr, "NEO98 HOST media dialog %u kind=%u %s\n", now.serial, now.kind, now.heading);
                dialog = now; status.dialog = now.serial; Changed();
            }
        }
    }
    static void Read(const Value &from, HM_Dialog &out)
    {
        const std::string kind = from["kind"].Text();
        out.serial = unsigned(from["serial"].Num());
        out.kind = kind == "ok" ? HM_DIALOG_OK : kind == "yesno" ? HM_DIALOG_YESNO : kind == "select" ? HM_DIALOG_SELECT
                 : kind == "input" ? HM_DIALOG_INPUT : kind == "progress" ? HM_DIALOG_PROGRESS : HM_DIALOG_NONE;
        if (!out.kind) { out.serial = 0; return; }
        for (const auto &flag : from["flags"].items)
            out.flags |= flag.Text() == "hidden" ? HM_DIALOG_HIDDEN : flag.Text() == "number" ? HM_DIALOG_NUMBER : flag.Text() == "cancel" ? HM_DIALOG_CANCEL : 0;
        out.percent = unsigned(std::min(std::max(from["percent"].Num(), 0.0), 100.0));
        Text(out.heading, HostMediaKodi::Plain(from["heading"].Text()));
        Text(out.text, HostMediaKodi::Plain(from["text"].Text()));
        Text(out.input, from["input"].Text());
        for (size_t i = 0; i < from["buttons"].Size() && i < 3; ++i) Text(out.button[i], HostMediaKodi::Plain(from["buttons"].At(i).Text()));
        for (size_t i = 0; i < from["choices"].Size() && out.count < HM_CHOICES; ++i) Text(out.choice[out.count++], HostMediaKodi::Plain(from["choices"].At(i).Text()));
    }

    void Paint()
    {
        for (;;)
        {
            ArtJob job;
            {
                std::unique_lock<std::mutex> guard(lock);
                art_wake.wait(guard, [&] { return stopping || !art_jobs.empty(); });
                if (stopping) return;
                job = art_jobs.front(); art_jobs.pop_front();
            }
            std::vector<unsigned char> bytes;
            std::vector<unsigned> decoded, fitted;
            unsigned w = 0, h = 0;
            const bool okay = HostMediaKodi::Fetch(job.art, bytes) && HostMediaPicture::Decode(bytes.data(), bytes.size(), decoded, w, h);
            if (okay) Fit(decoded, w, h, job.width, job.height, job.background, fitted);
            fprintf(stderr, "NEO98 HOST media picture %s %s %ux%u\n", okay ? "ready" : "none", HostMediaKodi::Unwrap(job.art).c_str(), w, h);
            std::lock_guard<std::mutex> guard(lock);
            auto &art = arts[job.key];
            art.state = okay ? HM_PICTURE_READY : HM_PICTURE_NONE;
            art.width = job.width; art.height = job.height; art.pixels = std::move(fitted);
        }
    }

    bool Embeds() { return embeds && embeds(); }

public:
    // The host shows the screen of `to` (HM_SCREEN_KODI: hide NEO98's
    // window; HM_SCREEN_NEO98: show and raise it; HM_SCREEN_WINDOW: NEO98
    // stays and shows Kodi's picture in the video area), on its main thread.
    std::function<void(unsigned to)> show_screen;
    // The host fills its screen with the video (true) or goes back, on its
    // main thread.
    std::function<void(bool on)> video_fullscreen;
    // Whether the host can show the video inside the window (the Mac host
    // with ScreenCaptureKit allowed). Asked from the provider's thread.
    std::function<bool()> embeds;
    // The areas that show Kodi's picture now: the video in Media Center
    // 98's window while it plays there.
    std::vector<Area> VideoAreas()
    {
        std::lock_guard<std::mutex> guard(lock);
        std::vector<Area> out;
        if (screen_shown == HM_SCREEN_WINDOW && area.shown && area.width > 0 && area.height > 0) { out.push_back(area); out.back().fill = full_shown; }
        return out;
    }
    // The width / height of the video Kodi plays, 0 when unknown.
    double VideoAspect() { std::lock_guard<std::mutex> guard(lock); return screen_shown == HM_SCREEN_WINDOW ? aspect : 0; }
    // Whether the host should record Kodi's window.
    bool WantsCapture()
    {
        std::lock_guard<std::mutex> guard(lock);
        return screen_shown == HM_SCREEN_WINDOW;
    }

    Service() { status.version = HM_VERSION; }
    ~Service() override
    {
        if (HostMedia::current == this) HostMedia::current = nullptr;
        { std::lock_guard<std::mutex> guard(lock); stopping = true; }
        wake.notify_all(); art_wake.notify_all(); answer_wake.notify_all(); prepare_wake.notify_all();
        if (worker.joinable()) worker.join();
        if (painter.joinable()) painter.join();
        if (watcher.joinable()) watcher.join();
        if (preparer.joinable()) preparer.join();
        // A Kodi this session started ends with it.
        if (launched && kodi.Connected())
        {
            Value result; std::string error;
            kodi.Call("Application.Quit", Value(), result, error, 2000);
        }
        kodi.Close();
    }
    void Install()
    {
        kodi.notify = [this](const std::string &method, const Value &params) { Notify(method, params); };
        kodi.lost = [this] { Lost(); };
        HostMedia::current = this;
        worker = std::thread([this] { Work(); });
        painter = std::thread([this] { Paint(); });
        watcher = std::thread([this] { Watch(); });
        preparer = std::thread([this] { Prepare(); });
    }
    // On the host's main thread: hands the screen over.
    void Pump()
    {
        const unsigned want = screen_wanted;
        if (want != screen_shown)
        {
            { std::lock_guard<std::mutex> guard(lock); screen_shown = want; }
            fprintf(stderr, "NEO98 HOST media screen %s\n", want == HM_SCREEN_KODI ? "kodi" : want == HM_SCREEN_WINDOW ? "window" : "neo98");
            if (show_screen) show_screen(want);
            std::lock_guard<std::mutex> guard(lock);
            status.screen = want; Changed();
        }
        else if (raise_neo98.exchange(false) && want == HM_SCREEN_NEO98 && show_screen)
            show_screen(HM_SCREEN_NEO98);
        // Full screen only while the video plays in the window.
        const bool full = full_wanted && screen_shown == HM_SCREEN_WINDOW;
        if (full != full_shown)
        {
            { std::lock_guard<std::mutex> guard(lock); full_shown = full; }
            fprintf(stderr, "NEO98 HOST media full screen %s\n", full ? "on" : "off");
            if (video_fullscreen) video_fullscreen(full);
        }
    }
    unsigned Screen() const { return screen_shown; }
    // The Linux hosts' compositor: a Kodi this host starts draws into it,
    // and the host shows its picture in the video area (embeds).
    void UseCompositor(const std::string &socket, const std::string &runtime)
    {
        std::lock_guard<std::mutex> guard(lock);
        wayland_socket = socket; wayland_runtime = runtime;
    }

    unsigned Status(HM_Status &out) override
    {
        std::lock_guard<std::mutex> guard(lock);
        // The first question of the program starts the connection.
        if (!asked) { asked = wanted = true; wake.notify_all(); }
        out = status;
        if (Embeds()) out.features |= HM_CAN_SHOW;
        return 0;
    }
    unsigned Browse(const HM_Request &request, HM_Listing &out) override
    {
        std::lock_guard<std::mutex> guard(lock);
        if (!asked) { asked = wanted = true; wake.notify_all(); }
        out = {};
        if (status.state == HM_NO_KODI || status.state == HM_FAILED)
        { out.state = HM_LIST_FAILED; memcpy(out.message, status.message, sizeof out.message); return 0; }
        const std::string path = request.path;
        auto found = folders.find(path);
        if (found == folders.end())
        {
            if (std::find(folder_jobs.begin(), folder_jobs.end(), path) == folder_jobs.end()) { folder_jobs.push_back(path); wake.notify_all(); }
            out.state = HM_LIST_LOADING;
            return 0;
        }
        const auto &folder = found->second;
        out.state = folder.state;
        out.total = unsigned(folder.entries.size());
        out.first = std::min(request.first, out.total);
        out.count = std::min(unsigned(HM_PAGE), out.total - out.first);
        Text(out.title, folder.title);
        Text(out.message, folder.message);
        for (unsigned i = 0; i < out.count; ++i) out.entry[i] = folder.entries[out.first + i];
        return 0;
    }
    unsigned Picture(const HM_Request &request, HM_Picture &out) override
    {
        if (!request.width || !request.height || request.width > HM_PICTURE_WIDTH || request.height > HM_PICTURE_HEIGHT) return 87;
        out = {};
        out.width = request.width; out.height = request.height;
        if (!request.path[0]) { out.state = HM_PICTURE_NONE; return 0; }
        const std::string key = std::string(request.path) + "|" + std::to_string(request.width) + "x" + std::to_string(request.height) + "|" +
                                std::to_string(unsigned(request.value) & 0xffffff);
        std::lock_guard<std::mutex> guard(lock);
        auto found = arts.find(key);
        if (found == arts.end())
        {
            // A long catalogue: keep the cache to the latest pictures.
            if (arts.size() > 600) for (auto i = arts.begin(); i != arts.end();) i = i->second.state == HM_PICTURE_LOADING ? std::next(i) : arts.erase(i);
            arts[key] = {};
            art_jobs.push_back({key, request.path, request.width, request.height, unsigned(request.value)});
            art_wake.notify_one();
            out.state = HM_PICTURE_LOADING;
            return 0;
        }
        out.state = found->second.state;
        if (out.state == HM_PICTURE_READY) std::copy(found->second.pixels.begin(), found->second.pixels.end(), out.pixels);
        return 0;
    }
    unsigned Dialog(HM_Dialog &out) override
    {
        std::lock_guard<std::mutex> guard(lock);
        out = dialog;
        return 0;
    }
    unsigned Control(const HM_Request &request) override
    {
        if (request.action < HM_START || request.action > HM_MUTE) return 87;
        std::unique_lock<std::mutex> guard(lock);
        if (request.action == HM_VIDEO_AREA)
        {
            area.x = short(request.first & 0xffff); area.y = short(request.first >> 16);
            area.width = int(std::min(request.width, 8192u)); area.height = int(std::min(request.height, 8192u));
            area.shown = request.value != 0;
            return 0;
        }
        if (request.action == HM_FULLSCREEN) { full_wanted = request.value != 0; return 0; }
        if (request.action == HM_PREPARE)
        {
            if (!HostMediaKodi::YouTubeId(request.path).empty()) { prepare_next = request.path; prepare_wake.notify_all(); }
            return 0;
        }
        if (request.action == HM_ANSWER)
        {
            // Only the dialog that shows; an answer to one Kodi has closed
            // meanwhile is dropped.
            if (!dialog.serial || request.first != dialog.serial) return 0;
            if (request.value != HM_ANSWER_CANCEL &&
                (request.value < 0 || (dialog.kind == HM_DIALOG_SELECT ? unsigned(request.value) >= dialog.count : request.value > 2)))
                return 87;
            answers.push_back({dialog.serial, dialog.kind, request.value, request.path});
            answer_wake.notify_all();
            return 0;
        }
        if (request.action == HM_START)
        {
            asked = wanted = true;
            if (status.state == HM_NO_KODI || status.state == HM_FAILED) { status.state = HM_IDLE; status.message[0] = 0; Changed(); }
            wake.notify_all();
            return 0;
        }
        if (request.action == HM_REFRESH) { folders.clear(); folder_jobs.clear(); Changed(); return 0; }
        if (status.state != HM_READY) return 21;  // ERROR_NOT_READY
        if (request.action == HM_PLAY && !request.path[0]) return 87;
        if (request.action == HM_VOLUME || request.action == HM_MUTE)
        {
            // At once in the status; Kodi confirms with OnVolumeChanged.
            if (request.action == HM_VOLUME) status.volume = unsigned(std::min(std::max(request.value, 0), 100)); else status.muted = request.value != 0;
            Changed(); controls.push_back(request); wake.notify_all();
            return 0;
        }
        if (request.action != HM_PLAY && request.action != HM_SHOW_VIDEO && status.player == HM_STOPPED) return 21;
        controls.push_back(request);
        wake.notify_all();
        return 0;
    }
};
}
