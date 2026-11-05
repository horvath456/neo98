// SPDX-License-Identifier: GPL-2.0-or-later
// Pixel shaders 1.1 to 1.4 into MSL. The token format is the one of the
// DirectX 8 DDK (d3d8types.h): a version token, instruction tokens with the
// opcode in the low sixteen bits and D3DSI_COISSUE in bit 30, parameter
// tokens with bit 31 set, the register number in bits 0 to 10, the register
// type in bits 28 to 30, the write mask in bits 16 to 19 of a destination,
// the result modifier in bits 20 to 23 and the shift in bits 24 to 27; a
// source has its swizzle in bits 16 to 23 and its modifier in bits 24 to 27.
// Version 1.x instructions carry no length; the parameter count follows from
// the opcode.
#include "d3d_shader_msl.h"
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>

namespace Direct3D {
static bool Model2ToMsl(const unsigned *tokens, unsigned count, const char *name, bool pixel, std::string &out, std::string &error);
namespace {

enum Opcode : unsigned
{
    Nop = 0, Mov = 1, Add = 2, Sub = 3, Mad = 4, Mul = 5, Dp3 = 8, Dp4 = 9, Lrp = 18,
    TexCoord = 64, TexKill = 65, Tex = 66, TexBem = 67, TexBemL = 68, TexReg2AR = 69, TexReg2GB = 70,
    TexM3x2Pad = 71, TexM3x2Tex = 72, TexM3x3Pad = 73, TexM3x3Tex = 74, TexM3x3Diff = 75, TexM3x3Spec = 76,
    TexM3x3VSpec = 77, Cnd = 80, Def = 81, TexReg2RGB = 82, TexDp3Tex = 83, TexM3x2Depth = 84, TexDp3 = 85,
    TexM3x3 = 86, TexDepth = 87, Cmp = 88, Bem = 89, Phase = 0xfffd, Comment = 0xfffe, End = 0xffff,
};
enum RegisterType : unsigned { Temp = 0, Input = 1, Const = 2, Texture = 3 };

struct Param
{
    unsigned token = 0;
    unsigned type() const { return ((token >> 28) & 7) | ((token >> 8) & 0x18); }
    unsigned number() const { return token & 0x7ff; }
    unsigned mask() const { return (token >> 16) & 15; }
    unsigned result() const { return (token >> 20) & 15; }
    int shift() const { const int s = int((token >> 24) & 15); return s >= 8 ? s - 16 : s; }
    unsigned swizzle() const { return (token >> 16) & 0xff; }
    unsigned modifier() const { return (token >> 24) & 15; }
};

struct Instruction
{
    unsigned opcode = 0;
    bool coissue = false;
    Param dest;
    Param src[3];
    unsigned sources = 0;
    float literal[4] = {};
};

// Destination and sources of an opcode in versions 1.x.
bool Shape(unsigned opcode, unsigned minor, bool &dest, unsigned &sources)
{
    dest = true;
    switch (opcode)
    {
    case Nop: case Phase: dest = false; sources = 0; return true;
    case Mov: sources = 1; return true;
    case Add: case Sub: case Mul: case Dp3: case Dp4: case Bem: sources = 2; return true;
    case Mad: case Lrp: case Cnd: case Cmp: sources = 3; return true;
    case TexCoord: case Tex: sources = minor >= 4 ? 1 : 0; return true;
    case TexKill: case TexDepth: sources = 0; return true;
    case TexBem: case TexBemL: case TexReg2AR: case TexReg2GB: case TexReg2RGB: case TexM3x2Pad: case TexM3x2Tex:
    case TexM3x3Pad: case TexM3x3Tex: case TexM3x3Diff: case TexM3x3VSpec: case TexDp3Tex: case TexDp3: case TexM3x3:
    case TexM3x2Depth:
        sources = 1; return true;
    case TexM3x3Spec: sources = 2; return true;
    default: return false;
    }
}

class Writer
{
  public:
    Writer(unsigned minor, std::string &out) : minor(minor), out(out) {}

    std::string Register(unsigned type, unsigned n) const
    {
        char text[32];
        switch (type)
        {
        case Temp: snprintf(text, sizeof text, "r[%u]", n); break;
        case Input: snprintf(text, sizeof text, "v%u", n); break;
        case Const: snprintf(text, sizeof text, "k[%u]", n); break;
        default: snprintf(text, sizeof text, "t[%u]", n); break;
        }
        return text;
    }

    // A source operand with its swizzle and modifier.
    std::string Source(const Param &p) const
    {
        std::string value = Register(p.type(), p.number());
        const unsigned sw = p.swizzle();
        if (sw != 0xe4)
        {
            static const char names[] = "xyzw";
            value += '.';
            for (unsigned i = 0; i < 4; ++i) value += names[(sw >> (i * 2)) & 3];
        }
        switch (p.modifier())
        {
        case 0: return value;
        case 1: return "(-" + value + ")";
        case 2: return "(" + value + " - 0.5)";
        case 3: return "(0.5 - " + value + ")";
        case 4: return "(2.0 * (" + value + " - 0.5))";
        case 5: return "(-2.0 * (" + value + " - 0.5))";
        case 6: return "(1.0 - " + value + ")";
        case 7: return "(2.0 * " + value + ")";
        case 8: return "(-2.0 * " + value + ")";
        case 9: return "(" + value + " / " + value + ".z)";   // _dz, 1.4 texld and texcrd
        case 10: return "(" + value + " / " + value + ".w)";  // _dw
        case 11: return "abs(" + value + ")";
        case 12: return "(-abs(" + value + "))";
        default: return value;
        }
    }

