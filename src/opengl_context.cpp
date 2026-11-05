// SPDX-License-Identifier: GPL-2.0-or-later
// The OpenGL 1.1 state machine of the NEO98 installable client driver. See
// src/opengl_context.h for the interface.
#include "opengl_context.h"
#include "win98_opengl_dispatch.h"
#include "render_profile.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <string>

namespace OpenGL
{
namespace
{
using U = unsigned;
using S = Win98OpenGL::Slot;

// The enumerants this file uses, from gl.h of OpenGL 1.1.
namespace E
{
constexpr U Points = 0, Lines = 1, LineLoop = 2, LineStrip = 3, Triangles = 4, TriangleStrip = 5,
            TriangleFan = 6, Quads = 7, QuadStrip = 8, Polygon = 9;
constexpr U Never = 0x200, Always = 0x207, Less = 0x201, LessEqual = 0x203;
constexpr U Zero = 0, One = 1, SrcAlpha = 0x302, OneMinusSrcAlpha = 0x303;
constexpr U Front = 0x404, Back = 0x405, FrontAndBack = 0x408, FrontLeft = 0x400, BackLeft = 0x402;
constexpr U InvalidEnum = 0x500, InvalidValue = 0x501, InvalidOperation = 0x502, StackOverflow = 0x503,
            StackUnderflow = 0x504, OutOfMemory = 0x505;
constexpr U Cw = 0x900, Ccw = 0x901;
constexpr U Point = 0x1b00, Line = 0x1b01, Fill = 0x1b02;
constexpr U Flat = 0x1d00, Smooth = 0x1d01;
constexpr U Modelview = 0x1700, Projection = 0x1701, Texture = 0x1702;
constexpr U Color = 0x1800, Depth = 0x1801, Stencil = 0x1802;
constexpr U Compile = 0x1300, CompileAndExecute = 0x1301;
constexpr U Byte = 0x1400, UnsignedByte = 0x1401, Short = 0x1402, UnsignedShort = 0x1403, Int = 0x1404,
            UnsignedInt = 0x1405, Float = 0x1406, Bytes2 = 0x1407, Bytes3 = 0x1408, Bytes4 = 0x1409,
            Double = 0x140a, Bitmap = 0x1a00;
constexpr U ColorIndex = 0x1900, StencilIndex = 0x1901, DepthComponent = 0x1902, Red = 0x1903, Green = 0x1904,
            Blue = 0x1905, Alpha = 0x1906, Rgb = 0x1907, Rgba = 0x1908, Luminance = 0x1909,
            LuminanceAlpha = 0x190a, Bgr = 0x80e0, Bgra = 0x80e1, Intensity = 0x8049;
constexpr U Texture1D = 0xde0, Texture2D = 0xde1;
constexpr U Nearest = 0x2600, Linear = 0x2601, NearestMipmapNearest = 0x2700, LinearMipmapNearest = 0x2701,
            NearestMipmapLinear = 0x2702, LinearMipmapLinear = 0x2703;
constexpr U MagFilter = 0x2800, MinFilter = 0x2801, WrapS = 0x2802, WrapT = 0x2803, Clamp = 0x2900,
            Repeat = 0x2901, BorderColor = 0x1004, TexturePriority = 0x8066, TextureResident = 0x8067;
constexpr U TextureEnvMode = 0x2200, TextureEnvColor = 0x2201, TextureEnv = 0x2300, Modulate = 0x2100,
            Decal = 0x2101, Blend = 0xbe2, Replace = 0x1e01, Add = 0x104;
constexpr U TextureGenMode = 0x2500, ObjectPlane = 0x2501, EyePlane = 0x2502, EyeLinear = 0x2400,
            ObjectLinear = 0x2401, SphereMap = 0x2402, S_ = 0x2000, T_ = 0x2001, R_ = 0x2002, Q_ = 0x2003;
constexpr U Ambient = 0x1200, Diffuse = 0x1201, Specular = 0x1202, Position = 0x1203, SpotDirection = 0x1204,
            SpotExponent = 0x1205, SpotCutoff = 0x1206, ConstantAttenuation = 0x1207,
            LinearAttenuation = 0x1208, QuadraticAttenuation = 0x1209, Emission = 0x1600, Shininess = 0x1601,
            AmbientAndDiffuse = 0x1602, ColorIndexes = 0x1603;
constexpr U LightModelLocalViewer = 0xb51, LightModelTwoSide = 0xb52, LightModelAmbient = 0xb53;
constexpr U FogIndex = 0xb61, FogDensity = 0xb62, FogStart = 0xb63, FogEnd = 0xb64, FogMode = 0xb65,
            FogColor = 0xb66, Exp = 0x800, Exp2 = 0x801;
constexpr U Light0 = 0x4000, ClipPlane0 = 0x3000;
constexpr U ColorBufferBit = 0x4000, DepthBufferBit = 0x100, StencilBufferBit = 0x400, AccumBufferBit = 0x200;
constexpr U Render = 0x1c00, Feedback = 0x1c01, Select = 0x1c02;
constexpr U Keep = 0x1e00, Incr = 0x1e02, Decr = 0x1e03, Invert = 0x150a;
constexpr U VertexArray = 0x8074, NormalArray = 0x8075, ColorArray = 0x8076, IndexArray = 0x8077,
            TextureCoordArray = 0x8078, EdgeFlagArray = 0x8079;
// glEnable capabilities.
constexpr U CapPointSmooth = 0xb10, CapLineSmooth = 0xb20, CapLineStipple = 0xb24, CapPolygonSmooth = 0xb41,
            CapPolygonStipple = 0xb42, CapCullFace = 0xb44, CapLighting = 0xb50, CapColorMaterial = 0xb57,
            CapFog = 0xb60, CapDepthTest = 0xb71, CapStencilTest = 0xb90, CapNormalize = 0xba1,
            CapAlphaTest = 0xbc0, CapDither = 0xbd0, CapBlend = 0xbe2, CapLogicOp = 0xbf1,
            CapColorLogicOp = 0xbf2, CapIndexLogicOp = 0xbf1, CapScissorTest = 0xc11, CapTextureGenS = 0xc60,
            CapTextureGenT = 0xc61, CapTextureGenR = 0xc62, CapTextureGenQ = 0xc63, CapAutoNormal = 0xd80,
            CapPolygonOffsetFill = 0x8037, CapPolygonOffsetLine = 0x2a02, CapPolygonOffsetPoint = 0x2a01;
constexpr U UnpackSwapBytes = 0xcf0, UnpackLsbFirst = 0xcf1, UnpackRowLength = 0xcf2, UnpackSkipRows = 0xcf3,
            UnpackSkipPixels = 0xcf4, UnpackAlignment = 0xcf5, PackSwapBytes = 0xd00, PackLsbFirst = 0xd01,
            PackRowLength = 0xd02, PackSkipRows = 0xd03, PackSkipPixels = 0xd04, PackAlignment = 0xd05;
} // namespace E

float Float(U word) { float f; memcpy(&f, &word, 4); return f; }
U Word(float f) { U w; memcpy(&w, &f, 4); return w; }
double Double(const U *words) { double d; memcpy(&d, words, 8); return d; }
float Clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

// ---------------------------------------------------------------------------
// Matrices, column major as OpenGL keeps them: m[column * 4 + row].
struct Matrix
{
    float m[16];
    static Matrix Identity()
    {
        Matrix r{};
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
        return r;
    }
    Matrix operator*(const Matrix &b) const
    {
        Matrix r{};
        for (int c = 0; c < 4; ++c)
            for (int row = 0; row < 4; ++row)
            {
                float s = 0.0f;
                for (int k = 0; k < 4; ++k) s += m[k * 4 + row] * b.m[c * 4 + k];
                r.m[c * 4 + row] = s;
            }
        return r;
    }
    void Apply(const float in[4], float out[4]) const
    {
        for (int row = 0; row < 4; ++row)
            out[row] = m[row] * in[0] + m[4 + row] * in[1] + m[8 + row] * in[2] + m[12 + row] * in[3];
    }
    bool IsIdentity() const
    {
        for (int i = 0; i < 16; ++i)
            if (m[i] != ((i % 5) == 0 ? 1.0f : 0.0f)) return false;
        return true;
    }
    Matrix Inverse() const
    {
        // Cofactors of the 4x4 matrix; a singular matrix gives the identity.
        const float *a = m;
        float inv[16];
        inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
        inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
        inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
        inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
        inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
        inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
        inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
        inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
        inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
        inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
        inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
        inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
        inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
        inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
        inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
        inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];
        const float det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
        if (det == 0.0f || !std::isfinite(det)) return Identity();
        Matrix r;
        for (int i = 0; i < 16; ++i) r.m[i] = inv[i] / det;
        return r;
    }
};

void Normalize3(float v[3])
{
    const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 0.0f) { v[0] /= l; v[1] /= l; v[2] /= l; }
}

U PackColor(const float c[4])
{
    auto b = [](float v) { return U(Clamp01(v) * 255.0f + 0.5f); };
    return (b(c[3]) << 24) | (b(c[0]) << 16) | (b(c[1]) << 8) | b(c[2]);
}

// ---------------------------------------------------------------------------
// The shared objects: display lists and texture objects. wglShareLists makes
// two contexts use the same set.

// One recorded command of a display list: its slot, its argument words and
// the bytes behind its pointers, copied at compile time as OpenGL demands.
struct Command
{
    U slot = 0;
    U args[14] = {};
    bool packed_image = false;   // images were repacked tightly at compile time
};
struct DisplayList
{
    std::vector<Command> commands;
    std::vector<unsigned char> data;
};
// Pointers inside a list point here; the replay reads them from the list.
constexpr U ListBase = 0xf0000000u;

enum BaseFormat : U { FormatAlpha, FormatLuminance, FormatLuminanceAlpha, FormatIntensity, FormatRgb, FormatRgba };

struct TextureObject
{
    U name = 0;
    U target = 0;
    struct Level
    {
        int width = 0, height = 0;
        std::vector<U> texels;   // 0xAARRGGBB, top row first as OpenGL stores row 0
    };
    static constexpr int MaxLevels = 12;
    Level levels[MaxLevels];
    BaseFormat base = FormatRgba;
    U internal_format = 1;
    U min_filter = E::NearestMipmapLinear, mag_filter = E::Linear, wrap_s = E::Repeat, wrap_t = E::Repeat;
    float border[4] = {0, 0, 0, 0};
    float priority = 1.0f;
    U generation = 1;
    U handle = 0;
    Gpu::Surface surface;
    bool Mipmapped() const { return min_filter != E::Nearest && min_filter != E::Linear; }
    // Level 0 exists, and with a mipmap filter every level down to 1x1.
    int Complete() const
    {
        const auto &l0 = levels[0];
        if (l0.width <= 0 || l0.height <= 0) return 0;
        if (!Mipmapped()) return 1;
        int w = l0.width, h = l0.height, n = 1;
        while (w > 1 || h > 1)
        {
            w = w > 1 ? w / 2 : 1;
            h = h > 1 ? h / 2 : 1;
            if (n >= MaxLevels || levels[n].width != w || levels[n].height != h) return 0;
            ++n;
        }
        return n;
    }
};

struct Shared
{
    std::map<U, std::shared_ptr<DisplayList>> lists;
    std::map<U, std::unique_ptr<TextureObject>> textures;
    U next_handle = 1;
};

// A vertex after the vertex stage: clip coordinates with Z already moved to
// 0..w, colours of both sides, texture coordinates and the fog factor.
struct Processed
{
    float clip[4];
    U color[2];
    float tex[2][4];
    float fog;
    bool edge;
};

// The vertex the backend gets: D3DFVF_XYZW | DIFFUSE | SPECULAR | TEX2 with
// four coordinates. The specular alpha is the fog factor.
struct Output
{
    float position[4];
    U diffuse;
    U specular;
    float tex[2][4];
};
constexpr U OutputFvf = 0x4002 | 0x40 | 0x80 | 0x200 | (2u << 16) | (2u << 18);

// A draw or clear in the queue.
struct Operation
{
    bool clear = false;
    U primitive = 0;          // Gpu::Primitive
    size_t first = 0, count = 0;   // vertices in the queue
    size_t index_first = 0, index_count = 0;
    U state = 0;              // index into the snapshots
    std::array<TextureObject *, 2> textures{};
    U clear_flags = 0, clear_color = 0, clear_stencil = 0;
    float clear_depth = 1.0f;
    int rect[4] = {};
    bool has_rect = false;
};

struct Light
{
    float ambient[4] = {0, 0, 0, 1}, diffuse[4] = {0, 0, 0, 1}, specular[4] = {0, 0, 0, 1};
    float position[4] = {0, 0, 1, 0};        // eye coordinates
    float direction[3] = {0, 0, -1};         // eye coordinates
    float exponent = 0.0f, cutoff = 180.0f;
    float attenuation[3] = {1, 0, 0};
};
struct Material
{
    float ambient[4] = {0.2f, 0.2f, 0.2f, 1}, diffuse[4] = {0.8f, 0.8f, 0.8f, 1};
    float specular[4] = {0, 0, 0, 1}, emission[4] = {0, 0, 0, 1};
    float shininess = 0.0f;
};

struct TextureUnit
{
    float texcoord[4] = {0, 0, 0, 1}, raster_tex[4] = {0, 0, 0, 1};
    bool texture_1d = false, texture_2d = false;
    U bound_1d = 0, bound_2d = 0;
    U env_mode = E::Modulate;
    float env_color[4] = {0, 0, 0, 0};
    bool gen_on[4] = {};
    U gen_mode[4] = {E::EyeLinear, E::EyeLinear, E::EyeLinear, E::EyeLinear};
    float object_plane[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}};
    float eye_plane[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}};
};

// The server attributes glPushAttrib saves, grouped by their bits.
struct Attributes
{
    // GL_CURRENT_BIT
    float color[4] = {1, 1, 1, 1}, normal[3] = {0, 0, 1};
    float raster[4] = {0, 0, 0, 1};   // window position, Z from 0 to 1, and W
    float raster_color[4] = {1, 1, 1, 1};
    bool raster_valid = true, edge = true;
    float index = 0.0f;
    // GL_POINT_BIT, GL_LINE_BIT
    float point_size = 1.0f, line_width = 1.0f;
    U line_pattern = 0xffff, line_repeat = 1;
    bool point_smooth = false, line_smooth = false, line_stipple = false;
    // GL_POLYGON_BIT
    U cull_face = E::Back, front_face = E::Ccw, polygon_mode[2] = {E::Fill, E::Fill};
    float offset_factor = 0.0f, offset_units = 0.0f;
    bool cull = false, polygon_smooth = false, polygon_stipple = false;
    bool offset_fill = false, offset_line = false, offset_point = false;
    // GL_LIGHTING_BIT
    Light lights[8];
    bool light_on[8] = {};
    Material material[2];
    float model_ambient[4] = {0.2f, 0.2f, 0.2f, 1};
    bool local_viewer = false, two_side = false;
    bool lighting = false, color_material = false;
    U color_material_face = E::FrontAndBack, color_material_mode = E::AmbientAndDiffuse;
    U shade = E::Smooth;
    // GL_FOG_BIT
    bool fog = false;
    U fog_mode = E::Exp;
    float fog_density = 1.0f, fog_start = 0.0f, fog_end = 1.0f, fog_color[4] = {0, 0, 0, 0};
    // GL_DEPTH_BUFFER_BIT
    bool depth_test = false, depth_mask = true;
    U depth_func = E::Less;
    float clear_depth = 1.0f;
    // GL_ACCUM_BUFFER_BIT
    float clear_accum[4] = {};
    // GL_STENCIL_BUFFER_BIT
    bool stencil_test = false;
    U stencil_func = E::Always, stencil_ref = 0, stencil_value_mask = ~0u, stencil_write_mask = ~0u;
    U stencil_fail = E::Keep, stencil_zfail = E::Keep, stencil_zpass = E::Keep, clear_stencil = 0;
    // GL_VIEWPORT_BIT
    int viewport[4] = {0, 0, 0, 0};
    float depth_near = 0.0f, depth_far = 1.0f;
    // GL_TRANSFORM_BIT
    U matrix_mode = E::Modelview;
    double clip_planes[6][4] = {};   // eye coordinates
    bool clip_on[6] = {};
    bool normalize = false;
    // GL_COLOR_BUFFER_BIT
    bool alpha_test = false, blend = false, dither = true, logic_op = false;
    U alpha_func = E::Always;
    float alpha_ref = 0.0f;
    U blend_src = E::One, blend_dst = E::Zero, logic = 0x1503;
    float clear_color[4] = {0, 0, 0, 0};
    bool color_mask[4] = {true, true, true, true};
    U draw_buffer = E::Back;
    float clear_index = 0.0f;
    U index_mask = ~0u;
    // GL_HINT_BIT
    U hints[5] = {0x1100, 0x1100, 0x1100, 0x1100, 0x1100};
    // GL_LIST_BIT
    U list_base = 0;
    TextureUnit units[2];
    unsigned active_texture = 0;
    // GL_SCISSOR_BIT
    bool scissor_test = false;
    int scissor[4] = {0, 0, 0, 0};
    // GL_PIXEL_MODE_BIT
    U read_buffer = E::Back;
    float zoom[2] = {1, 1};
    float transfer_scale[4] = {1, 1, 1, 1}, transfer_bias[4] = {0, 0, 0, 0};
    float depth_scale = 1.0f, depth_bias = 0.0f;
    // GL_EVAL_BIT
    bool auto_normal = false;
};

// Client state, glPushClientAttrib.
struct ClientArray
{
    bool enabled = false;
    int size = 4;
    U type = E::Float;
    int stride = 0;
    U pointer = 0;
};
struct Client
{
    ClientArray vertex, normal, color, index, texcoord[2], edge;
    unsigned active_texture = 0;
    int lock_first = 0, lock_count = 0;
    bool arrays_locked = false;
    int unpack_alignment = 4, unpack_row_length = 0, unpack_skip_rows = 0, unpack_skip_pixels = 0;
    bool unpack_lsb_first = false, unpack_swap = false;
    int pack_alignment = 4, pack_row_length = 0, pack_skip_rows = 0, pack_skip_pixels = 0;
    bool pack_lsb_first = false, pack_swap = false;
};

// The attribute families glColor, glNormal, glTexCoord, glVertex,
// glRasterPos, glIndex and glRect, read from their slot names.
enum Family : U { NotAttribute, FamilyColor, FamilyNormal, FamilyTexCoord, FamilyVertex, FamilyRasterPos, FamilyIndex, FamilyRect, FamilyEval1, FamilyEval2 };
struct AttributeCall
{
    Family family = NotAttribute;
    int count = 0;
    U type = 0;      // E::Byte...
    bool vector = false;
};
struct AttributeTable
{
    AttributeCall calls[Win98OpenGL::DispatchCount];
    AttributeTable()
    {
        static const struct { const char *prefix; Family family; } families[] = {
            {"glColor", FamilyColor}, {"glNormal", FamilyNormal}, {"glTexCoord", FamilyTexCoord},
            {"glVertex", FamilyVertex}, {"glRasterPos", FamilyRasterPos}, {"glIndex", FamilyIndex},
            {"glRect", FamilyRect}, {"glEvalCoord1", FamilyEval1}, {"glEvalCoord2", FamilyEval2}};
        for (U i = 0; i < Win98OpenGL::DispatchCount; ++i)
        {
            const char *name = Win98OpenGL::Dispatch[i].name;
            for (const auto &f : families)
            {
                const size_t n = strlen(f.prefix);
                if (strncmp(name, f.prefix, n)) continue;
                const char *rest = name + n;
                AttributeCall c;
                c.family = f.family;
                if (f.family == FamilyEval1 || f.family == FamilyEval2) c.count = f.family == FamilyEval1 ? 1 : 2;
                else if (f.family == FamilyIndex) c.count = 1;
                else if (f.family == FamilyRect) c.count = 4;
                else if (*rest >= '1' && *rest <= '4') c.count = *rest++ - '0';
                else continue;   // glColorMaterial, glColorMask, glColorPointer, glVertexPointer...
                const std::string type = rest;
                const std::string base = !type.empty() && type.back() == 'v' ? type.substr(0, type.size() - 1) : type;
                c.vector = base.size() != type.size();
                if (base == "b") c.type = E::Byte;
                else if (base == "ub") c.type = E::UnsignedByte;
                else if (base == "s") c.type = E::Short;
                else if (base == "us") c.type = E::UnsignedShort;
                else if (base == "i") c.type = E::Int;
                else if (base == "ui") c.type = E::UnsignedInt;
                else if (base == "f") c.type = E::Float;
                else if (base == "d") c.type = E::Double;
                else continue;
                calls[i] = c;
                break;
            }
        }
    }
};
const AttributeTable &Attributes_()
{
    static const AttributeTable table;
    return table;
}

unsigned TypeBytes(U type)
{
    switch (type)
    {
    case E::Byte: case E::UnsignedByte: return 1;
    case E::Short: case E::UnsignedShort: case E::Bytes2: return 2;
    case E::Bytes3: return 3;
    case E::Double: return 8;
    default: return 4;
    }
}

// One component of a client value of `type` at `p`, as a float. Colours and
// normals of integer types are normalized after the rules of OpenGL.
float Component(const unsigned char *p, U type, bool normalized)
{
    switch (type)
    {
    case E::Byte: { const float v = float(int8_t(*p)); return normalized ? (2.0f * v + 1.0f) / 255.0f : v; }
    case E::UnsignedByte: { const float v = float(*p); return normalized ? v / 255.0f : v; }
    case E::Short: { int16_t s; memcpy(&s, p, 2); const float v = float(s); return normalized ? (2.0f * v + 1.0f) / 65535.0f : v; }
    case E::UnsignedShort: { uint16_t s; memcpy(&s, p, 2); const float v = float(s); return normalized ? v / 65535.0f : v; }
    case E::Int: { int32_t s; memcpy(&s, p, 4); return normalized ? float((2.0 * double(s) + 1.0) / 4294967295.0) : float(s); }
    case E::UnsignedInt: { uint32_t s; memcpy(&s, p, 4); return normalized ? float(double(s) / 4294967295.0) : float(s); }
    case E::Double: { double d; memcpy(&d, p, 8); return float(d); }
    default: { float f; memcpy(&f, p, 4); return f; }
    }
}

