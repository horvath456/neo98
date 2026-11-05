// SPDX-License-Identifier: GPL-2.0-or-later
// logical DP2 batches, partial clears and external surface accesses.
#include "host_metal.h"
#include "../src/gpu_backend.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

struct Image
{
    std::vector<unsigned> pixels;
    Gpu::Surface surface;
    Image(unsigned handle) : pixels(64 * 64, 0xff123456)
    {
        surface.bits = reinterpret_cast<unsigned char *>(pixels.data());
        surface.width = surface.height = 64; surface.pitch = 256;
        surface.bit_count = 32; surface.r_mask = 0xff0000;
        surface.g_mask = 0xff00; surface.b_mask = 0xff; surface.a_mask = 0xff000000;
        surface.owner = 1; surface.handle = handle;
    }
};
struct Vertex { float x,y,z,w; unsigned color; float u,v; };
static Gpu::State State()
{
    Gpu::State s;
    s.viewport_width = s.viewport_height = 64;
    s.render[22] = 1; // cull none
    s.stage[0][1] = s.stage[0][4] = 2; // select arg1
    s.stage[0][2] = s.stage[0][5] = 0; // diffuse
    s.stage[1][1] = 1;
    return s;
}
static void Quad(Gpu::Backend &gpu, Gpu::State &state, unsigned color, float z=.5f)
{
    // The fixed-function shader declares all eight samplers. Validation
    // requires bindings even for stages the state disables.
    static Image unused(100);
    for (unsigned i=0; i<8; ++i)
        if (i || state.stage[0][2]==0) gpu.SetTexture(i,&unused.surface);
    Vertex v[] = {{-.5f,-.5f,z,1,color,0,0},{63.5f,-.5f,z,1,color,1,0},
                  {-.5f,63.5f,z,1,color,0,1},{63.5f,63.5f,z,1,color,1,1}};
    Gpu::Draw d;
    d.primitive = Gpu::TriangleStrip; d.fvf = 0x144; d.stride = sizeof(Vertex);
    d.vertices = reinterpret_cast<unsigned char *>(v); d.vertex_count = 4; d.primitive_count = 2;
    gpu.Draw(d,state);
}
static void Shot(const Image &a, const char *path)
{
    FILE *f = fopen(path,"wb"); assert(f);
    fprintf(f,"P6\n64 64\n255\n");
    for (unsigned c : a.pixels) { const unsigned char rgb[] = {(unsigned char)(c>>16),(unsigned char)(c>>8),(unsigned char)c}; fwrite(rgb,1,3,f); }
    fclose(f);
}
int main(int argc, char **argv)
{
    auto *backend = HostMetal::Install(); assert(backend);
    auto &gpu = *backend;
    const bool merged = argc > 1 && !strcmp(argv[1],"merged");
    Image a(1), b(2), tex(3), z(4);
    z.surface.depth = true; z.surface.z_bits = 24; z.surface.stencil_bits = 8;
    z.surface.r_mask = 8; z.surface.g_mask = 0xffffff;
    z.surface.b_mask = 0xff000000; z.surface.a_mask = 0;
    auto state = State();
    const auto before = HostMetal::RenderPasses();
    assert(gpu.Begin(a.surface,&z.surface));
    gpu.Clear(7,0xff000040,1,0,nullptr,0,state); gpu.End();
    // All eight stencil bits survive batch boundaries, with partial clears.
    for (unsigned bit=0; bit<8; ++bit)
    {
        assert(gpu.Begin(a.surface,&z.surface));
        int r[] = {int(bit*8),0,int(bit*8+8),64};
        gpu.Clear(4,0,1,1u<<bit,r,1,state);
        state.render[52]=1; state.render[53]=state.render[54]=state.render[55]=1;
        state.render[56]=3; state.render[57]=1u<<bit; state.render[58]=0xff; state.render[59]=0;
        Quad(gpu,state,0xff000000 | (0x102030u*(bit+1)));
        gpu.End();
    }
    if (merged) assert(HostMetal::RenderPasses()-before == 1);
    gpu.Sync(a.surface,false);
    for (unsigned y=0; y<64; ++y) for (unsigned x=0; x<64; ++x)
        assert(a.pixels[y*64+x] == (0xff000000 | (0x102030u*(x/8+1))));
    if (argc>2) Shot(a,argv[2]);
    if (!merged)
    {
        for (auto *s : {&a,&b,&tex,&z}) gpu.Release(s->surface.bits,s->pixels.size()*4);
        printf("METAL BASELINE PASS: eight stencil bits; passes=%llu\n",HostMetal::RenderPasses()-before);
        return 0;
    }
    state=State();
    // A partial depth clear does not alter color or stencil. Draw after it
    // must restore viewport, depth state, fill mode and stencil reference.
    assert(gpu.Begin(a.surface,&z.surface));
    gpu.Clear(7,0xff000000,1,0x80,nullptr,0,state);
    state.render[8]=2; // wireframe on the preceding draw
    Quad(gpu,state,0xffffffff);
    const int left[]={0,0,32,64};
    gpu.Clear(3,0xff00ff00,.25f,0,left,1,state);
    state.render[8]=3; state.render[7]=1; state.render[14]=1; state.render[23]=2;
    state.render[52]=1; state.render[56]=3; state.render[57]=0x80; state.render[58]=0xff;
    state.render[53]=state.render[54]=state.render[55]=1;
    Quad(gpu,state,0xffff0000,.5f); gpu.End(); gpu.Sync(a.surface,false);
    assert(a.pixels[16*64+16]==0xff00ff00 && a.pixels[16*64+48]==0xffff0000);
    // Lock/readback and CPU writes must finish the open pass and be uploaded.
    state=State();
    assert(gpu.Begin(a.surface,nullptr)); gpu.Clear(1,0xff0000ff,1,0,nullptr,0,state); gpu.End();
    if (merged) gpu.CpuAccess(a.surface.bits,true); else gpu.Sync(a.surface,true);
    assert(a.pixels[0]==0xff0000ff);
    a.pixels[0]=0xffffffff;
    assert(gpu.Begin(a.surface,nullptr));
    const int corner[]={63,63,64,64}; gpu.Clear(1,0xff00ff00,1,0,corner,1,state);
    gpu.End(); gpu.Sync(a.surface,false);
    assert(a.pixels[0]==0xffffffff && a.pixels.back()==0xff00ff00 && a.pixels[1]==0xff0000ff);
    // Render-to-texture, then sample it on a different render target.
    for (unsigned color : {0xffff0000u,0xff00ff00u})
    {
        assert(gpu.Begin(tex.surface,nullptr)); gpu.Clear(1,color,1,0,nullptr,0,state); gpu.End();
        assert(gpu.Begin(b.surface,nullptr)); gpu.SetTexture(0,&tex.surface);
        state.stage[0][2]=state.stage[0][5]=2;
        Quad(gpu,state,0xffffffff); gpu.End(); gpu.Sync(b.surface,false);
        assert(b.pixels[20*64+20]==color);
    }
    // A readback before a keyed upload must invalidate the previous converted
    // texture even though rendering did not change the guest generation.
    tex.surface.color_key = true;
    for (unsigned color : {0xff0000ffu,0xffff0000u})
    {
        assert(gpu.Begin(tex.surface,nullptr)); gpu.Clear(1,color,1,0,nullptr,0,state); gpu.End();
        gpu.Sync(tex.surface,false);
        assert(gpu.Begin(b.surface,nullptr)); gpu.SetTexture(0,&tex.surface);
        Quad(gpu,state,0xffffffff); gpu.End(); gpu.Sync(b.surface,false);
        assert(b.pixels[20*64+20]==color);
    }
    tex.surface.color_key = false;
    // An upload between two batches must not change what an earlier draw
    // samples. Both draws use the same cached texture handle, new generation.
    gpu.Release(tex.surface.bits,tex.pixels.size()*4);
    std::fill(tex.pixels.begin(),tex.pixels.end(),0xffff0000); ++tex.surface.generation;
    assert(gpu.Begin(b.surface,nullptr)); gpu.SetTexture(0,&tex.surface);
    state.render[174]=1; state.scissor[0]=0; state.scissor[1]=0; state.scissor[2]=32; state.scissor[3]=64;
    Quad(gpu,state,0xffffffff); gpu.End();
    gpu.Sync(tex.surface,true);
    std::fill(tex.pixels.begin(),tex.pixels.end(),0xff0000ff); ++tex.surface.generation;
    assert(gpu.Begin(b.surface,nullptr)); gpu.SetTexture(0,&tex.surface);
    state.scissor[0]=32; state.scissor[2]=64;
    Quad(gpu,state,0xffffffff); gpu.End(); gpu.Sync(b.surface,false);
    assert(b.pixels[20*64+20]==0xffff0000 && b.pixels[20*64+40]==0xff0000ff);
    if (merged)
    {
        // D24S8 survives a CPU roundtrip, including a write followed by draw.
        gpu.Sync(z.surface,true);
        z.pixels[0]=0x42123456;
        state=State(); assert(gpu.Begin(a.surface,&z.surface));
        gpu.Clear(6,0,.5f,0xa5,corner,1,state); gpu.End(); gpu.Sync(z.surface,false);
        assert(z.pixels[0]==0x42123456 && (z.pixels.back()>>24)==0xa5);
        assert((z.pixels.back()&0xffffff)>=0x7fffff && (z.pixels.back()&0xffffff)<=0x800000);
        // Depth replacement and release end a pass even if color is unchanged.
        assert(gpu.Begin(a.surface,&z.surface)); Quad(gpu,state,0xffffffff); gpu.End();
        const auto n=HostMetal::RenderPasses();
        gpu.Release(z.surface.bits,z.pixels.size()*4);
        assert(gpu.Begin(a.surface,nullptr)); Quad(gpu,state,0xff123456); gpu.End();
        assert(HostMetal::RenderPasses()==n+1); gpu.Flush();
        // A handle can be forgotten while its memory remains allocated.
        gpu.Forget(a.surface.owner,a.surface.handle);
        assert(a.pixels[20*64+20]==0xff123456);
        // The DX8/9 runtime puts stencil below depth, unlike the DX7 list.
        z.surface.g_mask=0xffffff00; z.surface.b_mask=0xff; z.surface.z_bits=32;
        z.pixels[0]=0x12345642;
        assert(gpu.Begin(a.surface,&z.surface));
        gpu.Clear(6,0,.5f,0xa5,corner,1,state); gpu.End(); gpu.Sync(z.surface,false);
        assert(z.pixels[0]==0x12345642 && (z.pixels.back()&255)==0xa5);
        assert((z.pixels.back()>>8)>=0x7fffff && (z.pixels.back()>>8)<=0x800000);
        gpu.Release(z.surface.bits,z.pixels.size()*4);
        // OpenGL has only an identity token, not CPU depth storage.
        unsigned token=0x12345678;
        auto private_depth=z.surface;
        private_depth.bits=reinterpret_cast<unsigned char *>(&token);
        private_depth.handle=5; private_depth.gpu_only_depth=true;
        assert(gpu.Begin(a.surface,&private_depth));
        gpu.Clear(1,0xffabcdef,1,0,nullptr,0,state); gpu.End();
        gpu.Sync(private_depth,false); gpu.Forget(private_depth.owner,private_depth.handle);
        assert(token==0x12345678);
    }
    for (auto *s : {&a,&b,&tex,&z}) gpu.Release(s->surface.bits,s->pixels.size()*4);
    printf("METAL PASSES PASS: batches, 8 stencil bits, rectangles, locks, RTT, texture versions; passes=%llu\n",HostMetal::RenderPasses());
}
