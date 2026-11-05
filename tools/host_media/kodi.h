// SPDX-License-Identifier: GPL-2.0-or-later
// Kodi on the host: find it, start it, and talk JSON-RPC with it over TCP on
// localhost (port 9090, "Allow remote control from applications on this
// system").
#pragma once
#include "json.h"
#include <arpa/inet.h>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <map>
#include <mutex>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <spawn.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>
extern char **environ;
namespace HostMediaKodi
{
using HostMediaJson::Value;

// A TCP connection to a port of this computer, or -1.
inline int ConnectLocal(unsigned port, unsigned timeout_ms)
{
    const int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;
#ifdef SO_NOSIGPIPE
    int nosigpipe = 1; setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &nosigpipe, sizeof nosigpipe);
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    fcntl(s, F_SETFL, fcntl(s, F_GETFL) | O_NONBLOCK);
    int result = connect(s, (sockaddr *)&address, sizeof address);
    if (result && errno == EINPROGRESS)
    {
        pollfd p{s, POLLOUT, 0};
        int error = 0; socklen_t size = sizeof error;
        result = poll(&p, 1, int(timeout_ms)) == 1 && !getsockopt(s, SOL_SOCKET, SO_ERROR, &error, &size) && !error ? 0 : -1;
    }
    if (result) { close(s); return -1; }
    fcntl(s, F_SETFL, fcntl(s, F_GETFL) & ~O_NONBLOCK);
    int one = 1; setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    return s;
}
inline bool SendAll(int fd, const std::string &text)
{
    for (size_t sent = 0; sent < text.size();)
    {
        const ssize_t n = send(fd, text.data() + sent, text.size() - sent,
#ifdef MSG_NOSIGNAL
                               MSG_NOSIGNAL
#else
                               0
#endif
        );
        if (n <= 0) return false;
        sent += size_t(n);
    }
    return true;
}

// One JSON-RPC connection. Calls block their caller until the answer with
// the same id arrives; notifications go to `notify` on the reader thread.
class Connection
{
    int fd = -1;
    std::thread reader;
    std::mutex lock;
    std::condition_variable answered;
    std::map<unsigned, Value> replies;
    unsigned next_id = 1;
    bool closed = true;

    void Read()
    {
        std::string buffer;
        char chunk[16384];
        for (;;)
        {
            const ssize_t n = recv(fd, chunk, sizeof chunk, 0);
            if (n <= 0) break;
            buffer.append(chunk, size_t(n));
            while (size_t length = HostMediaJson::MessageLength(buffer))
            {
                Value message;
                const bool okay = HostMediaJson::Parse(buffer.substr(0, length), message);
                buffer.erase(0, length);
                if (!okay) continue;
                if (!message["id"].IsNull())
                {
                    std::lock_guard<std::mutex> guard(lock);
                    replies[unsigned(message["id"].Num())] = std::move(message);
                    answered.notify_all();
                }
                else if (message["method"].type == Value::String && notify)
                    notify(message["method"].text, message["params"]);
            }
            // A message larger than any Kodi sends: the stream is broken.
            if (buffer.size() > (64u << 20)) break;
        }
        {
            std::lock_guard<std::mutex> guard(lock);
            closed = true;
            answered.notify_all();
        }
        if (lost) lost();
    }

public:
    std::function<void(const std::string &, const Value &)> notify;
    // The connection ended (Kodi quit or crashed), on the reader thread.
    std::function<void()> lost;