// The guest of a display list replay: pointers into the list read the list.
class ListGuest final : public Guest
{
  public:
    ListGuest(Guest &outer, const DisplayList &list) : outer(outer), list(list) {}
    bool Read(unsigned address, void *out, unsigned bytes) override
    {
        if (address >= ListBase)
        {
            const size_t at = address - ListBase;
            memset(out, 0, bytes);
            if (at >= list.data.size()) return false;
            memcpy(out, list.data.data() + at, std::min<size_t>(bytes, list.data.size() - at));
            return true;
        }
        return outer.Read(address, out, bytes);
    }
    bool Write(unsigned address, const void *in, unsigned bytes) override { return outer.Write(address, in, bytes); }
    const unsigned char *Span(unsigned address, unsigned bytes) override
    {
        if (address < ListBase) return outer.Span(address, bytes);
        const size_t at = address - ListBase;
        return at <= list.data.size() && bytes <= list.data.size() - at ? list.data.data() + at : nullptr;
    }
    unsigned String(unsigned name) override { return outer.String(name); }
  private:
    Guest &outer;
    const DisplayList &list;
};
} // namespace

// ---------------------------------------------------------------------------
struct Context::State
{
    Gpu::Backend &backend;
    U owner;
    Format format;
    std::shared_ptr<Shared> shared = std::make_shared<Shared>();
    Attributes a;
    Client client;
    std::vector<std::pair<U, Attributes>> attribute_stack;
    std::vector<Client> client_stack;
    std::vector<Matrix> modelview{Matrix::Identity()}, projection{Matrix::Identity()};
    std::vector<Matrix> texture[2] = {{Matrix::Identity()}, {Matrix::Identity()}};
    U error = 0;
    bool viewport_set = false;

    // Derived from the matrices, made again when they change.
    bool derived_valid = false;
    Matrix mvp, normal_matrix;   // normal_matrix: the inverse of the modelview, transposed on use
    TextureObject default_1d, default_2d;

    // glBegin.. glEnd.
    bool inside = false;
    U mode = 0;
    std::vector<Processed> vertices;
    std::vector<unsigned char> index_bytes;
    std::vector<Processed> element_cache;
    std::vector<U> element_marks;
    U element_epoch = 0;

    // Display lists being compiled.
    U compiling = 0;
    U list_mode = 0;
    DisplayList compiled;
    int call_depth = 0;

    // The buffers.
    struct Buffer
    {
        std::vector<U> pixels;
        Gpu::Surface surface;
    };
    Buffer front, back;
    U depth_word = 0;
    Gpu::Surface depth;
    int width = 0, height = 0;
    bool present = false;

    // The queue.
    std::vector<Output> queued;
    std::vector<unsigned short> queued_indices;
    std::vector<Operation> operations;
    std::vector<Gpu::State> snapshots;
    bool state_dirty = true;

    // Selection and feedback: the render mode only; nothing is recorded.
    U render_mode = E::Render;
    // GL_PROXY_TEXTURE_1D and _2D: what a glTexImage would have made, width,
    // height and internal format per level, or zeros when it would fail.
    struct ProxyLevel { int width = 0, height = 0; U internal = 0; };
    ProxyLevel proxy[2][TextureObject::MaxLevels];

    State(Gpu::Backend &b, U o, const Format &f) : backend(b), owner(o), format(f)
    {
        a.lights[0].diffuse[0] = a.lights[0].diffuse[1] = a.lights[0].diffuse[2] = 1.0f;
        a.lights[0].specular[0] = a.lights[0].specular[1] = a.lights[0].specular[2] = 1.0f;
        a.draw_buffer = a.read_buffer = f.double_buffer ? E::Back : E::Front;
        default_1d.target = E::Texture1D;
        default_2d.target = E::Texture2D;
        default_1d.handle = NewHandle();
        default_2d.handle = NewHandle();
        depth.depth = true;
        depth.gpu_only_depth = true;
        depth.bits = reinterpret_cast<unsigned char *>(&depth_word);
        depth.z_bits = f.depth_bits > 16 ? 24 : 16;
        depth.stencil_bits = f.stencil_bits ? 8 : 0;
        depth.bit_count = 32;
        depth.owner = owner;
        depth.handle = NewHandle();
    }
    ~State()
    {
        ReleaseBuffers();
        backend.Forget(owner, default_1d.handle);
        backend.Forget(owner, default_2d.handle);
        if (shared.use_count() == 1)
            for (auto &t : shared->textures) backend.Forget(owner, t.second->handle);
    }
    U NewHandle() { return shared->next_handle++; }
    void Error(U code) { if (!error) error = code; }
    Matrix &Top(U mode_)
    {
        return mode_ == E::Projection ? projection.back() : mode_ == E::Texture ? texture[a.active_texture].back() : modelview.back();
    }
    std::vector<Matrix> &Stack(U mode_)
    {
        return mode_ == E::Projection ? projection : mode_ == E::Texture ? texture[a.active_texture] : modelview;
    }
    void MatrixChanged() { derived_valid = false; }
    void Changed() { state_dirty = true; }
    void Derive()
    {
        if (derived_valid) return;
        mvp = projection.back() * modelview.back();
        normal_matrix = modelview.back().Inverse();
        derived_valid = true;
    }

    // -- Buffers ------------------------------------------------------------
    void ReleaseBuffers()
    {
        for (Buffer *b : {&front, &back})
            if (!b->pixels.empty())
            {
                backend.Release(reinterpret_cast<unsigned char *>(b->pixels.data()), b->pixels.size() * 4);
                b->pixels.clear();
                b->surface.bits = nullptr;
            }
        backend.Forget(owner, depth.handle);
    }
    void MakeBuffer(Buffer &b, int w, int h)
    {
        b.pixels.assign(size_t(w) * size_t(h), 0xff000000u);
        auto &s = b.surface;
        s = Gpu::Surface{};
        s.bits = reinterpret_cast<unsigned char *>(b.pixels.data());
        s.width = w;
        s.height = h;
        s.pitch = w * 4;
        s.bit_count = 32;
        s.r_mask = 0xff0000;
        s.g_mask = 0xff00;
        s.b_mask = 0xff;
        s.owner = owner;
        s.handle = NewHandle();
    }
    void Resize(int w, int h)
    {
        if (w < 1) w = 1;
        if (h < 1) h = 1;
        if (w == width && h == height && !front.pixels.empty()) return;
        Flush();
        ReleaseBuffers();
        width = w;
        height = h;
        MakeBuffer(front, w, h);
        if (format.double_buffer) MakeBuffer(back, w, h);
        depth.width = w;
        depth.height = h;
        depth.pitch = w * 4;
        // A new depth texture: the old one had the old size.
        depth.handle = NewHandle();
        if (!viewport_set)
        {
            a.viewport[0] = a.viewport[1] = 0;
            a.viewport[2] = w;
            a.viewport[3] = h;
            a.scissor[0] = a.scissor[1] = 0;
            a.scissor[2] = w;
            a.scissor[3] = h;
            viewport_set = true;
            Changed();
        }
    }
    Buffer &DrawTarget()
    {
        const U b = a.draw_buffer;
        if (!format.double_buffer || back.pixels.empty()) return front;
        return b == E::Front || b == E::FrontLeft || b == 0x406 /* LEFT */ ? front : back;
    }
    Buffer &ReadTarget()
    {
        const U b = a.read_buffer;
        if (!format.double_buffer || back.pixels.empty()) return front;
        return b == E::Front || b == E::FrontLeft || b == 0x406 ? front : back;
    }
    // The CPU is about to touch the pixels of `b`.
    void Touch(Buffer &b, bool write)
    {
        Flush();
        if (!b.pixels.empty()) backend.Sync(b.surface, write);
    }

    // -- The queue ----------------------------------------------------------
    void Flush()
    {
        RenderProfile::Scope profile(RenderProfile::GlFlush);
        if (operations.empty()) return;
        Buffer &target = *queue_target;
        const bool began = !target.pixels.empty() && backend.Begin(target.surface, format.depth_bits ? &depth : nullptr);
        if (began)
        {
            std::array<TextureObject *, 2> bound = {reinterpret_cast<TextureObject *>(1), reinterpret_cast<TextureObject *>(1)};
            for (const auto &op : operations)
            {
                const Gpu::State &st = snapshots[op.state];
                if (op.clear)
                {
                    backend.Clear(op.clear_flags, op.clear_color, op.clear_depth, op.clear_stencil,
                                  op.has_rect ? op.rect : nullptr, op.has_rect ? 1 : 0, st);
                    continue;
                }
                for (unsigned unit = 0; unit < 2; ++unit)
                    if (op.textures[unit] != bound[unit])
                    {
                        backend.SetTexture(unit, op.textures[unit] ? &op.textures[unit]->surface : nullptr);
                        bound[unit] = op.textures[unit];
                    }
                Gpu::Draw d;
                d.primitive = op.primitive;
                d.fvf = OutputFvf;
                d.stride = sizeof(Output);
                d.vertices = reinterpret_cast<const unsigned char *>(queued.data() + op.first);
                d.vertex_count = unsigned(op.count);
                d.first_vertex = 0;
                if (op.index_count)
                {
                    d.indices = queued_indices.data() + op.index_first;
                    d.index_count = unsigned(op.index_count);
                }
                d.primitive_count = op.index_count ? unsigned(op.index_count / 3)
                                  : op.primitive == Gpu::PointList ? unsigned(op.count)
                                  : op.primitive == Gpu::LineList ? unsigned(op.count / 2)
                                  : unsigned(op.count / 3);
                if (d.primitive_count) backend.Draw(d, st);
            }
            backend.SetTexture(0, nullptr);
            backend.SetTexture(1, nullptr);
            backend.End();
        }
        queued.clear();
        queued_indices.clear();
        operations.clear();
        snapshots.clear();
        state_dirty = true;
    }
    Buffer *queue_target = nullptr;
    // A new operation may join the queue: the queue is for one target.
    void PrepareQueue()
    {
        Buffer &target = DrawTarget();
        if (queue_target != &target) { Flush(); queue_target = &target; }
        if (queued.size() > (1u << 20) || queued_indices.size() > (1u << 20)) Flush();
        if (state_dirty || snapshots.empty())
        {
            snapshots.push_back(Snapshot());
            state_dirty = false;
        }
    }

    // The backend state of the current OpenGL state.
    Gpu::State Snapshot()
    {
        Gpu::State s;
        auto &r = s.render;
        // Viewport: OpenGL counts Y from the bottom, the backend from the top.
        s.viewport_x = a.viewport[0];
        s.viewport_y = height - (a.viewport[1] + a.viewport[3]);
        s.viewport_width = a.viewport[2];
        s.viewport_height = a.viewport[3];
        s.z_min = a.depth_near;
        s.z_max = a.depth_far;
        r[7] = a.depth_test && format.depth_bits ? 1 : 0;                        // ZENABLE
        r[14] = a.depth_test && a.depth_mask ? 1 : 0;                            // ZWRITEENABLE
        r[23] = Compare(a.depth_func);                                           // ZFUNC
        r[8] = 3;                                                                // FILLMODE solid
        r[9] = 2;                                                                // SHADEMODE gouraud
        r[22] = 1;                                                               // CULLMODE none: culled on the CPU
        r[15] = a.alpha_test ? 1 : 0;                                            // ALPHATESTENABLE
        r[25] = Compare(a.alpha_func);                                           // ALPHAFUNC
        r[24] = U(Clamp01(a.alpha_ref) * 255.0f + 0.5f);                         // ALPHAREF
        const bool masked = !a.color_mask[0] && !a.color_mask[1] && !a.color_mask[2] && !a.color_mask[3];
        r[27] = a.blend || masked ? 1 : 0;                                       // ALPHABLENDENABLE
        r[19] = masked ? 1 : BlendFactor(a.blend_src);                           // SRCBLEND
        r[20] = masked ? 2 : BlendFactor(a.blend_dst);                           // DESTBLEND
        r[28] = a.fog ? 1 : 0;                                                   // FOGENABLE
        r[34] = PackColor(a.fog_color);                                          // FOGCOLOR
        r[35] = 0;                                                               // FOGTABLEMODE none: vertex fog
        r[29] = 0;                                                               // SPECULARENABLE
        r[52] = a.stencil_test && format.stencil_bits ? 1 : 0;                   // STENCILENABLE
        r[53] = StencilOp(a.stencil_fail);
        r[54] = StencilOp(a.stencil_zfail);
        r[55] = StencilOp(a.stencil_zpass);
        r[56] = Compare(a.stencil_func);
        r[57] = a.stencil_ref;
        r[58] = a.stencil_value_mask;
        r[59] = a.stencil_write_mask;
        r[137] = 0;                                                              // LIGHTING off: lit already
        // Polygon offset: a coarse push towards the viewer.
        if ((a.offset_fill || a.offset_line || a.offset_point) && (a.offset_units != 0.0f || a.offset_factor != 0.0f))
        {
            const float amount = -(a.offset_units + a.offset_factor);
            r[47] = amount > 0 ? U(std::min(16.0f, amount)) : 0;                 // ZBIAS
        }
        // Texture stage 0 after the environment and the base format.
        for (unsigned unit = 0; unit < 2; ++unit)
        {
            TextureObject *t = ActiveTexture(unit);
            auto &st = s.stage[unit];
            if (unit && !t) { st[TssColorOp] = st[TssAlphaOp] = 1; break; }
            st[TssColorArg1] = 2;   // D3DTA_TEXTURE
            st[TssColorArg2] = unit ? 1 : 0;   // CURRENT after stage 0
            st[TssAlphaArg1] = 2;
            st[TssAlphaArg2] = unit ? 1 : 0;
            if (!t)
            {
                st[TssColorOp] = 3;   // SELECTARG2: the colour
                st[TssAlphaOp] = 3;
            }
            else
            {
                const bool alpha = t->base == FormatAlpha || t->base == FormatLuminanceAlpha || t->base == FormatIntensity ||
                                   t->base == FormatRgba;
                const bool colour = t->base != FormatAlpha;
                switch (a.units[unit].env_mode)
                {
                case E::Replace:
                    st[TssColorOp] = colour ? 2 : 3;
                    st[TssAlphaOp] = alpha ? 2 : 3;
                    break;
                case E::Decal:
                    st[TssColorOp] = t->base == FormatRgba ? 13 : t->base == FormatRgb ? 2 : 3;   // BLENDTEXTUREALPHA
                    st[TssAlphaOp] = 3;
                    break;
                case E::Add:
                    st[TssColorOp] = 7;   // ADD
                    st[TssAlphaOp] = 4;
                    break;
                default:   // GL_MODULATE, and GL_BLEND as its nearest
                    st[TssColorOp] = 4;
                    st[TssAlphaOp] = 4;
                    break;
                }
                // Filters and wrap.
                st[TssMagFilter] = t->mag_filter == E::Nearest ? 1 : 2;
                const U min = t->min_filter;
                st[TssMinFilter] = (min == E::Nearest || min == E::NearestMipmapNearest || min == E::NearestMipmapLinear) ? 1 : 2;
                st[TssMipFilter] = !t->surface.level_count ? 1
                                 : (min == E::NearestMipmapNearest || min == E::LinearMipmapNearest) ? 2
                                 : (min == E::NearestMipmapLinear || min == E::LinearMipmapLinear) ? 3 : 1;
                st[TssAddressU] = t->wrap_s == E::Repeat ? 1 : 3;
                st[TssAddressV] = t->target == E::Texture1D ? 3 : t->wrap_t == E::Repeat ? 1 : 3;
                st[TssTexCoordIndex] = unit;
                st[TssTransformFlags] = 0x100 | 4;   // projected by q
                // Vertex already applied OpenGL's texture matrix. The backend
                // still transforms COUNT4 coordinates before projecting by q.
                for (int i = 0; i < 4; ++i) s.texture[unit][i * 5] = 1.0f;
            }
        }
        s.stage[2][TssColorOp] = s.stage[2][TssAlphaOp] = 1;
        return s;
    }
    static constexpr U TssColorOp = 1, TssColorArg1 = 2, TssColorArg2 = 3, TssAlphaOp = 4, TssAlphaArg1 = 5,
                       TssAlphaArg2 = 6, TssTexCoordIndex = 11, TssAddressU = 13, TssAddressV = 14,
                       TssMagFilter = 16, TssMinFilter = 17, TssMipFilter = 18, TssTransformFlags = 24;
    static U Compare(U f) { return f >= E::Never && f <= E::Always ? f - E::Never + 1 : 8; }
    static U BlendFactor(U f)
    {
        switch (f)
        {
        case E::Zero: return 1;
        case E::One: return 2;
        case 0x300: return 3;    // SRC_COLOR
        case 0x301: return 4;    // ONE_MINUS_SRC_COLOR
        case 0x302: return 5;    // SRC_ALPHA
        case 0x303: return 6;    // ONE_MINUS_SRC_ALPHA
        case 0x304: return 7;    // DST_ALPHA
        case 0x305: return 8;    // ONE_MINUS_DST_ALPHA
        case 0x306: return 9;    // DST_COLOR
        case 0x307: return 10;   // ONE_MINUS_DST_COLOR
        case 0x308: return 11;   // SRC_ALPHA_SATURATE
        default: return 2;
        }
    }
    static U StencilOp(U op)
    {
        switch (op)
        {
        case E::Zero: return 2;
        case 0x1e01: return 3;   // REPLACE
        case E::Incr: return 4;
        case E::Decr: return 5;
        case E::Invert: return 6;
        default: return 1;       // KEEP
        }
    }

    // -- Textures -----------------------------------------------------------
    TextureObject *Bound(U target, unsigned unit = ~0u)
    {
        if (unit == ~0u) unit = a.active_texture;
        const U name = target == E::Texture1D ? a.units[unit].bound_1d : a.units[unit].bound_2d;
        if (!name) return target == E::Texture1D ? &default_1d : &default_2d;
        auto found = shared->textures.find(name);
        return found == shared->textures.end() ? nullptr : found->second.get();
    }
    // The texture that applies to a draw, or null when texturing is off or
    // the texture is not complete.
    TextureObject *ActiveTexture(unsigned unit)
    {
        TextureObject *t = nullptr;
        if (a.units[unit].texture_2d) t = Bound(E::Texture2D, unit);
        else if (a.units[unit].texture_1d) t = Bound(E::Texture1D, unit);
        if (!t) return nullptr;
        const int levels = t->Complete();
        if (!levels) return nullptr;
        auto &s = t->surface;
        const auto &l0 = t->levels[0];
        s.bits = reinterpret_cast<unsigned char *>(const_cast<U *>(l0.texels.data()));
        s.width = l0.width;
        s.height = l0.height;
        s.pitch = l0.width * 4;
        s.bit_count = 32;
        s.r_mask = 0xff0000;
        s.g_mask = 0xff00;
        s.b_mask = 0xff;
        s.a_mask = 0xff000000u;
        s.owner = owner;
        s.handle = t->handle;
        s.generation = t->generation;
        s.level_count = 0;
        for (int n = 1; n < levels && n <= int(Gpu::Surface::MaxLevels); ++n)
        {
            auto &l = t->levels[n];
            s.levels[n - 1].bits = reinterpret_cast<unsigned char *>(l.texels.data());
            s.levels[n - 1].width = l.width;
            s.levels[n - 1].height = l.height;
            s.levels[n - 1].pitch = l.width * 4;
            s.level_count = unsigned(n);
        }
        return t;
    }
    // A texture's pixels are about to change: queued draws must see the old
    // ones, so they go first.
    void TextureChanging() { Flush(); }
};

// ===========================================================================
// The vertex stage.
namespace
{
struct Lit
{
    float front[4], back[4];
};
} // namespace

static void LightVertex(Context::State &s, const float eye[4], const float normal_in[3], Lit &out)
{
    auto &a = s.a;
    for (int side = 0; side < (a.two_side ? 2 : 1); ++side)
    {
        Material m = a.material[side];
        if (a.color_material)
        {
            const U face = a.color_material_face;
            if (face == E::FrontAndBack || (face == E::Front && side == 0) || (face == E::Back && side == 1))
            {
                const U mode = a.color_material_mode;
                if (mode == E::Ambient || mode == E::AmbientAndDiffuse) memcpy(m.ambient, a.color, 16);
                if (mode == E::Diffuse || mode == E::AmbientAndDiffuse) memcpy(m.diffuse, a.color, 16);
                if (mode == E::Specular) memcpy(m.specular, a.color, 16);
                if (mode == E::Emission) memcpy(m.emission, a.color, 16);
            }
        }
        float n[3] = {normal_in[0], normal_in[1], normal_in[2]};
        if (side == 1) { n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2]; }
        float c[3];
        for (int k = 0; k < 3; ++k) c[k] = m.emission[k] + a.model_ambient[k] * m.ambient[k];
        float to_eye[3] = {0, 0, 1};
        if (a.local_viewer)
        {
            to_eye[0] = -eye[0]; to_eye[1] = -eye[1]; to_eye[2] = -eye[2];
            Normalize3(to_eye);
        }
        for (int i = 0; i < 8; ++i)
        {
            if (!a.light_on[i]) continue;
            const Light &l = a.lights[i];
            float L[3];
            float attenuation = 1.0f;
            if (l.position[3] != 0.0f)
            {
                L[0] = l.position[0] / l.position[3] - eye[0] / (eye[3] ? eye[3] : 1.0f);
                L[1] = l.position[1] / l.position[3] - eye[1] / (eye[3] ? eye[3] : 1.0f);
                L[2] = l.position[2] / l.position[3] - eye[2] / (eye[3] ? eye[3] : 1.0f);
                const float d = std::sqrt(L[0] * L[0] + L[1] * L[1] + L[2] * L[2]);
                if (d > 0.0f) { L[0] /= d; L[1] /= d; L[2] /= d; }
                const float k = l.attenuation[0] + l.attenuation[1] * d + l.attenuation[2] * d * d;
                attenuation = k > 0.0f ? 1.0f / k : 1.0f;
            }
            else
            {
                L[0] = l.position[0]; L[1] = l.position[1]; L[2] = l.position[2];
                Normalize3(L);
            }
            if (l.cutoff != 180.0f)
            {
                float D[3] = {l.direction[0], l.direction[1], l.direction[2]};
                Normalize3(D);
                const float cos_angle = -(L[0] * D[0] + L[1] * D[1] + L[2] * D[2]);
                const float limit = std::cos(l.cutoff * 3.14159265f / 180.0f);
                if (cos_angle < limit) attenuation = 0.0f;
                else if (l.exponent != 0.0f) attenuation *= std::pow(std::max(cos_angle, 0.0f), l.exponent);
            }
            if (attenuation == 0.0f) continue;
            const float n_dot_l = n[0] * L[0] + n[1] * L[1] + n[2] * L[2];
            for (int k = 0; k < 3; ++k) c[k] += attenuation * l.ambient[k] * m.ambient[k];
            if (n_dot_l > 0.0f)
            {
                for (int k = 0; k < 3; ++k) c[k] += attenuation * n_dot_l * l.diffuse[k] * m.diffuse[k];
                float H[3] = {L[0] + to_eye[0], L[1] + to_eye[1], L[2] + to_eye[2]};
                Normalize3(H);
                const float n_dot_h = n[0] * H[0] + n[1] * H[1] + n[2] * H[2];
                if (n_dot_h > 0.0f)
                {
                    const float f = m.shininess != 0.0f ? std::pow(n_dot_h, m.shininess) : 1.0f;
                    for (int k = 0; k < 3; ++k) c[k] += attenuation * f * l.specular[k] * m.specular[k];
                }
            }
        }
        float *o = side ? out.back : out.front;
        o[0] = Clamp01(c[0]); o[1] = Clamp01(c[1]); o[2] = Clamp01(c[2]);
        o[3] = Clamp01(m.diffuse[3]);
    }
    if (!a.two_side) memcpy(out.back, out.front, 16);
}