    // The value of an instruction, before the destination's modifiers.
    bool Value(const Instruction &in, std::string &value, std::string &error)
    {
        std::string s[3];
        for (unsigned i = 0; i < in.sources; ++i) s[i] = Source(in.src[i]);
        switch (in.opcode)
        {
        case Mov: value = s[0]; return true;
        case Add: value = s[0] + " + " + s[1]; return true;
        case Sub: value = s[0] + " - " + s[1]; return true;
        case Mul: value = s[0] + " * " + s[1]; return true;
        case Mad: value = s[0] + " * " + s[1] + " + " + s[2]; return true;
        case Lrp: value = "mix(" + s[2] + ", " + s[1] + ", " + s[0] + ")"; return true;
        case Dp3: value = "float4(dot((" + s[0] + ").xyz, (" + s[1] + ").xyz))"; return true;
        case Dp4: value = "float4(dot(" + s[0] + ", " + s[1] + "))"; return true;
        case Cmp: value = "select(" + s[2] + ", " + s[1] + ", " + s[0] + " >= 0.0)"; return true;
        case Cnd:
            // Before 1.4 the condition is r0.a, whole; 1.4 compares per component.
            if (minor < 4) value = "((" + s[0] + ").w > 0.5 ? " + s[1] + " : " + s[2] + ")";
            else value = "select(" + s[2] + ", " + s[1] + ", " + s[0] + " > 0.5)";
            return true;
        case Bem:
        {
            // dest.xy = src0.xy + the bump matrix of the stage times src1.xy.
            char text[64];
            snprintf(text, sizeof text, "bem(u, %u, ", in.dest.number());
            value = text + s[0] + ", " + s[1] + ")";
            return true;
        }
        default:
            error = "no arithmetic opcode " + std::to_string(in.opcode);
            return false;
        }
    }

    // The statement that stores `value` through the destination's mask,
    // shift, saturation and the register range.
    std::string Store(const Param &d, const std::string &value, unsigned opcode) const
    {
        std::string v = "(" + value + ")";
        const int shift = d.shift();
        if (shift > 0) v = "(" + v + " * " + std::to_string(1 << shift) + ".0)";
        else if (shift < 0) v = "(" + v + " / " + std::to_string(1 << -shift) + ".0)";
        if (d.result() & 1) v = "saturate(" + v + ")";
        else if (opcode != Def) v = "clamp(" + v + ", -range, range)";
        unsigned mask = d.mask();
        if (!mask) mask = 15;
        std::string target = Register(d.type(), d.number());
        if (mask == 15) return target + " = " + v + ";\n";
        static const char names[] = "xyzw";
        std::string swz;
        for (unsigned i = 0; i < 4; ++i) if (mask & (1u << i)) swz += names[i];
        return target + "." + swz + " = " + v + "." + swz + ";\n";
    }

