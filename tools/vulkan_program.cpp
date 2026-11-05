// SPDX-License-Identifier: GPL-2.0-or-later
#include "vulkan_program.h"
#include "../src/d3d_shader_msl.h"
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#ifdef __linux__
#include <unistd.h>
#include <filesystem>
#endif
#include <memory>
#include <regex>
#ifndef NEO98_VULKAN_SHADERS_HEADER
#define NEO98_VULKAN_SHADERS_HEADER "../build/generated/vulkan_shaders.h"
#endif
#include NEO98_VULKAN_SHADERS_HEADER
namespace HostVulkan::Program
{
namespace
{
// The shaderc C ABI keeps the runtime compiler out of the CLI and CPU builds.
struct Compiler
{
    void *library = nullptr, *compiler = nullptr;
    void *(*initialize)() = nullptr;
    void (*release)(void *) = nullptr;
    void *(*compile)(void *, const char *, size_t, int, const char *, const char *, void *) = nullptr;
    int (*status)(void *) = nullptr;
    const char *(*errors)(void *) = nullptr;
    size_t (*length)(void *) = nullptr;
    const char *(*bytes)(void *) = nullptr;
    void (*result_release)(void *) = nullptr;
    ~Compiler()
    {
        if (compiler)
            release(compiler);
        if (library)
            dlclose(library);
    }
    template <class T> bool Symbol(T &to, const char *name)
    {
        to = reinterpret_cast<T>(dlsym(library, name));
        return to != nullptr;
    }
    bool Open()
    {
        const char *path = getenv("NEO98_SHADERC_LIBRARY");
        std::string packaged;
#ifdef __linux__
        if (!path)
        {
            char executable[4096] = {};
            if (readlink("/proc/self/exe", executable, sizeof executable - 1) > 0)
            {
                const auto candidate = std::filesystem::path(executable).parent_path() / "../lib/neo98/libshaderc.so.1";
                if (std::filesystem::is_regular_file(candidate)) { packaged = candidate.string(); path = packaged.c_str(); }
            }
        }
#endif
        library = dlopen(path ? path : "libshaderc.so.1", RTLD_NOW | RTLD_LOCAL);
        if (!library && !path)
            library =
                dlopen("build/toolchain/usr/lib/x86_64-linux-gnu/libshaderc.so.1", RTLD_NOW | RTLD_LOCAL);
        if (!library)
            return false;
        if (!Symbol(initialize, "shaderc_compiler_initialize") ||
            !Symbol(release, "shaderc_compiler_release") || !Symbol(compile, "shaderc_compile_into_spv") ||
            !Symbol(status, "shaderc_result_get_compilation_status") ||
            !Symbol(errors, "shaderc_result_get_error_message") ||
            !Symbol(length, "shaderc_result_get_length") || !Symbol(bytes, "shaderc_result_get_bytes") ||
            !Symbol(result_release, "shaderc_result_release"))
            return false;
        compiler = initialize();
        return compiler != nullptr;
    }
};
Compiler *GetCompiler()
{
    static std::unique_ptr<Compiler> value = []
    {
        auto c = std::make_unique<Compiler>();
        if (!c->Open())
            c.reset();
        return c;
    }();
    return value.get();
}
void Replace(std::string &s, const std::string &from, const std::string &to)
{
    size_t at = 0;
    while ((at = s.find(from, at)) != std::string::npos)
    {
        s.replace(at, from.size(), to);
        at += to.size();
    }
}
std::vector<std::string> Arguments(const std::string &text)
{
    std::vector<std::string> args;
    size_t start = 0;
    int depth = 0;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '(' || text[i] == '[')
            ++depth;
        else if (text[i] == ')' || text[i] == ']')
            --depth;
        else if (text[i] == ',' && !depth)
        {
            args.push_back(text.substr(start, i - start));
            start = i + 1;
        }
    }
    args.push_back(text.substr(start));
    return args;
}
std::string Comparison(const std::string &s, unsigned size)
{
    int depth = 0;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '(' || s[i] == '[')
            ++depth;
        else if (s[i] == ')' || s[i] == ']')
            --depth;
        else if (!depth && (s[i] == '<' || s[i] == '>'))
        {
            const bool equal = i + 1 < s.size() && s[i + 1] == '=';
            const char *op = s[i] == '<' ? (equal ? "lessThanEqual" : "lessThan")
                                         : (equal ? "greaterThanEqual" : "greaterThan");
            const std::string type = "vec" + std::to_string(size);
            return std::string(op) + "(" + type + "(" + s.substr(0, i) + ")," + type + "(" +
                   s.substr(i + 1 + equal) + "))";
        }
    }
    return s;
}
// Adapt only expressions emitted by the checked D3D token parser. Balanced
// argument parsing preserves nested swizzles, constructors and comparisons.
std::string Calls(const std::string &s)
{
    std::string out;
    for (size_t i = 0; i < s.size();)
    {
        const char *call = s.compare(i, 7, "select(") == 0 ? "select"
                           : s.compare(i, 4, "any(") == 0  ? "any"
                                                           : nullptr;
        if (!call)
        {
            out += s[i++];
            continue;
        }
        const size_t start = i + strlen(call) + 1;
        size_t end = start;
        unsigned depth = 1;
        for (; end < s.size() && depth; ++end)
        {
            if (s[end] == '(')
                ++depth;
            else if (s[end] == ')')
                --depth;
        }
        if (depth)
        {
            out += s.substr(i);
            break;
        }
        auto args = Arguments(s.substr(start, end - start - 1));
        for (auto &a : args)
            a = Calls(a);
        if (!strcmp(call, "select") && args.size() == 3)
            out += "mix(" + args[0] + "," + args[1] + "," + Comparison(args[2], 4) + ")";
        else if (!strcmp(call, "any") && args.size() == 1)
            out += "any(" + Comparison(args[0], 3) + ")";
        else
            out += s.substr(i, end - i);
        i = end;
    }
    return out;
}
std::string Body(std::string msl, bool pixel)
{
    msl = msl.substr(msl.find('{'));
    Replace(msl, "constant float4 *k = c;", "vec4 k[256]; for(uint i=0u;i<256u;++i) k[i]=c[i];");
    msl = std::regex_replace(msl, std::regex("\\(void\\)[A-Za-z_][A-Za-z_0-9]*;"), "");
    Replace(msl, "float4", "vec4");
    Replace(msl, "float3", "vec3");
    Replace(msl, "float2", "vec2");
    Replace(msl, "int4", "ivec4");
    Replace(msl, "rint(", "roundEven(");
    Replace(msl, "rsqrt(", "inversesqrt(");
    // GLSL arrays use an array constructor, unlike the C/MSL initializer.
    msl = std::regex_replace(
        msl, std::regex("(vec4|float) ([A-Za-z_][A-Za-z_0-9]*)\\[([0-9]+)\\] = \\{([^;]*)\\};"),
        "$1 $2[$3] = $1[$3]($4);");
    Replace(msl, ", TEXTURE_NAMES", "");
    Replace(msl, "shader_sample(u, ", "shader_sample(");
    Replace(msl, "shader_sample2(u, ", "shader_sample2(");
    Replace(msl, "texbem(u, ", "texbem(");
    Replace(msl, "bem(u, ", "bem(");
    Replace(msl, "discard_fragment();", "discard;");
    msl = std::regex_replace(msl, std::regex("cb\\[([A-Za-z_0-9]+)\\]"), "shader_bool($1)");
    msl = Calls(msl);
    return pixel ? "vec4 translated(vec4 v0,vec4 v1,vec4 coords[8])\n" + msl
                 : "void translated(vec4 v[16],inout vec4 oPos,inout vec4 oD[2],inout vec4 oT[8],inout vec4 "
                   "oFog,inout vec4 oPts)\n" +
                       msl;
}
} // namespace
bool Available() { return GetCompiler() != nullptr; }
bool Compile(const std::string &source, bool pixel, std::vector<unsigned> &words, std::string &error)
{
    words.clear();
    auto *c = GetCompiler();
    if (!c)
    {
        error = "libshaderc.so.1 unavailable";
        return false;
    }
    void *r =
        c->compile(c->compiler, source.data(), source.size(), pixel ? 1 : 0, "neo98-d3d", "main", nullptr);
    if (!r)
    {
        error = "shaderc returned no result";
        return false;
    }
    const bool okay = c->status(r) == 0;
    if (okay)
    {
        const size_t n = c->length(r);
        if (n % 4)
        {
            error = "unaligned SPIR-V result";
            c->result_release(r);
            return false;
        }
        words.resize(n / 4);
        memcpy(words.data(), c->bytes(r), n);
    }
    else
        error = c->errors(r);
    c->result_release(r);
    return okay;
}
bool Translate(const unsigned *tokens, unsigned count, bool pixel, bool wbuffer, std::string &source,
               std::string &error, bool flat)
{
    std::string msl;
    if (!(pixel ? Direct3D::PixelShaderToMsl(tokens, count, "translated", msl, error)
                : Direct3D::VertexShaderToMsl(tokens, count, "translated", msl, error)))
        return false;
    source = pixel ? Vulkan_frag_source : Vulkan_program_vert_source;
    if (pixel)
        source.resize(source.find("void main()"));
    Replace(source, "#version 450", wbuffer ? "#version 450\n#define NEO98_WBUFFER 1" : "#version 450");
    if (flat)
        Replace(source, "#version 450", "#version 450\n#define NEO98_FLAT 1");
    if (flat)
    {
        Replace(source, "noperspective out vec4", "flat out vec4");
        Replace(source, "noperspective in vec4", "flat in vec4");
    }
    source += pixel ? "\n#define NEO98_PIXEL_PROGRAM 1\n" : "\n";
    source += Vulkan_program_helpers_source;
    source += pixel ? "\n#define c pc.floats\n#define ci pc.integers\n#define cb pc.booleans\n"
                    : "\n#define c vc.floats\n#define ci vc.integers\n#define cb vc.booleans\n";
    source += Body(msl, pixel);
    source += pixel ? Vulkan_program_frag_main_source : Vulkan_program_vert_main_source;
    return true;
}
} // namespace HostVulkan::Program