// One vertex through the vertex stage, into the primitive being assembled.
static void ProcessVertex(Context::State &s, const float object[4])
{
    auto &a = s.a;
    s.Derive();
    const Matrix &mv = s.modelview.back();
    Processed p;
    float eye[4];
    mv.Apply(object, eye);
    s.projection.back().Apply(eye, p.clip);
    // OpenGL clips Z from -w to w; the backend from 0 to w.
    p.clip[2] = (p.clip[2] + p.clip[3]) * 0.5f;
    // Colours.
    if (a.lighting)
    {
        // The normal through the inverse transpose of the modelview.
        const float *m = s.normal_matrix.m;
        float n[3];
        for (int r = 0; r < 3; ++r) n[r] = a.normal[0] * m[r * 4 + 0] + a.normal[1] * m[r * 4 + 1] + a.normal[2] * m[r * 4 + 2];
        if (a.normalize) Normalize3(n);
        Lit lit;
        LightVertex(s, eye, n, lit);
        p.color[0] = PackColor(lit.front);
        p.color[1] = PackColor(lit.back);
    }
    else
        p.color[0] = p.color[1] = PackColor(a.color);
    for (unsigned unit = 0; unit < 2; ++unit)
    {
    // Texture coordinates, generated or current, through the texture matrix.
    float t[4] = {a.units[unit].texcoord[0], a.units[unit].texcoord[1], a.units[unit].texcoord[2], a.units[unit].texcoord[3]};
    if (a.units[unit].gen_on[0] || a.units[unit].gen_on[1] || a.units[unit].gen_on[2] || a.units[unit].gen_on[3])
    {
        float reflect[3] = {0, 0, 0};
        bool have_reflect = false;
        for (int c = 0; c < 4; ++c)
        {
            if (!a.units[unit].gen_on[c]) continue;
            switch (a.units[unit].gen_mode[c])
            {
            case E::ObjectLinear:
            {
                const float *pl = a.units[unit].object_plane[c];
                t[c] = pl[0] * object[0] + pl[1] * object[1] + pl[2] * object[2] + pl[3] * object[3];
                break;
            }
            case E::EyeLinear:
            {
                const float *pl = a.units[unit].eye_plane[c];
                t[c] = pl[0] * eye[0] + pl[1] * eye[1] + pl[2] * eye[2] + pl[3] * eye[3];
                break;
            }
            case E::SphereMap:
                if (!have_reflect)
                {
                    const float *m = s.normal_matrix.m;
                    float n[3];
                    for (int r = 0; r < 3; ++r) n[r] = a.normal[0] * m[r * 4 + 0] + a.normal[1] * m[r * 4 + 1] + a.normal[2] * m[r * 4 + 2];
                    Normalize3(n);
                    float u[3] = {eye[0], eye[1], eye[2]};
                    Normalize3(u);
                    const float d = 2.0f * (n[0] * u[0] + n[1] * u[1] + n[2] * u[2]);
                    reflect[0] = u[0] - n[0] * d;
                    reflect[1] = u[1] - n[1] * d;
                    reflect[2] = u[2] - n[2] * d;
                    have_reflect = true;
                }
                {
                    const float m2 = 2.0f * std::sqrt(reflect[0] * reflect[0] + reflect[1] * reflect[1] + (reflect[2] + 1.0f) * (reflect[2] + 1.0f));
                    if (c < 2) t[c] = m2 > 0.0f ? reflect[c] / m2 + 0.5f : 0.5f;
                }
                break;
            }
        }
    }
    if (!s.texture[unit].back().IsIdentity())
    {
        float in[4] = {t[0], t[1], t[2], t[3]};
        s.texture[unit].back().Apply(in, t);
    }
    memcpy(p.tex[unit], t, 16);
    }
    // Fog from the eye distance.
    p.fog = 1.0f;
    if (a.fog)
    {
        const float z = std::fabs(eye[2] / (eye[3] ? eye[3] : 1.0f));
        float f;
        if (a.fog_mode == E::Linear) f = a.fog_end != a.fog_start ? (a.fog_end - z) / (a.fog_end - a.fog_start) : 1.0f;
        else if (a.fog_mode == E::Exp2) f = std::exp(-(a.fog_density * z) * (a.fog_density * z));
        else f = std::exp(-a.fog_density * z);
        p.fog = Clamp01(f);
    }
    p.edge = a.edge;
    s.vertices.push_back(p);
}

// The winding of a triangle in clip space: positive is counter-clockwise in
// window coordinates. The determinant of the homogeneous X, Y and W keeps
// its sign under the division by a positive W, so it holds for triangles
// that cross the eye plane too.
static float Winding(const Processed &p0, const Processed &p1, const Processed &p2)
{
    const float *a = p0.clip, *b = p1.clip, *c = p2.clip;
    return a[0] * (b[1] * c[3] - c[1] * b[3]) - b[0] * (a[1] * c[3] - c[1] * a[3]) + c[0] * (a[1] * b[3] - b[1] * a[3]);
}

static void Emit(Context::State &s, const Processed &p, U color, std::vector<Output> &out)
{
    Output o;
    memcpy(o.position, p.clip, 16);
    o.diffuse = color;
    o.specular = U(Clamp01(p.fog) * 255.0f + 0.5f) << 24;
    memcpy(o.tex, p.tex, sizeof o.tex);
    out.push_back(o);
    (void)s;
}

// Adds one assembled primitive batch to the queue.
static void Queue(Context::State &s, U primitive, const std::vector<Output> &out)
{
    RenderProfile::Scope profile(RenderProfile::GlQueue);
    if (out.empty()) return;
    s.PrepareQueue();
    const U snapshot = U(s.snapshots.size() - 1);
    const std::array<TextureObject *, 2> textures = {s.ActiveTexture(0), s.ActiveTexture(1)};
    if (!s.operations.empty())
    {
        Operation &last = s.operations.back();
        if (!last.clear && !last.index_count && last.primitive == primitive && last.state == snapshot && last.textures == textures &&
            last.first + last.count == s.queued.size())
        {
            s.queued.insert(s.queued.end(), out.begin(), out.end());
            last.count += out.size();
            return;
        }
    }
    Operation op;
    op.primitive = primitive;
    op.first = s.queued.size();
    op.count = out.size();
    op.state = snapshot;
    op.textures = textures;
    s.queued.insert(s.queued.end(), out.begin(), out.end());
    s.operations.push_back(op);
}

// Smooth, unlit triangle arrays keep their shared vertices all the way to the
// backend. Other polygon modes and two-sided lighting use Assemble's path.
static void QueueIndexed(Context::State &s, const unsigned char *indices, unsigned count,
                         unsigned index_size, unsigned first, unsigned span)
{
    RenderProfile::Scope profile(RenderProfile::GlQueue);
    s.PrepareQueue();
    Operation op;
    op.primitive = Gpu::TriangleList;
    op.first = s.queued.size(); op.count = span;
    op.index_first = s.queued_indices.size();
    op.state = U(s.snapshots.size() - 1);
    op.textures = {s.ActiveTexture(0), s.ActiveTexture(1)};
    auto index_at = [&](unsigned i) { U index = 0; memcpy(&index, indices + size_t(i) * index_size, index_size); return index - first; };
    for (unsigned i = 0; i + 2 < count; i += 3)
    {
        const U a = index_at(i), b = index_at(i + 1), c = index_at(i + 2);
        const float area = Winding(s.element_cache[a], s.element_cache[b], s.element_cache[c]);
        const bool ccw = area > 0.0f;
        const bool front = s.a.front_face == E::Ccw ? ccw || area == 0.0f : !ccw || area == 0.0f;
        if (s.a.cull && (s.a.cull_face == E::FrontAndBack || (s.a.cull_face == E::Front) == front)) continue;
        s.queued_indices.push_back(static_cast<unsigned short>(a));
        s.queued_indices.push_back(static_cast<unsigned short>(b));
        s.queued_indices.push_back(static_cast<unsigned short>(c));
    }
    op.index_count = s.queued_indices.size() - op.index_first;
    if (!op.index_count) return;
    s.queued.resize(op.first + span);
    for (unsigned i = 0; i < span; ++i)
        if (s.element_marks[i] == s.element_epoch)
        {
            const auto &p = s.element_cache[i];
            auto &o = s.queued[op.first + i];
            memcpy(o.position, p.clip, 16); o.diffuse = p.color[0];
            o.specular = U(Clamp01(p.fog) * 255.0f + 0.5f) << 24;
            memcpy(o.tex, p.tex, sizeof o.tex);
        }
    if (!s.operations.empty())
    {
        auto &last = s.operations.back();
        if (!last.clear && last.index_count && last.primitive == op.primitive && last.state == op.state &&
            last.textures == op.textures && last.first + last.count == op.first &&
            last.index_first + last.index_count == op.index_first && last.count + span <= 65536)
        {
            for (size_t i = op.index_first; i < s.queued_indices.size(); ++i)
                s.queued_indices[i] = static_cast<unsigned short>(s.queued_indices[i] + last.count);
            last.count += span; last.index_count += op.index_count;
            return;
        }
    }
    s.operations.push_back(op);
}

// glEnd: the vertices of the primitive become triangles, lines or points.
static void Assemble(Context::State &s)
{
    RenderProfile::Scope profile(RenderProfile::GlDraw);
    auto &v = s.vertices;
    auto &a = s.a;
    const U mode = s.mode;
    const size_t n = v.size();
    if (s.render_mode != E::Render || !n) { v.clear(); return; }
    static thread_local std::vector<Output> triangles, lines, points;
    triangles.clear(); lines.clear(); points.clear();
    const bool flat = a.shade == E::Flat;
    // A polygon of the vertices idx[0..count), with its provoking vertex.
    auto polygon = [&](const size_t *idx, size_t count, size_t provoking) {
        if (count < 3) return;
        float area = 0.0f;
        for (size_t i = 1; i + 1 < count; ++i) area += Winding(v[idx[0]], v[idx[i]], v[idx[i + 1]]);
        const bool ccw = area > 0.0f;
        const bool front = a.front_face == E::Ccw ? ccw || area == 0.0f : !ccw || area == 0.0f;
        if (a.cull && (a.cull_face == E::FrontAndBack || (a.cull_face == E::Front) == front)) return;
        const int side = front ? 0 : 1;
        const U mode_of_side = a.polygon_mode[side];
        const U flat_color = v[provoking].color[side];
        auto color_of = [&](size_t i) { return flat ? flat_color : v[i].color[side]; };
        if (mode_of_side == E::Point)
        {
            for (size_t i = 0; i < count; ++i)
                if (v[idx[i]].edge) Emit(s, v[idx[i]], color_of(idx[i]), points);
            return;
        }
        if (mode_of_side == E::Line)
        {
            for (size_t i = 0; i < count; ++i)
            {
                if (!v[idx[i]].edge) continue;
                const size_t j = (i + 1) % count;
                Emit(s, v[idx[i]], color_of(idx[i]), lines);
                Emit(s, v[idx[j]], color_of(idx[j]), lines);
            }
            return;
        }
        for (size_t i = 1; i + 1 < count; ++i)
        {
            Emit(s, v[idx[0]], color_of(idx[0]), triangles);
            Emit(s, v[idx[i]], color_of(idx[i]), triangles);
            Emit(s, v[idx[i + 1]], color_of(idx[i + 1]), triangles);
        }
    };
    auto triangle = [&](size_t i0, size_t i1, size_t i2, size_t provoking) {
        const size_t idx[3] = {i0, i1, i2};
        polygon(idx, 3, provoking);
    };
    auto line_color = [&](size_t i, size_t provoking) { return flat ? v[provoking].color[0] : v[i].color[0]; };
    switch (mode)
    {
    case E::Points:
        for (size_t i = 0; i < n; ++i) Emit(s, v[i], v[i].color[0], points);
        break;
    case E::Lines:
        for (size_t i = 0; i + 1 < n; i += 2)
        {
            Emit(s, v[i], line_color(i, i + 1), lines);
            Emit(s, v[i + 1], line_color(i + 1, i + 1), lines);
        }
        break;
    case E::LineStrip:
    case E::LineLoop:
        for (size_t i = 0; i + 1 < n; ++i)
        {
            Emit(s, v[i], line_color(i, i + 1), lines);
            Emit(s, v[i + 1], line_color(i + 1, i + 1), lines);
        }
        if (mode == E::LineLoop && n > 2)
        {
            Emit(s, v[n - 1], line_color(n - 1, 0), lines);
            Emit(s, v[0], line_color(0, 0), lines);
        }
        break;
    case E::Triangles:
        for (size_t i = 0; i + 2 < n; i += 3) triangle(i, i + 1, i + 2, i + 2);
        break;
    case E::TriangleStrip:
        for (size_t i = 0; i + 2 < n; ++i)
        {
            if (i & 1) triangle(i + 1, i, i + 2, i + 2);
            else triangle(i, i + 1, i + 2, i + 2);
        }
        break;
    case E::TriangleFan:
        for (size_t i = 1; i + 1 < n; ++i) triangle(0, i, i + 1, i + 1);
        break;
    case E::Quads:
        for (size_t i = 0; i + 3 < n; i += 4)
        {
            const size_t idx[4] = {i, i + 1, i + 2, i + 3};
            polygon(idx, 4, i + 3);
        }
        break;
    case E::QuadStrip:
        for (size_t i = 0; i + 3 < n; i += 2)
        {
            const size_t idx[4] = {i, i + 1, i + 3, i + 2};
            polygon(idx, 4, i + 3);
        }
        break;
    case E::Polygon:
    {
        std::vector<size_t> idx(n);
        for (size_t i = 0; i < n; ++i) idx[i] = i;
        polygon(idx.data(), n, 0);
        break;
    }
    default:
        break;
    }
    v.clear();
    Queue(s, Gpu::TriangleList, triangles);
    Queue(s, Gpu::LineList, lines);
    Queue(s, Gpu::PointList, points);
}

// ===========================================================================
// Pixel transfer: images in client memory, unpacked into RGBA floats.

// The bytes an image of the given shape occupies under the unpack state.
static size_t ImageBytes(const Client &c, int width, int height, U format, U type, bool pack = false)
{
    if (width <= 0 || height <= 0) return 0;
    const int alignment = pack ? c.pack_alignment : c.unpack_alignment;
    const int row_length = (pack ? c.pack_row_length : c.unpack_row_length) > 0 ? (pack ? c.pack_row_length : c.unpack_row_length) : width;
    const int skip_rows = pack ? c.pack_skip_rows : c.unpack_skip_rows;
    const int skip_pixels = pack ? c.pack_skip_pixels : c.unpack_skip_pixels;
    size_t row;
    if (type == E::Bitmap) row = size_t((row_length + 7) / 8);
    else
    {
        int components = 1;
        switch (format)
        {
        case E::Rgb: case E::Bgr: components = 3; break;
        case E::Rgba: case E::Bgra: components = 4; break;
        case E::LuminanceAlpha: components = 2; break;
        default: components = 1; break;
        }
        row = size_t(row_length) * components * TypeBytes(type);
    }
    row = (row + alignment - 1) / alignment * alignment;
    return row * size_t(height + skip_rows) + size_t(skip_pixels) * 16 + 16;
}

// Reads an image out of guest memory as RGBA floats, row 0 first as OpenGL
// numbers them (the bottom row for pixels, the first row for textures).
static bool UnpackImage(Guest &g, const Client &c, U address, int width, int height, U format, U type,
                        std::vector<float> &out)
{
    out.assign(size_t(width) * size_t(height) * 4, 0.0f);
    if (!address || width <= 0 || height <= 0) return address != 0 || width <= 0 || height <= 0;
    int components = 1;
    switch (format)
    {
    case E::Rgb: case E::Bgr: components = 3; break;
    case E::Rgba: case E::Bgra: components = 4; break;
    case E::LuminanceAlpha: components = 2; break;
    default: components = 1; break;
    }
    const int row_length = c.unpack_row_length > 0 ? c.unpack_row_length : width;
    const unsigned size = type == E::Bitmap ? 0 : TypeBytes(type);
    size_t row_bytes = type == E::Bitmap ? size_t((row_length + 7) / 8) : size_t(row_length) * components * size;
    row_bytes = (row_bytes + c.unpack_alignment - 1) / c.unpack_alignment * c.unpack_alignment;
    std::vector<unsigned char> row(row_bytes + 16);
    for (int y = 0; y < height; ++y)
    {
        const U at = address + U(size_t(y + c.unpack_skip_rows) * row_bytes);
        memset(row.data(), 0, row.size());
        g.Read(at, row.data(), U(row_bytes));
        for (int x = 0; x < width; ++x)
        {
            float px[4] = {0, 0, 0, 1};
            float values[4] = {0, 0, 0, 0};
            const int sx = x + c.unpack_skip_pixels;
            if (type == E::Bitmap)
            {
                const unsigned char byte = row[size_t(sx / 8)];
                const int bit = c.unpack_lsb_first ? (byte >> (sx & 7)) & 1 : (byte >> (7 - (sx & 7))) & 1;
                values[0] = float(bit);
            }
            else
                for (int k = 0; k < components; ++k)
                {
                    const size_t off = (size_t(sx) * components + k) * size;
                    if (off + size <= row_bytes) values[k] = Component(row.data() + off, type, type != E::Float);
                }
            switch (format)
            {
            case E::Red: px[0] = values[0]; break;
            case E::Green: px[1] = values[0]; break;
            case E::Blue: px[2] = values[0]; break;
            case E::Alpha: px[0] = px[1] = px[2] = 0.0f; px[3] = values[0]; break;
            case E::Luminance: case E::ColorIndex: px[0] = px[1] = px[2] = values[0]; break;
            case E::LuminanceAlpha: px[0] = px[1] = px[2] = values[0]; px[3] = values[1]; break;
            case E::Rgb: px[0] = values[0]; px[1] = values[1]; px[2] = values[2]; break;
            case E::Bgr: px[0] = values[2]; px[1] = values[1]; px[2] = values[0]; break;
            case E::Bgra: px[0] = values[2]; px[1] = values[1]; px[2] = values[0]; px[3] = values[3]; break;
            case E::Rgba: default:
                px[0] = values[0]; px[1] = values[1]; px[2] = values[2]; px[3] = components == 4 ? values[3] : 1.0f; break;
            }
            memcpy(&out[(size_t(y) * width + x) * 4], px, 16);
        }
    }
    return true;
}

static BaseFormat BaseOf(U internal)
{
    switch (internal)
    {
    case 1: case E::Luminance: case 0x803f: case 0x8040: case 0x8041: case 0x8042: return FormatLuminance;
    case 2: case E::LuminanceAlpha: case 0x8043: case 0x8044: case 0x8045: case 0x8046: case 0x8047: case 0x8048:
        return FormatLuminanceAlpha;
    case E::Alpha: case 0x803b: case 0x803c: case 0x803d: case 0x803e: return FormatAlpha;
    case E::Intensity: case 0x804a: case 0x804b: case 0x804c: case 0x804d: return FormatIntensity;
    case 3: case E::Rgb: case 0x2a10: case 0x804f: case 0x8050: case 0x8051: case 0x8052: case 0x8053: case 0x8054:
    case E::Bgr:
        return FormatRgb;
    default: return FormatRgba;
    }
}

// An unpacked RGBA pixel as the texel of a base format: 0xAARRGGBB, with
// white colour for alpha textures and opaque alpha for the colour ones, so
// that MODULATE on the GPU gives what OpenGL gives.
static U Texel(const float *px, BaseFormat base)
{
    float c[4] = {px[0], px[1], px[2], px[3]};
    switch (base)
    {
    case FormatAlpha: c[0] = c[1] = c[2] = 1.0f; break;
    case FormatLuminance: c[1] = c[2] = c[0]; c[3] = 1.0f; break;
    case FormatLuminanceAlpha: c[1] = c[2] = c[0]; break;
    case FormatIntensity: c[1] = c[2] = c[3] = c[0]; break;
    case FormatRgb: c[3] = 1.0f; break;
    default: break;
    }
    return PackColor(c);
}

// ===========================================================================
Context::Context(Gpu::Backend &b, unsigned owner, const Format &f)
    : backend(b), format(f), state(new State(b, owner, f))
{
}
Context::~Context() = default;

void Context::Drawable(int width, int height) { state->Resize(width, height); }
void Context::Flush() { state->Flush(); }
void Context::StateSync()
{
    state->Touch(state->front, false);
    state->Touch(state->back, false);
}

