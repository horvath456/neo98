// SPDX-License-Identifier: GPL-2.0-or-later
// Pixel shaders 1.1 to 1.4 through src/d3d_shader_msl.cpp and the Metal
// compiler, with the backend's own shader source (HostMetal::CompilePixelShader).
// The programs below use every instruction and modifier the translation
// knows; each has to translate and compile. Built and run by
// tools/test_d3d_shader_msl.py.
#ifdef NEO98_SHADER_VULKAN
#include "vulkan_program.h"
#else
#include "host_metal.h"
#endif
#include <cstdio>
#include <cstring>
#include <vector>
#ifdef NEO98_SHADER_VULKAN
namespace HostMetal
{
static bool Compile(const unsigned *tokens, unsigned count, bool pixel, char *error, unsigned capacity)
{
    std::string source, message;
    std::vector<unsigned> words;
    const bool okay = HostVulkan::Program::Translate(tokens, count, pixel, false, source, message) &&
                      HostVulkan::Program::Compile(source, pixel, words, message);
    if (!okay) snprintf(error, capacity, "%s", message.c_str());
    return okay;
}
static bool CompilePixelShader(const unsigned *tokens, unsigned count, char *error, unsigned capacity)
{ return Compile(tokens, count, true, error, capacity); }
static bool CompileVertexShader(const unsigned *tokens, unsigned count, char *error, unsigned capacity)
{ return Compile(tokens, count, false, error, capacity); }
}
#endif

namespace {

// A small assembler for the tokens of d3d8types.h.
enum Type : unsigned { R = 0, V = 1, C = 2, T = 3 };
struct Program
{
    std::vector<unsigned> tokens;
    explicit Program(unsigned minor) { tokens.push_back(0xffff0100u | minor); }
    // A destination: register, write mask, result modifier (1 saturate) and shift.
    static unsigned D(Type type, unsigned n, unsigned mask = 15, unsigned result = 0, int shift = 0)
    {
        return 0x80000000u | (unsigned(type) << 28) | (mask << 16) | (result << 20) | ((unsigned(shift) & 15) << 24) | n;
    }
    // A source: register, swizzle, modifier.
    static unsigned S(Type type, unsigned n, unsigned swizzle = 0xe4, unsigned modifier = 0)
    {
        return 0x80000000u | (unsigned(type) << 28) | (swizzle << 16) | (modifier << 24) | n;
    }
    Program &op(unsigned opcode, std::initializer_list<unsigned> params, bool coissue = false)
    {
        tokens.push_back(opcode | (coissue ? 0x40000000u : 0));
        tokens.insert(tokens.end(), params);
        return *this;
    }
    Program &def(unsigned n, float x, float y, float z, float w)
    {
        tokens.push_back(81);
        tokens.push_back(D(C, n));
        const float values[4] = {x, y, z, w};
        for (float f : values) { unsigned u; memcpy(&u, &f, 4); tokens.push_back(u); }
        return *this;
    }
    Program &comment(const char *text)
    {
        const unsigned words = unsigned((strlen(text) + 4) / 4);
        tokens.push_back(0xfffe | (words << 16));
        for (unsigned i = 0; i < words; ++i)
        {
            unsigned u = 0;
            memcpy(&u, text + i * 4, strlen(text) + 1 - i * 4 < 4 ? strlen(text) + 1 - i * 4 : 4);
            tokens.push_back(u);
        }
        return *this;
    }
    std::vector<unsigned> end() { tokens.push_back(0xffff); return tokens; }
};

enum Op : unsigned
{
    Nop = 0, Mov = 1, Add = 2, Sub = 3, Mad = 4, Mul = 5, Dp3 = 8, Dp4 = 9, Lrp = 18, TexCoord = 64, TexKill = 65, Tex = 66,
    TexBem = 67, TexBemL = 68, TexReg2AR = 69, TexReg2GB = 70, TexM3x2Pad = 71, TexM3x2Tex = 72, TexM3x3Pad = 73,
    TexM3x3Tex = 74, TexM3x3Spec = 76, TexM3x3VSpec = 77, Cnd = 80, TexReg2RGB = 82, TexDp3Tex = 83, TexM3x2Depth = 84,
    TexDp3 = 85, TexM3x3 = 86, TexDepth = 87, Cmp = 88, Bem = 89, Phase = 0xfffd,
};
using P = Program;

} // namespace