    unsigned minor;
    std::string &out;
};

} // namespace

bool PixelShaderToMsl(const unsigned *tokens, unsigned count, const char *name, std::string &out, std::string &error)
{
    if (!tokens || count < 2) { error = "no tokens"; return false; }
    const unsigned version = tokens[0];
    if ((version >> 16) == 0xffff && ((version >> 8) & 0xff) == 2) return Model2ToMsl(tokens, count, name, true, out, error);
    if ((version >> 16) != 0xffff || ((version >> 8) & 0xff) != 1 || (version & 0xff) < 1 || (version & 0xff) > 4)
    {
        char text[64];
        snprintf(text, sizeof text, "pixel shader version %08x", version);
        error = text;
        return false;
    }
    const unsigned minor = version & 0xff;
    // The instructions.
    std::vector<Instruction> program;
    unsigned at = 1;
    while (at < count)
    {
        const unsigned token = tokens[at++];
        const unsigned opcode = token & 0xffff;
        if (opcode == End) break;
        if (opcode == Comment) { at += (token >> 16) & 0x7fff; continue; }
        Instruction in;
        in.opcode = opcode;
        in.coissue = (token >> 30) & 1;
        if (opcode == Def)
        {
            if (at + 5 > count) { error = "def past the end"; return false; }
            in.dest.token = tokens[at++];
            for (unsigned i = 0; i < 4; ++i) memcpy(&in.literal[i], &tokens[at++], 4);
            program.push_back(in);
            continue;
        }
        bool dest = false;
        unsigned sources = 0;
        if (!Shape(opcode, minor, dest, sources)) { error = "unknown opcode " + std::to_string(opcode); return false; }
        if (at + (dest ? 1 : 0) + sources > count) { error = "instruction past the end"; return false; }
        if (dest) in.dest.token = tokens[at++];
        for (unsigned i = 0; i < sources; ++i) in.src[i].token = tokens[at++];
        in.sources = sources;
        program.push_back(in);
    }

    Writer w(minor, out);
    std::string body;
    char text[256];
    // Texture instructions of 1.1 to 1.3 name the stage by their
    // destination; the 3x2 and 3x3 matrix instructions gather dot products
    // across two or three of them.
    int dots = 0;
    for (size_t i = 0; i < program.size(); ++i)
    {
        const Instruction &in = program[i];
        const unsigned n = in.dest.number();
        std::string src0 = in.sources > 0 ? w.Source(in.src[0]) : std::string();
        switch (in.opcode)
        {
        case Nop: case Phase:
            break;
        case Def:
            snprintf(text, sizeof text, "k[%u] = float4(%.9g, %.9g, %.9g, %.9g);\n", n, in.literal[0], in.literal[1], in.literal[2], in.literal[3]);
            body += text;
            break;
        case Tex:
            if (minor >= 4)
            {
                // texld rN, src: stage N at the coordinates src names.
                snprintf(text, sizeof text, "shader_sample(u, %uu, %s, false, TEXTURE_NAMES)", n, src0.c_str());
                body += w.Store(in.dest, text, Tex);
            }
            else
            {
                snprintf(text, sizeof text, "t[%u] = shader_sample(u, %uu, coords[%u], true, TEXTURE_NAMES);\n", n, n, n);
                body += text;
            }
            break;
        case TexCoord:
            if (minor >= 4) body += w.Store(in.dest, src0, TexCoord);
            else
            {
                snprintf(text, sizeof text, "t[%u] = float4(saturate(coords[%u].xyz), 1.0);\n", n, n);
                body += text;
            }
            break;
        case TexKill:
            // Before 1.4 the coordinates of the stage, from 1.4 a register.
            if (minor >= 4) snprintf(text, sizeof text, "if (any(%s.xyz < 0.0)) discard_fragment();\n", w.Register(in.dest.type(), n).c_str());
            else snprintf(text, sizeof text, "if (any(coords[%u].xyz < 0.0)) discard_fragment();\n", n);
            body += text;
            break;
        case TexBem: case TexBemL:
            snprintf(text, sizeof text, "t[%u] = texbem(u, %uu, coords[%u], %s, %s, TEXTURE_NAMES);\n", n, n, n, src0.c_str(),
                     in.opcode == TexBemL ? "true" : "false");
            body += text;
            break;
        case TexReg2AR:
            snprintf(text, sizeof text, "t[%u] = shader_sample(u, %uu, float4((%s).w, (%s).x, 0.0, 1.0), false, TEXTURE_NAMES);\n", n, n, src0.c_str(), src0.c_str());
            body += text;
            break;
        case TexReg2GB:
            snprintf(text, sizeof text, "t[%u] = shader_sample(u, %uu, float4((%s).y, (%s).z, 0.0, 1.0), false, TEXTURE_NAMES);\n", n, n, src0.c_str(), src0.c_str());
            body += text;
            break;
        case TexReg2RGB:
            snprintf(text, sizeof text, "t[%u] = shader_sample(u, %uu, float4((%s).xyz, 1.0), false, TEXTURE_NAMES);\n", n, n, src0.c_str());
            body += text;
            break;
        case TexDp3Tex:
            snprintf(text, sizeof text, "t[%u] = shader_sample(u, %uu, float4(dot(coords[%u].xyz, (%s).xyz), 0.0, 0.0, 1.0), false, TEXTURE_NAMES);\n",
                     n, n, n, src0.c_str());
            body += text;
            break;
        case TexDp3:
            snprintf(text, sizeof text, "t[%u] = float4(dot(coords[%u].xyz, (%s).xyz));\n", n, n, src0.c_str());
            body += text;
            break;
        case TexM3x2Pad: case TexM3x3Pad:
            snprintf(text, sizeof text, "d[%d] = dot(coords[%u].xyz, (%s).xyz);\n", dots, n, src0.c_str());
            body += text;
            ++dots;
            break;
        case TexM3x2Tex: case TexM3x2Depth:
            snprintf(text, sizeof text, "d[%d] = dot(coords[%u].xyz, (%s).xyz);\n", dots, n, src0.c_str());
            body += text;
            if (in.opcode == TexM3x2Tex)
            {
                snprintf(text, sizeof text, "t[%u] = shader_sample(u, %uu, float4(d[0], d[1], 0.0, 1.0), false, TEXTURE_NAMES);\n", n, n);
                body += text;
            }
            dots = 0;
            break;
        case TexM3x3Tex: case TexM3x3Diff: case TexM3x3: case TexM3x3Spec: case TexM3x3VSpec:
        {
            snprintf(text, sizeof text, "d[%d] = dot(coords[%u].xyz, (%s).xyz);\n", dots, n, src0.c_str());
            body += text;
            if (in.opcode == TexM3x3)
                snprintf(text, sizeof text, "t[%u] = float4(d[0], d[1], d[2], 1.0);\n", n);
            else if (in.opcode == TexM3x3Tex || in.opcode == TexM3x3Diff)
                snprintf(text, sizeof text, "t[%u] = shader_sample(u, %uu, float4(d[0], d[1], d[2], 1.0), false, TEXTURE_NAMES);\n", n, n);
            else
            {
                // The eye vector: a constant, or the w of the three
                // coordinate sets; the normal reflects it.
                std::string eye = in.opcode == TexM3x3Spec ? "(" + w.Source(in.src[1]) + ").xyz"
                                  : "float3(coords[" + std::to_string(n - 2) + "].w, coords[" + std::to_string(n - 1) + "].w, coords[" +
                                        std::to_string(n) + "].w)";
                snprintf(text, sizeof text, "t[%u] = shader_sample(u, %uu, float4(reflect_eye(float3(d[0], d[1], d[2]), %s), 1.0), false, TEXTURE_NAMES);\n",
                         n, n, eye.c_str());
            }
            body += text;
            dots = 0;
            break;
        }
        case TexDepth:
            break;   // the depth of the pixel stays the rasterized one
        default:
        {
            std::string value;
            if (!w.Value(in, value, error)) return false;
            snprintf(text, sizeof text, "float4 value%zu = %s;\n", i, value.c_str());
            body += text;
            // A co-issued pair reads its sources before either writes: the
            // first of a pair waits, and the second stores both.
            const bool paired_next = i + 1 < program.size() && program[i + 1].coissue;
            if (in.coissue && i > 0)
            {
                snprintf(text, sizeof text, "value%zu", i - 1);
                body += w.Store(program[i - 1].dest, text, program[i - 1].opcode);
            }
            snprintf(text, sizeof text, "value%zu", i);
            if (!paired_next) body += w.Store(in.dest, text, in.opcode);
            break;
        }
        }
    }

    out += "static float4 ";
    out += name;
    out += "(float4 v0, float4 v1, thread const float4 *coords, constant FragmentUniforms &u, constant float4 *c, "
           "constant int4 *ci, constant uint *cb, TEXTURES)\n{\n";
    snprintf(text, sizeof text, "    const float range = %.1f;\n", PixelShaderRange);
    out += text;
    out += "    float4 r[6] = {float4(0.0), float4(0.0), float4(0.0), float4(0.0), float4(0.0), float4(0.0)};\n";
    out += "    float4 t[8];\n    for (uint i = 0u; i < 8u; ++i) t[i] = coords[i];\n";
    out += "    float4 k[32];\n    for (uint i = 0u; i < 32u; ++i) k[i] = c[i];\n";
    out += "    float d[3] = {0.0, 0.0, 0.0};\n";
    size_t start = 0;
    while (start < body.size())
    {
        const size_t end = body.find('\n', start);
        out += "    " + body.substr(start, end - start + 1);
        start = end + 1;
    }
    out += "    (void)d; (void)range; (void)ci; (void)cb;\n    return r[0];\n}\n";
    return true;
}

} // namespace Direct3D