bool Context::Swap(Picture &picture, bool readback)
{
    auto &s = *state;
    if (s.back.pixels.empty() && s.front.pixels.empty()) return false;
    s.Flush();
    // Our pixel formats advertise PFD_SWAP_COPY. Publishing the back buffer
    // itself lets the next clear/draw change the visible image before the
    // next SwapBuffers, and leaves GL_FRONT stale.
    if (s.format.double_buffer && !s.back.pixels.empty())
    {
        if (!s.backend.CopyColor(s.back.surface, s.front.surface))
        {
            s.Touch(s.back, false);
            s.Touch(s.front, true);
            memcpy(s.front.pixels.data(), s.back.pixels.data(), s.back.pixels.size() * sizeof(unsigned));
        }
    }
    auto &b = s.front;
    if (readback) s.Touch(b, false);
    picture.bits = reinterpret_cast<const unsigned char *>(b.pixels.data());
    picture.width = s.width;
    picture.height = s.height;
    picture.pitch = s.width * 4;
    s.present = false;
    return true;
}
bool Context::TakePresent()
{
    const bool p = state->present;
    state->present = false;
    return p;
}
bool Context::Front(Picture &picture)
{
    auto &s = *state;
    if (s.front.pixels.empty()) return false;
    s.Touch(s.front, false);
    picture.bits = reinterpret_cast<const unsigned char *>(s.front.pixels.data());
    picture.width = s.width;
    picture.height = s.height;
    picture.pitch = s.width * 4;
    return true;
}
bool Context::ShareWith(Context &other)
{
    auto &s = *state;
    // OpenGL refuses a context that has objects of its own already.
    if (!s.shared->lists.empty() || !s.shared->textures.empty()) return false;
    s.Flush();
    s.shared = other.state->shared;
    return true;
}

// ===========================================================================
// The dispatch: one function of the table.
namespace
{
// The bytes a command reads through its pointer arguments, for recording it
// into a display list: which argument words are pointers and how many bytes
// each covers.
struct PointerArg { int arg; size_t bytes; };
int ListPointers(const Context::State &s, U slot, const U *a, PointerArg *out)
{
    const auto *extension = ExtensionFor(slot);
    const U legacy = extension ? extension->legacy : slot;
    if (legacy >= Win98OpenGL::DispatchCount) return 0;
    const auto &call = Attributes_().calls[legacy];
    if (call.family != NotAttribute && call.vector)
    {
        const size_t bytes = size_t(call.count) * TypeBytes(call.type);
        if (call.family == FamilyRect)
        {
            out[0] = {0, bytes / 2};
            out[1] = {1, bytes / 2};
            return 2;
        }
        out[0] = {extension ? 1 : 0, bytes};
        return 1;
    }
    switch (slot)
    {
    case S::Fogfv: case S::Fogiv: case S::LightModelfv: case S::LightModeliv:
        out[0] = {1, 16}; return 1;
    case S::Lightfv: case S::Lightiv: case S::Materialfv: case S::Materialiv: case S::TexEnvfv: case S::TexEnviv:
    case S::TexParameterfv: case S::TexParameteriv: case S::TexGenfv: case S::TexGeniv:
        out[0] = {2, 16}; return 1;
    case S::TexGendv: out[0] = {2, 32}; return 1;
    case S::LoadMatrixf: case S::MultMatrixf: out[0] = {0, 64}; return 1;
    case S::LoadMatrixd: case S::MultMatrixd: out[0] = {0, 128}; return 1;
    case S::ClipPlane: out[0] = {1, 32}; return 1;
    case S::PolygonStipple: out[0] = {0, 128}; return 1;
    case S::CallLists:
    {
        const U type = a[1];
        const size_t n = size_t(std::max(0, int(a[0])));
        out[0] = {2, n * TypeBytes(type >= E::Bytes2 && type <= E::Bytes4 ? type : type)};
        return 1;
    }
    case S::PixelMapfv: case S::PixelMapuiv: case S::PixelMapusv:
        out[0] = {2, size_t(std::max(0, int(a[1]))) * 4}; return 1;
    default:
        return 0;
    }
    (void)s;
}
} // namespace

// Commands that run at once even while a list is being compiled.
static bool NeverCompiled(U slot)
{
    if (slot == ClientActiveTextureEntry) return true;
    switch (slot)
    {
    case S::IsList: case S::GenLists: case S::DeleteLists: case S::FeedbackBuffer: case S::SelectBuffer:
    case S::RenderMode: case S::ReadPixels: case S::PixelStoref: case S::PixelStorei: case S::GenTextures:
    case S::DeleteTextures: case S::AreTexturesResident: case S::IsTexture: case S::GetBooleanv:
    case S::GetClipPlane: case S::GetDoublev: case S::GetError: case S::GetFloatv: case S::GetIntegerv:
    case S::GetLightfv: case S::GetLightiv: case S::GetMapdv: case S::GetMapfv: case S::GetMapiv:
    case S::GetMaterialfv: case S::GetMaterialiv: case S::GetPixelMapfv: case S::GetPixelMapuiv:
    case S::GetPixelMapusv: case S::GetPolygonStipple: case S::GetString: case S::GetTexEnvfv:
    case S::GetTexEnviv: case S::GetTexGendv: case S::GetTexGenfv: case S::GetTexGeniv: case S::GetTexImage:
    case S::GetTexParameterfv: case S::GetTexParameteriv: case S::GetTexLevelParameterfv:
    case S::GetTexLevelParameteriv: case S::IsEnabled: case S::Flush: case S::Finish: case S::EnableClientState:
    case S::DisableClientState: case S::VertexPointer: case S::NormalPointer: case S::ColorPointer:
    case S::IndexPointer: case S::TexCoordPointer: case S::EdgeFlagPointer: case S::InterleavedArrays:
    case S::PushClientAttrib: case S::PopClientAttrib: case S::GetPointerv: case S::NewList: case S::EndList:
        return true;
    default:
        return false;
    }
}

namespace
{
// The execution of one command, shared by direct calls and list replays.
class Executor
{
  public:
    Executor(Context::State &s, Guest &g) : s(s), a(s.a), g(g) {}
    U Run(U slot, const U *w);
  private:
    Context::State &s;
    Attributes &a;
    Guest &g;
    unsigned texcoord_unit = 0;

    float F(const U *w, int i) const { return Float(w[i]); }
    void ReadFloats(U address, float *out, int n, bool doubles = false, bool ints = false)
    {
        for (int i = 0; i < n; ++i)
        {
            if (doubles) { double d = 0; g.Read(address + i * 8, &d, 8); out[i] = float(d); }
            else if (ints) { int32_t v = 0; g.Read(address + i * 4, &v, 4); out[i] = float(v); }
            else { float f = 0; g.Read(address + i * 4, &f, 4); out[i] = f; }
        }
    }
    void Attribute(const AttributeCall &c, const U *w);
    void Vertex(const float v[4]);
    void RasterPos(const float v[4]);
    void Enable(U cap, bool on);
    bool IsEnabled(U cap);
    void Light(U light, U pname, const float *v);
    void MaterialSet(U face, U pname, const float *v);
    void TexParameter(U target, U pname, const float *v);
    void TexEnv(U pname, const float *v);
    void TexGen(U coord, U pname, const float *v);
    void Fog(U pname, const float *v);
    void LightModel(U pname, const float *v);
    void MultMatrix(const Matrix &m);
    void LoadMatrix(const Matrix &m);
    void Clear(U mask);
    void TexImage(U target, int level, U internal, int width, int height, int border, U format, U type, U pixels);
    void TexSubImage(U target, int level, int x, int y, int width, int height, U format, U type, U pixels);
    void CopyTexImage(U target, int level, U internal, int x, int y, int width, int height, bool sub, int xoff, int yoff);
    void DrawPixels(int width, int height, U format, U type, U pixels);
    void ReadPixels(int x, int y, int width, int height, U format, U type, U pixels);
    void CopyPixels(int x, int y, int width, int height, U type);
    void Bitmap(int width, int height, float xorig, float yorig, float xmove, float ymove, U bitmap);
    void Element(int i);
    void PrepareArrays(unsigned first, unsigned count);
    struct ArrayView { const ClientArray *array = nullptr; const unsigned char *bits = nullptr; unsigned first = 0; };
    ArrayView array_views[7];
    void DrawArrays(U mode, int first, int count);
    void Get(U pname, float *out, int &count, bool &is_bool);
    void GetValues(U pname, U address, int kind);   // 0 bool, 1 int, 2 float, 3 double
    U CallList(U list);
};

void Executor::Vertex(const float v[4])
{
    if (!s.inside) return;
    ProcessVertex(s, v);
}

void Executor::Attribute(const AttributeCall &c, const U *w)
{
    float v[4] = {0, 0, 0, 1};
    const bool normalized = c.family == FamilyColor || c.family == FamilyNormal;
    if (c.family == FamilyColor && c.count == 3) v[3] = 1.0f;
    if (c.family == FamilyRect)
    {
        // Two corners, by value or through two pointers.
        float r[4];
        if (c.vector)
        {
            const unsigned size = TypeBytes(c.type);
            unsigned char p[32] = {};
            g.Read(w[0], p, 2 * size);
            g.Read(w[1], p + 2 * size, 2 * size);
            for (int k = 0; k < 4; ++k) r[k] = Component(p + k * size, c.type, false);
        }
        else if (c.type == E::Double)
            for (int k = 0; k < 4; ++k) r[k] = float(Double(w + k * 2));
        else
            for (int k = 0; k < 4; ++k) r[k] = c.type == E::Float ? F(w, k) : c.type == E::Int ? float(int(w[k])) : float(int16_t(w[k]));
        // glRect is a polygon from (x1, y1) to (x2, y2).
        const U saved = s.mode;
        const bool was = s.inside;
        s.inside = true;
        s.mode = E::Polygon;
        const float p0[4] = {r[0], r[1], 0, 1}, p1[4] = {r[2], r[1], 0, 1}, p2[4] = {r[2], r[3], 0, 1}, p3[4] = {r[0], r[3], 0, 1};
        Vertex(p0); Vertex(p1); Vertex(p2); Vertex(p3);
        Assemble(s);
        s.inside = was;
        s.mode = saved;
        return;
    }
    if (c.vector)
    {
        const unsigned size = TypeBytes(c.type);
        unsigned char p[32] = {};
        g.Read(w[0], p, c.count * size);
        for (int k = 0; k < c.count; ++k) v[k] = Component(p + k * size, c.type, normalized);
    }
    else
    {
        int word = 0;
        for (int k = 0; k < c.count; ++k)
        {
            float value;
            switch (c.type)
            {
            case E::Double: value = float(Double(w + word)); word += 2; break;
            case E::Float: value = F(w, word++); break;
            case E::Byte: { const int8_t b = int8_t(w[word++] & 255); value = normalized ? (2.0f * b + 1.0f) / 255.0f : float(b); break; }
            case E::UnsignedByte: { const U b = w[word++] & 255; value = normalized ? float(b) / 255.0f : float(b); break; }
            case E::Short: { const int16_t b = int16_t(w[word++] & 65535); value = normalized ? (2.0f * b + 1.0f) / 65535.0f : float(b); break; }
            case E::UnsignedShort: { const U b = w[word++] & 65535; value = normalized ? float(b) / 65535.0f : float(b); break; }
            case E::Int: { const int b = int(w[word++]); value = normalized ? float((2.0 * b + 1.0) / 4294967295.0) : float(b); break; }
            default: { const U b = w[word++]; value = normalized ? float(double(b) / 4294967295.0) : float(b); break; }
            }
            v[k] = value;
        }
    }
    switch (c.family)
    {
    case FamilyColor:
        memcpy(a.color, v, 16);
        if (c.count == 3) a.color[3] = 1.0f;
        break;
    case FamilyNormal:
        memcpy(a.normal, v, 12);
        break;
    case FamilyTexCoord:
        a.units[texcoord_unit].texcoord[0] = v[0];
        a.units[texcoord_unit].texcoord[1] = c.count > 1 ? v[1] : 0.0f;
        a.units[texcoord_unit].texcoord[2] = c.count > 2 ? v[2] : 0.0f;
        a.units[texcoord_unit].texcoord[3] = c.count > 3 ? v[3] : 1.0f;
        break;
    case FamilyVertex:
    {
        float p[4] = {v[0], c.count > 1 ? v[1] : 0.0f, c.count > 2 ? v[2] : 0.0f, c.count > 3 ? v[3] : 1.0f};
        Vertex(p);
        break;
    }
    case FamilyRasterPos:
    {
        float p[4] = {v[0], c.count > 1 ? v[1] : 0.0f, c.count > 2 ? v[2] : 0.0f, c.count > 3 ? v[3] : 1.0f};
        RasterPos(p);
        break;
    }
    case FamilyIndex:
        a.index = v[0];
        break;
    default:
        break;   // evaluators are not built
    }
}

void Executor::RasterPos(const float v[4])
{
    s.Derive();
    float eye[4], clip[4];
    s.modelview.back().Apply(v, eye);
    s.projection.back().Apply(eye, clip);
    if (clip[3] <= 0.0f || std::fabs(clip[0]) > clip[3] * 1.0001f || std::fabs(clip[1]) > clip[3] * 1.0001f ||
        std::fabs(clip[2]) > clip[3] * 1.0001f)
    {
        a.raster_valid = false;
        return;
    }
    const float nx = clip[0] / clip[3], ny = clip[1] / clip[3], nz = clip[2] / clip[3];
    a.raster[0] = a.viewport[0] + (nx + 1.0f) * 0.5f * a.viewport[2];
    a.raster[1] = a.viewport[1] + (ny + 1.0f) * 0.5f * a.viewport[3];
    a.raster[2] = a.depth_near + (nz + 1.0f) * 0.5f * (a.depth_far - a.depth_near);
    a.raster[3] = clip[3];
    a.raster_valid = true;
    memcpy(a.raster_color, a.color, 16);
    for (auto &unit : a.units) memcpy(unit.raster_tex, unit.texcoord, 16);
}

void Executor::Enable(U cap, bool on)
{
    s.Changed();
    if (cap >= E::Light0 && cap < E::Light0 + 8) { a.light_on[cap - E::Light0] = on; return; }
    if (cap >= E::ClipPlane0 && cap < E::ClipPlane0 + 6) { a.clip_on[cap - E::ClipPlane0] = on; return; }
    switch (cap)
    {
    case E::CapPointSmooth: a.point_smooth = on; break;
    case E::CapLineSmooth: a.line_smooth = on; break;
    case E::CapLineStipple: a.line_stipple = on; break;
    case E::CapPolygonSmooth: a.polygon_smooth = on; break;
    case E::CapPolygonStipple: a.polygon_stipple = on; break;
    case E::CapCullFace: a.cull = on; break;
    case E::CapLighting: a.lighting = on; break;
    case E::CapColorMaterial: a.color_material = on; break;
    case E::CapFog: a.fog = on; break;
    case E::CapDepthTest: a.depth_test = on; break;
    case E::CapStencilTest: a.stencil_test = on; break;
    case E::CapNormalize: a.normalize = on; break;
    case E::CapAlphaTest: a.alpha_test = on; break;
    case E::CapDither: a.dither = on; break;
    case E::CapBlend: a.blend = on; break;
    case E::CapLogicOp: case E::CapColorLogicOp: a.logic_op = on; break;
    case E::CapScissorTest: a.scissor_test = on; break;
    case E::CapTextureGenS: a.units[a.active_texture].gen_on[0] = on; break;
    case E::CapTextureGenT: a.units[a.active_texture].gen_on[1] = on; break;
    case E::CapTextureGenR: a.units[a.active_texture].gen_on[2] = on; break;
    case E::CapTextureGenQ: a.units[a.active_texture].gen_on[3] = on; break;
    case E::Texture1D: a.units[a.active_texture].texture_1d = on; break;
    case E::Texture2D: a.units[a.active_texture].texture_2d = on; break;
    case E::CapAutoNormal: a.auto_normal = on; break;
    case E::CapPolygonOffsetFill: a.offset_fill = on; break;
    case E::CapPolygonOffsetLine: a.offset_line = on; break;
    case E::CapPolygonOffsetPoint: a.offset_point = on; break;
    case E::VertexArray: case E::NormalArray: case E::ColorArray: case E::IndexArray: case E::TextureCoordArray:
    case E::EdgeFlagArray:
        s.Error(E::InvalidEnum);
        break;
    default:
        // The evaluator maps (0xd90..0xdb8) and anything else without effect here.
        if (!(cap >= 0xd90 && cap <= 0xdb8)) s.Error(E::InvalidEnum);
        break;
    }
}

bool Executor::IsEnabled(U cap)
{
    if (cap >= E::Light0 && cap < E::Light0 + 8) return a.light_on[cap - E::Light0];
    if (cap >= E::ClipPlane0 && cap < E::ClipPlane0 + 6) return a.clip_on[cap - E::ClipPlane0];
    switch (cap)
    {
    case E::CapPointSmooth: return a.point_smooth;
    case E::CapLineSmooth: return a.line_smooth;
    case E::CapLineStipple: return a.line_stipple;
    case E::CapPolygonSmooth: return a.polygon_smooth;
    case E::CapPolygonStipple: return a.polygon_stipple;
    case E::CapCullFace: return a.cull;
    case E::CapLighting: return a.lighting;
    case E::CapColorMaterial: return a.color_material;
    case E::CapFog: return a.fog;
    case E::CapDepthTest: return a.depth_test;
    case E::CapStencilTest: return a.stencil_test;
    case E::CapNormalize: return a.normalize;
    case E::CapAlphaTest: return a.alpha_test;
    case E::CapDither: return a.dither;
    case E::CapBlend: return a.blend;
    case E::CapLogicOp: case E::CapColorLogicOp: return a.logic_op;
    case E::CapScissorTest: return a.scissor_test;
    case E::CapTextureGenS: return a.units[a.active_texture].gen_on[0];
    case E::CapTextureGenT: return a.units[a.active_texture].gen_on[1];
    case E::CapTextureGenR: return a.units[a.active_texture].gen_on[2];
    case E::CapTextureGenQ: return a.units[a.active_texture].gen_on[3];
    case E::Texture1D: return a.units[a.active_texture].texture_1d;
    case E::Texture2D: return a.units[a.active_texture].texture_2d;
    case E::CapAutoNormal: return a.auto_normal;
    case E::CapPolygonOffsetFill: return a.offset_fill;
    case E::CapPolygonOffsetLine: return a.offset_line;
    case E::CapPolygonOffsetPoint: return a.offset_point;
    case E::VertexArray: return s.client.vertex.enabled;
    case E::NormalArray: return s.client.normal.enabled;
    case E::ColorArray: return s.client.color.enabled;
    case E::IndexArray: return s.client.index.enabled;
    case E::TextureCoordArray: return s.client.texcoord[s.client.active_texture].enabled;
    case E::EdgeFlagArray: return s.client.edge.enabled;
    default: return false;
    }
}

void Executor::Light(U light, U pname, const float *v)
{
    if (light < E::Light0 || light >= E::Light0 + 8) { s.Error(E::InvalidEnum); return; }
    auto &l = a.lights[light - E::Light0];
    s.Changed();
    switch (pname)
    {
    case E::Ambient: memcpy(l.ambient, v, 16); break;
    case E::Diffuse: memcpy(l.diffuse, v, 16); break;
    case E::Specular: memcpy(l.specular, v, 16); break;
    case E::Position:
    {
        // Stored in eye coordinates, through the modelview of this moment.
        s.modelview.back().Apply(v, l.position);
        break;
    }
    case E::SpotDirection:
    {
        const float d[4] = {v[0], v[1], v[2], 0.0f};
        float e[4];
        s.modelview.back().Apply(d, e);
        memcpy(l.direction, e, 12);
        break;
    }
    case E::SpotExponent: l.exponent = v[0]; break;
    case E::SpotCutoff: l.cutoff = v[0]; break;
    case E::ConstantAttenuation: l.attenuation[0] = v[0]; break;
    case E::LinearAttenuation: l.attenuation[1] = v[0]; break;
    case E::QuadraticAttenuation: l.attenuation[2] = v[0]; break;
    default: s.Error(E::InvalidEnum); break;
    }
}

void Executor::MaterialSet(U face, U pname, const float *v)
{
    for (int side = 0; side < 2; ++side)
    {
        if (face == E::Front && side == 1) continue;
        if (face == E::Back && side == 0) continue;
        auto &m = a.material[side];
        switch (pname)
        {
        case E::Ambient: memcpy(m.ambient, v, 16); break;
        case E::Diffuse: memcpy(m.diffuse, v, 16); break;
        case E::Specular: memcpy(m.specular, v, 16); break;
        case E::Emission: memcpy(m.emission, v, 16); break;
        case E::Shininess: m.shininess = v[0]; break;
        case E::AmbientAndDiffuse: memcpy(m.ambient, v, 16); memcpy(m.diffuse, v, 16); break;
        case E::ColorIndexes: break;
        default: s.Error(E::InvalidEnum); return;
        }
    }
}

void Executor::TexParameter(U target, U pname, const float *v)
{
    TextureObject *t = s.Bound(target);
    if (!t) { s.Error(E::InvalidEnum); return; }
    s.Changed();
    switch (pname)
    {
    case E::MinFilter: t->min_filter = U(v[0]); break;
    case E::MagFilter: t->mag_filter = U(v[0]); break;
    case E::WrapS: t->wrap_s = U(v[0]); break;
    case E::WrapT: t->wrap_t = U(v[0]); break;
    case E::BorderColor: memcpy(t->border, v, 16); break;
    case E::TexturePriority: t->priority = v[0]; break;
    default: s.Error(E::InvalidEnum); break;
    }
}

void Executor::TexEnv(U pname, const float *v)
{
    s.Changed();
    if (pname == E::TextureEnvMode) a.units[a.active_texture].env_mode = U(v[0]);
    else if (pname == E::TextureEnvColor) memcpy(a.units[a.active_texture].env_color, v, 16);
    else s.Error(E::InvalidEnum);
}

void Executor::TexGen(U coord, U pname, const float *v)
{
    if (coord < E::S_ || coord > E::Q_) { s.Error(E::InvalidEnum); return; }
    const int c = int(coord - E::S_);
    switch (pname)
    {
    case E::TextureGenMode: a.units[a.active_texture].gen_mode[c] = U(v[0]); break;
    case E::ObjectPlane: memcpy(a.units[a.active_texture].object_plane[c], v, 16); break;
    case E::EyePlane:
    {
        // The plane times the inverse of the modelview of this moment.
        const Matrix inv = s.modelview.back().Inverse();
        float p[4];
        for (int k = 0; k < 4; ++k)
            p[k] = v[0] * inv.m[k * 4 + 0] + v[1] * inv.m[k * 4 + 1] + v[2] * inv.m[k * 4 + 2] + v[3] * inv.m[k * 4 + 3];
        memcpy(a.units[a.active_texture].eye_plane[c], p, 16);
        break;
    }
    default: s.Error(E::InvalidEnum); break;
    }
}

void Executor::Fog(U pname, const float *v)
{
    s.Changed();
    switch (pname)
    {
    case E::FogMode: a.fog_mode = U(v[0]); break;
    case E::FogDensity: a.fog_density = v[0]; break;
    case E::FogStart: a.fog_start = v[0]; break;
    case E::FogEnd: a.fog_end = v[0]; break;
    case E::FogColor: memcpy(a.fog_color, v, 16); break;
    case E::FogIndex: break;
    default: s.Error(E::InvalidEnum); break;
    }
}

void Executor::LightModel(U pname, const float *v)
{
    switch (pname)
    {
    case E::LightModelAmbient: memcpy(a.model_ambient, v, 16); break;
    case E::LightModelLocalViewer: a.local_viewer = v[0] != 0.0f; break;
    case E::LightModelTwoSide: a.two_side = v[0] != 0.0f; break;
    default: s.Error(E::InvalidEnum); break;
    }
}

void Executor::MultMatrix(const Matrix &m)
{
    Matrix &top = s.Top(a.matrix_mode);
    top = top * m;
    s.MatrixChanged();
}
void Executor::LoadMatrix(const Matrix &m)
{
    s.Top(a.matrix_mode) = m;
    s.MatrixChanged();
}

void Executor::Clear(U mask)
{
    if (s.render_mode != E::Render) return;
    U flags = 0;
    if (mask & E::ColorBufferBit) flags |= 1;
    if ((mask & E::DepthBufferBit) && s.format.depth_bits && a.depth_mask) flags |= 2;
    if ((mask & E::StencilBufferBit) && s.format.stencil_bits) flags |= 4;
    if (!flags) return;
    s.PrepareQueue();
    Operation op;
    op.clear = true;
    op.clear_flags = flags;
    float c[4] = {a.clear_color[0], a.clear_color[1], a.clear_color[2], a.clear_color[3]};
    op.clear_color = PackColor(c);
    op.clear_depth = a.clear_depth;
    op.clear_stencil = a.clear_stencil;
    op.state = U(s.snapshots.size() - 1);
    // The scissor box, or the whole buffer; OpenGL clears ignore the viewport.
    int left = 0, bottom = 0, right = s.width, top = s.height;
    if (a.scissor_test)
    {
        left = std::max(0, a.scissor[0]);
        bottom = std::max(0, a.scissor[1]);
        right = std::min(s.width, a.scissor[0] + a.scissor[2]);
        top = std::min(s.height, a.scissor[1] + a.scissor[3]);
    }
    op.has_rect = true;
    op.rect[0] = left;
    op.rect[1] = s.height - top;
    op.rect[2] = right;
    op.rect[3] = s.height - bottom;
    s.operations.push_back(op);
}

// Texture images.
void Executor::TexImage(U target, int level, U internal, int width, int height, int border, U format, U type, U pixels)
{
    if (target == 0x8063 || target == 0x8064)
    {
        // A proxy: the checks of a real upload, the pixels untouched.
        if (level < 0 || level >= TextureObject::MaxLevels) { s.Error(E::InvalidValue); return; }
        auto power = [](int v) { return v >= 0 && !(v & (v - 1)); };
        const int w = width - 2 * border, h = target == 0x8063 ? 1 : height - 2 * border;
        auto &p = s.proxy[target - 0x8063][level];
        const bool fits = border >= 0 && border <= 1 && power(w) && power(h) && w <= 2048 && h <= 2048;
        p.width = fits ? w : 0;
        p.height = fits ? h : 0;
        p.internal = fits ? internal : 0;
        return;
    }
    if (target != E::Texture1D && target != E::Texture2D) { s.Error(E::InvalidEnum); return; }
    if (level < 0 || level >= TextureObject::MaxLevels || width < 0 || height < 0) { s.Error(E::InvalidValue); return; }
    TextureObject *t = s.Bound(target);
    if (!t) return;
    s.TextureChanging();
    width -= 2 * border;
    height -= 2 * border;
    if (width < 0) width = 0;
    if (height < 0) height = 0;
    std::vector<float> rgba;
    UnpackImage(g, s.client, pixels, width + 2 * border, height + 2 * border, format, type, rgba);
    auto &l = t->levels[level];
    l.width = width;
    l.height = height;
    l.texels.assign(size_t(width) * height, 0);
    const BaseFormat base = BaseOf(internal);
    if (level == 0) { t->base = base; t->internal_format = internal; }
    const int full = width + 2 * border;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            l.texels[size_t(y) * width + x] = Texel(&rgba[(size_t(y + border) * full + x + border) * 4], base);
    ++t->generation;
    s.Changed();
}

void Executor::TexSubImage(U target, int level, int x0, int y0, int width, int height, U format, U type, U pixels)
{
    TextureObject *t = s.Bound(target);
    if (!t || level < 0 || level >= TextureObject::MaxLevels) { s.Error(E::InvalidValue); return; }
    auto &l = t->levels[level];
    if (x0 < 0 || y0 < 0 || x0 + width > l.width || y0 + height > l.height) { s.Error(E::InvalidValue); return; }
    s.TextureChanging();
    std::vector<float> rgba;
    UnpackImage(g, s.client, pixels, width, height, format, type, rgba);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            l.texels[size_t(y0 + y) * l.width + x0 + x] = Texel(&rgba[(size_t(y) * width + x) * 4], t->base);
    ++t->generation;
    s.Changed();
}

void Executor::CopyTexImage(U target, int level, U internal, int x, int y, int width, int height, bool sub, int xoff, int yoff)
{
    TextureObject *t = s.Bound(target);
    if (!t || level < 0 || level >= TextureObject::MaxLevels || width < 0 || height < 0) { s.Error(E::InvalidValue); return; }
    auto &source = s.ReadTarget();
    s.Touch(source, false);
    auto &l = t->levels[level];
    if (!sub)
    {
        l.width = width;
        l.height = height;
        l.texels.assign(size_t(width) * height, 0);
        if (level == 0) { t->base = BaseOf(internal); t->internal_format = internal; }
        xoff = yoff = 0;
    }
    else if (xoff < 0 || yoff < 0 || xoff + width > l.width || yoff + height > l.height) { s.Error(E::InvalidValue); return; }
    for (int row = 0; row < height; ++row)
        for (int col = 0; col < width; ++col)
        {
            const int sx = x + col, sy = y + row;
            float px[4] = {0, 0, 0, 1};
            if (sx >= 0 && sy >= 0 && sx < s.width && sy < s.height)
            {
                const U p = source.pixels[size_t(s.height - 1 - sy) * s.width + sx];
                px[0] = ((p >> 16) & 255) / 255.0f;
                px[1] = ((p >> 8) & 255) / 255.0f;
                px[2] = (p & 255) / 255.0f;
            }
            l.texels[size_t(yoff + row) * l.width + xoff + col] = Texel(px, t->base);
        }
    ++t->generation;
    s.Changed();
}

// glDrawPixels at the raster position, through the zoom, without the
// fragment tests.
void Executor::DrawPixels(int width, int height, U format, U type, U pixels)
{
    if (!a.raster_valid || width <= 0 || height <= 0 || s.render_mode != E::Render) return;
    if (format == E::StencilIndex || format == E::DepthComponent) return;
    std::vector<float> rgba;
    UnpackImage(g, s.client, pixels, width, height, format, type, rgba);
    auto &target = s.DrawTarget();
    s.Touch(target, true);
    const int x0 = int(std::floor(a.raster[0])), y0 = int(std::floor(a.raster[1]));
    const float zx = a.zoom[0], zy = a.zoom[1];
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const float *px = &rgba[(size_t(y) * width + x) * 4];
            float c[4] = {px[0], px[1], px[2], px[3]};
            const U packed = PackColor(c);
            const int left = x0 + int(std::floor(x * zx)), right = x0 + int(std::floor((x + 1) * zx));
            const int lower = y0 + int(std::floor(y * zy)), upper = y0 + int(std::floor((y + 1) * zy));
            for (int wy = std::min(lower, upper); wy < std::max(lower, upper); ++wy)
                for (int wx = std::min(left, right); wx < std::max(left, right); ++wx)
                {
                    if (wx < 0 || wy < 0 || wx >= s.width || wy >= s.height) continue;
                    U &d = target.pixels[size_t(s.height - 1 - wy) * s.width + wx];
                    if (a.blend && a.blend_src == E::SrcAlpha && a.blend_dst == E::OneMinusSrcAlpha)
                    {
                        const float al = c[3];
                        const U r = U(((d >> 16) & 255) * (1 - al) + c[0] * 255 * al), gg = U(((d >> 8) & 255) * (1 - al) + c[1] * 255 * al),
                                b = U((d & 255) * (1 - al) + c[2] * 255 * al);
                        d = 0xff000000u | (std::min(r, 255u) << 16) | (std::min(gg, 255u) << 8) | std::min(b, 255u);
                    }
                    else if (!(a.alpha_test && a.alpha_func == 0x204 && c[3] <= a.alpha_ref))
                        d = packed | 0xff000000u;
                }
        }
}

