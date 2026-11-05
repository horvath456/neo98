// SPDX-License-Identifier: GPL-2.0-or-later
// Exploratory runner. Read-only host files are copied into the native RAM volume.
#include "../src/x86_app.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>
static std::vector<unsigned char> load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return {};
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    if (n <= 0 || n > 8 * 1024 * 1024)
    {
        fclose(f);
        return {};
    }
    std::vector<unsigned char> b(n);
    if (fread(b.data(), 1, b.size(), f) != b.size())
        b.clear();
    fclose(f);
    return b;
}
static void logger(const char *s)
{
    puts(s);
    fflush(stdout);
}
static const char *leaf(const char *s)
{
    const auto *p = strrchr(s, '/');
    return p ? p + 1 : s;
}
static void hex_text(const char *text)
{
    for (const auto *p = reinterpret_cast<const unsigned char *>(text); *p; ++p)
        printf("%02x", *p);
}
static void snapshot_windows()
{
    unsigned handles[192], count = 0;
    if (!User::WindowSnapshot(User::GetDesktopWindow(), handles, 192, count))
        return;
    for (unsigned i = 0; i < count; ++i)
    {
        const unsigned hwnd = handles[i];
        char klass[64];
        std::vector<char> text(User::TextLength(hwnd) + 1);
        User::GetClassName(hwnd, klass, sizeof klass);
        User::GetWindowText(hwnd, text.data(), text.size());
        printf("PROBE_WINDOW hwnd=%u parent=%u owner=%u visible=%u class=", hwnd, User::Parent(hwnd),
               User::Owner(hwnd), User::IsVisible(hwnd));
        hex_text(klass);
        printf(" text=");
        hex_text(text.data());
        putchar('\n');
    }
}
int main(int argc, char **argv)
{
    if (argc < 3)
        return 2;
    const char *command = nullptr;
    for (int i = 3; i < argc; i++)
    {
        if (!strcmp(argv[i], "--command"))
        {
            if (++i == argc)
                return 2;
            command = argv[i];
            continue;
        }
        auto bytes = load(argv[i]);
        char guest[260];
        snprintf(guest, sizeof guest, "C:\\WINDOWS\\SYSTEM\\%s", leaf(argv[i]));
        if (bytes.empty() || Files::System().Seed(guest, bytes.data(), bytes.size()))
            return 3;
    }
    auto exe = load(argv[1]), font = load(argv[2]);
    auto *a = new X86::Application(logger);
    const bool started = a->Start(exe.data(), exe.size(), font.data(), font.size(), leaf(argv[1]), command);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    if (started)
        while (a->Status() != X86::Application::Exited && a->Status() != X86::Application::Failed &&
               std::chrono::steady_clock::now() < deadline)
        {
            a->Pump();
            if (a->WaitingForInput())
                break;
            if (a->Status() == X86::Application::Idle)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    printf("PROBE started=%u state=%u code=%u calls=%u callbacks=%u windows=%u error=%s\n", started,
           a->Status(), a->ExitCode(), a->ApiCalls(), a->Callbacks(), User::LiveWindows(), a->Error());
    if (started)
        snapshot_windows(); // Native read-only inspection; no window callbacks are entered.
    if(started){
        User::Render();FILE*f=fopen("paint-startup.ppm","wb");if(!f)return 7;
        fprintf(f,"P6\n800 600\n255\n");const auto*p=User::Pixels();
        for(unsigned i=0;i<800*600;++i){unsigned char rgb[3]={p[i*4+2],p[i*4+1],p[i*4]};fwrite(rgb,1,3,f);}fclose(f);
        printf("PAINT STARTUP SCREENSHOT captured; editor interaction not tested\n");
    }
    delete a;
}