// Vertex shaders 1.1. The registers: inputs v0 to v15, temporaries r0 to r11,
// constants c0 up, the address register a0, and the outputs oPos, oFog and
// oPts (RASTOUT), oD0 and oD1 (ATTROUT) and oT0 to oT7 (TEXCRDOUT). A source
// with D3DVS_ADDRMODE_RELATIVE (bit 13) reads the constant a0.x further on.
namespace Direct3D {
namespace {

enum VertexOpcode : unsigned
{
    VNop = 0, VMov = 1, VAdd = 2, VSub = 3, VMad = 4, VMul = 5, VRcp = 6, VRsq = 7, VDp3 = 8, VDp4 = 9, VMin = 10, VMax = 11,
    VSlt = 12, VSge = 13, VExp = 14, VLog = 15, VLit = 16, VDst = 17, VFrc = 19, VM4x4 = 20, VM4x3 = 21, VM3x4 = 22,
    VM3x3 = 23, VM3x2 = 24, VMova = 46, VExpp = 78, VLogp = 79, VDef = 81, VComment = 0xfffe, VEnd = 0xffff,
};
enum VertexRegister : unsigned { VTemp = 0, VInput = 1, VConst = 2, VAddress = 3, VRast = 4, VAttr = 5, VTexCoord = 6 };

unsigned VertexSources(unsigned opcode)
{
    switch (opcode)
    {
    case VNop: return 0;
    case VMov: case VRcp: case VRsq: case VExp: case VLog: case VLit: case VFrc: case VMova: case VExpp: case VLogp: return 1;
    case VMad: return 3;
    case VAdd: case VSub: case VMul: case VDp3: case VDp4: case VMin: case VMax: case VSlt: case VSge: case VDst:
    case VM4x4: case VM4x3: case VM3x4: case VM3x3: case VM3x2:
        return 2;
    default: return ~0u;
    }
}

std::string VertexRegisterName(unsigned type, unsigned n, bool relative)
{
    char text[48];
    switch (type)
    {
    case VTemp: snprintf(text, sizeof text, "r[%u]", n); break;
    case VInput: snprintf(text, sizeof text, "v[%u]", n); break;
    case VConst:
        if (relative) snprintf(text, sizeof text, "k[clamp(int(%u) + a0.x, 0, %d)]", n, int(VertexShaderConstants) - 1);
        else snprintf(text, sizeof text, "k[%u]", n < VertexShaderConstants ? n : VertexShaderConstants - 1);
        break;
    case VAddress: snprintf(text, sizeof text, "a0"); break;
    case VRast: snprintf(text, sizeof text, "%s", n == 0 ? "oPos" : n == 1 ? "oFog" : "oPts"); break;
    case VAttr: snprintf(text, sizeof text, "oD[%u]", n & 1); break;
    case VTexCoord: snprintf(text, sizeof text, "oT[%u]", n & 7); break;
    default: snprintf(text, sizeof text, "r[0]"); break;
    }
    return text;
}

std::string VertexSource(const Param &p)
{
    std::string value = VertexRegisterName(p.type(), p.number(), (p.token >> 13) & 1);
    if (p.type() == VAddress) value = "float4(a0)";
    const unsigned sw = p.swizzle();
    if (sw != 0xe4)
    {
        static const char names[] = "xyzw";
        value += '.';
        for (unsigned i = 0; i < 4; ++i) value += names[(sw >> (i * 2)) & 3];
    }
    if (p.modifier() == 1) return "(-" + value + ")";
    return value;
}

} // namespace

bool VertexShaderToMsl(const unsigned *tokens, unsigned count, const char *name, std::string &out, std::string &error)
{
    if (!tokens || count < 2) { error = "no tokens"; return false; }
    const unsigned version = tokens[0];
    if ((version >> 16) == 0xfffe && ((version >> 8) & 0xff) == 2) return Model2ToMsl(tokens, count, name, false, out, error);
    if (version != 0xfffe0100u && version != 0xfffe0101u)
    {
        char text[64];
        snprintf(text, sizeof text, "vertex shader version %08x", version);
        error = text;
        return false;
    }
    std::string body;
    bool defines = false;
    char text[320];
    unsigned at = 1;
    while (at < count)
    {
        const unsigned token = tokens[at++];
        const unsigned opcode = token & 0xffff;
        if (opcode == VEnd) break;
        if (opcode == VComment) { at += (token >> 16) & 0x7fff; continue; }
        if (opcode == VDef)
        {
            if (at + 5 > count) { error = "def past the end"; return false; }
            const unsigned n = tokens[at] & 0x7ff;
            float f[4];
            memcpy(f, &tokens[at + 1], 16);
            at += 5;
            snprintf(text, sizeof text, "k[%u] = float4(%.9g, %.9g, %.9g, %.9g);\n", n < VertexShaderConstants ? n : 0, f[0], f[1], f[2], f[3]);
            body += text;
            defines = true;
            continue;
        }
        // dcl of a DirectX 9 vs_1_1: the usage token and the register; the
        // declaration of the draw already put the element there.
        if (opcode == 31) { at += 2; continue; }
        const unsigned sources = VertexSources(opcode);
        if (sources == ~0u) { error = "unknown vertex opcode " + std::to_string(opcode); return false; }
        if (opcode == VNop) continue;
        if (at + 1 + sources > count) { error = "instruction past the end"; return false; }
        Param dest;
        dest.token = tokens[at++];
        Param src[3];
        std::string s[3];
        for (unsigned i = 0; i < sources; ++i) { src[i].token = tokens[at++]; s[i] = VertexSource(src[i]); }
        std::string value;
        auto matrix = [&](unsigned rows, bool four) {
            // Rows are consecutive registers from the second source on.
            value = "float4(";
            for (unsigned row = 0; row < 4; ++row)
            {
                if (row) value += ", ";
                if (row >= rows) { value += "0.0"; continue; }
                Param p = src[1];
                p.token = (p.token & ~0x7ffu) | ((p.number() + row) & 0x7ff);
                const std::string m = VertexSource(p);
                value += four ? "dot(" + s[0] + ", " + m + ")" : "dot((" + s[0] + ").xyz, (" + m + ").xyz)";
            }
            value += ")";
        };
        switch (opcode)
        {
        case VMov: case VMova: value = s[0]; break;
        case VAdd: value = s[0] + " + " + s[1]; break;
        case VSub: value = s[0] + " - " + s[1]; break;
        case VMul: value = s[0] + " * " + s[1]; break;
        case VMad: value = s[0] + " * " + s[1] + " + " + s[2]; break;
        case VRcp: value = "float4(vs_rcp((" + s[0] + ").w))"; break;
        case VRsq: value = "float4(vs_rsq((" + s[0] + ").w))"; break;
        case VDp3: value = "float4(dot((" + s[0] + ").xyz, (" + s[1] + ").xyz))"; break;
        case VDp4: value = "float4(dot(" + s[0] + ", " + s[1] + "))"; break;
        case VMin: value = "min(" + s[0] + ", " + s[1] + ")"; break;
        case VMax: value = "max(" + s[0] + ", " + s[1] + ")"; break;
        case VSlt: value = "select(float4(0.0), float4(1.0), " + s[0] + " < " + s[1] + ")"; break;
        case VSge: value = "select(float4(0.0), float4(1.0), " + s[0] + " >= " + s[1] + ")"; break;
        case VExp: value = "float4(exp2((" + s[0] + ").w))"; break;
        case VLog: value = "float4(vs_log((" + s[0] + ").w))"; break;
        case VExpp: value = "vs_expp((" + s[0] + ").w)"; break;
        case VLogp: value = "vs_logp((" + s[0] + ").w)"; break;
        case VLit: value = "vs_lit(" + s[0] + ")"; break;
        case VDst: value = "float4(1.0, (" + s[0] + ").y * (" + s[1] + ").y, (" + s[0] + ").z, (" + s[1] + ").w)"; break;
        case VFrc: value = "fract(" + s[0] + ")"; break;
        case VM4x4: matrix(4, true); break;
        case VM4x3: matrix(3, true); break;
        case VM3x4: matrix(4, false); break;
        case VM3x3: matrix(3, false); break;
        case VM3x2: matrix(2, false); break;
        default: error = "vertex opcode " + std::to_string(opcode); return false;
        }
        // The destination through its write mask; a0 takes the integer part
        // (mov floors in 1.1, mova rounds).
        unsigned mask = dest.mask();
        if (!mask) mask = 15;
        static const char names[] = "xyzw";
        std::string swz;
        for (unsigned i = 0; i < 4; ++i) if (mask & (1u << i)) swz += names[i];
        if (dest.type() == VAddress)
            value = opcode == VMova ? "int4(rint(" + value + "))" : "int4(floor(" + value + "))";
        const std::string target = VertexRegisterName(dest.type(), dest.number(), false);
        if (mask == 15) snprintf(text, sizeof text, "%s = %s;\n", target.c_str(), value.c_str());
        else snprintf(text, sizeof text, "%s.%s = (%s).%s;\n", target.c_str(), swz.c_str(), value.c_str(), swz.c_str());
        body += text;
    }
    out += "static void ";
    out += name;
    out += "(thread const float4 *v, constant float4 *c, constant int4 *ci, constant uint *cb, thread float4 &oPos, "
           "thread float4 *oD, thread float4 *oT, thread float4 &oFog, thread float4 &oPts)\n{\n";
    out += "    float4 r[12];\n    for (uint i = 0u; i < 12u; ++i) r[i] = float4(0.0);\n    int4 a0 = int4(0);\n";
    if (defines)
    {
        snprintf(text, sizeof text, "    float4 k[%u];\n    for (uint i = 0u; i < %uu; ++i) k[i] = c[i];\n", VertexShaderConstants, VertexShaderConstants);
        out += text;
    }
    else out += "    constant float4 *k = c;\n";
    size_t start = 0;
    while (start < body.size())
    {
        const size_t end = body.find('\n', start);
        out += "    " + body.substr(start, end - start + 1);
        start = end + 1;
    }
    out += "    (void)a0; (void)ci; (void)cb;\n}\n";
    return true;
}

} // namespace Direct3D

