// SPDX-License-Identifier: GPL-2.0-or-later
// NEO98DX.DLL, the Direct3D part: the rasterizer below the Direct3D 7 of the
// ARM64 DDRAW.DLL (sdk/dx/ddraw/d3d_*.c).
// For an x86 process the original D3DIM700.DLL keeps the state of a device
// and sends the driver a DrawPrimitives2 command stream, which
// src/x86_ddhal.cpp turns into calls of Gpu::Backend. For an ARM64 process
// the Direct3D of the SDK keeps the state, and what crosses into the host is
// a stream of this driver's own commands (sdk/dx/neo98dx_abi.h): the changes
// of state since the last batch and the draws, one call per batch. A context
// here is what a context of the HAL is, a render target, a Z buffer and a
// Gpu::State, and it ends in the same calls: Begin, SetTexture, Draw, Clear,
// End. The state a device starts with, the layout of lights, materials and
// transforms in it and the caps are shared with the HAL (direct3d_state.h,
// win98_ddhal.h).
// The vertices of a draw lie in memory of the process, which the host reaches
// in place (an ARM64 process is host memory below 4 GiB), so nothing is
// copied on the way to the rasterizer. Surfaces are named by the handles of
// src/x86_dx_driver.cpp.
#include "../sdk/dx/neo98dx_abi.h"
#include "direct3d_state.h"
#include "gpu_backend.h"
#include "user.h"
#include "win98_ddhal.h"
#include "x86_app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
namespace X86
{
namespace
{
namespace H = Win98DDHal;
enum : unsigned
{
    D3dOk = 0,
    D3dInvalidParams = 0x80070057u,
    D3dOutOfMemory = 0x8007000eu,
    D3dGeneric = 0x80004005u,
    D3dInvalidObject = 0x88760082u,
    D3dSurfaceLost = 0x887601c2u,
    D3dNo3D = 0x887600aau,   // DDERR_NO3D
};
constexpr unsigned ContextCapacity = 16;
} // namespace

struct Application::DxD3dContext
{
    unsigned target = 0, depth = 0;
    unsigned textures[Gpu::State::Stages] = {};
    Gpu::Surface cube_faces[Gpu::State::Stages][6];
    Gpu::State state;
};

struct Application::DxD3d
{
    DxD3dContext *contexts[ContextCapacity] = {};
};

void Application::FreeDxD3d()
{
    if (!dx_d3d) return;
    for (auto *context : dx_d3d->contexts) delete context;
    delete dx_d3d;
    dx_d3d = nullptr;
}

Application::DxD3dContext *Application::DxD3dContextOf(unsigned handle)
{
    if (!dx_d3d || !handle || handle > ContextCapacity) return nullptr;
    return dx_d3d->contexts[handle - 1];
}

// What the rasterizer does: the values the Direct3D HAL publishes in its
// D3DDEVICEDESC_V1 and D3DHAL_D3DEXTENDEDCAPS, as fields.
unsigned Application::DxD3dGetCaps(unsigned address, unsigned bytes)
{
    NEO98D3D_CAPS caps{};
    if (bytes != sizeof caps || !virtual_memory.Access(address, sizeof caps, true)) return D3dInvalidParams;
    if (!gpu_backend) return D3dNo3D;
    caps.size = sizeof caps;
    caps.flags = gpu_backend->TransformAndLight() ? NEO98D3D_CAP_TNL : 0;
    caps.devCaps = H::DeviceDesc::DevCapsValue | (gpu_backend->TransformAndLight() ? H::DeviceDesc::HwTransformAndLight : 0);
    caps.miscCaps = H::PrimCaps::MiscValue;
    caps.rasterCaps = H::PrimCaps::RasterValue;
    caps.zCmpCaps = caps.alphaCmpCaps = H::PrimCaps::CmpValue;
    caps.srcBlendCaps = caps.destBlendCaps = H::PrimCaps::BlendValue;
    caps.shadeCaps = H::PrimCaps::ShadeValue;
    caps.textureCaps = H::PrimCaps::TextureValue;
    caps.textureFilterCaps = H::PrimCaps::FilterValue;
    caps.textureBlendCaps = H::PrimCaps::TextureBlendValue;
    caps.textureAddressCaps = H::PrimCaps::AddressValue;
    caps.renderBitDepth = caps.zBufferBitDepth = H::DDBD_16 | H::DDBD_32;
    caps.minTextureWidth = caps.minTextureHeight = 1;
    caps.maxTextureWidth = caps.maxTextureHeight = 4096;
    caps.maxTextureRepeat = 8192;
    caps.maxTextureAspectRatio = 0;
    caps.maxAnisotropy = H::ExtendedCaps::MaxAnisotropyValue;
    caps.stencilCaps = H::ExtendedCaps::StencilValue;
    caps.fvfCaps = H::ExtendedCaps::Stages;
    caps.textureOpCaps = H::ExtendedCaps::TextureOpValue;
    caps.maxTextureBlendStages = caps.maxSimultaneousTextures = H::ExtendedCaps::Stages;
    caps.maxActiveLights = H::DeviceDesc::MaxLights;
    caps.maxUserClipPlanes = H::ExtendedCaps::ClipPlanes;
    caps.maxVertexBlendMatrices = H::ExtendedCaps::BlendMatrices;
    caps.vertexProcessingCaps = H::ExtendedCaps::VertexProcessingValue;
    const float big = 1.0e10f;
    memcpy(&caps.maxVertexW, &big, 4);
    auto put = [](NEO98DX_FORMAT &out, const Direct3DState::Format &f) { out = {f.flags, f.fourcc, f.bits, f.r, f.g, f.b, f.a}; };
    const unsigned textures = Direct3DState::TextureFormatCount - (gpu_backend->CompressedTextures() ? 0 : Direct3DState::CompressedFormatCount);
    for (unsigned i = 0; i < textures && caps.textureFormatCount < NEO98D3D_MAX_FORMATS; ++i)
        put(caps.textureFormats[caps.textureFormatCount++], Direct3DState::texture_formats[i]);
    for (unsigned i = 0; i < Direct3DState::ZFormatCount && caps.zFormatCount < NEO98D3D_MAX_FORMATS; ++i)
        put(caps.zFormats[caps.zFormatCount++], Direct3DState::z_formats[i]);
    return Store(address, &caps, sizeof caps) ? D3dOk : D3dInvalidParams;
}

unsigned Application::DxD3dCreateContext(unsigned target, unsigned depth, unsigned out)
{
    if (!out || !virtual_memory.Access(out, 4, true)) return D3dInvalidParams;
    if (!gpu_backend) return D3dNo3D;
    Gpu::Surface view;
    if (!DxSurfaceView(target, view) || view.depth) return D3dInvalidObject;
    if (depth && (!DxSurfaceView(depth, view) || !view.depth)) return D3dInvalidObject;
    if (!dx_d3d) dx_d3d = new DxD3d;
    unsigned slot = 0;
    while (slot < ContextCapacity && dx_d3d->contexts[slot]) ++slot;
    if (slot == ContextCapacity) return D3dOutOfMemory;
    auto *context = new DxD3dContext;
    context->target = target;
    context->depth = depth;
    Direct3DState::Default(context->state);
    dx_d3d->contexts[slot] = context;
    Write32(out, slot + 1);
    if (log && trace_calls)
    {
        char text[120];
        snprintf(text, sizeof text, "DXD3D CreateContext %u target=%x depth=%x", slot + 1, target, depth);
        log(text);
    }
    return D3dOk;
}

unsigned Application::DxD3dDestroyContext(unsigned handle)
{
    auto *context = DxD3dContextOf(handle);
    if (!context) return D3dInvalidObject;
    delete context;
    dx_d3d->contexts[handle - 1] = nullptr;
    return D3dOk;
}

// Begins a batch on the render target of the context. False when the target
// has no bytes the rasterizer can render into (it is lost, or gone).
bool Application::DxD3dBegin(DxD3dContext &c)
{
    Gpu::Surface target, z;
    if (!DxSurfaceView(c.target, target) || target.depth) return false;
    const bool has_z = c.depth && DxSurfaceView(c.depth, z) && z.depth;
    return gpu_backend->Begin(target, has_z ? &z : nullptr);
}

// The texture of every stage, as the HAL binds them (HalBindTextures): the
// stages end at the first one whose colour operation is D3DTOP_DISABLE.
void Application::DxD3dBindTextures(DxD3dContext &c)
{
    bool ended = false;
    for (unsigned stage = 0; stage < Gpu::State::Stages; ++stage)
    {
        ended = ended || c.state.stage[stage][H::TSS::ColorOp] == 1;
        Gpu::Surface view;
        const unsigned handle = ended ? 0 : c.textures[stage];
        if (handle && DxSurfaceView(handle, view) && !view.depth)
        {
            DxCubeFaces(handle, c.cube_faces[stage], view);
            gpu_backend->SetTexture(stage, &view);
        }
        else gpu_backend->SetTexture(stage, nullptr);
    }
}

// One batch. The commands are read where they lie; every length is checked
// against the bytes of the call before it is used. A command the driver does
// not know, or one that runs past the end, stops the batch: what came before
// it is drawn, and the answer is DDERR_INVALIDPARAMS.
unsigned Application::DxD3dRun(unsigned handle, unsigned commands, unsigned bytes)
{
    auto *context = DxD3dContextOf(handle);
    if (!context) return D3dInvalidObject;
    if (!gpu_backend) return D3dNo3D;
    if (!bytes) return D3dOk;
    if ((bytes & 3) || bytes > (64u << 20) || !virtual_memory.Access(commands, bytes, false)) return D3dInvalidParams;
    const auto *base = static_cast<const unsigned char *>(cpu->HostPointer(commands, bytes));
    if (!base) return D3dInvalidParams;
    auto &c = *context;
    auto word = [&](unsigned at) { unsigned v; memcpy(&v, base + at, 4); return v; };
    auto real = [&](unsigned at) { float v; memcpy(&v, base + at, 4); return v; };
    bool begun = false, failed = false, bound = false, drew = false;
    auto begin = [&]() {
        if (!begun && !failed)
        {
            begun = DxD3dBegin(c);
            failed = !begun;
            bound = false;
        }
        return begun;
    };
    auto end = [&]() {
        if (begun) gpu_backend->End();
        begun = false;
    };
    unsigned result = D3dOk, at = 0;
    while (at + 8 <= bytes)
    {
        const unsigned op = word(at), size = word(at + 4);
        if (size < 8 || (size & 3) || size > bytes - at) { result = D3dInvalidParams; break; }
        const unsigned body = at + 8, words = (size - 8) / 4;
        switch (op)
        {
        case NEO98D3D_OP_RENDERSTATE:
            for (unsigned i = 0; i + 2 <= words; i += 2)
            {
                const unsigned state = word(body + i * 4);
                // Direct3D 7 has no color-write mask. Its reserved state
                // slots must not override the backend's all-channel default.
                if (state == 168) continue;
                if (state < Gpu::State::RenderStates) c.state.render[state] = word(body + i * 4 + 4);
            }
            break;
        case NEO98D3D_OP_STAGESTATE:
            for (unsigned i = 0; i + 3 <= words; i += 3)
            {
                const unsigned stage = word(body + i * 4), state = word(body + i * 4 + 4);
                if (stage >= Gpu::State::Stages || state >= Gpu::State::StageStates || state == H::TSS::TextureMap) continue;
                c.state.stage[stage][state] = word(body + i * 4 + 8);
                if (state == H::TSS::ColorOp) bound = false;
            }
            break;
        case NEO98D3D_OP_TEXTURE:
            for (unsigned i = 0; i + 2 <= words; i += 2)
            {
                const unsigned stage = word(body + i * 4);
                if (stage >= Gpu::State::Stages) continue;
                c.textures[stage] = word(body + i * 4 + 4);
                bound = false;
            }
            break;
        case NEO98D3D_OP_VIEWPORT:
            if (words < 6) { result = D3dInvalidParams; break; }
            c.state.viewport_x = int(word(body));
            c.state.viewport_y = int(word(body + 4));
            c.state.viewport_width = int(word(body + 8));
            c.state.viewport_height = int(word(body + 12));
            c.state.z_min = real(body + 16);
            c.state.z_max = real(body + 20);
            break;
        case NEO98D3D_OP_TRANSFORM:
        {
            if (words < 17) { result = D3dInvalidParams; break; }
            const unsigned type = word(body);
            if (float *m = Direct3DState::Transform(c.state, type)) memcpy(m, base + body + 4, 64);
            if (type == 3)
            {
                // The W range of the projection, what D3DIM sends the HAL as
                // D3DHAL_DP2WINFO: the W of the near and of the far plane.
                const float *p = c.state.projection;
                const float m33 = p[10], m34 = p[11], m43 = p[14], m44 = p[15];
                if (m33 == m34 || m33 == 0.0f) { c.state.w_near = 0.0f; c.state.w_far = 1.0f; }
                else
                {
                    c.state.w_near = m44 - m43 / m33 * m34;
                    c.state.w_far = (m44 - m43) / (m33 - m34) * m34 + m44;
                }
            }
            break;
        }
        case NEO98D3D_OP_MATERIAL:
            if (words < 17) { result = D3dInvalidParams; break; }
            Direct3DState::Material(c.state.material, base + body);
            break;
        case NEO98D3D_OP_LIGHT:
        {
            if (words < 27) { result = D3dInvalidParams; break; }
            const unsigned slot = word(body);
            if (slot < Gpu::State::Lights) Direct3DState::Light(c.state.lights[slot], base + body + 4);
            break;
        }
        case NEO98D3D_OP_LIGHTENABLE:
            for (unsigned i = 0; i + 2 <= words; i += 2)
            {
                const unsigned slot = word(body + i * 4);
                if (slot < Gpu::State::Lights) c.state.lights[slot].enabled = word(body + i * 4 + 4) != 0;
            }
            break;
        case NEO98D3D_OP_CLIPPLANE:
        {
            if (words < 5) { result = D3dInvalidParams; break; }
            const unsigned index = word(body);
            if (index < Gpu::State::ClipPlanes) memcpy(c.state.clip_plane[index], base + body + 4, 16);
            break;
        }
        case NEO98D3D_OP_TARGET:
            if (words < 2) { result = D3dInvalidParams; break; }
            end();
            failed = false;
            c.target = word(body);
            c.depth = word(body + 4);
            break;
        case NEO98D3D_OP_CLEAR:
        {
            if (words < 5) { result = D3dInvalidParams; break; }
            unsigned count = word(body + 16);
            if (count > (words - 5) / 4) { result = D3dInvalidParams; break; }
            if (!begin()) break;
            // The rasterizer takes sixty-four rects at a time.
            int list[64][4];
            unsigned done = 0;
            do
            {
                const unsigned n = count - done < 64 ? count - done : 64;
                for (unsigned i = 0; i < n; ++i)
                    for (unsigned k = 0; k < 4; ++k) list[i][k] = int(word(body + 20 + (done + i) * 16 + k * 4));
                gpu_backend->Clear(word(body), word(body + 4), real(body + 8), word(body + 12), n ? &list[0][0] : nullptr, n, c.state);
                done += n;
            } while (done < count);
            drew = true;
            break;
        }
        case NEO98D3D_OP_DRAW:
        {
            NEO98D3D_DRAW d;
            if (size < 8 + sizeof d) { result = D3dInvalidParams; break; }
            memcpy(&d, base + body, sizeof d);
            Gpu::Draw draw;
            const unsigned elements = d.indices ? d.indexCount : d.vertexCount;
            switch (d.primitive)   // D3DPRIMITIVETYPE
            {
            case 1: draw.primitive = Gpu::PointList; draw.primitive_count = elements; break;
            case 2: draw.primitive = Gpu::LineList; draw.primitive_count = elements / 2; break;
            case 3: draw.primitive = Gpu::LineStrip; draw.primitive_count = elements ? elements - 1 : 0; break;
            case 4: draw.primitive = Gpu::TriangleList; draw.primitive_count = elements / 3; break;
            case 5: draw.primitive = Gpu::TriangleStrip; draw.primitive_count = elements > 2 ? elements - 2 : 0; break;
            case 6: draw.primitive = Gpu::TriangleFan; draw.primitive_count = elements > 2 ? elements - 2 : 0; break;
            default: break;
            }
            const auto layout = Gpu::VertexLayout::Of(d.fvf);
            if (!draw.primitive || d.stride < layout.stride || d.stride > 1024 || !d.vertexCount || d.vertexCount > (16u << 20) ||
                d.indexCount > (16u << 20))
            {
                result = D3dInvalidParams;
                break;
            }
            if (!draw.primitive_count) break;
            const unsigned vertex_bytes = d.vertexCount * d.stride;
            if (!virtual_memory.Access(d.vertices, vertex_bytes, false) ||
                (d.indices && !virtual_memory.Access(d.indices, d.indexCount * 2, false)))
            {
                result = D3dInvalidParams;
                break;
            }
            draw.fvf = d.fvf;
            draw.stride = d.stride;
            draw.vertices = static_cast<const unsigned char *>(cpu->HostPointer(d.vertices, vertex_bytes));
            draw.vertex_count = d.vertexCount;
            if (d.indices)
            {
                draw.indices = static_cast<const unsigned short *>(cpu->HostPointer(d.indices, d.indexCount * 2));
                draw.index_count = d.indexCount;
                if (!draw.indices) { result = D3dInvalidParams; break; }
            }
            if (!draw.vertices) { result = D3dInvalidParams; break; }
            if (!begin()) break;
            if (!bound)
            {
                DxD3dBindTextures(c);
                bound = true;
            }
            if (log && trace_calls)
            {
                char text[200];
                snprintf(text, sizeof text, "DXD3D draw prim=%u n=%u fvf=%08x stride=%u vertices=%u indices=%u tex0=%x op0=%u light=%u",
                         draw.primitive, draw.primitive_count, d.fvf, d.stride, d.vertexCount, d.indexCount, c.textures[0],
                         c.state.stage[0][H::TSS::ColorOp], c.state.render[H::RS::Lighting]);
                log(text);
            }
            gpu_backend->Draw(draw, c.state);
            drew = true;
            break;
        }
        default:
            result = D3dInvalidParams;
            break;
        }
        if (result != D3dOk) break;
        at += size;
    }
    end();
    // A render target the monitor shows, a program that draws straight into
    // the front buffer, gets its picture into the bytes at the end of the
    // batch (HalShowIfVisible).
    if (drew)
    {
        Gpu::Surface view;
        if (DxSurfaceView(c.target, view) && view.bits == User::ScanoutBits())
        {
            DxSync(view.bits, false);
            User::ScreenChanged();
        }
    }
    if (result == D3dOk && failed) result = D3dSurfaceLost;
    return result;
}
} // namespace X86
