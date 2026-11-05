// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_crt_args.h"
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace Win98CrtArgs;
struct Runtime final : Environment
{
    State state;
    std::vector<void *> owned;
    std::vector<unsigned> sizes;
    const char *module = "C:\\Program Files\\wordpad.exe";
    unsigned failure = 0, expand_status = 0, expanded = 0, module_calls = 0;
    ~Runtime() { for (void *p : owned) std::free(p); }
    void *Allocate(unsigned bytes) override
    {
        sizes.push_back(bytes);
        if (failure && !--failure)
            return nullptr;
        void *p = std::malloc(bytes);
        if (p)
        {
            std::memset(p, 0xcc, bytes);
            owned.push_back(p);
        }
        return p;
    }
    void ModuleName(char *out, unsigned size) override
    {
        ++module_calls;
        std::strncpy(out, module, size);
    }
    Status ExpandWildcards(State &s) override
    {
        ++expanded;
        if (!expand_status)
            for (unsigned i = 0; i < s.argc; ++i)
                ++s.argv[i]; // Controlled provider removes the raw-first-byte markers.
        return static_cast<Status>(expand_status);
    }
};
extern "C"
{
void args_parse(const char *text, unsigned markers, unsigned *offsets, char *bytes, unsigned *counts)
{
    auto measured = Parse(text, nullptr, nullptr, markers);
    std::vector<char *> argv(measured.pointers);
    auto written = Parse(text, argv.data(), bytes, markers);
    counts[0] = measured.pointers;
    counts[1] = measured.characters;
    counts[2] = written.pointers;
    counts[3] = written.characters;
    for (unsigned i = 0; i < written.pointers; ++i)
        offsets[i] = argv[i] ? argv[i] - bytes : ~0u;
}
void *args_new() { return new Runtime; }
void args_delete(Runtime *r) { delete r; }
void args_failure(Runtime *r, unsigned value) { r->failure = value; }
void args_wildcard_status(Runtime *r, unsigned value) { r->expand_status = value; }
unsigned args_environment(Runtime *r, const char *block)
{
    return static_cast<unsigned>(InitializeEnvironment(*r, r->state, block));
}
unsigned args_main(Runtime *r, const char *text, unsigned wildcard, unsigned mode)
{
    return static_cast<unsigned>(GetMainArgs(*r, r->state, text, wildcard, mode));
}
const char *args_value(Runtime *r, unsigned kind, unsigned index)
{
    if (kind == 2)
        return r->state.program_name;
    char **table = kind ? r->state.environ : r->state.argv;
    return table ? table[index] : nullptr;
}
unsigned args_word(Runtime *r, unsigned index)
{
    const unsigned values[] = {r->state.argc, r->state.new_mode, r->expanded, r->module_calls,
                               unsigned(r->sizes.size()), unsigned(r->state.initenv != nullptr)};
    return values[index];
}
unsigned args_allocation(Runtime *r, unsigned index) { return r->sizes[index]; }
}
#ifdef ARGS_NATIVE_TEST
#include <cassert>
#include <cstdio>
#include <string>
int main()
{
    Runtime r;
    const char environment[] = "=C:=C:\\WORK\0A=1\0B=\0NO_EQUALS\0";
    assert(InitializeEnvironment(r, r.state, environment) == Status::Ok);
    assert(!std::strcmp(r.state.environ[0], "A=1") && !std::strcmp(r.state.environ[1], "B=") &&
           !std::strcmp(r.state.environ[2], "NO_EQUALS") && !r.state.environ[3]);
    assert(!r.state.initenv);
    unsigned random = 0x987a1263;
    const char alphabet[] = "abAZ12 \t\r\n\\\"*?=\x80\xe9\xff";
    for (unsigned i = 0; i < 1000; ++i)
    {
        char input[4096];
        const unsigned length = i % sizeof input;
        for (unsigned k = 0; k < length; ++k)
        {
            random = random * 1664525u + 1013904223u;
            input[k] = alphabet[(random >> 16) % (sizeof alphabet - 1)];
        }
        input[length] = 0;
        char **previous = r.state.argv;
        std::vector<std::string> saved;
        for (unsigned k = 0; k < r.state.argc; ++k)
            saved.emplace_back(previous[k]);
        assert(GetMainArgs(r, r.state, input, i & 1, i) == Status::Ok);
        assert(r.state.argv && r.state.argc && !r.state.argv[r.state.argc] && r.state.new_mode == i);
        for (unsigned k = 0; k < saved.size(); ++k)
            assert(saved[k] == previous[k]);
    }
    char **previous = r.state.argv;
    r.failure = 1;
    assert(GetMainArgs(r, r.state, "p a b", false, 98) == Status::ArgumentMemory);
    assert(r.state.argv == previous && r.state.new_mode == 98);
    r.expand_status = unsigned(Status::UnsupportedWildcards);
    assert(GetMainArgs(r, r.state, "p *.txt", true, 1) == Status::UnsupportedWildcards);
    assert(GetMainArgs(r, r.state, nullptr, false, 0) == Status::InvalidInput);
    assert(InitializeEnvironment(r, r.state, nullptr) == Status::InvalidInput);
    for (unsigned failure = 1; failure <= 4; ++failure)
    {
        Runtime failing;
        failing.failure = failure;
        assert(InitializeEnvironment(failing, failing.state, "A=1\0B=2\0C=3\0") == Status::EnvironmentMemory);
    }
    std::puts("NATIVE CRT ARGS ASAN/UBSAN PASS");
}
#endif
