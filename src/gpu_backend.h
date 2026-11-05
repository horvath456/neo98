// SPDX-License-Identifier: GPL-2.0-or-later
// The one GPU backend of the port: the rasterizer behind the Direct3D HAL of
// the NEO98 display driver and behind its OpenGL client driver.
// The HAL in src/x86_ddhal.cpp reads the DrawPrimitives2 command stream of the
// original D3DIM700.DLL and turns it into these calls; the OpenGL context of
// src/opengl_context.cpp hands over clip-space vertices (D3DFVF_XYZW) the same
// way. A host provider draws them, on macOS with Metal (tools/host_metal.mm).
// This header is the only place a provider for another host (Vulkan on Linux)
// has to implement. src/ stays free of the host API, the way the sound mixer
// and the screen are: the provider is installed by the runner
// (Application::SetGpuBackend), and without one the display driver offers no
// DirectDraw HAL and no OpenGL driver; DirectDraw keeps its emulation path and
// OPENGL32 its software path. The interface speaks Direct3D's vocabulary
// (FVF codes, render states), which both front ends map onto.
// Every surface the backend sees is a DirectDraw surface in video memory of
// the driver, and the driver's video memory is host memory the guest maps.
// A render target lives on the GPU between batches, the way it lives in the
// memory of a card: a pass uploads its bytes only when the CPU wrote them since
// (Sync with will_write), End leaves the picture on the GPU, and Sync writes
// it back into the bytes before the CPU reads them, which the driver asks for
// at Lock, at a blit, at a texture blit and at a flip whose host needs CPU
// pixels. A host with GPU scanout can leave the displayed image resident.
// GDI access through a DC also asks the provider before touching its bytes.
// A texture is uploaded
// again whenever its generation changed, and the HAL raises the generation at
// every Lock, Unlock and blit of the surface.
//
#pragma once
namespace Gpu
{
// A DirectDraw surface as the backend sees it: host bytes, geometry and the
// pixel format of the DDPIXELFORMAT the surface was created with.
struct Surface
{
    unsigned char *bits = nullptr;
    int width = 0, height = 0, pitch = 0;
    unsigned bit_count = 0;   // 8, 16, 24 or 32 for colour, the Z depth for depth surfaces
    unsigned fourcc = 0;      // DXT1 to DXT5: rows of 4x4 blocks, pitch is the bytes of one block row
    unsigned r_mask = 0, g_mask = 0, b_mask = 0, a_mask = 0;
    bool indexed = false;         // eight bit palette indices
    const unsigned *palette = nullptr;   // 256 entries as 0xFFRRGGBB when indexed, peFlags on top
    bool palette_alpha = false;   // the palette carries alpha (DDRAWIPAL_ALPHA): the top byte is it
    // A bump map (DDPF_BUMPDUDV): du and dv in the red and green masks,
    // signed, and a luminance in the blue one with DDPF_BUMPLUMINANCE.
    bool bump = false, bump_luminance = false;
    // A cube map: the six faces +X, -X, +Y, -Y, +Z, -Z, each with its mip
    // levels; this surface is the first of them.
    const Surface *faces = nullptr;
    // A volume: `slices` images of pitch times height bytes, one after the
    // other; zero for a flat surface.
    int slices = 0;
    bool depth = false;           // a Z buffer; z_bits and stencil_bits describe it
    unsigned z_bits = 0, stencil_bits = 0;
    // OpenGL uses bits as an identity token for depth kept only on the GPU.
    bool gpu_only_depth = false;
    unsigned generation = 0;      // raised whenever the guest wrote the bytes
    unsigned handle = 0;          // the DirectDraw surface handle, unique within its process
    unsigned owner = 0;           // the process the handle belongs to; together they are the identity
    // The source colour key of the surface (DDCKEY_SRCBLT), pixel values in
    // the surface's own format; texels inside the range are transparent when
    // D3DRENDERSTATE_COLORKEYENABLE is on.
    bool color_key = false;
    unsigned key_low = 0, key_high = 0;
    // The mip levels below level 0, the attached DDSCAPS_MIPMAP surfaces in
    // the same format; level_count counts them without level 0.
    static constexpr unsigned MaxLevels = 12;
    struct Level { unsigned char *bits = nullptr; int width = 0, height = 0, pitch = 0, slices = 0; };
    Level levels[MaxLevels];
    unsigned level_count = 0;
};

// D3DPRIMITIVETYPE.
enum Primitive : unsigned
{
    PointList = 1, LineList = 2, LineStrip = 3, TriangleList = 4, TriangleStrip = 5, TriangleFan = 6
};

// One draw of DrawPrimitives2. With D3DFVF_XYZRHW the vertices are in screen
// space: D3DIM transformed and lit them (the HAL device, or a program that
// hands transformed vertices to the transform-and-lighting device). With
// D3DFVF_XYZW (the DirectX 9 value 0x4002) they are in clip space and lit:
// x, y, z, w with z from 0 to w, which the backend clips and maps onto the
// viewport with the Z range of the State. The OpenGL front end hands its
// vertices over that way, because its own vertex stage follows the rules of
// OpenGL and only the GPU should clip (src/opengl_context.cpp). Any other
// position format is model space, and the backend runs the fixed function
// pipeline of DirectX 7 on it with the transforms, lights and material of the
// State.
struct Draw
{
    unsigned primitive = 0;      // Primitive
    unsigned fvf = 0;            // the vertex format of the batch
    unsigned stride = 0;         // bytes from one vertex to the next
    const unsigned char *vertices = nullptr;   // vertex 0 of the batch
    unsigned vertex_count = 0;   // vertices the batch holds
    unsigned first_vertex = 0;   // the start vertex of a non-indexed primitive
    const unsigned short *indices = nullptr;   // null for a non-indexed primitive
    unsigned index_count = 0;
    unsigned base_vertex = 0;    // added to every index
    unsigned primitive_count = 0;
    // The vertices are the input registers of State::vertex_shader, sixteen
    // bytes each (four floats), stride bytes per vertex, from v0 on.
    bool shader_inputs = false;
};

// D3DLIGHT7 as the HAL keeps it, in world space as the program set it.
struct Light
{
    unsigned type = 0;            // D3DLIGHT_POINT 1, SPOT 2, DIRECTIONAL 3
    float diffuse[4] = {}, specular[4] = {}, ambient[4] = {};
    float position[3] = {}, direction[3] = {0.0f, 0.0f, 1.0f};
    float range = 0.0f, falloff = 0.0f, attenuation[3] = {}, theta = 0.0f, phi = 0.0f;
    bool enabled = false;
};

// D3DMATERIAL7.
struct Material
{
    float diffuse[4] = {}, ambient[4] = {}, specular[4] = {}, emissive[4] = {};
    float power = 0.0f;
};

// The state a draw runs under: the D3DRENDERSTATE_ values by number, the
// texture stage states by stage and D3DTSS_ number, the viewport and W range,
// and for the transform-and-lighting device the transforms, lights, material
// and user clip planes. Matrices are D3DMATRIX, row major, row vectors.
// A shader of the DirectX 8 driver model: the program's tokens as the
// runtime handed them to the driver, and a key that is new for every shader
// the driver was given.
struct Shader
{
    const unsigned *tokens = nullptr;
    unsigned count = 0;
    unsigned long long key = 0;
};

struct State
{
    State() { render[168] = 15; } // D3DRS_COLORWRITEENABLE defaults to every channel.
    static constexpr unsigned RenderStates = 256, Stages = 8, StageStates = 32;
    static constexpr unsigned PixelConstants = 32, VertexConstants = 256, IntegerConstants = 16;
    static constexpr unsigned Lights = 32, ClipPlanes = 6;
    unsigned render[RenderStates] = {};
    // Filter values use the D3D7 enums: mip NONE/POINT/LINEAR = 1/2/3,
    // mag ANISOTROPIC = 5. The HAL normalizes newer runtime enums here.
    unsigned stage[Stages][StageStates] = {};
    int viewport_x = 0, viewport_y = 0, viewport_width = 0, viewport_height = 0;
    // D3DDP2OP_SETSCISSORRECT, used while D3DRS_SCISSORTESTENABLE (174) is set.
    int scissor[4] = {0, 0, 0, 0};
    float z_min = 0.0f, z_max = 1.0f;
    float w_near = 0.0f, w_far = 1.0f;
    float world[4][16] = {}, view[16] = {}, projection[16] = {}, texture[Stages][16] = {};
    Light lights[Lights];
    Material material;
    float clip_plane[ClipPlanes][4] = {};
    // The pixel shader in use (no tokens: the texture stages) and its constants.
    Shader pixel_shader;
    float ps_constants[PixelConstants][4] = {};
    // The vertex shader in use (no tokens: the fixed function pipeline) and its constants.
    Shader vertex_shader;
    float vs_constants[VertexConstants][4] = {};
    // The integer and boolean constants of shader model 2.
    int vs_integers[IntegerConstants][4] = {}, ps_integers[IntegerConstants][4] = {};
    unsigned vs_booleans[IntegerConstants] = {}, ps_booleans[IntegerConstants] = {};
};

// The vertex layout an FVF describes. Position is XYZRHW here, sixteen bytes
// at offset zero; the other elements follow in the order the FVF defines.
struct VertexLayout
{
    unsigned stride = 0;
    bool transformed = false;               // D3DFVF_XYZRHW, and D3DFVF_XYZW
    bool clip_space = false;                // D3DFVF_XYZW: x, y, z, w in clip space
    unsigned weights = 0;                   // blend weights after the position (XYZB1..XYZB5)
    int normal = -1, point_size = -1;
    int diffuse = -1, specular = -1;        // byte offsets or -1
    unsigned texture_sets = 0;
    int texture[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    unsigned texture_size[8] = {};          // floats per set
    static VertexLayout Of(unsigned fvf)
    {
        VertexLayout l;
        unsigned at = 0;
        if ((fvf & 0x400e) == 0x4002)   // D3DFVF_XYZW
        {
            at = 16;
            l.transformed = l.clip_space = true;
        }
        else switch (fvf & 0xe)   // D3DFVF_POSITION_MASK
        {
        case 0x2: at = 12; break;              // D3DFVF_XYZ
        case 0x4: at = 16; l.transformed = true; break;   // D3DFVF_XYZRHW
        case 0x6: at = 16; l.weights = 1; break;          // D3DFVF_XYZB1
        case 0x8: at = 20; l.weights = 2; break;          // D3DFVF_XYZB2
        case 0xa: at = 24; l.weights = 3; break;          // D3DFVF_XYZB3
        case 0xc: at = 28; l.weights = 4; break;          // D3DFVF_XYZB4
        case 0xe: at = 32; l.weights = 5; break;          // D3DFVF_XYZB5
        default: at = 16; l.transformed = true; break;
        }
        if (fvf & 0x10) { l.normal = int(at); at += 12; }       // D3DFVF_NORMAL
        if (fvf & 0x20) { l.point_size = int(at); at += 4; }    // D3DFVF_RESERVED1, the point size
        if (fvf & 0x40) { l.diffuse = int(at); at += 4; }    // D3DFVF_DIFFUSE
        if (fvf & 0x80) { l.specular = int(at); at += 4; }   // D3DFVF_SPECULAR
        l.texture_sets = (fvf >> 8) & 15;
        for (unsigned i = 0; i < l.texture_sets && i < 8; ++i)
        {
            const unsigned bits = (fvf >> (16 + 2 * i)) & 3;
            const unsigned floats = bits == 0 ? 2 : bits == 3 ? 1 : bits == 1 ? 3 : 4;
            l.texture[i] = int(at);
            l.texture_size[i] = floats;
            at += floats * 4;
        }
        l.stride = at;
        return l;
    }
};

// The second way into the backend: the pixel pipeline of a 3dfx Voodoo for
// the Glide driver (src/glide_context.cpp). Glide hands over
// vertices in screen pixels whose values the card interpolates linearly on
// the screen, colour included, and it describes the pixel with its own
// combine units, a W buffer and a fog table rather than with texture stages,
// so it gets a state and a vertex of its own. The constants are Glide's.
struct GlideVertex
{
    float x = 0, y = 0;     // screen pixels, pixel centres on integers, y down
    float ooz = 0;          // the Z buffer value, 0 to 65535
    float oow = 0;          // 1/w, for the W buffer, the fog table and texturing
    float r = 0, g = 0, b = 0, a = 0;   // 0 to 255
    // Per TMU: s/w and t/w in texels of a 256 wide texture, and the 1/w the
    // TMU divides by, after the driver resolved the hints of grHints.
    float s0 = 0, t0 = 0, q0 = 0, s1 = 0, t1 = 0, q1 = 0;
};

struct GlideState
{
    // grColorCombine and grAlphaCombine: GR_COMBINE_FUNCTION_, _FACTOR_,
    // _LOCAL_ and _OTHER_ values and the invert flag. Beyond Glide, for a
    // program that writes fbzColorPath itself (src/dos_voodoo.cpp): colour
    // local 2 takes the constant where the texture alpha is at least one
    // half and the iterated colour elsewhere.
    unsigned color_function = 1, color_factor = 0, color_local = 0, color_other = 0;
    unsigned alpha_function = 1, alpha_factor = 0, alpha_local = 0, alpha_other = 0;
    bool color_invert = false, alpha_invert = false;
    // Per TMU: grTexCombine, the texture it samples (SetTexture stage 0 and
    // 1), its filters, clamping and LOD bias. The sizes are the ones of the
    // largest level; a texture's long side spans 256 in s or t.
    struct Tmu
    {
        unsigned rgb_function = 1, rgb_factor = 0, alpha_function = 1, alpha_factor = 0;
        bool rgb_invert = false, alpha_invert = false;
        bool textured = false;
        bool clamp_s = false, clamp_t = false;
        bool min_bilinear = false, mag_bilinear = false;
        unsigned mipmap = 0;          // GR_MIPMAP_DISABLE, _NEAREST, _NEAREST_DITHER
        float lod_bias = 0.0f;
        int width = 1, height = 1;
        // grTexDetailControl: bias, scale and maximum of the detail factor.
        int detail_bias = 0;
        unsigned detail_scale = 0;
        float detail_max = 0.0f;
    } tmu[2];
    // ARGB. The card has two constants, color0 for the local and color1
    // for the other input; grConstantColorValue sets both, and so does
    // whoever leaves `constant_split` clear.
    unsigned constant_color = 0;
    bool constant_split = false;
    unsigned constant_other = 0;      // color1, with constant_split
    unsigned alpha_test = 7;          // GR_CMP_ALWAYS
    unsigned alpha_reference = 0;
    bool chroma_key = false;
    unsigned chroma_value = 0;        // RGB
    // The alpha mask of fbzMode: a pixel whose alpha other input has a clear
    // low bit is not drawn.
    bool alpha_bit_test = false;
    // GR_FOG_ with GR_FOG_MULT2 and GR_FOG_ADD2; 4 is the constant fog of
    // fogMode, the fog colour added to the pixel (replacing it with MULT2).
    unsigned fog_mode = 0;
    unsigned fog_color = 0;           // RGB
    unsigned char fog_table[64] = {};
    unsigned depth_mode = 0;          // GR_DEPTHBUFFER_
    unsigned depth_function = 1;      // GR_CMP_LESS
    bool depth_mask = false;
    int depth_bias = 0;
    unsigned rgb_source = 4, rgb_destination = 0, alpha_source = 4, alpha_destination = 0;   // GR_BLEND_
    bool rgb_mask = true, alpha_mask = false;
    // grClipWindow in target pixels: left, top, right, bottom.
    int clip[4] = {0, 0, 0, 0};
    // Target pixels per screen pixel; the driver renders at a multiple of
    // the program's resolution.
    float scale = 1.0f;
};

class Backend
{
  public:
    enum class Capability { Raster, Shade, Filter, Anisotropy, TextureOps, LegacyTexture };
    virtual ~Backend() {}
    virtual const char *Name() const = 0;
    // Limit the shared HAL tables to what this provider implements.
    virtual unsigned Limit(Capability, unsigned value) const { return value; }
    // Whether the GPU samples DXT1 to DXT5 textures as they are; only then
    // does the driver offer them.
    virtual bool CompressedTextures() const = 0;
    // Whether the backend runs the fixed function vertex pipeline; only then
    // does the driver offer the transform-and-lighting device.
    virtual bool TransformAndLight() const = 0;
    // Whether the driver answers the DirectX 8 and 9 runtime as a driver of
    // that model (GetDriverInfo2, streams, buffers); without it the runtime
    // drives it as a DirectX 7 driver.
    virtual bool DriverModel8() const = 0;
    // Whether it answers the DirectX 9 runtime as a driver of that model
    // (D3DCAPS9, declarations and shader functions of their own).
    virtual bool DriverModel9() const = 0;
    // Whether the backend draws with pixel shaders 1.1 to 1.4 (State::pixel_shader).
    virtual bool PixelShaders() const = 0;
    // Whether the backend runs vertex shaders 1.1 (State::vertex_shader, Draw::shader_inputs).
    virtual bool VertexShaders() const = 0;
    // One batch on one render target, with or without a depth surface. Begin
    // brings the guest's bytes of the target in; End closes the logical batch.
    // Backends may retain GPU work across batches until Sync makes it visible
    // to the CPU. Begin answers false when the target cannot be rendered to,
    // and the HAL then reports the batch as unhandled.
    virtual bool Begin(const Surface &target, const Surface *depth) = 0;
    // D3DCLEAR_TARGET 1, D3DCLEAR_ZBUFFER 2, D3DCLEAR_STENCIL 4. Rects are
    // left, top, right, bottom; none means the whole viewport.
    virtual void Clear(unsigned flags, unsigned color, float depth, unsigned stencil,
                       const int *rects, unsigned rect_count, const State &state) = 0;
    // The texture of a stage, or null for none.
    virtual void SetTexture(unsigned stage, const Surface *texture) = 0;
    virtual void Draw(const Draw &draw, const State &state) = 0;
    virtual void End() = 0;
    // Submit retained render work at a target change or host frame boundary.
    // Unlike End, this preserves the logical batch and its texture bindings:
    // a front end can continue drawing after a CPU access split its pass.
    virtual void Flush() {}
    // The surface is gone; a cached copy of it must go too.
    virtual void Forget(unsigned owner, unsigned handle) = 0;
    // The CPU is about to read the bytes of `surface` (and, with will_write,
    // to change them): the picture the GPU holds for them goes into the
    // bytes first, and with will_write the next Begin uploads them again.
    virtual void Sync(const Surface &surface, bool will_write) = 0;
    // Copy an entire colour buffer without changing the source. OpenGL's
    // PFD_SWAP_COPY uses this to keep its published front separate from back.
    // False leaves both buffers untouched; the caller supplies a CPU copy.
    virtual bool CopyColor(const Surface &, const Surface &) { return false; }
    // A host can display the resident image without reading its guest bytes.
    // This is only a readiness query; Sync still supplies all CPU readers.
    virtual bool CanScanout(const unsigned char *) const { return false; }
    // GDI may access a GPU-backed surface through a DC without DDRAW Lock.
    virtual void CpuAccess(const unsigned char *, bool) {}
    // The memory from `bits` for `bytes` bytes no longer holds the surfaces
    // it held; what the GPU keeps for it is dropped without writing back.
    virtual void Release(const unsigned char *bits, unsigned long bytes) = 0;
    // A blit with DDBLT_DEPTHFILL on a Z buffer: the depth the backend keeps
    // for it becomes `value` (0 to 1) inside the rect, left, top, right,
    // bottom. SyncDepth exposes the result in the guest's packed Z bytes.
    virtual void FillDepth(const Surface &depth, const int *rect, float value) = 0;
    // The CPU is about to read a Z buffer that draws and clears wrote. Copy
    // depth and stencil into its packed guest format (16-bit Glide depth is
    // the normalized GPU value times 65535).
    // A Z buffer the backend never drew with keeps its bytes.
    virtual void SyncDepth(const Surface &depth) = 0;
    // Whether the backend draws Glide's pixel pipeline (DrawGlide); only then
    // does the port offer GLIDE2X.DLL.
    virtual bool Glide() const = 0;
    // Glide primitives into the batch Begin opened: PointList, LineList or
    // TriangleList, `count` vertices. The textures are those of stages 0 and
    // 1. The depth written is the Z or W value of the pixel over 65535,
    // which Clear also takes.
    virtual void DrawGlide(unsigned primitive, const GlideVertex *vertices, unsigned count, const GlideState &state) = 0;
};
} // namespace Gpu
