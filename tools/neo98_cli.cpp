// SPDX-License-Identifier: GPL-2.0-or-later
// The host adapter reaches the runtime's host service methods (the guest
// directory of --directory), as tools/neo98_linux.cpp does.
#define private public
#include "../src/x86_app.h"
#undef private
#include "linux_session.h"
#include <iconv.h>
#include <poll.h>
#include <thread>
namespace
{
volatile sig_atomic_t interrupted = 0;
void Signal(int) { interrupted = 1; }
void Output(const unsigned char *bytes, unsigned count, bool error, unsigned page)
{
    // The native console uses the Win98 OEM page, independently of LC_CTYPE.
    char text[4096]; char *source = reinterpret_cast<char *>(const_cast<unsigned char *>(bytes));
    size_t input = count;
    FILE *stream = error ? stderr : stdout;
    const std::string encoding = "CP" + std::to_string(page);
    iconv_t converter = iconv_open("UTF-8", encoding.c_str());
    if (converter != reinterpret_cast<iconv_t>(-1))
    {
        while (input)
        {
            char *destination = text; size_t capacity = sizeof text;
            const auto result = iconv(converter, &source, &input, &destination, &capacity);
            fwrite(text, 1, destination - text, stream);
            if (result == static_cast<size_t>(-1) && errno != E2BIG) break;
        }
        iconv_close(converter);
    }
    else fwrite(bytes, 1, count, stream);
    fflush(stream);
}
class Input
{
    std::string utf8, ready;
    iconv_t converter;
    bool open = true, ended = false, previous_cr = false;
  public:
    explicit Input(unsigned page)
    { const auto encoding = "CP" + std::to_string(page); converter = iconv_open(encoding.c_str(), "UTF-8"); }
    ~Input() { if (converter != reinterpret_cast<iconv_t>(-1)) iconv_close(converter); }
    void Pump(X86::Application &app)
    {
        if (open && ready.empty() && app.ConsoleInputSpace() >= 2)
        {
            pollfd input{STDIN_FILENO, POLLIN, 0};
            if (poll(&input, 1, 0) > 0 && (input.revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL)))
            {
                char data[256]; const auto count = read(STDIN_FILENO, data, sizeof data);
                if (count == 0 || (count < 0 && errno != EINTR && errno != EAGAIN)) open = false;
                else if (count > 0) utf8.append(data, count);
            }
        }
        if (converter == reinterpret_cast<iconv_t>(-1)) ready += utf8, utf8.clear();
        else while (!utf8.empty())
        {
            char data[256], *destination = data, *source = utf8.data();
            size_t bytes = utf8.size(), capacity = sizeof data;
            const auto result = iconv(converter, &source, &bytes, &destination, &capacity);
            ready.append(data, destination - data); utf8.erase(0, utf8.size() - bytes);
            if (result != static_cast<size_t>(-1)) break;
            if (errno == EINVAL && open) break;
            if (errno == E2BIG) continue;
            // An unavailable character becomes '?'. Keep an incomplete UTF-8
            // character for the next read, including one split across writes.
            ready += '?';
            size_t skip = 1;
            while (skip < utf8.size() && (static_cast<unsigned char>(utf8[skip]) & 0xc0) == 0x80) ++skip;
            utf8.erase(0, skip);
        }
        size_t consumed = 0;
        while (consumed < ready.size() && app.ConsoleInputSpace() >= 2)
        {
            unsigned character = static_cast<unsigned char>(ready[consumed++]);
            if (character == '\n' && previous_cr) { previous_cr = false; continue; }
            previous_cr = character == '\r';
            if (character == '\n') character = 13;
            app.RawKey(character, character, true); app.RawKey(character, 0, false);
        }
        ready.erase(0, consumed);
        if (!open && ready.empty() && utf8.empty() && !ended) { app.EndConsoleInput(); ended = true; }
    }
};
}
int main(int argc, char **argv)
{
    LinuxHost::Options options;
    if (!LinuxHost::Parse(argc, argv, options)) { LinuxHost::Help(false); return 2; }
    if (!options.state_save.empty() || !options.state_load.empty())
    { fprintf(stderr, "--state-save and --state-load are available in neo98-host only.\n"); return 2; }
    if (options.help) { LinuxHost::Help(false); return 0; }
    if (!options.load_state.empty()) { fprintf(stderr, "Saved states require the graphical host.\n"); return 2; }
    for (int i = 1; i < argc; ++i) if (!strcmp(argv[i], "--setup-linux")) { fprintf(stderr, "Setup requires the graphical host.\n"); return 2; }
    signal(SIGINT, Signal); signal(SIGTERM, Signal);
    LinuxHost::Session session(argc, argv, options);
    X86::Application::SetConsoleOutput(Output);
    if (!session.Start()) return 1;
    Input input(session.app->ConsoleCodePage());
    const auto start = std::chrono::steady_clock::now();
    bool expired = false;
    while (!interrupted && !session.Finished())
    {
        session.app->Pump(8);
        input.Pump(*session.app);
        if (options.seconds && std::chrono::steady_clock::now() - start >= std::chrono::seconds(options.seconds))
        { expired = true; break; }
        if (X86::Application::SessionWaitingForInput()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (expired || !options.quiet) session.Describe();
    if (!options.screenshot.empty() && !session.Screenshot(options.screenshot)) return 1;
    if (session.app->Status() == X86::Application::Failed)
    { fprintf(stderr, "%s\n", session.app->Error()); session.Report("guest-failed"); }
    if (interrupted) return 130;
    if (expired) return 124;
    return session.Result();
}
