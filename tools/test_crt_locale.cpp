// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_crt_locale.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <set>
struct Runtime final : Win98Locale::Environment
{
    Win98Locale::State state;
    Win98Locale::SystemData data;
    std::set<char *> live;
    unsigned failure = 0;
    ~Runtime()
    {
        for (auto p : state.names)
            if (p != state.c_locale)
                Free(p);
        assert(live.empty());
    }
    char *Allocate(unsigned n) override
    {
        if (failure && !--failure)
            return nullptr;
        auto *p = (char *)std::malloc(n);
        if (p)
            live.insert(p);
        return p;
    }
    void Free(char *p) override
    {
        if (p)
        {
            assert(live.erase(p));
            std::free(p);
        }
    }
    void Lock(unsigned n) override { assert(n == 19); }
    void Unlock(unsigned n) override { assert(n == 19); }
    void Sleep(unsigned n) override
    {
        assert(n == 1 && state.read_active);
        --state.read_active;
    }
    bool Qualify(Win98Locale::Parts &p, Win98Locale::Id &i) override
    {
        return Win98Locale::QualifySystem(p, i);
    }
    int Initialize(unsigned c, Win98Locale::State &s) override { return data.Initialize(c, s); }
    const char *Set(int c, const char *s) { return Win98Locale::Set(*this, state, c, s); }
};
int main()
{
    Runtime r;
    assert(!strcmp(r.Set(0, nullptr), "C"));
    assert(r.live.empty());
    assert(!(Win98Locale::IsCType(r.data, 0xe4, 2)));
    r.state.read_active = 2;
    assert(!strcmp(r.Set(0, ""), "English_United States.1252"));
    assert(!r.state.read_active);
    assert(r.state.code_page == 1252 && r.state.collate_page == 1252 && r.data.mb_max == 1);
    assert(Win98Locale::IsCType(r.data, 0xe4, 2) == 2);
    assert(Win98Locale::Case(r.state, r.data, 0xe4, true) == 0xc4);
    assert(Win98Locale::Case(r.state, r.data, 0xc4, false) == 0xe4);
    assert(Win98Locale::Case(r.state, r.data, 0xdf, true) == 0xdf);
    assert(Win98Locale::IsCType(r.data, 0x1e4, 2) == 2);
    assert(Win98Locale::Case(r.state, r.data, 0x141, true) == 0x41);
    assert(!strcmp(r.data.numeric[0], ".") && !strcmp(r.data.numeric[1], ",") && r.data.numeric[2][0] == 3 &&
           r.data.numeric[2][1] == 0);
    assert(!strcmp(r.data.monetary[0], "USD") && !strcmp(r.data.monetary[1], "$") &&
           r.data.monetary[4][0] == 3);
    assert(!strcmp(r.data.time[0], "Sun") && !strcmp(r.data.time[7], "Sunday") &&
           !strcmp(r.data.time[26], "January"));
    assert(!strcmp(r.data.time[42], "h:mm:ss tt"));
    const char *prior = r.state.names[2];
    assert(!r.Set(2, "Japanese_Japan.932") && r.state.names[2] == prior);
    r.failure = 1;
    assert(!r.Set(2, "C") && r.state.names[2] == prior && r.state.code_page == 1252);
    const char *mixed = r.Set(0, "LC_CTYPE=C;LC_TIME=English");
    assert(mixed && strstr(mixed, "LC_CTYPE=C") && strstr(mixed, "LC_TIME=English_United States.1252"));
    assert(r.state.code_page == 0 && Win98Locale::Case(r.state, r.data, 0x141, true) == 0x141);
    assert(!Win98Locale::IsCType(r.data, 0xe4, 2) && Win98Locale::IsCType(r.data, 0x1e4, 2) == 2);
    assert(!strcmp(r.Set(0, "C"), "C"));
    assert(!strcmp(r.data.time[42], "H:mm:ss"));
    for (auto n : r.data.monetary_flags)
        assert(n == 127);
    char oversized[300];
    memset(oversized, 'x', sizeof oversized);
    oversized[299] = 0;
    assert(!r.Set(0, oversized));
    assert(!Win98Locale::SystemInfo(0x407, 0x14) && !Win98Locale::SystemInfo(0x409, 0xffff));
    assert(!r.Set(-1, "C") && !r.Set(6, "C"));
    // All's original unchecked malloc failure is replaced by a null result.
    r.failure = 1;
    assert(!r.Set(0, nullptr));
    assert(!strcmp(r.Set(0, nullptr), "C"));
    for (unsigned c = 0; c < 257; ++c)
        assert(r.data.ctype[c] == Win98Locale::ClassicType()[c]);
    std::puts("NATIVE CRT LOCALE ASAN/UBSAN PASS");
}