// Shader model 2: vs_2_0 and ps_2_0 with their static flow control. Every
// instruction token carries the count of the tokens after it in bits 24 to
// 27; a relatively addressed source (bit 13) is followed by a token naming
// the address register (a0 or aL) and its component. The dcl of a pixel
// shader names the type of each sampler; texld takes the sampler as its
// second source, texldp (control 1) divides by w, texldb (2) biases by w.
namespace Direct3D {
namespace {

enum Model2Opcode : unsigned
{
    M2Nop = 0, M2Mov = 1, M2Add = 2, M2Sub = 3, M2Mad = 4, M2Mul = 5, M2Rcp = 6, M2Rsq = 7, M2Dp3 = 8, M2Dp4 = 9,
    M2Min = 10, M2Max = 11, M2Slt = 12, M2Sge = 13, M2Exp = 14, M2Log = 15, M2Lit = 16, M2Dst = 17, M2Lrp = 18,
    M2Frc = 19, M2M4x4 = 20, M2M4x3 = 21, M2M3x4 = 22, M2M3x3 = 23, M2M3x2 = 24, M2Call = 25, M2CallNz = 26,
    M2Loop = 27, M2Ret = 28, M2EndLoop = 29, M2Label = 30, M2Dcl = 31, M2Pow = 32, M2Crs = 33, M2Sgn = 34,
    M2Abs = 35, M2Nrm = 36, M2SinCos = 37, M2Rep = 38, M2EndRep = 39, M2If = 40, M2IfC = 41, M2Else = 42,
    M2EndIf = 43, M2Break = 44, M2BreakC = 45, M2Mova = 46, M2DefB = 47, M2DefI = 48, M2TexKill = 65, M2Tex = 66,
    M2Def = 81, M2Cmp = 88, M2Dp2Add = 90, M2Dsx = 91, M2Dsy = 92, M2TexLdl = 95, M2Comment = 0xfffe, M2End = 0xffff,
};
enum Model2Register : unsigned
{
    R2Temp = 0, R2Input = 1, R2Const = 2, R2AddrTexture = 3, R2Rast = 4, R2Attr = 5, R2TexCoordOut = 6,
    R2ConstInt = 7, R2ColorOut = 8, R2DepthOut = 9, R2Sampler = 10, R2ConstBool = 14, R2Loop = 15,
};

struct Model2
{
    bool pixel = false;
    unsigned constants = 0;
    std::string error;