    ~Connection() { Close(); }
    bool Connected() { std::lock_guard<std::mutex> guard(lock); return !closed; }
    bool Open(unsigned port, unsigned timeout_ms)
    {
        Close();
        const int s = ConnectLocal(port, timeout_ms);
        if (s < 0) return false;
        {
            std::lock_guard<std::mutex> guard(lock);
            fd = s; closed = false; replies.clear();
        }
        reader = std::thread([this] { Read(); });
        return true;
    }
    void Close()
    {
        if (fd >= 0) shutdown(fd, SHUT_RDWR);
        if (reader.joinable()) reader.join();
        if (fd >= 0) close(fd);
        fd = -1;
        std::lock_guard<std::mutex> guard(lock);
        closed = true;
    }
    // A call: true and its result, or false and Kodi's error (or why there
    // was no answer).
    bool Call(const char *method, Value params, Value &result, std::string &error, unsigned timeout_ms = 15000)
    {
        unsigned id;
        std::string text;
        {
            std::lock_guard<std::mutex> guard(lock);
            if (closed) { error = "Kodi is not connected."; return false; }
            id = next_id++;
            Value request = Value::MakeObject();
            request.Set("jsonrpc", "2.0");
            request.Set("method", method);
            if (!params.IsNull()) request.Set("params", std::move(params));
            request.Set("id", id);
            text = HostMediaJson::Dump(request);
        }
        if (!SendAll(fd, text)) { error = "The connection to Kodi broke."; return false; }
        std::unique_lock<std::mutex> guard(lock);
        const bool arrived = answered.wait_for(guard, std::chrono::milliseconds(timeout_ms), [&] { return closed || replies.count(id); });
        auto found = replies.find(id);
        if (!arrived || found == replies.end())
        {
            error = closed ? "Kodi closed the connection." : "Kodi did not answer in time.";
            return false;
        }
        Value reply = std::move(found->second);
        replies.erase(found);
        if (!reply["error"].IsNull())
        {
            error = reply["error"]["message"].Text("Kodi refused the request.");
            const auto &data = reply["error"]["data"];
            if (data["message"].type == Value::String) error += " (" + data["message"].text + ")";
            return false;
        }
        result = Value();
        for (auto &member : reply.members) if (member.first == "result") result = std::move(member.second);
        return true;
    }
};

// How Kodi is started on this computer.
struct Install
{
    std::vector<std::string> command;
    std::string where;
};

inline bool Executable(const std::string &path)
{
    struct stat info;
    return !stat(path.c_str(), &info) && S_ISREG(info.st_mode) && !access(path.c_str(), X_OK);
}
inline bool Folder(const std::string &path) { struct stat info; return !stat(path.c_str(), &info) && S_ISDIR(info.st_mode); }

inline std::string InPath(const char *name)
{
    const char *path = getenv("PATH");
    std::string list = path ? path : "/usr/local/bin:/usr/bin:/bin";
    for (size_t from = 0; from <= list.size();)
    {
        size_t end = list.find(':', from);
        if (end == std::string::npos) end = list.size();
        std::string candidate = list.substr(from, end - from) + "/" + name;
        if (end > from && Executable(candidate)) return candidate;
        from = end + 1;
    }
    return {};
}

// NEO98_KODI names Kodi (a program, or Kodi.app on the Mac). Otherwise the
// usual places: Kodi.app in /Applications or ~/Applications on the Mac;
// kodi in PATH, then the Flatpak tv.kodi.Kodi on Linux.
inline bool Find(Install &out)
{
    const char *home = getenv("HOME");
    if (const char *chosen = getenv("NEO98_KODI"); chosen && *chosen)
    {
        std::string path = chosen;
        if (Folder(path) && path.size() > 4 && path.compare(path.size() - 4, 4, ".app") == 0)
        { out = {{"/usr/bin/open", "-a", path}, path}; return true; }
        if (Executable(path)) { out = {{path}, path}; return true; }
        return false;
    }
#ifdef __APPLE__
    for (std::string app : {std::string("/Applications/Kodi.app"), std::string(home ? home : "") + "/Applications/Kodi.app"})
        if (Folder(app)) { out = {{"/usr/bin/open", "-a", app}, app}; return true; }
#else
    if (auto path = InPath("kodi"); !path.empty()) { out = {{path}, path}; return true; }
    for (std::string folder : {std::string("/var/lib/flatpak/app/tv.kodi.Kodi"), std::string(home ? home : "") + "/.local/share/flatpak/app/tv.kodi.Kodi"})
        if (Folder(folder))
            if (auto flatpak = InPath("flatpak"); !flatpak.empty()) { out = {{flatpak, "run", "tv.kodi.Kodi"}, "Flatpak tv.kodi.Kodi"}; return true; }
#endif
    return false;
}

// Start a program beside NEO98, in a process group of its own and without
// NEO98's terminal. Returns its process id, or 0.
inline pid_t Spawn(const std::vector<std::string> &command, int output = -1, const std::vector<std::string> &variables = {})
{
    if (command.empty()) return 0;
    // NEO98's environment with the given NAME=VALUE in place of its own.
    std::vector<std::string> keep;
    for (char **e = environ; *e; ++e)
    {
        const std::string entry = *e;
        bool replaced = false;
        for (const auto &v : variables) replaced |= entry.compare(0, v.find('=') + 1, v, 0, v.find('=') + 1) == 0;
        if (!replaced) keep.push_back(entry);
    }
    keep.insert(keep.end(), variables.begin(), variables.end());
    std::vector<char *> envp;
    for (auto &entry : keep) envp.push_back(const_cast<char *>(entry.c_str()));
    envp.push_back(nullptr);
    std::vector<char *> argv;
    for (auto &part : command) argv.push_back(const_cast<char *>(part.c_str()));
    argv.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    if (output >= 0) posix_spawn_file_actions_adddup2(&actions, output, 1);
    else posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
    posix_spawnattr_t attributes;
    posix_spawnattr_init(&attributes);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attributes, 0);
    pid_t pid = 0;
    if (posix_spawnp(&pid, argv[0], &actions, &attributes, argv.data(), envp.data())) pid = 0;
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attributes);
    return pid;
}

