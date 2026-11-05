// SPDX-License-Identifier: GPL-2.0-or-later
// Guest storage and process providers for the implemented Win98 CRT startup.
#include "win98_crt_args.h"
#include "x86_app.h"
#include <string.h>
namespace X86
{
namespace
{
constexpr unsigned Argc = 44, Argv = 20, Environ = 32, Program = 36, NewMode = 40;
// crt_data+16 remains _adjust_fdiv; +12 belongs to the guest's __initenv.
}
struct Application::CrtArguments final : Win98CrtArgs::Environment
{
    struct Allocation
    {
        char *bytes;
        Allocation *next;
    };
    Application &app;
    Win98CrtArgs::State state;
    Allocation *owned = nullptr;
    unsigned program_guest = 0;
    bool ready = false;
    explicit CrtArguments(Application &a) : app(a) {}
    ~CrtArguments()
    {
        while (owned)
        {
            auto *next = owned->next;
            delete[] owned->bytes;
            delete owned;
            owned = next;
        }
    }
    void *Allocate(unsigned bytes) override
    {
        auto *record = new Allocation;
        if (!record)
            return nullptr;
        record->bytes = new char[bytes];
        if (!record->bytes)
        {
            delete record;
            return nullptr;
        }
        record->next = owned;
        owned = record;
        return record->bytes;
    }
    void ModuleName(char *out, unsigned capacity) override
    {
        if (!capacity)
            return;
        unsigned length = strlen(app.modules[0].path);
        if (length >= capacity)
            length = capacity - 1;
        memcpy(out, app.modules[0].path, length);
        out[length] = 0;
    }
    Win98CrtArgs::Status ExpandWildcards(Win98CrtArgs::State &) override
    {
        return Win98CrtArgs::Status::UnsupportedWildcards;
    }
    unsigned PublishVector(char *const *strings, unsigned count)
    {
        if (count > ~0u / 4 - 1)
            return 0;
        unsigned size = (count + 1) * 4;
        for (unsigned i = 0; i < count; ++i)
        {
            const auto length = strlen(strings[i]) + 1;
            if (length > ~0u - size)
                return 0;
            size += length;
        }
        const unsigned base = app.Allocate(size);
        if (!base)
            return 0;
        unsigned text = base + (count + 1) * 4;
        for (unsigned i = 0; i < count; ++i)
        {
            const unsigned length = strlen(strings[i]) + 1;
            app.Write32(base + i * 4, text);
            app.Store(text, strings[i], length);
            text += length;
        }
        app.Write32(base + count * 4, 0);
        return app.state == Failed ? 0 : base;
    }
};
void Application::FreeArguments()
{
    delete crt_arguments;
    crt_arguments = nullptr;
}
bool Application::InitializeArguments()
{
    if (crt_arguments)
        return crt_arguments->ready && state != Failed;
    crt_arguments = new CrtArguments(*this);
    if (!crt_arguments)
    {
        Fail("CRT startup allocation failed");
        return false;
    }
    auto &e = *crt_arguments;
    const unsigned environment = Api("GetEnvironmentStrings", nullptr);
    if (!environment || state == Failed)
    {
        Fail("CRT environment acquisition failed");
        return false;
    }
    // Borrow the process block. Its ownership belongs to GetEnvironmentStrings,
    // whereas the original CRT initializer consumes a private copy of it.
    unsigned length = 0;
    unsigned char previous = 1, current = 1;
    bool terminated = false;
    while (length < 65536)
    {
        if (!Read(environment + length, &current, 1))
            return false;
        ++length;
        if (!previous && !current)
        {
            terminated = true;
            break;
        }
        previous = current;
    }
    if (!terminated)
    {
        Fail("CRT environment block exceeds its native import limit");
        return false;
    }
    auto *block = new char[length];
    if (!block)
    {
        Fail("CRT environment copy allocation failed");
        return false;
    }
    const bool read = Read(environment, block, length);
    const auto result = read ? Win98CrtArgs::InitializeEnvironment(e, e.state, block)
                             : Win98CrtArgs::Status::InvalidInput;
    delete[] block;
    if (!read)
        return false;
    if (result != Win98CrtArgs::Status::Ok)
    {
        Fail("CRT environment initialization failed (_amsg_exit 9)");
        return false;
    }
    unsigned count = 0;
    while (e.state.environ[count])
        ++count;
    crt_env = e.PublishVector(e.state.environ, count);
    e.program_guest = Allocate(sizeof e.state.program_name);
    if (!crt_env || !e.program_guest)
    {
        Fail("CRT environment guest publication failed");
        return false;
    }
    Write32(crt_data + Argc, 0);
    Write32(crt_data + Argv, 0);
    Write32(crt_data + Environ, crt_env);
    Write32(crt_data + Program, 0);
    Write32(crt_data + NewMode, 0);
    // Preserve crt_data+12: the startup program owns __initenv assignments.
    e.ready = state != Failed;
    return e.ready;
}
unsigned Application::ArgumentsApi(ApiName name, const unsigned *a)
{
    if (!InitializeArguments())
        return 0;
    switch (name.id)
    {
    API_CASE("__p___argc")
        return crt_data + Argc;
    API_CASE("__p___argv")
        return crt_data + Argv;
    API_CASE("__p__environ")
        return crt_data + Environ;
    API_CASE("__p__pgmptr")
        return crt_data + Program;
    API_CASE("__p___initenv")
        return crt_data + 12;
    API_CASE("__p__acmdln")
        return crt_data;
    default:
        break;
    }
    if (!API_IS(name,"__getmainargs"))
    {
        Fail(name);
        return 0;
    }
    auto &e = *crt_arguments;
    const unsigned mode = Read32(a[4]), command = Read32(crt_data);
    if (state == Failed)
        return 0;
    if (!command)
    {
        Fail("CRT command-line pointer is null");
        return 0;
    }
    const char *input = ReadString(command);
    if (state == Failed)
        return 0;
    Write32(crt_data + NewMode, mode);
    const auto result = Win98CrtArgs::GetMainArgs(e, e.state, input, a[3] != 0, mode);
    if (result != Win98CrtArgs::Status::Ok)
    {
        Fail(result == Win98CrtArgs::Status::UnsupportedWildcards
                 ? "CRT wildcard argument expansion is not ported"
                 : "CRT argument initialization failed (_amsg_exit 8)");
        return 0;
    }
    const unsigned arguments = e.PublishVector(e.state.argv, e.state.argc);
    if (!arguments)
    {
        Fail("CRT argument guest publication failed (_amsg_exit 8)");
        return 0;
    }
    crt_argv = arguments;
    Write32(crt_data + Argc, e.state.argc);
    Write32(crt_data + Argv, crt_argv);
    Store(e.program_guest, e.state.program_name, sizeof e.state.program_name);
    Write32(crt_data + Program, e.program_guest);
    // _environ and __initenv are writable exports; do not reset either here.
    Write32(a[0], e.state.argc);
    Write32(a[1], crt_argv);
    Write32(a[2], Read32(crt_data + Environ));
    return a[2]; // Observed EAX at the original7800B549 return.
}
unsigned Application::ArgumentsExport(const char *name)
{
    unsigned offset;
    if (!strcmp(name, "__argc"))
        offset = Argc;
    else if (!strcmp(name, "__argv"))
        offset = Argv;
    else if (!strcmp(name, "_environ"))
        offset = Environ;
    else if (!strcmp(name, "_pgmptr"))
        offset = Program;
    else if (!strcmp(name, "__initenv"))
        offset = 12;
    else if (!strcmp(name, "_acmdln"))
        offset = 0;
    else
        return 0;
    return InitializeArguments() ? crt_data + offset : 0;
}
} // namespace X86
