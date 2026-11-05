// SPDX-License-Identifier: GPL-2.0-or-later
// The host media provider (tools/host_media/service.h) with the x86 guest
// tools/host_media_fixture.c, which talks to it through \\.\NEO98MEDIA like
// Media Center 98. The parts and what they check: tools/test_host_media_native.py.
#include "../src/x86_app.h"
#include "host_media/service.h"
#include <cassert>
#include <chrono>
#include <clocale>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
using App = X86::Application;
using HostMediaJson::Value;

static std::vector<unsigned char> load(const char *path)
{
    FILE *f = fopen(path, "rb");
    assert(f);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    std::vector<unsigned char> b(static_cast<size_t>(n));
    assert(fread(b.data(), 1, size_t(n), f) == size_t(n));
    fclose(f);
    return b;
}

static const char *font_path, *exe_path;

// One run of the fixture with its own provider. The host's main loop is the
// pump here: it hands the screen over as neo98_linux.cpp does.
static unsigned run(const char *part, std::vector<unsigned> *screens = nullptr)
{
    static std::vector<unsigned char> exe = load(exe_path), font = load(font_path);
    HostMediaService::Service media;
    media.show_screen = [&](unsigned to) { if (screens) screens->push_back(to); };
    media.Install();
    auto *app = new App(nullptr);
    std::string command = std::string("MEDIAFIX.EXE ") + part;
    assert(app->Start(exe.data(), exe.size(), font.data(), font.size(), "MEDIAFIX.EXE", command.c_str()));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
    while (app->Status() != App::Exited && app->Status() != App::Failed && std::chrono::steady_clock::now() < deadline)
    {
        media.Pump();
        if (app->Pump(64) == App::Idle) std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    const unsigned code = app->Status() == App::Exited ? app->ExitCode() : ~0u;
    printf("HOSTMEDIA %s state=%u exit=%u error=%s\n", part, app->Status(), app->ExitCode(), app->Error());
    fflush(stdout);
    delete app;
    return code;
}

// The JSON of Kodi's JSON-RPC, its framing on TCP and the art of its lists.
static void units()
{
    Value v;
    assert(HostMediaJson::Parse(R"({"a":[1,2.5,"x\"\u00e9\ud83d\ude00"],"b":{"c":true,"d":null}})", v));
    assert(v["a"].At(0).Num() == 1 && v["a"].At(1).Num() == 2.5 && v["a"].At(2).Text() == "x\"\xc3\xa9\xf0\x9f\x98\x80");
    assert(v["b"]["c"].Truth() && v["b"]["d"].IsNull() && v["missing"]["deeper"].IsNull());
    assert(HostMediaJson::Dump(v) == "{\"a\":[1,2.5,\"x\\\"\xc3\xa9\xf0\x9f\x98\x80\"],\"b\":{\"c\":true,\"d\":null}}");
    assert(!HostMediaJson::Parse("{\"a\":1", v) && !HostMediaJson::Parse("{\"a\":1} x", v));
    // Numbers as JSON writes them, also under a locale with a decimal comma.
    setlocale(LC_NUMERIC, "de_AT.UTF-8");
    assert(HostMediaJson::Parse("{\"id\":1,\"x\":-2.5e2,\"y\":0.125}", v) && v["id"].Num() == 1 && v["x"].Num() == -250 && v["y"].Num() == 0.125);
    assert(HostMediaJson::Dump(Value(0.5)) == "0.5" && !HostMediaJson::Parse("{\"a\":01.}", v));
    setlocale(LC_NUMERIC, "C");
    // Two messages back to back, the second cut off, braces inside a string.
    const std::string stream = R"({"id":1,"result":"}{"}{"method":"Player.OnStop","params":{)";
    const size_t first = HostMediaJson::MessageLength(stream);
    assert(first == 22 && HostMediaJson::Parse(stream.substr(0, first), v) && v["result"].Text() == "}{");
    assert(!HostMediaJson::MessageLength(stream.substr(first)));
    assert(HostMediaKodi::Plain("[B]The Crown[/B] [COLOR red]New[/COLOR][CR]2") == "The Crown New 2");
    assert(HostMediaKodi::Plain("[Season 1]") == "[Season 1]");
    assert(HostMediaKodi::Unwrap("image://https%3a%2f%2fart.example%2fp%20q.jpg/") == "https://art.example/p q.jpg");
    assert(HostMediaKodi::Unwrap("/home/kodi/poster.png") == "/home/kodi/poster.png");
    // A 2x1 picture, red and transparent, into a 4x4 box on white.
    std::vector<unsigned> fitted;
    HostMediaService::Fit({0xffff0000u, 0x00000000u}, 2, 1, 4, 4, 0xffffff, fitted);
    assert(fitted.size() == 16 && fitted[0] == 0xffffff && fitted[4] == 0xff0000 && fitted[5] == 0xff0000 && fitted[6] == 0xffffff);
    // Labels cut at a character, not inside one.
    char small[4];
    HostMediaService::Text(small, "ab\xc3\xa9");
    assert(!strcmp(small, "ab"));
    puts("HOSTMEDIA units PASS");
}

int main(int argc, char **argv)
{
    assert(argc >= 4);
    exe_path = argv[1];
    font_path = argv[2];
    const std::string part = argv[3];
    if (part == "units") { units(); return 0; }
    if (part == "none")
    {
        setenv("NEO98_TEST_KODI", "none", 1);
        return run("none") ? 1 : 0;
    }
    if (part == "missing")
    {
        // No test server, a Kodi that does not exist, a port nobody listens on.
        unsetenv("NEO98_TEST_KODI");
        setenv("NEO98_KODI", "/nonexistent/kodi", 1);
        setenv("NEO98_KODI_PORT", argv[4], 1);
        return run("none") ? 1 : 0;
    }
    if (part == "screens")
    {
        // play, and the screen went to Kodi and came back exactly once each
        // way while the video ran.
        std::vector<unsigned> screens;
        const unsigned code = run("play", &screens);
        printf("HOSTMEDIA screens");
        for (unsigned s : screens) printf(" %s", s == HM_SCREEN_KODI ? "kodi" : "neo98");
        printf("\n");
        return code || screens.size() < 2 || screens[0] != HM_SCREEN_KODI || screens[1] != HM_SCREEN_NEO98 ? 1 : 0;
    }
    return run(part.c_str()) ? 1 : 0;
}