// Brings Kodi's window to the front. On the Mac `open -a` activates the
// application; on Linux Kodi's window is the one below NEO98's.
inline void Activate()
{
#ifdef __APPLE__
    Install install;
    if (Find(install) && install.command.size() == 3 && install.command[0] == "/usr/bin/open") Spawn(install.command);
#endif
}

// The bytes a program writes, with a deadline (curl for the pictures).
inline bool Output(const std::vector<std::string> &command, std::vector<unsigned char> &out, unsigned limit, unsigned timeout_ms)
{
    int pipe_fds[2];
    if (pipe(pipe_fds)) return false;
    fcntl(pipe_fds[0], F_SETFD, FD_CLOEXEC);
    const pid_t pid = Spawn(command, pipe_fds[1]);
    close(pipe_fds[1]);
    if (!pid) { close(pipe_fds[0]); return false; }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    bool okay = true;
    unsigned char chunk[65536];
    for (;;)
    {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        pollfd p{pipe_fds[0], POLLIN, 0};
        if (left <= 0 || poll(&p, 1, int(left)) <= 0) { okay = false; break; }
        const ssize_t n = read(pipe_fds[0], chunk, sizeof chunk);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        out.insert(out.end(), chunk, chunk + n);
        if (out.size() > limit) { okay = false; break; }
    }
    close(pipe_fds[0]);
    if (!okay) kill(pid, SIGKILL);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    return okay && WIFEXITED(status) && !WEXITSTATUS(status);
}

inline std::string PercentDecode(const std::string &s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); ++i)
    {
        auto hex = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
        if (s[i] == '%' && i + 2 < s.size() && hex(s[i + 1]) >= 0 && hex(s[i + 2]) >= 0)
        { out += char(hex(s[i + 1]) * 16 + hex(s[i + 2])); i += 2; }
        else out += s[i];
    }
    return out;
}

// Kodi's EventServer (UDP, port 9777 or NEO98_KODI_EVENTS_PORT): a built-in
// function of Kodi (UpdateLocalAddons), which JSON-RPC cannot run.
class Events
{
    int fd = -1;
    sockaddr_in to{};
    unsigned sequence = 0;
    std::mutex lock;
    void Packet(unsigned short type, const std::string &payload)
    {
        std::string out = "XBMC";
        out += char(2); out += char(0);
        auto u16 = [&](unsigned v) { out += char(v >> 8); out += char(v & 255); };
        auto u32 = [&](unsigned v) { u16(v >> 16); u16(v & 0xffff); };
        u16(type); u32(++sequence); u32(1); u16(unsigned(payload.size())); u32(0x41524d39); out.append(10, '\0');
        out += payload;
        sendto(fd, out.data(), out.size(), 0, (const sockaddr *)&to, sizeof to);
    }

public:
    ~Events() { if (fd >= 0) close(fd); }
    void Builtin(const std::string &function)
    {
        std::lock_guard<std::mutex> guard(lock);
        if (fd < 0)
        {
            fd = socket(AF_INET, SOCK_DGRAM, 0);
            if (fd < 0) return;
            const char *port = getenv("NEO98_KODI_EVENTS_PORT");
            to.sin_family = AF_INET; to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            to.sin_port = htons(port && *port ? (unsigned short)atoi(port) : 9777);
        }
        // HELO (no icon, port 0, two reserved words), then ACTION with
        // ACTION_EXECBUILTIN.
        std::string helo("NEO98", 6);
        helo += char(0); helo.append(10, '\0');
        Packet(0x01, helo);
        std::string action(1, char(0x01));
        action += function; action += char(0);
        Packet(0x0a, action);
    }
};