void Executor::ReadPixels(int x, int y, int width, int height, U format, U type, U pixels)
{
    if (width <= 0 || height <= 0 || !pixels) return;
    auto &source = s.ReadTarget();
    s.Touch(source, false);
    int components = 1;
    switch (format)
    {
    case E::Rgb: case E::Bgr: components = 3; break;
    case E::Rgba: case E::Bgra: components = 4; break;
    case E::LuminanceAlpha: components = 2; break;
    default: components = 1; break;
    }
    const auto &c = s.client;
    const int row_length = c.pack_row_length > 0 ? c.pack_row_length : width;
    const unsigned size = TypeBytes(type);
    size_t row_bytes = size_t(row_length) * components * size;
    row_bytes = (row_bytes + c.pack_alignment - 1) / c.pack_alignment * c.pack_alignment;
    std::vector<unsigned char> row(size_t(width) * components * size);
    for (int r = 0; r < height; ++r)
    {
        const int sy = y + r;
        for (int col = 0; col < width; ++col)
        {
            const int sx = x + col;
            float px[4] = {0, 0, 0, 1};
            if (sx >= 0 && sy >= 0 && sx < s.width && sy < s.height)
            {
                const U p = source.pixels[size_t(s.height - 1 - sy) * s.width + sx];
                px[0] = ((p >> 16) & 255) / 255.0f;
                px[1] = ((p >> 8) & 255) / 255.0f;
                px[2] = (p & 255) / 255.0f;
            }
            float values[4] = {0, 0, 0, 0};
            switch (format)
            {
            case E::Red: values[0] = px[0]; break;
            case E::Green: values[0] = px[1]; break;
            case E::Blue: values[0] = px[2]; break;
            case E::Alpha: values[0] = px[3]; break;
            case E::Luminance: values[0] = std::min(1.0f, px[0] + px[1] + px[2]); break;
            case E::LuminanceAlpha: values[0] = std::min(1.0f, px[0] + px[1] + px[2]); values[1] = px[3]; break;
            case E::Bgr: values[0] = px[2]; values[1] = px[1]; values[2] = px[0]; break;
            case E::Bgra: values[0] = px[2]; values[1] = px[1]; values[2] = px[0]; values[3] = px[3]; break;
            case E::DepthComponent: values[0] = 1.0f; break;
            default: memcpy(values, px, 16); break;
            }
            for (int k = 0; k < components; ++k)
            {
                unsigned char *d = row.data() + (size_t(col) * components + k) * size;
                const float v = values[k];
                switch (type)
                {
                case E::UnsignedByte: *d = (unsigned char)(Clamp01(v) * 255.0f + 0.5f); break;
                case E::Byte: *d = (unsigned char)int8_t(Clamp01(v) * 127.0f + 0.5f); break;
                case E::UnsignedShort: { uint16_t q = uint16_t(Clamp01(v) * 65535.0f + 0.5f); memcpy(d, &q, 2); break; }
                case E::Short: { int16_t q = int16_t(Clamp01(v) * 32767.0f + 0.5f); memcpy(d, &q, 2); break; }
                case E::UnsignedInt: { uint32_t q = uint32_t(double(Clamp01(v)) * 4294967295.0); memcpy(d, &q, 4); break; }
                case E::Int: { int32_t q = int32_t(double(Clamp01(v)) * 2147483647.0); memcpy(d, &q, 4); break; }
                default: memcpy(d, &v, 4); break;
                }
            }
        }
        g.Write(pixels + U(size_t(r + c.pack_skip_rows) * row_bytes + size_t(c.pack_skip_pixels) * components * size),
                row.data(), U(row.size()));
    }
}

void Executor::CopyPixels(int x, int y, int width, int height, U type)
{
    if (type != E::Color || !a.raster_valid || width <= 0 || height <= 0) return;
    auto &source = s.ReadTarget();
    s.Touch(source, false);
    std::vector<U> copy(size_t(width) * height, 0xff000000u);
    for (int r = 0; r < height; ++r)
        for (int c = 0; c < width; ++c)
        {
            const int sx = x + c, sy = y + r;
            if (sx >= 0 && sy >= 0 && sx < s.width && sy < s.height)
                copy[size_t(r) * width + c] = source.pixels[size_t(s.height - 1 - sy) * s.width + sx];
        }
    auto &target = s.DrawTarget();
    s.Touch(target, true);
    const int x0 = int(std::floor(a.raster[0])), y0 = int(std::floor(a.raster[1]));
    for (int r = 0; r < height; ++r)
        for (int c = 0; c < width; ++c)
        {
            const int wx = x0 + c, wy = y0 + r;
            if (wx < 0 || wy < 0 || wx >= s.width || wy >= s.height) continue;
            target.pixels[size_t(s.height - 1 - wy) * s.width + wx] = copy[size_t(r) * width + c];
        }
}

void Executor::Bitmap(int width, int height, float xorig, float yorig, float xmove, float ymove, U bitmap)
{
    if (!a.raster_valid || s.render_mode != E::Render) return;
    if (width > 0 && height > 0 && bitmap)
    {
        std::vector<float> bits;
        UnpackImage(g, s.client, bitmap, width, height, E::ColorIndex, E::Bitmap, bits);
        auto &target = s.DrawTarget();
        s.Touch(target, true);
        const U color = PackColor(a.raster_color) | 0xff000000u;
        const int x0 = int(std::floor(a.raster[0] - xorig)), y0 = int(std::floor(a.raster[1] - yorig));
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                if (bits[(size_t(y) * width + x) * 4] == 0.0f) continue;
                const int wx = x0 + x, wy = y0 + y;
                if (wx < 0 || wy < 0 || wx >= s.width || wy >= s.height) continue;
                target.pixels[size_t(s.height - 1 - wy) * s.width + wx] = color;
            }
    }
    a.raster[0] += xmove;
    a.raster[1] += ymove;
}

// Validate each contiguous array once for a batch, keeping the copying path
// for engines that cannot expose a host view or for non-contiguous memory.
void Executor::PrepareArrays(unsigned first, unsigned count)
{
    if (!count) return;
    auto &c = s.client;
    const ClientArray *arrays[] = {&c.vertex, &c.normal, &c.color, &c.index, &c.texcoord[0], &c.texcoord[1], &c.edge};
    for (unsigned k = 0; k < 7; ++k)
    {
        const auto &arr = *arrays[k];
        if (!arr.enabled || arr.size < 1 || arr.size > 4 || arr.stride < 0) continue;
        const unsigned bytes = TypeBytes(arr.type) * arr.size;
        const unsigned stride = arr.stride ? unsigned(arr.stride) : bytes;
        const uint64_t address = uint64_t(arr.pointer) + uint64_t(first) * stride;
        const uint64_t length = uint64_t(count - 1) * stride + bytes;
        if (address + length > 0x100000000ull || length > 0xffffffffu) continue;
        array_views[k] = {&arr, g.Span(U(address), U(length)), first};
    }
}

// One element of the enabled client arrays.
void Executor::Element(int i)
{
    auto &c = s.client;
    auto fetch = [&](const ClientArray &arr, int components, float *out, bool normalized) {
        const unsigned size = TypeBytes(arr.type);
        const int stride = arr.stride ? arr.stride : int(size) * components;
        unsigned char p[64] = {};
        const unsigned char *data = nullptr;
        for (const auto &view : array_views)
            if (view.array == &arr && view.bits) { data = view.bits + size_t(U(i) - view.first) * stride; break; }
        if (!data) { g.Read(arr.pointer + U(i * stride), p, unsigned(components) * size); data = p; }
        if (arr.type == E::Float) memcpy(out, data, components * 4);
        else for (int k = 0; k < components; ++k) out[k] = Component(data + k * size, arr.type, normalized);
    };
    if (c.edge.enabled)
    {
        unsigned char e = 1;
        g.Read(c.edge.pointer + U(i * (c.edge.stride ? c.edge.stride : 1)), &e, 1);
        a.edge = e != 0;
    }
    for (unsigned unit = 0; unit < 2; ++unit)
        if (c.texcoord[unit].enabled)
        {
            float t[4] = {0, 0, 0, 1};
            fetch(c.texcoord[unit], c.texcoord[unit].size, t, false);
            memcpy(a.units[unit].texcoord, t, 16);
        }
    if (c.color.enabled)
    {
        float col[4] = {0, 0, 0, 1};
        fetch(c.color, c.color.size, col, true);
        memcpy(a.color, col, 16);
    }
    if (c.normal.enabled) fetch(c.normal, 3, a.normal, true);
    if (c.vertex.enabled)
    {
        float v[4] = {0, 0, 0, 1};
        fetch(c.vertex, c.vertex.size, v, false);
        Vertex(v);
    }
}

void Executor::DrawArrays(U mode, int first, int count)
{
    if (count <= 0) return;
    if (first < 0) { s.Error(E::InvalidValue); return; }
    PrepareArrays(unsigned(first), unsigned(count));
    s.inside = true;
    s.mode = mode;
    for (int i = 0; i < count; ++i) Element(first + i);
    Assemble(s);
    s.inside = false;
}

U Executor::CallList(U list)
{
    auto found = s.shared->lists.find(list);
    if (found == s.shared->lists.end() || !found->second || s.call_depth >= 64) return 0;
    ++s.call_depth;
    // The list may be deleted while it runs: hold it until the end.
    const std::shared_ptr<DisplayList> held = found->second;
    ListGuest lg(g, *held);
    Executor inner(s, lg);
    for (const auto &cmd : held->commands)
    {
        if (cmd.packed_image)
        {
            const Client saved = s.client;
            s.client.unpack_alignment = 1;
            s.client.unpack_row_length = s.client.unpack_skip_rows = s.client.unpack_skip_pixels = 0;
            s.client.unpack_lsb_first = false;
            inner.Run(cmd.slot, cmd.args);
            s.client = saved;
        }
        else
            inner.Run(cmd.slot, cmd.args);
    }
    --s.call_depth;
    return 0;
}