int main()
{
#ifdef NEO98_SHADER_VULKAN
    if (!HostVulkan::Program::Available()) { puts("SKIP: libshaderc.so.1 unavailable"); return 77; }
#endif
    struct Case
    {
        const char *name;
        std::vector<unsigned> tokens;
    };
    std::vector<Case> cases;
    // 1.1: arithmetic with every source modifier and result modifier, a
    // co-issued pair, cnd on r0.a, the bump and dependent reads.
    cases.push_back({"1.1 arithmetic", P(1)
        .comment("neo98")
        .def(0, 0.5f, 0.25f, 1.0f, 1.0f)
        .op(Tex, {P::D(T, 0)})
        .op(TexCoord, {P::D(T, 1)})
        .op(Mul, {P::D(R, 0, 15, 0, 1), P::S(T, 0), P::S(V, 0, 0xe4, 1)})
        .op(Add, {P::D(R, 1, 7), P::S(R, 0, 0xe4, 2), P::S(C, 0, 0xe4, 3)})
        .op(Mov, {P::D(R, 1, 8), P::S(V, 1, 0xff, 4)}, true)
        .op(Mad, {P::D(R, 0, 15, 1), P::S(R, 1, 0xe4, 5), P::S(C, 0, 0xe4, 6), P::S(T, 1)})
        .op(Sub, {P::D(R, 0, 15, 0, -1), P::S(R, 0), P::S(C, 0)})
        .op(Lrp, {P::D(R, 0), P::S(C, 0, 0xff), P::S(R, 0), P::S(R, 1)})
        .op(Dp3, {P::D(R, 1, 15, 1), P::S(T, 0, 0xe4, 4), P::S(V, 0, 0xe4, 4)})
        .op(Cnd, {P::D(R, 0), P::S(R, 0, 0xff), P::S(R, 1), P::S(T, 0)})
        .op(Nop, {})
        .end()});
    cases.push_back({"1.1 bump", P(1)
        .op(Tex, {P::D(T, 0)})
        .op(TexBem, {P::D(T, 1), P::S(T, 0)})
        .op(TexBemL, {P::D(T, 2), P::S(T, 0)})
        .op(TexReg2AR, {P::D(T, 3), P::S(T, 1)})
        .op(Mul, {P::D(R, 0), P::S(T, 2), P::S(T, 3)})
        .end()});
    cases.push_back({"1.1 texreg2gb and texkill", P(1)
        .op(Tex, {P::D(T, 0)})
        .op(TexKill, {P::D(T, 1)})
        .op(TexReg2GB, {P::D(T, 2), P::S(T, 0)})
        .op(Mov, {P::D(R, 0), P::S(T, 2)})
        .end()});
    cases.push_back({"1.1 3x2 matrix", P(1)
        .op(Tex, {P::D(T, 0)})
        .op(TexM3x2Pad, {P::D(T, 1), P::S(T, 0, 0xe4, 4)})
        .op(TexM3x2Tex, {P::D(T, 2), P::S(T, 0, 0xe4, 4)})
        .op(Mov, {P::D(R, 0), P::S(T, 2)})
        .end()});
    cases.push_back({"1.1 3x3 environment", P(1)
        .def(0, 0.0f, 0.0f, 1.0f, 0.0f)
        .op(Tex, {P::D(T, 0)})
        .op(TexM3x3Pad, {P::D(T, 1), P::S(T, 0, 0xe4, 4)})
        .op(TexM3x3Pad, {P::D(T, 2), P::S(T, 0, 0xe4, 4)})
        .op(TexM3x3Spec, {P::D(T, 3), P::S(T, 0, 0xe4, 4), P::S(C, 0)})
        .op(Mov, {P::D(R, 0), P::S(T, 3)})
        .end()});
    cases.push_back({"1.1 3x3 eye vector", P(1)
        .op(Tex, {P::D(T, 0)})
        .op(TexM3x3Pad, {P::D(T, 1), P::S(T, 0)})
        .op(TexM3x3Pad, {P::D(T, 2), P::S(T, 0)})
        .op(TexM3x3VSpec, {P::D(T, 3), P::S(T, 0)})
        .op(Mov, {P::D(R, 0), P::S(T, 3)})
        .end()});
    cases.push_back({"1.1 3x3 texture", P(1)
        .op(Tex, {P::D(T, 0)})
        .op(TexM3x3Pad, {P::D(T, 1), P::S(T, 0)})
        .op(TexM3x3Pad, {P::D(T, 2), P::S(T, 0)})
        .op(TexM3x3Tex, {P::D(T, 3), P::S(T, 0)})
        .op(Mov, {P::D(R, 0), P::S(T, 3)})
        .end()});
    // 1.2 and 1.3: cmp, dp4, the vector forms and the depth of 3x2.
    cases.push_back({"1.2 cmp and dp4", P(2)
        .op(Tex, {P::D(T, 0)})
        .op(TexReg2RGB, {P::D(T, 1), P::S(T, 0)})
        .op(Dp4, {P::D(R, 1), P::S(T, 0), P::S(T, 1)})
        .op(Cmp, {P::D(R, 0), P::S(R, 1, 0xe4, 2), P::S(T, 0), P::S(T, 1)})
        .end()});
    cases.push_back({"1.2 3x3 vector and dot products", P(2)
        .op(Tex, {P::D(T, 0)})
        .op(TexM3x3Pad, {P::D(T, 1), P::S(T, 0)})
        .op(TexM3x3Pad, {P::D(T, 2), P::S(T, 0)})
        .op(TexM3x3, {P::D(T, 3), P::S(T, 0)})
        .op(Mov, {P::D(R, 0), P::S(T, 3)})
        .end()});
    cases.push_back({"1.2 texdp3", P(2)
        .op(Tex, {P::D(T, 0)})
        .op(TexDp3Tex, {P::D(T, 1), P::S(T, 0)})
        .op(TexDp3, {P::D(T, 2), P::S(T, 0)})
        .op(Mul, {P::D(R, 0), P::S(T, 1), P::S(T, 2)})
        .end()});
    cases.push_back({"1.3 3x2 depth", P(3)
        .op(Tex, {P::D(T, 0)})
        .op(TexM3x2Pad, {P::D(T, 1), P::S(T, 0)})
        .op(TexM3x2Depth, {P::D(T, 2), P::S(T, 0)})
        .op(Mov, {P::D(R, 0), P::S(T, 0)})
        .end()});
    // 1.4: two phases, texld with a projective divide, texcrd, bem, texkill
    // of a register, the per component cnd, texdepth.
    cases.push_back({"1.4 two phases", P(4)
        .def(0, 0.5f, 0.5f, 0.5f, 1.0f)
        .op(Tex, {P::D(R, 0), P::S(T, 0)})
        .op(TexCoord, {P::D(R, 1, 7), P::S(T, 1, 0xe4, 9)})
        .op(Bem, {P::D(R, 2, 3), P::S(R, 1), P::S(R, 0)})
        .op(Phase, {})
        .op(Tex, {P::D(R, 2), P::S(R, 2)})
        .op(Tex, {P::D(R, 3), P::S(T, 3, 0xe4, 10)})
        .op(TexKill, {P::D(R, 1)})
        .op(Cnd, {P::D(R, 0), P::S(R, 3), P::S(R, 2), P::S(V, 0, 0xe4, 7)})
        .op(Mad, {P::D(R, 0, 15, 1), P::S(R, 0, 0xe4, 11), P::S(C, 0, 0xe4, 12), P::S(V, 1, 0xe4, 8)})
        .op(TexDepth, {P::D(R, 5)})
        .end()});
    unsigned failed = 0;
    for (const auto &c : cases)
    {
        char error[8192] = {};
        if (!HostMetal::CompilePixelShader(c.tokens.data(), unsigned(c.tokens.size()), error, sizeof error))
        {
            printf("FAIL %s: %s\n", c.name, error);
            ++failed;
        }
        else printf("ok   %s\n", c.name);
    }
    // Vertex shaders 1.1: every instruction, relative constants through a0,
    // every output register, a def and a dcl of DirectX 9.
    {
        enum : unsigned { VR = 0, VV = 1, VC = 2, VA = 3, VRast = 4, VAttr = 5, VTex = 6 };
        auto d = [](unsigned type, unsigned n, unsigned mask = 15) {
            return 0x80000000u | ((type & 7) << 28) | (mask << 16) | n;
        };
        auto s = [](unsigned type, unsigned n, unsigned swizzle = 0xe4, unsigned modifier = 0, bool relative = false) {
            return 0x80000000u | ((type & 7) << 28) | (swizzle << 16) | (modifier << 24) | (relative ? 0x2000u : 0) | n;
        };
        std::vector<unsigned> t = {0xfffe0101u};
        auto op = [&](unsigned opcode, std::initializer_list<unsigned> params) { t.push_back(opcode); t.insert(t.end(), params); };
        op(31, {0x80000000u, d(VV, 0)});                                      // dcl_position v0
        op(81, {d(VC, 95), 0x3f800000u, 0, 0, 0x3f800000u});                  // def c95
        op(20, {d(VRast, 0), s(VV, 0), s(VC, 0)});                            // m4x4 oPos
        op(1, {d(VA, 0, 1), s(VV, 1, 0x00)});                                 // mov a0.x
        op(4, {d(VR, 0), s(VV, 1), s(VC, 4, 0xe4, 0, true), s(VC, 95)});      // mad with c[a0.x + 4]
        op(2, {d(VR, 1), s(VR, 0), s(VV, 2, 0xe4, 1)});                       // add
        op(3, {d(VR, 1, 7), s(VR, 1), s(VC, 5)});                             // sub
        op(5, {d(VR, 2), s(VR, 1), s(VR, 0)});                                // mul
        op(6, {d(VR, 3, 1), s(VR, 2, 0xff)});                                 // rcp
        op(7, {d(VR, 3, 2), s(VR, 2, 0xaa)});                                 // rsq
        op(8, {d(VR, 4, 1), s(VR, 1), s(VR, 2)});                             // dp3
        op(9, {d(VR, 4, 2), s(VR, 1), s(VR, 2)});                             // dp4
        op(10, {d(VR, 5), s(VR, 1), s(VR, 2)});                               // min
        op(11, {d(VR, 6), s(VR, 1), s(VR, 2)});                               // max
        op(12, {d(VR, 7), s(VR, 5), s(VR, 6)});                               // slt
        op(13, {d(VR, 8), s(VR, 5), s(VR, 6)});                               // sge
        op(14, {d(VR, 9), s(VR, 3, 0x00)});                                   // exp
        op(15, {d(VR, 9, 2), s(VR, 3, 0x55)});                                // log
        op(78, {d(VR, 10), s(VR, 9, 0x00)});                                  // expp
        op(79, {d(VR, 11), s(VR, 9, 0x55)});                                  // logp
        op(16, {d(VR, 10), s(VR, 4)});                                        // lit
        op(17, {d(VR, 11), s(VR, 10), s(VR, 4)});                             // dst
        op(19, {d(VR, 11, 8), s(VR, 11)});                                    // frc
        op(21, {d(VR, 0, 7), s(VV, 3), s(VC, 8)});                            // m4x3
        op(22, {d(VR, 1), s(VV, 3), s(VC, 8)});                               // m3x4
        op(23, {d(VR, 2, 7), s(VV, 3), s(VC, 8)});                            // m3x3
        op(24, {d(VR, 3, 3), s(VV, 3), s(VC, 8)});                            // m3x2
        op(1, {d(VAttr, 0), s(VR, 7)});                                       // oD0
        op(1, {d(VAttr, 1), s(VR, 8)});                                       // oD1
        op(1, {d(VTex, 0), s(VR, 0)});                                        // oT0
        op(1, {d(VTex, 7, 3), s(VR, 3)});                                     // oT7.xy
        op(1, {d(VRast, 1, 1), s(VR, 11, 0xff)});                             // oFog
        op(1, {d(VRast, 2, 1), s(VR, 11, 0xaa)});                             // oPts
        op(0, {});
        t.push_back(0xffff);
        char error[8192] = {};
        if (!HostMetal::CompileVertexShader(t.data(), unsigned(t.size()), error, sizeof error))
        {
            printf("FAIL vs 1.1 every instruction: %s\n", error);
            ++failed;
        }
        else printf("ok   vs 1.1 every instruction\n");
    }
    // Shader model 2: instruction tokens with their length, dcl, texld and
    // its forms, flow control, integer and boolean constants, a0 and aL.
    {
        auto d = [](unsigned type, unsigned n, unsigned mask = 15, unsigned result = 0) {
            return 0x80000000u | ((type & 7) << 28) | ((type & 0x18) << 8) | (mask << 16) | (result << 20) | n;
        };
        auto s = [](unsigned type, unsigned n, unsigned swizzle = 0xe4, unsigned modifier = 0, bool relative = false) {
            return 0x80000000u | ((type & 7) << 28) | ((type & 0x18) << 8) | (swizzle << 16) | (modifier << 24) | (relative ? 0x2000u : 0) | n;
        };
        enum : unsigned { T2 = 0, In = 1, Cf = 2, Ad = 3, Rast = 4, Attr = 5, TexOut = 6, Ci = 7, ColorOut = 8, Samp = 10, Cb = 14, Lp = 15 };
        auto program = [](unsigned version) { return std::vector<unsigned>{version}; };
        auto op = [](std::vector<unsigned> &t, unsigned opcode, std::initializer_list<unsigned> params, unsigned control = 0) {
            t.push_back(opcode | (control << 16) | (unsigned(params.size()) << 24));
            t.insert(t.end(), params);
        };
        auto f = [](float x) { unsigned u; memcpy(&u, &x, 4); return u; };
        std::vector<unsigned> vs = program(0xfffe0200u);
        op(vs, 31, {0x80000000u, d(In, 0)});                                   // dcl_position v0
        op(vs, 31, {0x8000000au, d(In, 1)});                                   // dcl_color v1
        op(vs, 81, {d(Cf, 200), f(1.0f), f(0.5f), f(0.25f), f(0.0f)});        // def c200
        op(vs, 48, {d(Ci, 0), 4u, 0u, 1u, 0u});                                // defi i0
        op(vs, 47, {d(Cb, 0), 1u});                                            // defb b0
        op(vs, 20, {d(Rast, 0), s(In, 0), s(Cf, 0)});                          // m4x4 oPos
        op(vs, 46, {d(Ad, 0, 1), s(In, 1, 0x00)});                             // mova a0.x
        op(vs, 1, {d(T2, 0), s(Cf, 10, 0xe4, 0, true), s(Ad, 0, 0x00)});       // mov r0, c[a0.x + 10]
        op(vs, 27, {s(Lp, 0), s(Ci, 0)});                                      // loop aL, i0
        op(vs, 2, {d(T2, 1), s(T2, 1), s(Cf, 20, 0xe4, 0, true), s(Lp, 0, 0x00)});   // add r1, r1, c[aL + 20]
        op(vs, 29, {});                                                        // endloop
        op(vs, 38, {s(Ci, 0)});                                                // rep i0
        op(vs, 41, {s(T2, 1, 0x00), s(T2, 0, 0x00)}, 1);                      // if_gt r1.x, r0.x
        op(vs, 44, {});                                                        // break
        op(vs, 43, {});                                                        // endif
        op(vs, 39, {});                                                        // endrep
        op(vs, 40, {s(Cb, 0)});                                                // if b0
        op(vs, 25, {0x80000000u | 0xf0000000u | 1u});                          // call l1
        op(vs, 42, {});                                                        // else
        op(vs, 32, {d(T2, 2), s(T2, 0, 0x00), s(T2, 1, 0x55)});                // pow
        op(vs, 43, {});
        op(vs, 33, {d(T2, 3, 7), s(T2, 0), s(T2, 1)});                         // crs
        op(vs, 34, {d(T2, 4), s(T2, 3)});                                      // sgn
        op(vs, 35, {d(T2, 5), s(T2, 4, 0xe4, 1)});                             // abs
        op(vs, 36, {d(T2, 6), s(T2, 5)});                                      // nrm
        op(vs, 37, {d(T2, 7, 3), s(T2, 6, 0xaa), s(Cf, 30), s(Cf, 31)});       // sincos
        op(vs, 18, {d(T2, 8), s(T2, 7), s(T2, 6), s(T2, 5)});                  // lrp
        op(vs, 1, {d(Attr, 0), s(In, 1)});                                     // oD0
        op(vs, 1, {d(TexOut, 0), s(T2, 8)});                                   // oT0
        op(vs, 28, {});                                                        // ret
        op(vs, 30, {0x80000000u | 0xf0000000u | 1u});                          // label l1
        op(vs, 5, {d(T2, 2), s(T2, 0), s(Cf, 200)});
        op(vs, 28, {});
        vs.push_back(0xffff);
        std::vector<unsigned> ps = program(0xffff0200u);
        op(ps, 31, {0x80000000u, d(Ad, 0, 3)});                                // dcl t0.xy
        op(ps, 31, {0x80000000u, d(In, 0)});                                   // dcl v0
        op(ps, 31, {0x90000000u, d(Samp, 0)});                                 // dcl_2d s0
        op(ps, 31, {0x98000000u, d(Samp, 1)});                                 // dcl_cube s1
        op(ps, 81, {d(Cf, 5), f(0.5f), f(0.5f), f(0.5f), f(1.0f)});
        op(ps, 66, {d(T2, 0), s(Ad, 0), s(Samp, 0)});                          // texld
        op(ps, 66, {d(T2, 1), s(Ad, 0), s(Samp, 0)}, 1);                       // texldp
        op(ps, 66, {d(T2, 2), s(Ad, 0), s(Samp, 1)}, 2);                       // texldb
        op(ps, 90, {d(T2, 3), s(T2, 0), s(T2, 1), s(Cf, 5, 0x00)});            // dp2add
        op(ps, 88, {d(T2, 4), s(T2, 3, 0xe4, 1), s(T2, 0), s(T2, 1)});         // cmp
        op(ps, 65, {d(T2, 4)});                                                // texkill
        op(ps, 4, {d(T2, 5, 15, 1), s(T2, 4), s(In, 0), s(Cf, 5)});            // mad_sat
        op(ps, 6, {d(T2, 6), s(T2, 5, 0xff)});                                 // rcp
        op(ps, 36, {d(T2, 7), s(T2, 6)});                                      // nrm
        op(ps, 1, {d(ColorOut, 0), s(T2, 7)});                                 // oC0
        ps.push_back(0xffff);
        char error[16384] = {};
        if (!HostMetal::CompileVertexShader(vs.data(), unsigned(vs.size()), error, sizeof error)) { printf("FAIL vs_2_0: %s\n", error); ++failed; }
        else printf("ok   vs_2_0 every instruction and flow control\n");
        if (!HostMetal::CompilePixelShader(ps.data(), unsigned(ps.size()), error, sizeof error)) { printf("FAIL ps_2_0: %s\n", error); ++failed; }
        else printf("ok   ps_2_0 texld forms, dp2add, cmp, texkill\n");
    }
    // A version the translation does not take is refused, not guessed at.
    {
        const unsigned tokens[] = {0xffff0300u, 0x0000ffffu};
        char error[256] = {};
        if (HostMetal::CompilePixelShader(tokens, 2, error, sizeof error)) { printf("FAIL ps_3_0 was taken\n"); ++failed; }
        else printf("ok   ps_3_0 refused: %s\n", error);
    }
    if (failed) { printf("D3DSHADER FAIL %u of %zu\n", failed, cases.size()); return 1; }
    printf("D3DSHADER PASS %zu pixel shaders of 1.x, vertex shaders of 1.1 and 2.0, a pixel shader of 2.0 translate and compile\n", cases.size());
    return 0;
}