// NEO98's add-on service.neo98 in Kodi (tools/host_media/kodi_addon): the
// dialog Kodi shows, and the answer to it, one JSON line each way over TCP
// on localhost (port 9779 or NEO98_KODI_HELPER_PORT). It answers while
// Kodi's JSON-RPC waits for an add-on's dialog.
class Helper
{
    int fd = -1;
    std::string buffer;

public:
    ~Helper() { Close(); }
    void Close() { if (fd >= 0) close(fd); fd = -1; buffer.clear(); }
    static unsigned Port()
    {
        const char *port = getenv("NEO98_KODI_HELPER_PORT");
        return port && *port ? unsigned(atoi(port)) : 9779u;
    }
    // One request and its answer; false when the add-on does not run.
    bool Ask(const Value &request, Value &reply, unsigned timeout_ms = 1500)
    {
        if (fd < 0 && (fd = ConnectLocal(Port(), 300)) < 0) return false;
        if (!SendAll(fd, HostMediaJson::Dump(request) + "\n")) { Close(); return false; }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        size_t end;
        while ((end = buffer.find('\n')) == std::string::npos)
        {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
            pollfd p{fd, POLLIN, 0};
            char chunk[16384];
            ssize_t n = 0;
            if (left <= 0 || poll(&p, 1, int(left)) <= 0 || (n = recv(fd, chunk, sizeof chunk, 0)) <= 0 || buffer.size() > (1u << 20))
            { Close(); return false; }
            buffer.append(chunk, size_t(n));
        }
        const std::string line = buffer.substr(0, end);
        buffer.erase(0, end + 1);
        return HostMediaJson::Parse(line, reply);
    }
};