// State queries. Fills out with up to 16 values.
void Executor::Get(U pname, float *out, int &n, bool &is_bool)
{
    n = 1;
    is_bool = false;
    auto put = [&](std::initializer_list<float> values) {
        n = 0;
        for (float v : values) out[n++] = v;
    };
    auto matrix = [&](const Matrix &m) { n = 16; memcpy(out, m.m, 64); };
    auto boolean = [&](bool b) { out[0] = b ? 1.0f : 0.0f; n = 1; is_bool = true; };
    switch (pname)
    {
    case 0xba6: matrix(s.modelview.back()); return;            // MODELVIEW_MATRIX
    case 0xba7: matrix(s.projection.back()); return;           // PROJECTION_MATRIX
    case 0x84e0: put({float(0x84c0 + a.active_texture)}); return;
    case 0x84e1: put({float(0x84c0 + s.client.active_texture)}); return;
    case 0x84e2: put({2}); return;
    case 0xba8: matrix(s.texture[a.active_texture].back()); return;              // TEXTURE_MATRIX
    case 0xba3: put({float(s.modelview.size())}); return;      // MODELVIEW_STACK_DEPTH
    case 0xba4: put({float(s.projection.size())}); return;
    case 0xba5: put({float(s.texture[a.active_texture].size())}); return;
    case 0xd36: put({32}); return;                             // MAX_MODELVIEW_STACK_DEPTH
    case 0xd38: put({2}); return;                              // MAX_PROJECTION_STACK_DEPTH
    case 0xd39: put({2}); return;                              // MAX_TEXTURE_STACK_DEPTH
    case 0xd35: put({16}); return;                             // MAX_ATTRIB_STACK_DEPTH
    case 0xd3b: put({16}); return;                             // MAX_CLIENT_ATTRIB_STACK_DEPTH
    case 0xd31: put({8}); return;                              // MAX_LIGHTS
    case 0xd32: put({6}); return;                              // MAX_CLIP_PLANES
    case 0xd33: put({2048}); return;                           // MAX_TEXTURE_SIZE
    case 0x81a8: put({float(s.client.lock_first)}); return;
    case 0x81a9: put({float(s.client.lock_count)}); return;
    case 0xd3a: put({2048, 2048}); return;                     // MAX_VIEWPORT_DIMS
    case 0xd30: put({8}); return;                              // MAX_EVAL_ORDER
    case 0xd34: put({256}); return;                            // MAX_PIXEL_MAP_TABLE
    case 0xd37: put({64}); return;                             // MAX_NAME_STACK_DEPTH
    case 0xb31: put({64}); return;                             // MAX_LIST_NESTING
    case 0xba2: put({float(a.viewport[0]), float(a.viewport[1]), float(a.viewport[2]), float(a.viewport[3])}); return;
    case 0xb70: put({a.depth_near, a.depth_far}); return;      // DEPTH_RANGE
    case 0xc10: put({float(a.scissor[0]), float(a.scissor[1]), float(a.scissor[2]), float(a.scissor[3])}); return;
    case 0xc22: put({a.clear_color[0], a.clear_color[1], a.clear_color[2], a.clear_color[3]}); return;
    case 0xb73: put({a.clear_depth}); return;                  // DEPTH_CLEAR_VALUE
    case 0xb91: put({float(a.clear_stencil)}); return;
    case 0xb00: put({a.color[0], a.color[1], a.color[2], a.color[3]}); return;   // CURRENT_COLOR
    case 0xb02: put({a.normal[0], a.normal[1], a.normal[2]}); return;
    case 0xb03: put({a.units[a.active_texture].texcoord[0], a.units[a.active_texture].texcoord[1], a.units[a.active_texture].texcoord[2], a.units[a.active_texture].texcoord[3]}); return;
    case 0xb07: put({a.raster[0], a.raster[1], a.raster[2], a.raster[3]}); return;   // CURRENT_RASTER_POSITION
    case 0xb08: boolean(a.raster_valid); return;
    case 0xb04: put({a.raster_color[0], a.raster_color[1], a.raster_color[2], a.raster_color[3]}); return;
    case 0xba0: put({float(a.matrix_mode)}); return;           // MATRIX_MODE
    case 0xb45: put({float(a.cull_face)}); return;
    case 0xb46: put({float(a.front_face)}); return;
    case 0xb40: put({float(a.polygon_mode[0]), float(a.polygon_mode[1])}); return;
    case 0xb54: put({float(a.shade)}); return;
    case 0xb74: put({float(a.depth_func)}); return;
    case 0xb72: boolean(a.depth_mask); return;
    case 0xbc1: put({float(a.alpha_func)}); return;
    case 0xbc2: put({a.alpha_ref}); return;
    case 0xbe1: put({float(a.blend_src)}); return;
    case 0xbe0: put({float(a.blend_dst)}); return;
    case 0xc23: n = 4; is_bool = true; for (int k = 0; k < 4; ++k) out[k] = a.color_mask[k] ? 1.0f : 0.0f; return;
    case 0xc01: put({float(a.draw_buffer)}); return;
    case 0xc02: put({float(a.read_buffer)}); return;
    case 0xc32: boolean(s.format.double_buffer); return;       // DOUBLEBUFFER
    case 0xc33: boolean(false); return;                        // STEREO
    case 0xc31: boolean(true); return;                         // RGBA_MODE
    case 0xc30: boolean(false); return;                        // INDEX_MODE
    case 0xd52: case 0xd53: case 0xd54: put({8}); return;      // RED/GREEN/BLUE_BITS
    case 0xd55: put({0}); return;                              // ALPHA_BITS
    case 0xd56: put({float(s.format.depth_bits)}); return;
    case 0xd57: put({float(s.format.stencil_bits)}); return;
    case 0xd58: case 0xd59: case 0xd5a: case 0xd5b: put({0}); return;   // ACCUM bits
    case 0xd51: put({0}); return;                              // INDEX_BITS
    case 0xd50: put({4}); return;                              // SUBPIXEL_BITS
    case 0xc00: put({0}); return;                              // AUX_BUFFERS
    case 0xb11: put({a.point_size}); return;
    case 0xb12: put({1, 1}); return;                           // POINT_SIZE_RANGE
    case 0xb13: put({1}); return;
    case 0xb21: put({a.line_width}); return;
    case 0xb22: put({1, 1}); return;
    case 0xb23: put({1}); return;
    case 0xb25: put({float(a.line_pattern)}); return;
    case 0xb26: put({float(a.line_repeat)}); return;
    case 0xb33: put({float(s.compiling ? s.compiling : 0)}); return;   // LIST_INDEX
    case 0xb30: put({float(s.compiling ? s.list_mode : 0)}); return;   // LIST_MODE
    case 0xb32: put({float(a.list_base)}); return;                     // LIST_
    case 0xc40: put({float(s.render_mode)}); return;                   // RENDER_MODE
    case 0x8069: put({float(a.units[a.active_texture].bound_2d)}); return;             // TEXTURE_BINDING_2D
    case 0x8068: put({float(a.units[a.active_texture].bound_1d)}); return;
    case 0xb62: put({a.fog_density}); return;
    case 0xb63: put({a.fog_start}); return;
    case 0xb64: put({a.fog_end}); return;
    case 0xb65: put({float(a.fog_mode)}); return;
    case 0xb66: put({a.fog_color[0], a.fog_color[1], a.fog_color[2], a.fog_color[3]}); return;
    case 0xb53: put({a.model_ambient[0], a.model_ambient[1], a.model_ambient[2], a.model_ambient[3]}); return;
    case 0xb51: boolean(a.local_viewer); return;
    case 0xb52: boolean(a.two_side); return;
    case 0xb55: put({float(a.color_material_face)}); return;
    case 0xb56: put({float(a.color_material_mode)}); return;
    case 0xb92: put({float(a.stencil_func)}); return;
    case 0xb93: put({float(a.stencil_value_mask)}); return;
    case 0xb94: put({float(a.stencil_fail)}); return;
    case 0xb95: put({float(a.stencil_zfail)}); return;
    case 0xb96: put({float(a.stencil_zpass)}); return;
    case 0xb97: put({float(a.stencil_ref)}); return;
    case 0xb98: put({float(a.stencil_write_mask)}); return;
    case 0xc50: case 0xc51: case 0xc52: case 0xc53: case 0xc54: put({float(a.hints[pname - 0xc50])}); return;
    case E::UnpackAlignment: put({float(s.client.unpack_alignment)}); return;
    case E::UnpackRowLength: put({float(s.client.unpack_row_length)}); return;
    case E::UnpackSkipRows: put({float(s.client.unpack_skip_rows)}); return;
    case E::UnpackSkipPixels: put({float(s.client.unpack_skip_pixels)}); return;
    case E::UnpackLsbFirst: boolean(s.client.unpack_lsb_first); return;
    case E::UnpackSwapBytes: boolean(s.client.unpack_swap); return;
    case E::PackAlignment: put({float(s.client.pack_alignment)}); return;
    case E::PackRowLength: put({float(s.client.pack_row_length)}); return;
    case E::PackSkipRows: put({float(s.client.pack_skip_rows)}); return;
    case E::PackSkipPixels: put({float(s.client.pack_skip_pixels)}); return;
    case E::PackLsbFirst: boolean(s.client.pack_lsb_first); return;
    case E::PackSwapBytes: boolean(s.client.pack_swap); return;
    case 0x807a: put({float(s.client.vertex.size)}); return;   // VERTEX_ARRAY_SIZE
    case 0x807b: put({float(s.client.vertex.type)}); return;
    case 0x807c: put({float(s.client.vertex.stride)}); return;
    case 0x8081: put({float(s.client.color.size)}); return;
    case 0x8082: put({float(s.client.color.type)}); return;
    case 0x8083: put({float(s.client.color.stride)}); return;
    case 0x8088: put({float(s.client.texcoord[s.client.active_texture].size)}); return;
    case 0x8089: put({float(s.client.texcoord[s.client.active_texture].type)}); return;
    case 0x808a: put({float(s.client.texcoord[s.client.active_texture].stride)}); return;
    case 0x807e: put({float(s.client.normal.type)}); return;
    case 0x807f: put({float(s.client.normal.stride)}); return;
    case 0x8038: put({a.offset_factor}); return;
    case 0x2a00: put({a.offset_units}); return;
    case 0xd16: put({a.zoom[0]}); return;                      // ZOOM_X
    case 0xd17: put({a.zoom[1]}); return;                      // ZOOM_Y
    default:
        break;
    }
    // A capability asks as a boolean.
    is_bool = true;
    out[0] = IsEnabled(pname) ? 1.0f : 0.0f;
}

void Executor::GetValues(U pname, U address, int kind)
{
    float values[16] = {};
    int n = 1;
    bool is_bool = false;
    Get(pname, values, n, is_bool);
    for (int i = 0; i < n; ++i)
    {
        switch (kind)
        {
        case 0: { const unsigned char b = values[i] != 0.0f; g.Write(address + i, &b, 1); break; }
        case 1:
        {
            // Colours and depth ranges map to the whole integer range.
            int32_t v;
            if ((pname == 0xc22 || pname == 0xb00 || pname == 0xb04 || pname == 0xb66 || pname == 0xb53 || pname == 0xb70 ||
                 pname == 0xb73 || pname == 0xbc2) && !is_bool)
                v = int32_t(std::max(-2147483647.0, std::min(2147483647.0, double(values[i]) * 2147483647.0)));
            else v = int32_t(std::lround(values[i]));
            g.Write(address + i * 4, &v, 4);
            break;
        }
        case 2: g.Write(address + i * 4, &values[i], 4); break;
        default: { const double d = values[i]; g.Write(address + i * 8, &d, 8); break; }
        }
    }
}