    std::string Name(unsigned type, unsigned n, const std::string &relative) const
    {
        char text[96];
        switch (type)
        {
        case R2Temp: snprintf(text, sizeof text, "r[%u]", n & 31); break;
        case R2Input: snprintf(text, sizeof text, "v[%u]", n & 15); break;
        case R2Const:
            if (!relative.empty()) snprintf(text, sizeof text, "k[clamp(int(%u) + %s, 0, %u)]", n, relative.c_str(), constants - 1);
            else snprintf(text, sizeof text, "k[%u]", n < constants ? n : constants - 1);
            break;
        case R2AddrTexture:
            if (pixel) snprintf(text, sizeof text, "t[%u]", n & 7);
            else snprintf(text, sizeof text, "float4(a0)");
            break;
        case R2Rast: snprintf(text, sizeof text, "%s", n == 0 ? "oPos" : n == 1 ? "oFog" : "oPts"); break;
        case R2Attr: snprintf(text, sizeof text, "oD[%u]", n & 1); break;
        case R2TexCoordOut: snprintf(text, sizeof text, "oT[%u]", n & 7); break;
        case R2ConstInt: snprintf(text, sizeof text, "float4(ki[%u])", n & 15); break;
        case R2ColorOut: snprintf(text, sizeof text, "oC[%u]", n & 3); break;
        case R2DepthOut: snprintf(text, sizeof text, "oDepth"); break;
        case R2ConstBool: snprintf(text, sizeof text, "float4(float(kb[%u]))", n & 15); break;
        case R2Loop: snprintf(text, sizeof text, "float4(float(aL))"); break;
        default: snprintf(text, sizeof text, "r[0]"); break;
        }
        return text;
    }
    // A destination register as an lvalue.
    std::string Target(unsigned type, unsigned n) const
    {
        char text[32];
        switch (type)
        {
        case R2Temp: snprintf(text, sizeof text, "r[%u]", n & 31); return text;
        case R2AddrTexture: return pixel ? "r[31]" : "a0";
        case R2Rast: return n == 0 ? "oPos" : n == 1 ? "oFog" : "oPts";
        case R2Attr: snprintf(text, sizeof text, "oD[%u]", n & 1); return text;
        case R2TexCoordOut: snprintf(text, sizeof text, "oT[%u]", n & 7); return text;
        case R2ColorOut: snprintf(text, sizeof text, "oC[%u]", n & 3); return text;
        case R2DepthOut: return "oDepth";
        default: return "r[31]";
        }
    }
};

} // namespace

static bool Model2ToMsl(const unsigned *tokens, unsigned count, const char *name, bool pixel, std::string &out, std::string &error)
{
    Model2 m;
    m.pixel = pixel;
    m.constants = pixel ? PixelShaderConstants2 : VertexShaderConstants2;
    // The instructions, with the label bodies kept apart for calls.
    struct Op
    {
        unsigned opcode = 0, control = 0;
        std::vector<unsigned> params;
    };
    std::vector<Op> ops;
    for (unsigned at = 1; at < count;)
    {
        const unsigned token = tokens[at++];
        const unsigned opcode = token & 0xffff;
        if (opcode == M2End) break;
        if (opcode == M2Comment) { at += (token >> 16) & 0x7fff; continue; }
        if (token & 0x10000000u) { error = "predicated instruction"; return false; }
        const unsigned length = (token >> 24) & 15;
        if (at + length > count) { error = "instruction past the end"; return false; }
        Op op;
        op.opcode = opcode;
        op.control = (token >> 16) & 0xff;
        op.params.assign(tokens + at, tokens + at + length);
        at += length;
        ops.push_back(op);
    }
    // The main program ends at its first ret; each label runs to its ret.
    std::vector<size_t> label_start(2048, size_t(-1));
    size_t main_end = ops.size();
    for (size_t i = 0; i < ops.size(); ++i)
    {
        if (ops[i].opcode == M2Ret && main_end == ops.size()) main_end = i;
        if (ops[i].opcode == M2Label && !ops[i].params.empty()) label_start[ops[i].params[0] & 0x7ff] = i + 1;
    }
    std::string defs, samplers;
    unsigned sampler_kind[16] = {};
    // Reads a source parameter at `p`, with its relative address token.
    auto source = [&](const Op &op, size_t &p) -> std::string {
        if (p >= op.params.size()) { m.error = "missing source"; return "float4(0.0)"; }
        Param s;
        s.token = op.params[p++];
        std::string relative;
        if (s.token & 0x2000u)
        {
            if (!pixel && p < op.params.size())
            {
                Param a;
                a.token = op.params[p++];
                static const char comp[] = "xyzw";
                const unsigned c = (a.token >> 16) & 3;
                relative = a.type() == R2Loop ? "aL" : std::string("a0.") + comp[c];
            }
            else relative = "0";
        }
        std::string value = m.Name(s.type(), s.number(), relative);
        const unsigned sw = s.swizzle();
        if (sw != 0xe4)
        {
            static const char names[] = "xyzw";
            value += '.';
            for (unsigned i = 0; i < 4; ++i) value += names[(sw >> (i * 2)) & 3];
        }
        switch (s.modifier())
        {
        case 1: return "(-" + value + ")";
        case 11: return "abs(" + value + ")";
        case 12: return "(-abs(" + value + "))";
        default: return value;
        }
    };
    auto store = [&](const Param &d, const std::string &value) {
        unsigned mask = d.mask();
        if (!mask) mask = 15;
        std::string v = "(" + value + ")";
        if (d.result() & 1) v = "saturate(" + v + ")";
        const std::string target = m.Target(d.type(), d.number());
        if (target == "a0") v = "int4(" + std::string(pixel ? "floor" : "rint") + "(" + v + "))";
        if (target == "oDepth") return "oDepth = (" + v + ").x;\n";
        if (mask == 15) return target + " = " + v + ";\n";
        static const char names[] = "xyzw";
        std::string swz;
        for (unsigned i = 0; i < 4; ++i) if (mask & (1u << i)) swz += names[i];
        return target + "." + swz + " = " + v + "." + swz + ";\n";
    };
    // Emits the instructions [from, to); calls inline their label, to a
    // depth of four as the models allow.
    std::function<bool(size_t, size_t, unsigned, std::string &)> emit = [&](size_t from, size_t to, unsigned depth, std::string &body) -> bool {
        char text[512];
        for (size_t i = from; i < to; ++i)
        {
            const Op &op = ops[i];
            if (op.opcode == M2Ret || op.opcode == M2Label) break;
            size_t p = 0;
            Param d;
            auto dest = [&]() { if (p < op.params.size()) d.token = op.params[p++]; };
            auto scalar = [&](const std::string &s) { return "(" + s + ").x"; };
            switch (op.opcode)
            {
            case M2Nop: break;
            case M2Dcl:
            {
                if (op.params.size() < 2) break;
                Param reg;
                reg.token = op.params[1];
                if (reg.type() == R2Sampler) sampler_kind[reg.number() & 15] = (op.params[0] >> 27) & 15;
                break;
            }
            case M2Def:
            {
                if (op.params.size() < 5) { error = "def"; return false; }
                float f[4];
                memcpy(f, &op.params[1], 16);
                snprintf(text, sizeof text, "k[%u] = float4(%.9g, %.9g, %.9g, %.9g);\n", op.params[0] & 0x7ff, f[0], f[1], f[2], f[3]);
                defs += text;
                break;
            }
            case M2DefI:
            {
                if (op.params.size() < 5) { error = "defi"; return false; }
                snprintf(text, sizeof text, "ki[%u] = int4(%d, %d, %d, %d);\n", op.params[0] & 15, int(op.params[1]), int(op.params[2]),
                         int(op.params[3]), int(op.params[4]));
                defs += text;
                break;
            }
            case M2DefB:
            {
                if (op.params.size() < 2) { error = "defb"; return false; }
                snprintf(text, sizeof text, "kb[%u] = %uu;\n", op.params[0] & 15, op.params[1] ? 1u : 0u);
                defs += text;
                break;
            }
            case M2If:
            {
                Param b;
                b.token = op.params.empty() ? 0 : op.params[0];
                snprintf(text, sizeof text, "if (kb[%u] != 0u) {\n", b.number() & 15);
                body += text;
                break;
            }
            case M2IfC: case M2BreakC:
            {
                const std::string a = source(op, p), b = source(op, p);
                static const char *compare[7] = {"!=", ">", "==", ">=", "<", "!=", "<="};
                const char *c = op.control < 7 ? compare[op.control] : "!=";
                if (op.opcode == M2IfC) body += "if (" + scalar(a) + " " + c + " " + scalar(b) + ") {\n";
                else body += "if (" + scalar(a) + " " + c + " " + scalar(b) + ") break;\n";
                break;
            }
            case M2Else: body += "} else {\n"; break;
            case M2EndIf: case M2EndRep: body += "}\n"; break;
            case M2EndLoop: body += "}}\n"; break;
            case M2Break: body += "break;\n"; break;
            case M2Rep:
            {
                Param n;
                n.token = op.params.empty() ? 0 : op.params[0];
                snprintf(text, sizeof text, "for (int rep%zu = 0; rep%zu < ki[%u].x; ++rep%zu) {\n", i, i, n.number() & 15, i);
                body += text;
                break;
            }
            case M2Loop:
            {
                Param n;
                n.token = op.params.size() > 1 ? op.params[1] : 0;
                const unsigned k = n.number() & 15;
                snprintf(text, sizeof text, "{ int aL = ki[%u].y; for (int loop%zu = 0; loop%zu < ki[%u].x; ++loop%zu, aL += ki[%u].z) {\n",
                         k, i, i, k, i, k);
                body += text;
                break;
            }
            case M2Call: case M2CallNz:
            {
                if (op.params.empty() || depth >= 4) { error = "call"; return false; }
                const unsigned label = op.params[0] & 0x7ff;
                if (label_start[label] == size_t(-1)) { error = "call of a missing label"; return false; }
                if (op.opcode == M2CallNz)
                {
                    Param b;
                    b.token = op.params.size() > 1 ? op.params[1] : 0;
                    snprintf(text, sizeof text, "if (kb[%u] != 0u) {\n", b.number() & 15);
                    body += text;
                }
                else body += "{\n";
                if (!emit(label_start[label], ops.size(), depth + 1, body)) return false;
                body += "}\n";
                break;
            }
            case M2TexKill:
            {
                Param reg;
                reg.token = op.params.empty() ? 0 : op.params[0];
                body += "if (any((" + m.Name(reg.type(), reg.number(), "") + ").xyz < 0.0)) discard_fragment();\n";
                break;
            }
            case M2Tex: case M2TexLdl:
            {
                dest();
                const std::string coord = source(op, p);
                Param sampler;
                sampler.token = p < op.params.size() ? op.params[p++] : 0;
                const unsigned stage = sampler.number() & 7;
                std::string c = coord;
                if (op.opcode == M2Tex && op.control == 1) c = "(" + coord + " / (" + coord + ").w)";
                const char *mode = op.opcode == M2TexLdl ? "2u" : op.opcode == M2Tex && op.control == 2 ? "1u" : "0u";
                snprintf(text, sizeof text, "shader_sample2(u, %uu, %s, %s, TEXTURE_NAMES)", stage, c.c_str(), mode);
                body += store(d, text);
                break;
            }
            default:
            {
                dest();
                std::string s[4];
                size_t n = 0;
                while (p < op.params.size() && n < 4) s[n++] = source(op, p);
                std::string value;
                auto matrix = [&](unsigned rows, bool four) {
                    // Rows are consecutive registers from the second source on.
                    Param base;
                    base.token = op.params.size() > 2 ? op.params[2] : 0;
                    value = "float4(";
                    for (unsigned row = 0; row < 4; ++row)
                    {
                        if (row) value += ", ";
                        if (row >= rows) { value += "0.0"; continue; }
                        const std::string mrow = m.Name(base.type(), base.number() + row, "");
                        value += four ? "dot(" + s[0] + ", " + mrow + ")" : "dot((" + s[0] + ").xyz, (" + mrow + ").xyz)";
                    }
                    value += ")";
                };
                switch (op.opcode)
                {
                case M2Mov: case M2Mova: value = s[0]; break;
                case M2Add: value = s[0] + " + " + s[1]; break;
                case M2Sub: value = s[0] + " - " + s[1]; break;
                case M2Mul: value = s[0] + " * " + s[1]; break;
                case M2Mad: value = s[0] + " * " + s[1] + " + " + s[2]; break;
                case M2Rcp: value = "float4(vs_rcp(" + scalar(s[0]) + "))"; break;
                case M2Rsq: value = "float4(vs_rsq(" + scalar(s[0]) + "))"; break;
                case M2Dp3: value = "float4(dot((" + s[0] + ").xyz, (" + s[1] + ").xyz))"; break;
                case M2Dp4: value = "float4(dot(" + s[0] + ", " + s[1] + "))"; break;
                case M2Min: value = "min(" + s[0] + ", " + s[1] + ")"; break;
                case M2Max: value = "max(" + s[0] + ", " + s[1] + ")"; break;
                case M2Slt: value = "select(float4(0.0), float4(1.0), " + s[0] + " < " + s[1] + ")"; break;
                case M2Sge: value = "select(float4(0.0), float4(1.0), " + s[0] + " >= " + s[1] + ")"; break;
                case M2Exp: value = "float4(exp2(" + scalar(s[0]) + "))"; break;
                case M2Log: value = "float4(vs_log(" + scalar(s[0]) + "))"; break;
                case M2Lit: value = "vs_lit(" + s[0] + ")"; break;
                case M2Dst: value = "float4(1.0, (" + s[0] + ").y * (" + s[1] + ").y, (" + s[0] + ").z, (" + s[1] + ").w)"; break;
                case M2Lrp: value = "mix(" + s[2] + ", " + s[1] + ", " + s[0] + ")"; break;
                case M2Frc: value = "fract(" + s[0] + ")"; break;
                case M2M4x4: matrix(4, true); break;
                case M2M4x3: matrix(3, true); break;
                case M2M3x4: matrix(4, false); break;
                case M2M3x3: matrix(3, false); break;
                case M2M3x2: matrix(2, false); break;
                case M2Pow: value = "float4(pow(abs(" + scalar(s[0]) + "), " + scalar(s[1]) + "))"; break;
                case M2Crs: value = "float4(cross((" + s[0] + ").xyz, (" + s[1] + ").xyz), 0.0)"; break;
                case M2Sgn: value = "sign(" + s[0] + ")"; break;
                case M2Abs: value = "abs(" + s[0] + ")"; break;
                case M2Nrm: value = "vs_nrm(" + s[0] + ")"; break;
                case M2SinCos: value = "float4(cos(" + scalar(s[0]) + "), sin(" + scalar(s[0]) + "), 0.0, 0.0)"; break;
                case M2Cmp: value = "select(" + s[2] + ", " + s[1] + ", " + s[0] + " >= 0.0)"; break;
                case M2Dp2Add: value = "float4(dot((" + s[0] + ").xy, (" + s[1] + ").xy) + " + scalar(s[2]) + ")"; break;
                case M2Dsx: value = "dfdx(" + s[0] + ")"; break;
                case M2Dsy: value = "dfdy(" + s[0] + ")"; break;
                default:
                    error = "shader model 2 opcode " + std::to_string(op.opcode);
                    return false;
                }
                body += store(d, value);
                break;
            }
            }
            if (!m.error.empty()) { error = m.error; return false; }
        }
        return true;
    };
    std::string body;
    if (!emit(0, main_end, 0, body)) return false;
    // Samplers: the type the dcl named, 2D when none did.
    (void)sampler_kind;
    out += pixel ? "static float4 " : "static void ";
    out += name;
    if (pixel)
        out += "(float4 v0, float4 v1, thread const float4 *coords, constant FragmentUniforms &u, constant float4 *c, "
               "constant int4 *ci, constant uint *cb, TEXTURES)\n{\n"
               "    float4 v[2] = {v0, v1};\n"
               "    float4 t[8];\n    for (uint i = 0u; i < 8u; ++i) t[i] = coords[i];\n"
               "    float4 oC[4] = {float4(0.0), float4(0.0), float4(0.0), float4(0.0)};\n    float oDepth = 0.0;\n";
    else
        out += "(thread const float4 *v, constant float4 *c, constant int4 *ci, constant uint *cb, thread float4 &oPos, "
               "thread float4 *oD, thread float4 *oT, thread float4 &oFog, thread float4 &oPts)\n{\n"
               "    int4 a0 = int4(0);\n";
    char text[160];
    snprintf(text, sizeof text, "    float4 r[32];\n    for (uint i = 0u; i < 32u; ++i) r[i] = float4(0.0);\n"
                                "    float4 k[%u];\n    for (uint i = 0u; i < %uu; ++i) k[i] = c[i];\n", m.constants, m.constants);
    out += text;
    out += "    int4 ki[16];\n    uint kb[16];\n    for (uint i = 0u; i < 16u; ++i) { ki[i] = ci[i]; kb[i] = cb[i]; }\n";
    auto indent = [&](const std::string &text) {
        size_t start = 0;
        while (start < text.size())
        {
            const size_t end = text.find('\n', start);
            out += "    " + text.substr(start, end - start + 1);
            start = end + 1;
        }
    };
    indent(defs);
    indent(body);
    out += pixel ? "    (void)oDepth; (void)v;\n    return oC[0];\n}\n" : "    (void)a0;\n}\n";
    return true;
}

} // namespace Direct3D