// A YouTube video of Kodi's YouTube add-on (plugin.video.youtube/play/
// ?video_id=...) as the HLS address yt-dlp finds for it, or empty. YouTube
// refuses the add-on's own streams more and more often (HTTP 403, "One or
// more items failed to play"); yt-dlp keeps up with it, and Kodi plays its
// HLS address directly. Without yt-dlp the add-on plays as before.
// NEO98_YTDLP names yt-dlp; NEO98_YTDLP=none turns this off.
// The video id of a YouTube video of the add-on, or empty.
inline std::string YouTubeId(const std::string &path)
{
    if (path.compare(0, 36, "plugin://plugin.video.youtube/play/?") != 0) return "";
    const size_t at = path.find("video_id=");
    if (at == std::string::npos) return "";
    std::string id = path.substr(at + 9, 11);
    if (id.size() != 11 || id.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") != std::string::npos) return "";
    return id;
}

inline std::string YouTubeStream(const std::string &path)
{
    const std::string id = YouTubeId(path);
    if (id.empty()) return "";
    std::string program;
    if (const char *chosen = getenv("NEO98_YTDLP"); chosen && *chosen) program = strcmp(chosen, "none") ? chosen : "";
    else
    {
        // An app started from the Finder has no Homebrew in its PATH.
        program = InPath("yt-dlp");
        for (const char *place : {"/opt/homebrew/bin/yt-dlp", "/usr/local/bin/yt-dlp"})
            if (program.empty() && Executable(place)) program = place;
    }
    if (program.empty() || !Executable(program)) return "";
    std::vector<unsigned char> out;
    if (!Output({program, "--no-warnings", "--no-playlist", "-j", "--", id}, out, 16u << 20, 45000)) return "";
    Value info;
    if (!HostMediaJson::Parse(std::string(out.begin(), out.end()), info)) return "";
    const Value &formats = info["formats"];
    for (size_t i = 0; i < formats.Size(); i++)
    {
        const std::string protocol = formats.At(i)["protocol"].Text(), manifest = formats.At(i)["manifest_url"].Text();
        if (protocol.compare(0, 4, "m3u8") == 0 && manifest.compare(0, 8, "https://") == 0) return manifest;
    }
    return "";
}

// An HLS address for inputstream.adaptive: a.strm file in the host's
// temporary folder that names the add-on (#KODIPROP), one per video so
// that Kodi's resume points stay apart; empty if it cannot be written.
// YouTube's master playlists now list some 17 variants and several dubbed
// audio tracks; Kodi's own demuxer probes them all and gives up after 30
// seconds, inputstream.adaptive opens one variant and one track in a few.
inline std::string AdaptiveFile(const std::string &name, const std::string &url)
{
    const char *tmp = getenv("TMPDIR");
    std::string folder = std::string(tmp && *tmp ? tmp : "/tmp/");
    if (folder.back() != '/') folder += '/';
    folder += "neo98-host-media/";
    mkdir(folder.c_str(), 0700);
    const std::string file = folder + name + ".strm";
    FILE *f = fopen(file.c_str(), "w");
    if (!f) return "";
    const bool okay = fprintf(f, "#KODIPROP:inputstream=inputstream.adaptive\n%s\n", url.c_str()) > 0;
    return fclose(f) == 0 && okay ? file : "";
}

// Kodi wraps art as image://<url, percent-encoded>/; the address inside.
inline std::string Unwrap(const std::string &art)
{
    if (art.compare(0, 8, "image://") != 0) return art;
    std::string inner = art.substr(8);
    if (!inner.empty() && inner.back() == '/') inner.pop_back();
    return PercentDecode(inner);
}

// Kodi's folder of the user (special://home/), the first that exists of
// the usual places or NEO98_KODI_HOME; empty if there is none.
inline std::string Home()
{
    if (const char *chosen = getenv("NEO98_KODI_HOME"); chosen && *chosen)
        return Folder(chosen) ? std::string(chosen) + (chosen[strlen(chosen) - 1] == '/' ? "" : "/") : "";
    const char *home = getenv("HOME");
    const std::string user = home ? home : "";
#ifdef __APPLE__
    for (const std::string root : {user + "/Library/Application Support/Kodi/"})
#else
    for (const std::string root : {user + "/.kodi/", user + "/.var/app/tv.kodi.Kodi/data/"})
#endif
        if (Folder(root)) return root;
    return "";
}

// special://home/ and special://xbmc/ (add-on icons) as a file of the host:
// the first existing one of Kodi's usual places, empty if none fits.
inline std::string Special(const std::string &where)
{
    const char *home = getenv("HOME");
    const std::string user = home ? home : "";
    std::vector<std::string> roots;
    if (where.compare(0, 15, "special://home/") == 0)
    {
        if (const std::string root = Home(); !root.empty()) roots = {root};
    }
    else if (where.compare(0, 15, "special://xbmc/") == 0)
    {
#ifdef __APPLE__
        roots = {"/Applications/Kodi.app/Contents/Resources/Kodi/", user + "/Applications/Kodi.app/Contents/Resources/Kodi/"};
#else
        roots = {"/usr/share/kodi/", "/usr/local/share/kodi/"};
#endif
    }
    const std::string rest = where.substr(where.find('/', 10) + 1);
    if (rest.find("..") != std::string::npos) return "";
    for (const auto &root : roots)
        if (access((root + rest).c_str(), R_OK) == 0) return root + rest;
    return "";
}

// The bytes of a picture of the catalogue: over HTTP(S) with curl, or a
// file of the host, also behind special://home/ and special://xbmc/. Other
// places (pictures inside videos) give no picture.
inline bool Fetch(const std::string &art, std::vector<unsigned char> &out)
{
    const std::string where = Unwrap(art);
    constexpr unsigned limit = 12u << 20;
    if (where.compare(0, 7, "http://") == 0 || where.compare(0, 8, "https://") == 0)
        return Output({"curl", "-sfL", "--max-time", "20", "--max-filesize", std::to_string(limit), "-o", "-", "--", where}, out, limit, 25000);
    std::string file = where.compare(0, 7, "file://") == 0 ? PercentDecode(where.substr(7))
                     : where.compare(0, 10, "special://") == 0 ? Special(where) : where;
    if (file.empty() || file[0] != '/') return false;
    FILE *f = fopen(file.c_str(), "rb");
    if (!f) return false;
    unsigned char chunk[65536];
    size_t n;
    while ((n = fread(chunk, 1, sizeof chunk, f)) > 0 && out.size() <= limit) out.insert(out.end(), chunk, chunk + n);
    fclose(f);
    return !out.empty() && out.size() <= limit;
}

// Kodi's label markup ([B], [COLOR red], [CR]...) taken out.
inline std::string Plain(const std::string &label)
{
    static const char *const tags[] = {"B", "/B", "I", "/I", "COLOR", "/COLOR", "UPPERCASE", "/UPPERCASE", "LOWERCASE",
                                       "/LOWERCASE", "CAPITALIZE", "/CAPITALIZE", "LIGHT", "/LIGHT", "CR", "TABS", "/TABS"};
    std::string out;
    for (size_t i = 0; i < label.size(); ++i)
    {
        if (label[i] == '[')
        {
            const size_t end = label.find(']', i);
            if (end != std::string::npos)
            {
                std::string tag = label.substr(i + 1, end - i - 1);
                if (auto space = tag.find(' '); space != std::string::npos) tag.resize(space);
                bool known = false;
                for (auto *t : tags) known |= tag == t;
                if (known) { if (tag == "CR") out += ' '; i = end; continue; }
            }
        }
        out += label[i];
    }
    return out;
}
}