U Executor::Run(U slot, const U *w)
{
    if (const auto *extension = ExtensionFor(slot))
    {
        if (extension->legacy != ~0u)
        {
            if (w[0] < 0x84c0 || w[0] >= 0x84c2) { s.Error(E::InvalidEnum); return 0; }
            texcoord_unit = w[0] - 0x84c0;
            Attribute(Attributes_().calls[extension->legacy], w + 1);
            texcoord_unit = 0;
            return 0;
        }
        if (slot == ActiveTextureEntry || slot == ClientActiveTextureEntry)
        {
            if (s.inside) { s.Error(E::InvalidOperation); return 0; }
            if (w[0] < 0x84c0 || w[0] >= 0x84c2) { s.Error(E::InvalidEnum); return 0; }
            if (slot == ActiveTextureEntry) { a.active_texture = w[0] - 0x84c0; s.Changed(); }
            else s.client.active_texture = w[0] - 0x84c0;
            return 0;
        }
    }
    if (slot >= Win98OpenGL::DispatchCount) return 0;
    const auto &attr = Attributes_().calls[slot];
    if (attr.family != NotAttribute)
    {
        Attribute(attr, w);
        return 0;
    }
    // Between glBegin and glEnd only a few commands are allowed.
    if (s.inside)
    {
        switch (slot)
        {
        case S::EdgeFlag: case S::EdgeFlagv: case S::Materialf: case S::Materialfv: case S::Materiali:
        case S::Materialiv: case S::CallList: case S::CallLists: case S::End: case S::ArrayElement:
        case S::EvalPoint1: case S::EvalPoint2:
            break;
        default:
            s.Error(E::InvalidOperation);
            return 0;
        }
    }
    float v[16] = {};
    switch (slot)
    {
    // -- Lists --------------------------------------------------------------
    case S::CallList: return CallList(w[0]);
    case S::CallLists:
    {
        const int n = int(w[0]);
        const U type = w[1];
        const unsigned size = TypeBytes(type);
        for (int i = 0; i < n; ++i)
        {
            unsigned char p[4] = {};
            g.Read(w[2] + U(i) * size, p, size);
            U id;
            switch (type)
            {
            case E::Byte: id = U(int(int8_t(p[0]))); break;
            case E::UnsignedByte: id = p[0]; break;
            case E::Short: { int16_t q; memcpy(&q, p, 2); id = U(int(q)); break; }
            case E::UnsignedShort: { uint16_t q; memcpy(&q, p, 2); id = q; break; }
            case E::Int: case E::UnsignedInt: memcpy(&id, p, 4); break;
            case E::Float: { float f; memcpy(&f, p, 4); id = U(f); break; }
            case E::Bytes2: id = (U(p[0]) << 8) | p[1]; break;
            case E::Bytes3: id = (U(p[0]) << 16) | (U(p[1]) << 8) | p[2]; break;
            case E::Bytes4: id = (U(p[0]) << 24) | (U(p[1]) << 16) | (U(p[2]) << 8) | p[3]; break;
            default: s.Error(E::InvalidEnum); return 0;
            }
            CallList(id + a.list_base);
        }
        return 0;
    }
    case S::DeleteLists:
        for (U i = 0; i < w[1]; ++i) s.shared->lists.erase(w[0] + i);
        return 0;
    case S::GenLists:
    {
        const U range = w[0];
        if (!range) return 0;
        U base = 1;
        for (;;)
        {
            bool free_run = true;
            for (U i = 0; i < range; ++i)
                if (s.shared->lists.count(base + i)) { base = base + i + 1; free_run = false; break; }
            if (free_run) break;
        }
        for (U i = 0; i < range; ++i) s.shared->lists[base + i];
        return base;
    }
    case S::IsList: return s.shared->lists.count(w[0]) ? 1 : 0;
    case S::ListBase: a.list_base = w[0]; return 0;

    // -- Primitives -----------------------------------------------------------
    case S::Begin:
        if (w[0] > E::Polygon) { s.Error(E::InvalidEnum); return 0; }
        s.inside = true;
        s.mode = w[0];
        s.vertices.clear();
        return 0;
    case S::End:
        if (!s.inside) { s.Error(E::InvalidOperation); return 0; }
        Assemble(s);
        s.inside = false;
        return 0;
    case S::EdgeFlag: a.edge = (w[0] & 255) != 0; return 0;
    case S::EdgeFlagv: { unsigned char b = 1; g.Read(w[0], &b, 1); a.edge = b != 0; return 0; }
    case S::ArrayElement: Element(int(w[0])); return 0;
    case S::DrawArrays: DrawArrays(w[0], int(w[1]), int(w[2])); return 0;
    case S::DrawElements:
    {
        const int count = int(w[1]);
        const U type = w[2];
        const unsigned size = TypeBytes(type);
        if (count <= 0) return 0;
        if (type != E::UnsignedByte && type != E::UnsignedShort && type != E::UnsignedInt)
        { s.Error(E::InvalidEnum); return 0; }
        s.inside = true;
        s.mode = w[0];
        const unsigned char *indices = nullptr;
        U first = ~0u, last = 0;
        const bool bounded = count <= 1000000;
        auto index_at = [&](const unsigned char *p) { U index = 0; memcpy(&index, p, size); return index; };
        if (bounded)
        {
            indices = g.Span(w[3], U(count) * size);
            if (!indices)
            {
                s.index_bytes.resize(size_t(count) * size);
                g.Read(w[3], s.index_bytes.data(), U(count) * size);
                indices = s.index_bytes.data();
            }
            for (int i = 0; i < count; ++i)
            {
                const U index = index_at(indices + size_t(i) * size);
                first = std::min(first, index); last = std::max(last, index);
            }
        }
        const bool cache = bounded && uint64_t(last) - first < std::min(uint64_t(count) * 4, uint64_t(1000000));
        const bool indexed = cache && s.client.vertex.enabled && last - first < 65536 && s.mode == E::Triangles &&
            !a.lighting && a.shade == E::Smooth && a.polygon_mode[0] == E::Fill &&
            a.polygon_mode[1] == E::Fill && s.render_mode == E::Render;
        if (cache)
        {
            const size_t span = size_t(last - first) + 1;
            s.element_cache.resize(span);
            if (s.element_marks.size() < span) s.element_marks.resize(span, 0);
            if (!++s.element_epoch) { std::fill(s.element_marks.begin(), s.element_marks.end(), 0); ++s.element_epoch; }
            PrepareArrays(first, U(span));
        }
        s.vertices.reserve(count);
        for (int i = 0; i < count; ++i)
        {
            unsigned char p[4] = {};
            if (!indices) g.Read(w[3] + U(i) * size, p, size);
            const U index = index_at(indices ? indices + size_t(i) * size : p);
            if (cache && i + 1 != count && s.element_marks[index - first] == s.element_epoch)
            {
                if (!indexed) s.vertices.push_back(s.element_cache[index - first]);
            }
            else
            {
                const size_t before = s.vertices.size();
                Element(int(index));
                if (cache && s.vertices.size() != before)
                {
                    s.element_marks[index - first] = s.element_epoch;
                    s.element_cache[index - first] = s.vertices.back();
                }
                if (indexed) s.vertices.resize(before);
            }
        }
        if (indexed) QueueIndexed(s, indices, U(count), size, first, last - first + 1);
        else Assemble(s);
        s.inside = false;
        return 0;
    }

    // -- Client arrays --------------------------------------------------------
    case S::VertexPointer: s.client.vertex = {s.client.vertex.enabled, int(w[0]), w[1], int(w[2]), w[3]}; return 0;
    case S::ColorPointer: s.client.color = {s.client.color.enabled, int(w[0]), w[1], int(w[2]), w[3]}; return 0;
    case S::TexCoordPointer: s.client.texcoord[s.client.active_texture] = {s.client.texcoord[s.client.active_texture].enabled, int(w[0]), w[1], int(w[2]), w[3]}; return 0;
    case S::NormalPointer: s.client.normal = {s.client.normal.enabled, 3, w[0], int(w[1]), w[2]}; return 0;
    case S::IndexPointer: s.client.index = {s.client.index.enabled, 1, w[0], int(w[1]), w[2]}; return 0;
    case S::EdgeFlagPointer: s.client.edge = {s.client.edge.enabled, 1, E::UnsignedByte, int(w[0]), w[1]}; return 0;
    case S::EnableClientState: case S::DisableClientState:
    {
        const bool on = slot == S::EnableClientState;
        switch (w[0])
        {
        case E::VertexArray: s.client.vertex.enabled = on; break;
        case E::NormalArray: s.client.normal.enabled = on; break;
        case E::ColorArray: s.client.color.enabled = on; break;
        case E::IndexArray: s.client.index.enabled = on; break;
        case E::TextureCoordArray: s.client.texcoord[s.client.active_texture].enabled = on; break;
        case E::EdgeFlagArray: s.client.edge.enabled = on; break;
        default: s.Error(E::InvalidEnum); break;
        }
        return 0;
    }
    case S::InterleavedArrays:
    {
        // GL_V2F.. GL_T4F_C4F_N3F_V4F (0x2a20.. 0x2a2d).
        const U format = w[0];
        int stride = int(w[1]);
        const U p = w[2];
        struct Layout { int tc, cc, vc; U ctype; int toff, coff, noff, voff, size; bool n; };
        static const Layout layouts[] = {
            {0, 0, 2, 0, 0, 0, 0, 0, 8, false},          // V2F
            {0, 0, 3, 0, 0, 0, 0, 0, 12, false},         // V3F
            {0, 4, 2, E::UnsignedByte, 0, 0, 0, 4, 12, false},   // C4UB_V2F
            {0, 4, 3, E::UnsignedByte, 0, 0, 0, 4, 16, false},   // C4UB_V3F
            {0, 3, 3, E::Float, 0, 0, 0, 12, 24, false},  // C3F_V3F
            {0, 0, 3, 0, 0, 0, 0, 12, 24, true},          // N3F_V3F
            {0, 4, 3, E::Float, 0, 0, 16, 28, 40, true},  // C4F_N3F_V3F
            {2, 0, 3, 0, 0, 0, 0, 8, 20, false},          // T2F_V3F
            {4, 0, 4, 0, 0, 0, 0, 16, 32, false},         // T4F_V4F
            {2, 4, 3, E::UnsignedByte, 0, 8, 0, 12, 24, false},   // T2F_C4UB_V3F
            {2, 3, 3, E::Float, 0, 8, 0, 20, 32, false},  // T2F_C3F_V3F
            {2, 0, 3, 0, 0, 0, 8, 20, 32, true},          // T2F_N3F_V3F
            {2, 4, 3, E::Float, 0, 8, 24, 36, 48, true},  // T2F_C4F_N3F_V3F
            {4, 4, 4, E::Float, 0, 16, 32, 44, 60, true}, // T4F_C4F_N3F_V4F
        };
        if (format < 0x2a20 || format > 0x2a2d) { s.Error(E::InvalidEnum); return 0; }
        const Layout &l = layouts[format - 0x2a20];
        if (!stride) stride = l.size;
        auto &c = s.client;
        c.edge.enabled = c.index.enabled = false;
        c.texcoord[c.active_texture] = {l.tc != 0, l.tc ? l.tc : 4, E::Float, stride, p + U(l.toff)};
        c.color = {l.cc != 0, l.cc ? l.cc : 4, l.ctype ? l.ctype : E::Float, stride, p + U(l.coff)};
        c.normal = {l.n, 3, E::Float, stride, p + U(l.noff)};
        c.vertex = {true, l.vc, E::Float, stride, p + U(l.voff)};
        return 0;
    }
    case S::GetPointerv:
    {
        U value = 0;
        switch (w[0])
        {
        case 0x808e: value = s.client.vertex.pointer; break;
        case 0x808f: value = s.client.normal.pointer; break;
        case 0x8090: value = s.client.color.pointer; break;
        case 0x8091: value = s.client.index.pointer; break;
        case 0x8092: value = s.client.texcoord[s.client.active_texture].pointer; break;
        case 0x8093: value = s.client.edge.pointer; break;
        default: s.Error(E::InvalidEnum); return 0;
        }
        g.Write(w[1], &value, 4);
        return 0;
    }
    case S::PushClientAttrib:
        if (s.client_stack.size() >= 16) { s.Error(E::StackOverflow); return 0; }
        s.client_stack.push_back(s.client);
        return 0;
    case S::PopClientAttrib:
        if (s.client_stack.empty()) { s.Error(E::StackUnderflow); return 0; }
        s.client = s.client_stack.back();
        s.client_stack.pop_back();
        return 0;

    // -- Lighting, material, fog ---------------------------------------------
    case S::ColorMaterial: a.color_material_face = w[0]; a.color_material_mode = w[1]; s.Changed(); return 0;
    case S::Lightf: case S::Lighti: v[0] = slot == S::Lightf ? F(w, 2) : float(int(w[2])); Light(w[0], w[1], v); return 0;
    case S::Lightfv: case S::Lightiv:
        ReadFloats(w[2], v, 4, false, slot == S::Lightiv);
        if (slot == S::Lightiv && (w[1] == E::Ambient || w[1] == E::Diffuse || w[1] == E::Specular))
            for (int k = 0; k < 4; ++k) v[k] = float(double(int32_t(v[k])) / 2147483647.0);
        Light(w[0], w[1], v);
        return 0;
    case S::LightModelf: case S::LightModeli: v[0] = slot == S::LightModelf ? F(w, 1) : float(int(w[1])); LightModel(w[0], v); return 0;
    case S::LightModelfv: case S::LightModeliv:
        ReadFloats(w[1], v, 4, false, slot == S::LightModeliv);
        if (slot == S::LightModeliv && w[0] == E::LightModelAmbient)
            for (int k = 0; k < 4; ++k) v[k] = float(double(v[k]) / 2147483647.0);
        LightModel(w[0], v);
        return 0;
    case S::Materialf: case S::Materiali: v[0] = slot == S::Materialf ? F(w, 2) : float(int(w[2])); MaterialSet(w[0], w[1], v); return 0;
    case S::Materialfv: case S::Materialiv:
        ReadFloats(w[2], v, 4, false, slot == S::Materialiv);
        if (slot == S::Materialiv && w[1] != E::Shininess)
            for (int k = 0; k < 4; ++k) v[k] = float(double(v[k]) / 2147483647.0);
        MaterialSet(w[0], w[1], v);
        return 0;
    case S::ShadeModel: a.shade = w[0]; return 0;
    case S::Fogf: case S::Fogi: v[0] = slot == S::Fogf ? F(w, 1) : float(int(w[1])); Fog(w[0], v); return 0;
    case S::Fogfv: case S::Fogiv:
        ReadFloats(w[1], v, 4, false, slot == S::Fogiv);
        if (slot == S::Fogiv && w[0] == E::FogColor)
            for (int k = 0; k < 4; ++k) v[k] = float(double(v[k]) / 2147483647.0);
        Fog(w[0], v);
        return 0;

    // -- Rasterization state ----------------------------------------------------
    case S::CullFace: a.cull_face = w[0]; return 0;
    case S::FrontFace: a.front_face = w[0]; return 0;
    case S::PolygonMode:
        if (w[0] == E::Front || w[0] == E::FrontAndBack) a.polygon_mode[0] = w[1];
        if (w[0] == E::Back || w[0] == E::FrontAndBack) a.polygon_mode[1] = w[1];
        return 0;
    case S::PolygonOffset: a.offset_factor = F(w, 0); a.offset_units = F(w, 1); s.Changed(); return 0;
    case S::PolygonStipple: return 0;
    case S::GetPolygonStipple: { unsigned char none[128]; memset(none, 0xff, 128); g.Write(w[0], none, 128); return 0; }
    case S::LineStipple: a.line_repeat = w[0]; a.line_pattern = w[1] & 65535; return 0;
    case S::LineWidth: a.line_width = F(w, 0); return 0;
    case S::PointSize: a.point_size = F(w, 0); return 0;
    case S::Hint: if (w[0] >= 0xc50 && w[0] <= 0xc54) a.hints[w[0] - 0xc50] = w[1]; return 0;
    case S::Scissor:
        a.scissor[0] = int(w[0]); a.scissor[1] = int(w[1]); a.scissor[2] = int(w[2]); a.scissor[3] = int(w[3]);
        return 0;
    case S::Enable: Enable(w[0], true); return 0;
    case S::Disable: Enable(w[0], false); return 0;
    case S::IsEnabled: return IsEnabled(w[0]) ? 1 : 0;

    // -- Fragment state ------------------------------------------------------------
    case S::AlphaFunc: a.alpha_func = w[0]; a.alpha_ref = Clamp01(F(w, 1)); s.Changed(); return 0;
    case S::BlendFunc: a.blend_src = w[0]; a.blend_dst = w[1]; s.Changed(); return 0;
    case S::LogicOp: a.logic = w[0]; return 0;
    case S::StencilFunc: a.stencil_func = w[0]; a.stencil_ref = w[1]; a.stencil_value_mask = w[2]; s.Changed(); return 0;
    case S::StencilOp: a.stencil_fail = w[0]; a.stencil_zfail = w[1]; a.stencil_zpass = w[2]; s.Changed(); return 0;
    case S::StencilMask: a.stencil_write_mask = w[0]; s.Changed(); return 0;
    case S::DepthFunc: a.depth_func = w[0]; s.Changed(); return 0;
    case S::DepthMask: a.depth_mask = (w[0] & 255) != 0; s.Changed(); return 0;
    case S::ColorMask:
        for (int k = 0; k < 4; ++k) a.color_mask[k] = (w[k] & 255) != 0;
        s.Changed();
        return 0;
    case S::IndexMask: a.index_mask = w[0]; return 0;
    case S::DepthRange:
        a.depth_near = Clamp01(float(Double(w)));
        a.depth_far = Clamp01(float(Double(w + 2)));
        s.Changed();
        return 0;
    case S::Viewport:
        a.viewport[0] = int(w[0]); a.viewport[1] = int(w[1]); a.viewport[2] = int(w[2]); a.viewport[3] = int(w[3]);
        s.viewport_set = true;
        s.Changed();
        return 0;

    // -- Buffers --------------------------------------------------------------------
    case S::DrawBuffer: a.draw_buffer = w[0]; return 0;
    case S::ReadBuffer: a.read_buffer = w[0]; return 0;
    case S::Clear: Clear(w[0]); return 0;
    case S::ClearColor:
        a.clear_color[0] = Clamp01(F(w, 0)); a.clear_color[1] = Clamp01(F(w, 1));
        a.clear_color[2] = Clamp01(F(w, 2)); a.clear_color[3] = Clamp01(F(w, 3));
        return 0;
    case S::ClearDepth: a.clear_depth = Clamp01(float(Double(w))); return 0;
    case S::ClearStencil: a.clear_stencil = w[0]; return 0;
    case S::ClearIndex: a.clear_index = F(w, 0); return 0;
    case S::ClearAccum: for (int k = 0; k < 4; ++k) a.clear_accum[k] = F(w, k); return 0;
    case S::Accum: return 0;
    case S::Flush: case S::Finish:
    {
        s.Flush();
        auto &target = s.DrawTarget();
        if (&target == &s.front) s.present = true;
        return 0;
    }

    // -- Attribute stack ----------------------------------------------------------
    case S::PushAttrib:
        if (s.attribute_stack.size() >= 16) { s.Error(E::StackOverflow); return 0; }
        s.attribute_stack.emplace_back(w[0], a);
        return 0;
    case S::PopAttrib:
    {
        if (s.attribute_stack.empty()) { s.Error(E::StackUnderflow); return 0; }
        const U mask = s.attribute_stack.back().first;
        const Attributes old = s.attribute_stack.back().second;
        s.attribute_stack.pop_back();
        Attributes &n = a;
        if (mask & 0x1)   // CURRENT
        {
            memcpy(n.color, old.color, 16); memcpy(n.normal, old.normal, 12); for (unsigned u = 0; u < 2; ++u) memcpy(n.units[u].texcoord, old.units[u].texcoord, 16);
            memcpy(n.raster, old.raster, 16); memcpy(n.raster_color, old.raster_color, 16);
            for (unsigned u = 0; u < 2; ++u) memcpy(n.units[u].raster_tex, old.units[u].raster_tex, 16); n.raster_valid = old.raster_valid; n.edge = old.edge; n.index = old.index;
        }
        if (mask & 0x2) { n.point_size = old.point_size; n.point_smooth = old.point_smooth; }
        if (mask & 0x4) { n.line_width = old.line_width; n.line_pattern = old.line_pattern; n.line_repeat = old.line_repeat; n.line_smooth = old.line_smooth; n.line_stipple = old.line_stipple; }
        if (mask & 0x8)
        {
            n.cull_face = old.cull_face; n.front_face = old.front_face; n.polygon_mode[0] = old.polygon_mode[0];
            n.polygon_mode[1] = old.polygon_mode[1]; n.offset_factor = old.offset_factor; n.offset_units = old.offset_units;
            n.cull = old.cull; n.polygon_smooth = old.polygon_smooth; n.polygon_stipple = old.polygon_stipple;
            n.offset_fill = old.offset_fill; n.offset_line = old.offset_line; n.offset_point = old.offset_point;
        }
        if (mask & 0x40)
        {
            memcpy(n.lights, old.lights, sizeof n.lights); memcpy(n.light_on, old.light_on, sizeof n.light_on);
            n.material[0] = old.material[0]; n.material[1] = old.material[1];
            memcpy(n.model_ambient, old.model_ambient, 16); n.local_viewer = old.local_viewer; n.two_side = old.two_side;
            n.lighting = old.lighting; n.color_material = old.color_material; n.color_material_face = old.color_material_face;
            n.color_material_mode = old.color_material_mode; n.shade = old.shade;
        }
        if (mask & 0x80) { n.fog = old.fog; n.fog_mode = old.fog_mode; n.fog_density = old.fog_density; n.fog_start = old.fog_start; n.fog_end = old.fog_end; memcpy(n.fog_color, old.fog_color, 16); }
        if (mask & 0x100) { n.depth_test = old.depth_test; n.depth_mask = old.depth_mask; n.depth_func = old.depth_func; n.clear_depth = old.clear_depth; }
        if (mask & 0x200) memcpy(n.clear_accum, old.clear_accum, 16);
        if (mask & 0x400)
        {
            n.stencil_test = old.stencil_test; n.stencil_func = old.stencil_func; n.stencil_ref = old.stencil_ref;
            n.stencil_value_mask = old.stencil_value_mask; n.stencil_write_mask = old.stencil_write_mask;
            n.stencil_fail = old.stencil_fail; n.stencil_zfail = old.stencil_zfail; n.stencil_zpass = old.stencil_zpass;
            n.clear_stencil = old.clear_stencil;
        }
        if (mask & 0x800) { memcpy(n.viewport, old.viewport, 16); n.depth_near = old.depth_near; n.depth_far = old.depth_far; }
        if (mask & 0x1000) { n.matrix_mode = old.matrix_mode; memcpy(n.clip_planes, old.clip_planes, sizeof n.clip_planes); memcpy(n.clip_on, old.clip_on, 6); n.normalize = old.normalize; }
        if (mask & 0x2000)
        {
            n.alpha_test = old.alpha_test; n.auto_normal = old.auto_normal; n.blend = old.blend;
            memcpy(n.clip_on, old.clip_on, 6); n.color_material = old.color_material; n.cull = old.cull;
            n.depth_test = old.depth_test; n.dither = old.dither; n.fog = old.fog; memcpy(n.light_on, old.light_on, 8);
            n.lighting = old.lighting; n.line_smooth = old.line_smooth; n.line_stipple = old.line_stipple;
            n.logic_op = old.logic_op; n.normalize = old.normalize; n.point_smooth = old.point_smooth;
            n.polygon_smooth = old.polygon_smooth; n.polygon_stipple = old.polygon_stipple; n.scissor_test = old.scissor_test;
            n.stencil_test = old.stencil_test;
            for (unsigned u = 0; u < 2; ++u) {
                n.units[u].texture_1d = old.units[u].texture_1d; n.units[u].texture_2d = old.units[u].texture_2d;
                memcpy(n.units[u].gen_on, old.units[u].gen_on, sizeof n.units[u].gen_on);
            } n.offset_fill = old.offset_fill; n.offset_line = old.offset_line;
            n.offset_point = old.offset_point;
        }
        if (mask & 0x4000)
        {
            n.alpha_test = old.alpha_test; n.alpha_func = old.alpha_func; n.alpha_ref = old.alpha_ref; n.blend = old.blend;
            n.blend_src = old.blend_src; n.blend_dst = old.blend_dst; n.dither = old.dither; n.logic_op = old.logic_op;
            n.logic = old.logic; memcpy(n.clear_color, old.clear_color, 16); memcpy(n.color_mask, old.color_mask, 4);
            n.draw_buffer = old.draw_buffer; n.clear_index = old.clear_index; n.index_mask = old.index_mask;
        }
        if (mask & 0x8000) memcpy(n.hints, old.hints, sizeof n.hints);
        if (mask & 0x10000) n.auto_normal = old.auto_normal;
        if (mask & 0x20000) n.list_base = old.list_base;
        if (mask & 0x40000)
        {
            for (unsigned u = 0; u < 2; ++u) {
                const auto coords = n.units[u];
                n.units[u] = old.units[u];
                memcpy(n.units[u].texcoord, coords.texcoord, 16);
                memcpy(n.units[u].raster_tex, coords.raster_tex, 16);
            }
            n.active_texture = old.active_texture;
        }
        if (mask & 0x80000) { n.scissor_test = old.scissor_test; memcpy(n.scissor, old.scissor, 16); }
        if (mask & 0x20) { n.read_buffer = old.read_buffer; memcpy(n.zoom, old.zoom, 8); }
        s.Changed();
        return 0;
    }

    // -- Matrices ---------------------------------------------------------------------
    case S::MatrixMode: a.matrix_mode = w[0]; return 0;
    case S::LoadIdentity: LoadMatrix(Matrix::Identity()); return 0;
    case S::LoadMatrixf: case S::MultMatrixf: case S::LoadMatrixd: case S::MultMatrixd:
    {
        Matrix m;
        const bool d = slot == S::LoadMatrixd || slot == S::MultMatrixd;
        ReadFloats(w[0], m.m, 16, d);
        if (slot == S::LoadMatrixf || slot == S::LoadMatrixd) LoadMatrix(m);
        else MultMatrix(m);
        return 0;
    }
    case S::PushMatrix:
    {
        auto &stack = s.Stack(a.matrix_mode);
        const size_t limit = a.matrix_mode == E::Modelview ? 32 : 2;
        if (stack.size() >= limit) { s.Error(E::StackOverflow); return 0; }
        stack.push_back(stack.back());
        return 0;
    }
    case S::PopMatrix:
    {
        auto &stack = s.Stack(a.matrix_mode);
        if (stack.size() <= 1) { s.Error(E::StackUnderflow); return 0; }
        stack.pop_back();
        s.MatrixChanged();
        return 0;
    }
    case S::Rotated: case S::Rotatef:
    {
        float angle, x, y, z;
        if (slot == S::Rotated) { angle = float(Double(w)); x = float(Double(w + 2)); y = float(Double(w + 4)); z = float(Double(w + 6)); }
        else { angle = F(w, 0); x = F(w, 1); y = F(w, 2); z = F(w, 3); }
        const float l = std::sqrt(x * x + y * y + z * z);
        if (l == 0.0f) return 0;
        x /= l; y /= l; z /= l;
        const float r = angle * 3.14159265358979f / 180.0f, c = std::cos(r), sn = std::sin(r), t = 1.0f - c;
        Matrix m = Matrix::Identity();
        m.m[0] = x * x * t + c;     m.m[4] = x * y * t - z * sn; m.m[8] = x * z * t + y * sn;
        m.m[1] = y * x * t + z * sn; m.m[5] = y * y * t + c;     m.m[9] = y * z * t - x * sn;
        m.m[2] = x * z * t - y * sn; m.m[6] = y * z * t + x * sn; m.m[10] = z * z * t + c;
        MultMatrix(m);
        return 0;
    }
    case S::Scaled: case S::Scalef:
    {
        Matrix m = Matrix::Identity();
        if (slot == S::Scaled) { m.m[0] = float(Double(w)); m.m[5] = float(Double(w + 2)); m.m[10] = float(Double(w + 4)); }
        else { m.m[0] = F(w, 0); m.m[5] = F(w, 1); m.m[10] = F(w, 2); }
        MultMatrix(m);
        return 0;
    }
    case S::Translated: case S::Translatef:
    {
        Matrix m = Matrix::Identity();
        if (slot == S::Translated) { m.m[12] = float(Double(w)); m.m[13] = float(Double(w + 2)); m.m[14] = float(Double(w + 4)); }
        else { m.m[12] = F(w, 0); m.m[13] = F(w, 1); m.m[14] = F(w, 2); }
        MultMatrix(m);
        return 0;
    }
    case S::Frustum: case S::Ortho:
    {
        const double l = Double(w), r = Double(w + 2), b = Double(w + 4), t = Double(w + 6), n = Double(w + 8), f = Double(w + 10);
        if (l == r || b == t || n == f || (slot == S::Frustum && (n <= 0 || f <= 0))) { s.Error(E::InvalidValue); return 0; }
        Matrix m{};
        if (slot == S::Frustum)
        {
            m.m[0] = float(2 * n / (r - l));
            m.m[5] = float(2 * n / (t - b));
            m.m[8] = float((r + l) / (r - l));
            m.m[9] = float((t + b) / (t - b));
            m.m[10] = float(-(f + n) / (f - n));
            m.m[11] = -1.0f;
            m.m[14] = float(-2 * f * n / (f - n));
        }
        else
        {
            m = Matrix::Identity();
            m.m[0] = float(2 / (r - l));
            m.m[5] = float(2 / (t - b));
            m.m[10] = float(-2 / (f - n));
            m.m[12] = float(-(r + l) / (r - l));
            m.m[13] = float(-(t + b) / (t - b));
            m.m[14] = float(-(f + n) / (f - n));
        }
        MultMatrix(m);
        return 0;
    }
    case S::ClipPlane:
    {
        if (w[0] < E::ClipPlane0 || w[0] >= E::ClipPlane0 + 6) { s.Error(E::InvalidEnum); return 0; }
        double p[4] = {};
        g.Read(w[1], p, 32);
        const Matrix inv = s.modelview.back().Inverse();
        double *out = a.clip_planes[w[0] - E::ClipPlane0];
        for (int k = 0; k < 4; ++k)
            out[k] = p[0] * inv.m[k * 4 + 0] + p[1] * inv.m[k * 4 + 1] + p[2] * inv.m[k * 4 + 2] + p[3] * inv.m[k * 4 + 3];
        return 0;
    }
    case S::GetClipPlane:
    {
        if (w[0] < E::ClipPlane0 || w[0] >= E::ClipPlane0 + 6) { s.Error(E::InvalidEnum); return 0; }
        g.Write(w[1], a.clip_planes[w[0] - E::ClipPlane0], 32);
        return 0;
    }

    // -- Textures -------------------------------------------------------------------------
    case S::TexImage1D: TexImage(w[0], int(w[1]), w[2], int(w[3]), 1, int(w[4]), w[5], w[6], w[7]); return 0;
    case S::TexImage2D: TexImage(w[0], int(w[1]), w[2], int(w[3]), int(w[4]), int(w[5]), w[6], w[7], w[8]); return 0;
    case S::TexSubImage1D: TexSubImage(w[0], int(w[1]), int(w[2]), 0, int(w[3]), 1, w[4], w[5], w[6]); return 0;
    case S::TexSubImage2D: TexSubImage(w[0], int(w[1]), int(w[2]), int(w[3]), int(w[4]), int(w[5]), w[6], w[7], w[8]); return 0;
    case S::CopyTexImage1D: CopyTexImage(w[0], int(w[1]), w[2], int(w[3]), int(w[4]), int(w[5]), 1, false, 0, 0); return 0;
    case S::CopyTexImage2D: CopyTexImage(w[0], int(w[1]), w[2], int(w[3]), int(w[4]), int(w[5]), int(w[6]), false, 0, 0); return 0;
    case S::CopyTexSubImage1D: CopyTexImage(w[0], int(w[1]), 0, int(w[3]), int(w[4]), int(w[5]), 1, true, int(w[2]), 0); return 0;
    case S::CopyTexSubImage2D: CopyTexImage(w[0], int(w[1]), 0, int(w[4]), int(w[5]), int(w[6]), int(w[7]), true, int(w[2]), int(w[3])); return 0;
    case S::TexParameterf: case S::TexParameteri:
        v[0] = slot == S::TexParameterf ? F(w, 2) : float(int(w[2]));
        TexParameter(w[0], w[1], v);
        return 0;
    case S::TexParameterfv: case S::TexParameteriv:
        ReadFloats(w[2], v, 4, false, slot == S::TexParameteriv);
        TexParameter(w[0], w[1], v);
        return 0;
    case S::TexEnvf: case S::TexEnvi: v[0] = slot == S::TexEnvf ? F(w, 2) : float(int(w[2])); TexEnv(w[1], v); return 0;
    case S::TexEnvfv: case S::TexEnviv: ReadFloats(w[2], v, 4, false, slot == S::TexEnviv); TexEnv(w[1], v); return 0;
    case S::TexGend: v[0] = float(Double(w + 2)); TexGen(w[0], w[1], v); return 0;
    case S::TexGenf: case S::TexGeni: v[0] = slot == S::TexGenf ? F(w, 2) : float(int(w[2])); TexGen(w[0], w[1], v); return 0;
    case S::TexGendv: ReadFloats(w[2], v, 4, true); TexGen(w[0], w[1], v); return 0;
    case S::TexGenfv: case S::TexGeniv: ReadFloats(w[2], v, 4, false, slot == S::TexGeniv); TexGen(w[0], w[1], v); return 0;
    case S::BindTexture:
    {
        const U target = w[0], name = w[1];
        if (target != E::Texture1D && target != E::Texture2D) { s.Error(E::InvalidEnum); return 0; }
        if (name)
        {
            auto &slot_ = s.shared->textures[name];
            if (!slot_)
            {
                slot_.reset(new TextureObject);
                slot_->name = name;
                slot_->target = target;
                slot_->handle = s.NewHandle();
            }
            // A name from glGenTextures has no target until its first bind.
            else if (!slot_->target) slot_->target = target;
            else if (slot_->target != target) { s.Error(E::InvalidOperation); return 0; }
        }
        (target == E::Texture1D ? a.units[a.active_texture].bound_1d : a.units[a.active_texture].bound_2d) = name;
        s.Changed();
        return 0;
    }
    case S::GenTextures:
    {
        const int n = int(w[0]);
        U next = 1;
        for (int i = 0; i < n; ++i)
        {
            while (s.shared->textures.count(next)) ++next;
            // Reserve the name with an empty object of no target yet.
            auto &slot_ = s.shared->textures[next];
            slot_.reset(new TextureObject);
            slot_->name = next;
            slot_->target = 0;
            slot_->handle = s.NewHandle();
            g.Write(w[1] + U(i) * 4, &next, 4);
            ++next;
        }
        return 0;
    }
    case S::DeleteTextures:
    {
        s.Flush();
        for (int i = 0; i < int(w[0]); ++i)
        {
            U name = 0;
            g.Read(w[1] + U(i) * 4, &name, 4);
            auto found = s.shared->textures.find(name);
            if (!name || found == s.shared->textures.end()) continue;
            s.backend.Forget(s.owner, found->second->handle);
            s.shared->textures.erase(found);
            for (auto &unit : a.units) {
                if (unit.bound_1d == name) unit.bound_1d = 0;
                if (unit.bound_2d == name) unit.bound_2d = 0;
            }
        }
        s.Changed();
        return 0;
    }
    case S::IsTexture:
    {
        auto found = s.shared->textures.find(w[0]);
        return found != s.shared->textures.end() && found->second->target ? 1 : 0;
    }
    case S::AreTexturesResident:
    {
        const unsigned char yes = 1;
        for (int i = 0; i < int(w[0]); ++i) g.Write(w[2] + U(i), &yes, 1);
        return 1;
    }
    case S::PrioritizeTextures:
        for (int i = 0; i < int(w[0]); ++i)
        {
            U name = 0;
            float priority = 1.0f;
            g.Read(w[1] + U(i) * 4, &name, 4);
            g.Read(w[2] + U(i) * 4, &priority, 4);
            auto found = s.shared->textures.find(name);
            if (found != s.shared->textures.end()) found->second->priority = priority;
        }
        return 0;
    case S::GetTexImage:
    {
        TextureObject *t = s.Bound(w[0]);
        const int level = int(w[1]);
        if (!t || level < 0 || level >= TextureObject::MaxLevels) return 0;
        const auto &l = t->levels[level];
        const U format = w[2], type = w[3];
        if (type != E::UnsignedByte) return 0;
        const int components = format == E::Rgba || format == E::Bgra ? 4 : format == E::Rgb || format == E::Bgr ? 3 : format == E::LuminanceAlpha ? 2 : 1;
        std::vector<unsigned char> out(size_t(l.width) * l.height * components);
        for (size_t i = 0; i < size_t(l.width) * l.height; ++i)
        {
            const U p = l.texels[i];
            const unsigned char r = (p >> 16) & 255, gg = (p >> 8) & 255, b = p & 255, al = p >> 24;
            unsigned char *d = &out[i * components];
            switch (format)
            {
            case E::Rgba: d[0] = r; d[1] = gg; d[2] = b; d[3] = al; break;
            case E::Bgra: d[0] = b; d[1] = gg; d[2] = r; d[3] = al; break;
            case E::Rgb: d[0] = r; d[1] = gg; d[2] = b; break;
            case E::Bgr: d[0] = b; d[1] = gg; d[2] = r; break;
            case E::LuminanceAlpha: d[0] = r; d[1] = al; break;
            case E::Alpha: d[0] = al; break;
            default: d[0] = r; break;
            }
        }
        g.Write(w[4], out.data(), U(out.size()));
        return 0;
    }
    case S::GetTexParameterfv: case S::GetTexParameteriv:
    {
        TextureObject *t = s.Bound(w[0]);
        if (!t) return 0;
        float values[4] = {};
        int n = 1;
        switch (w[1])
        {
        case E::MinFilter: values[0] = float(t->min_filter); break;
        case E::MagFilter: values[0] = float(t->mag_filter); break;
        case E::WrapS: values[0] = float(t->wrap_s); break;
        case E::WrapT: values[0] = float(t->wrap_t); break;
        case E::BorderColor: memcpy(values, t->border, 16); n = 4; break;
        case E::TexturePriority: values[0] = t->priority; break;
        case E::TextureResident: values[0] = 1; break;
        default: s.Error(E::InvalidEnum); return 0;
        }
        for (int k = 0; k < n; ++k)
        {
            if (slot == S::GetTexParameterfv) g.Write(w[2] + U(k) * 4, &values[k], 4);
            else { const int32_t q = int32_t(values[k]); g.Write(w[2] + U(k) * 4, &q, 4); }
        }
        return 0;
    }
    case S::GetTexLevelParameterfv: case S::GetTexLevelParameteriv:
    {
        const int level = int(w[1]);
        float value = 0;
        if ((w[0] == 0x8063 || w[0] == 0x8064) && level >= 0 && level < TextureObject::MaxLevels)
        {
            const auto &p = s.proxy[w[0] - 0x8063][level];
            switch (w[2])
            {
            case 0x1000: value = float(p.width); break;
            case 0x1001: value = float(p.height); break;
            case 0x1003: value = float(p.internal); break;
            case 0x805c: case 0x805d: case 0x805e: case 0x805f: case 0x8060: case 0x8061: value = p.width ? 8.0f : 0.0f; break;
            default: break;
            }
            if (slot == S::GetTexLevelParameterfv) g.Write(w[3], &value, 4);
            else { const int32_t q = int32_t(value); g.Write(w[3], &q, 4); }
            return 0;
        }
        TextureObject *t = s.Bound(w[0]);
        if (t && level >= 0 && level < TextureObject::MaxLevels)
        {
            const auto &l = t->levels[level];
            switch (w[2])
            {
            case 0x1000: value = float(l.width); break;               // TEXTURE_WIDTH
            case 0x1001: value = float(l.height); break;              // TEXTURE_HEIGHT
            case 0x1003: value = float(t->internal_format); break;    // TEXTURE_COMPONENTS
            case 0x1005: value = 0; break;                            // TEXTURE_BORDER
            case 0x805c: case 0x805d: case 0x805e: case 0x805f: value = 8; break;   // RED..ALPHA_SIZE
            case 0x8060: case 0x8061: value = 8; break;               // LUMINANCE, INTENSITY size
            default: break;
            }
        }
        if (slot == S::GetTexLevelParameterfv) g.Write(w[3], &value, 4);
        else { const int32_t q = int32_t(value); g.Write(w[3], &q, 4); }
        return 0;
    }
    case S::GetTexEnvfv: case S::GetTexEnviv:
    {
        float values[4] = {float(a.units[a.active_texture].env_mode), 0, 0, 0};
        int n = 1;
        if (w[1] == E::TextureEnvColor) { memcpy(values, a.units[a.active_texture].env_color, 16); n = 4; }
        for (int k = 0; k < n; ++k)
        {
            if (slot == S::GetTexEnvfv) g.Write(w[2] + U(k) * 4, &values[k], 4);
            else { const int32_t q = int32_t(values[k]); g.Write(w[2] + U(k) * 4, &q, 4); }
        }
        return 0;
    }
    case S::GetTexGendv: case S::GetTexGenfv: case S::GetTexGeniv:
    {
        if (w[0] < E::S_ || w[0] > E::Q_) return 0;
        const int c = int(w[0] - E::S_);
        float values[4] = {float(a.units[a.active_texture].gen_mode[c]), 0, 0, 0};
        int n = 1;
        if (w[1] == E::ObjectPlane) { memcpy(values, a.units[a.active_texture].object_plane[c], 16); n = 4; }
        if (w[1] == E::EyePlane) { memcpy(values, a.units[a.active_texture].eye_plane[c], 16); n = 4; }
        for (int k = 0; k < n; ++k)
        {
            if (slot == S::GetTexGendv) { const double d = values[k]; g.Write(w[2] + U(k) * 8, &d, 8); }
            else if (slot == S::GetTexGenfv) g.Write(w[2] + U(k) * 4, &values[k], 4);
            else { const int32_t q = int32_t(values[k]); g.Write(w[2] + U(k) * 4, &q, 4); }
        }
        return 0;
    }

    // -- Pixels ---------------------------------------------------------------------------
    case S::PixelStoref: case S::PixelStorei:
    {
        const int value = slot == S::PixelStoref ? int(F(w, 1)) : int(w[1]);
        auto &c = s.client;
        switch (w[0])
        {
        case E::UnpackAlignment: if (value == 1 || value == 2 || value == 4 || value == 8) c.unpack_alignment = value; else s.Error(E::InvalidValue); break;
        case E::UnpackRowLength: c.unpack_row_length = value; break;
        case E::UnpackSkipRows: c.unpack_skip_rows = value; break;
        case E::UnpackSkipPixels: c.unpack_skip_pixels = value; break;
        case E::UnpackLsbFirst: c.unpack_lsb_first = value != 0; break;
        case E::UnpackSwapBytes: c.unpack_swap = value != 0; break;
        case E::PackAlignment: if (value == 1 || value == 2 || value == 4 || value == 8) c.pack_alignment = value; else s.Error(E::InvalidValue); break;
        case E::PackRowLength: c.pack_row_length = value; break;
        case E::PackSkipRows: c.pack_skip_rows = value; break;
        case E::PackSkipPixels: c.pack_skip_pixels = value; break;
        case E::PackLsbFirst: c.pack_lsb_first = value != 0; break;
        case E::PackSwapBytes: c.pack_swap = value != 0; break;
        default: s.Error(E::InvalidEnum); break;
        }
        return 0;
    }
    case S::PixelZoom: a.zoom[0] = F(w, 0); a.zoom[1] = F(w, 1); return 0;
    case S::PixelTransferf: case S::PixelTransferi: return 0;
    case S::PixelMapfv: case S::PixelMapuiv: case S::PixelMapusv: return 0;
    case S::GetPixelMapfv: case S::GetPixelMapuiv: case S::GetPixelMapusv: { const U zero = 0; g.Write(w[1], &zero, 4); return 0; }
    case S::DrawPixels: DrawPixels(int(w[0]), int(w[1]), w[2], w[3], w[4]); return 0;
    case S::ReadPixels: ReadPixels(int(w[0]), int(w[1]), int(w[2]), int(w[3]), w[4], w[5], w[6]); return 0;
    case S::CopyPixels: CopyPixels(int(w[0]), int(w[1]), int(w[2]), int(w[3]), w[4]); return 0;
    case S::Bitmap: Bitmap(int(w[0]), int(w[1]), F(w, 2), F(w, 3), F(w, 4), F(w, 5), w[6]); return 0;

    // -- Queries --------------------------------------------------------------------------
    case S::GetError: { const U e = s.error; s.error = 0; return e; }
    case S::GetString: return g.String(w[0]);
    case S::GetBooleanv: GetValues(w[0], w[1], 0); return 0;
    case S::GetIntegerv: GetValues(w[0], w[1], 1); return 0;
    case S::GetFloatv: GetValues(w[0], w[1], 2); return 0;
    case S::GetDoublev: GetValues(w[0], w[1], 3); return 0;
    case S::GetLightfv: case S::GetLightiv:
    {
        if (w[0] < E::Light0 || w[0] >= E::Light0 + 8) { s.Error(E::InvalidEnum); return 0; }
        const auto &l = a.lights[w[0] - E::Light0];
        float values[4] = {};
        int n = 1;
        switch (w[1])
        {
        case E::Ambient: memcpy(values, l.ambient, 16); n = 4; break;
        case E::Diffuse: memcpy(values, l.diffuse, 16); n = 4; break;
        case E::Specular: memcpy(values, l.specular, 16); n = 4; break;
        case E::Position: memcpy(values, l.position, 16); n = 4; break;
        case E::SpotDirection: memcpy(values, l.direction, 12); n = 3; break;
        case E::SpotExponent: values[0] = l.exponent; break;
        case E::SpotCutoff: values[0] = l.cutoff; break;
        case E::ConstantAttenuation: values[0] = l.attenuation[0]; break;
        case E::LinearAttenuation: values[0] = l.attenuation[1]; break;
        case E::QuadraticAttenuation: values[0] = l.attenuation[2]; break;
        default: s.Error(E::InvalidEnum); return 0;
        }
        for (int k = 0; k < n; ++k)
        {
            if (slot == S::GetLightfv) g.Write(w[2] + U(k) * 4, &values[k], 4);
            else { const int32_t q = int32_t(values[k]); g.Write(w[2] + U(k) * 4, &q, 4); }
        }
        return 0;
    }
    case S::GetMaterialfv: case S::GetMaterialiv:
    {
        const auto &m = a.material[w[0] == E::Back ? 1 : 0];
        float values[4] = {};
        int n = 4;
        switch (w[1])
        {
        case E::Ambient: memcpy(values, m.ambient, 16); break;
        case E::Diffuse: memcpy(values, m.diffuse, 16); break;
        case E::Specular: memcpy(values, m.specular, 16); break;
        case E::Emission: memcpy(values, m.emission, 16); break;
        case E::Shininess: values[0] = m.shininess; n = 1; break;
        default: s.Error(E::InvalidEnum); return 0;
        }
        for (int k = 0; k < n; ++k)
        {
            if (slot == S::GetMaterialfv) g.Write(w[2] + U(k) * 4, &values[k], 4);
            else { const int32_t q = int32_t(values[k]); g.Write(w[2] + U(k) * 4, &q, 4); }
        }
        return 0;
    }
    case S::GetMapdv: case S::GetMapfv: case S::GetMapiv: return 0;

    // -- Selection and feedback: the modes, without records ------------------------
    case S::RenderMode:
    {
        const U old = s.render_mode;
        s.render_mode = w[0];
        (void)old;
        return 0;
    }
    case S::FeedbackBuffer: case S::SelectBuffer: case S::InitNames: case S::LoadName: case S::PassThrough:
    case S::PopName: case S::PushName:
        return 0;

    // -- Evaluators: not built ----------------------------------------------------------
    case S::Map1d: case S::Map1f: case S::Map2d: case S::Map2f: case S::MapGrid1d: case S::MapGrid1f:
    case S::MapGrid2d: case S::MapGrid2f: case S::EvalMesh1: case S::EvalPoint1: case S::EvalMesh2: case S::EvalPoint2:
        return 0;

    default:
        return 0;
    }
}
} // namespace

// ---------------------------------------------------------------------------
unsigned Context::Call(unsigned slot, const unsigned *args, Guest &guest)
{
    RenderProfile::Scope profile(RenderProfile::GlCall, slot);
    auto &s = *state;
    // Array locks are immediate client state, including during list compilation.
    if (slot == LockArraysEntry || slot == UnlockArraysEntry)
    {
        auto &c = s.client;
        if (s.inside || (slot == LockArraysEntry ? c.arrays_locked : !c.arrays_locked))
        { s.Error(E::InvalidOperation); return 0; }
        if (slot == LockArraysEntry)
        {
            if (int(args[0]) < 0 || int(args[1]) <= 0) { s.Error(E::InvalidValue); return 0; }
            c.lock_first = int(args[0]); c.lock_count = int(args[1]); c.arrays_locked = true;
        }
        else { c.arrays_locked = false; c.lock_first = c.lock_count = 0; }
        return 0;
    }
    if (slot >= Win98OpenGL::DispatchCount && !ExtensionFor(slot)) return 0;
    // Display list compilation.
    if (slot == S::NewList)
    {
        if (!args[0] || s.compiling) { s.Error(args[0] ? E::InvalidOperation : E::InvalidValue); return 0; }
        if (args[1] != E::Compile && args[1] != E::CompileAndExecute) { s.Error(E::InvalidEnum); return 0; }
        s.compiling = args[0];
        s.list_mode = args[1];
        s.compiled = DisplayList{};
        return 0;
    }
    if (slot == S::EndList)
    {
        if (!s.compiling) { s.Error(E::InvalidOperation); return 0; }
        s.shared->lists[s.compiling] = std::make_shared<DisplayList>(std::move(s.compiled));
        s.compiled = DisplayList{};
        s.compiling = 0;
        return 0;
    }
    if (s.compiling && !NeverCompiled(slot))
    {
        // Vertex arrays are dereferenced now; the list keeps the values.
        if (slot == S::ArrayElement || slot == S::DrawArrays || slot == S::DrawElements)
        {
            // Run them into a recorder: attribute commands with values.
            struct Recorder final : public Guest
            {
                Guest &outer;
                explicit Recorder(Guest &o) : outer(o) {}
                bool Read(unsigned address, void *out, unsigned bytes) override { return outer.Read(address, out, bytes); }
                bool Write(unsigned address, const void *in, unsigned bytes) override { return outer.Write(address, in, bytes); }
                unsigned String(unsigned name) override { return outer.String(name); }
            } recorder(guest);
            auto record = [&](U cmd_slot, std::initializer_list<float> values) {
                Command c;
                c.slot = cmd_slot;
                int i = 0;
                for (float f : values) c.args[i++] = Word(f);
                s.compiled.commands.push_back(c);
            };
            auto element = [&](int i) {
                auto &cl = s.client;
                auto fetch = [&](const ClientArray &arr, int components, float *out, bool normalized) {
                    const unsigned size = TypeBytes(arr.type);
                    const int stride = arr.stride ? arr.stride : int(size) * components;
                    unsigned char p[64] = {};
                    recorder.Read(arr.pointer + U(i * stride), p, unsigned(components) * size);
                    for (int k = 0; k < components; ++k) out[k] = Component(p + k * size, arr.type, normalized);
                };
                for (unsigned unit = 0; unit < 2; ++unit) if (cl.texcoord[unit].enabled) {
                    float t[4] = {0, 0, 0, 1}; fetch(cl.texcoord[unit], cl.texcoord[unit].size, t, false);
                    Command c; c.slot = MultiTexCoordBase + 26; c.args[0] = 0x84c0 + unit;
                    for (unsigned k = 0; k < 4; ++k) c.args[k + 1] = Word(t[k]);
                    s.compiled.commands.push_back(c);
                }
                if (cl.color.enabled) { float c4[4] = {0, 0, 0, 1}; fetch(cl.color, cl.color.size, c4, true); record(S::Color4f, {c4[0], c4[1], c4[2], c4[3]}); }
                if (cl.normal.enabled) { float n[3]; fetch(cl.normal, 3, n, true); record(S::Normal3f, {n[0], n[1], n[2]}); }
                if (cl.vertex.enabled) { float v[4] = {0, 0, 0, 1}; fetch(cl.vertex, cl.vertex.size, v, false); record(S::Vertex4f, {v[0], v[1], v[2], v[3]}); }
            };
            if (slot == S::ArrayElement) element(int(args[0]));
            else
            {
                Command begin;
                begin.slot = S::Begin;
                begin.args[0] = args[0];
                s.compiled.commands.push_back(begin);
                if (slot == S::DrawArrays)
                    for (int i = 0; i < int(args[2]); ++i) element(int(args[1]) + i);
                else
                {
                    const unsigned size = TypeBytes(args[2]);
                    for (int i = 0; i < int(args[1]); ++i)
                    {
                        unsigned char p[4] = {};
                        guest.Read(args[3] + U(i) * size, p, size);
                        const U index = args[2] == E::UnsignedByte ? p[0] : args[2] == E::UnsignedShort ? U(p[0] | (p[1] << 8)) : U(p[0] | (p[1] << 8) | (p[2] << 16) | (U(p[3]) << 24));
                        element(int(index));
                    }
                }
                Command end;
                end.slot = S::End;
                s.compiled.commands.push_back(end);
            }
        }
        else
        {
            Command c;
            c.slot = slot;
            const unsigned words = std::min(14u, ArgumentBytes(slot) / 4);
            memcpy(c.args, args, words * 4);
            // Images are repacked tightly with the unpack state of now.
            int image_arg = -1;
            size_t image_bytes = 0;
            switch (slot)
            {
            case S::Bitmap: image_arg = 6; image_bytes = ImageBytes(s.client, int(args[0]), int(args[1]), E::ColorIndex, E::Bitmap); break;
            case S::DrawPixels: image_arg = 4; image_bytes = ImageBytes(s.client, int(args[0]), int(args[1]), args[2], args[3]); break;
            case S::TexImage1D: image_arg = 7; image_bytes = ImageBytes(s.client, int(args[3]), 1, args[5], args[6]); break;
            case S::TexImage2D: image_arg = 8; image_bytes = ImageBytes(s.client, int(args[3]), int(args[4]), args[6], args[7]); break;
            case S::TexSubImage1D: image_arg = 6; image_bytes = ImageBytes(s.client, int(args[3]), 1, args[4], args[5]); break;
            case S::TexSubImage2D: image_arg = 8; image_bytes = ImageBytes(s.client, int(args[4]), int(args[5]), args[6], args[7]); break;
            default: break;
            }
            if (image_arg >= 0 && args[image_arg])
            {
                // Copy the bytes as the unpack state reads them; the replay
                // reads them back with the same state it had here.
                const size_t at = s.compiled.data.size();
                s.compiled.data.resize(at + image_bytes);
                guest.Read(args[image_arg], s.compiled.data.data() + at, U(image_bytes));
                c.args[image_arg] = ListBase + U(at);
                // The unpack state of now goes with it: keep it by value.
                Command state_cmd;
                (void)state_cmd;
                if (s.client.unpack_alignment != 1 || s.client.unpack_row_length || s.client.unpack_skip_rows ||
                    s.client.unpack_skip_pixels || s.client.unpack_lsb_first)
                {
                    // Repack into alignment 1 without skips.
                    std::vector<unsigned char> repacked;
                    const int w_ = slot == S::Bitmap || slot == S::DrawPixels ? int(args[0]) : int(args[3]);
                    const int h_ = slot == S::Bitmap || slot == S::DrawPixels ? int(args[1]) : slot == S::TexImage2D || slot == S::TexSubImage2D ? int(args[slot == S::TexImage2D ? 4 : 5]) : 1;
                    const U fmt = slot == S::Bitmap ? E::ColorIndex : slot == S::DrawPixels ? args[2] : slot == S::TexImage1D ? args[5] : slot == S::TexImage2D ? args[6] : slot == S::TexSubImage1D ? args[4] : args[6];
                    const U type = slot == S::Bitmap ? E::Bitmap : slot == S::DrawPixels ? args[3] : slot == S::TexImage1D ? args[6] : slot == S::TexImage2D ? args[7] : slot == S::TexSubImage1D ? args[5] : args[7];
                    std::vector<float> rgba;
                    ListGuest lg(guest, s.compiled);
                    UnpackImage(lg, s.client, ListBase + U(at), w_, h_, fmt, type, rgba);
                    // Store as RGBA floats, tightly.
                    const size_t at2 = s.compiled.data.size();
                    s.compiled.data.resize(at2 + rgba.size() * 4);
                    memcpy(s.compiled.data.data() + at2, rgba.data(), rgba.size() * 4);
                    c.args[image_arg] = ListBase + U(at2);
                    if (slot == S::Bitmap)
                    {
                        // A bitmap keeps its bit form: pack the floats back.
                        const size_t at3 = s.compiled.data.size();
                        const size_t row = size_t((w_ + 7) / 8);
                        s.compiled.data.resize(at3 + row * h_, 0);
                        for (int y = 0; y < h_; ++y)
                            for (int x = 0; x < w_; ++x)
                                if (rgba[(size_t(y) * w_ + x) * 4] != 0.0f)
                                    s.compiled.data[at3 + y * row + x / 8] |= (unsigned char)(0x80 >> (x & 7));
                        c.args[image_arg] = ListBase + U(at3);
                    }
                    else
                    {
                        // RGBA floats: the replay reads them as such.
                        if (slot == S::DrawPixels) { c.args[2] = E::Rgba; c.args[3] = E::Float; }
                        else if (slot == S::TexImage1D) { c.args[5] = E::Rgba; c.args[6] = E::Float; }
                        else if (slot == S::TexImage2D) { c.args[6] = E::Rgba; c.args[7] = E::Float; }
                        else if (slot == S::TexSubImage1D) { c.args[4] = E::Rgba; c.args[5] = E::Float; }
                        else { c.args[6] = E::Rgba; c.args[7] = E::Float; }
                    }
                }
                c.packed_image = true;
            }
            else
            {
                PointerArg pointers[2];
                const int n = ListPointers(s, slot, args, pointers);
                for (int i = 0; i < n; ++i)
                {
                    const U address = args[pointers[i].arg];
                    const size_t at = s.compiled.data.size();
                    s.compiled.data.resize(at + pointers[i].bytes);
                    if (address) guest.Read(address, s.compiled.data.data() + at, U(pointers[i].bytes));
                    c.args[pointers[i].arg] = ListBase + U(at);
                }
            }
            s.compiled.commands.push_back(c);
        }
        if (s.list_mode != E::CompileAndExecute) return 0;
    }
    Executor e(s, guest);
    return e.Run(slot, args);
}
} // namespace OpenGL
