// SPDX-License-Identifier: GPL-2.0-or-later
// all D3D color write masks, fixed function and pixel shader pipelines.
#include "host_metal.h"
#include "../src/gpu_backend.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
int main(int argc, char **argv)
{
    auto *gpu=HostMetal::Install(); assert(gpu);
    std::vector<unsigned> pixels(64*64);
    Gpu::Surface surface;
    surface.bits=(unsigned char*)pixels.data();surface.width=surface.height=64;surface.pitch=256;
    surface.bit_count=32;surface.r_mask=0xff0000;surface.g_mask=0xff00;surface.b_mask=0xff;surface.a_mask=0xff000000;
    surface.owner=1;surface.handle=1;
    unsigned texel=0xffffffff;auto texture=surface;texture.bits=(unsigned char*)&texel;
    texture.width=texture.height=1;texture.pitch=4;texture.handle=2;
    for(unsigned i=0;i<8;++i)gpu->SetTexture(i,&texture);
    Gpu::State state;state.viewport_width=state.viewport_height=64;
    state.render[22]=1;state.render[19]=2;state.render[20]=1;
    state.stage[0][1]=state.stage[0][4]=2;state.stage[0][2]=state.stage[0][5]=0;state.stage[1][1]=1;
    struct Vertex {float x,y,z,w;unsigned color;};
    Vertex vertices[]={{-.5,-.5,.5,1,0x78563412},{63.5,-.5,.5,1,0x78563412},
                       {-.5,63.5,.5,1,0x78563412},{63.5,63.5,.5,1,0x78563412}};
    Gpu::Draw draw;draw.primitive=Gpu::TriangleStrip;draw.fvf=0x44;draw.stride=sizeof(Vertex);
    draw.vertices=(unsigned char*)vertices;draw.vertex_count=4;draw.primitive_count=2;
    const unsigned shader[]={0xffff0101,1,0x800f0000,0xa0e40000,0xffff};
    state.ps_constants[0][0]=0x56/255.0f;state.ps_constants[0][1]=0x34/255.0f;
    state.ps_constants[0][2]=0x12/255.0f;state.ps_constants[0][3]=0x78/255.0f;
    unsigned failures=0,cases=0;
    for(unsigned program=0;program<2;++program)
    for(unsigned blend=0;blend<2;++blend)
    for(unsigned step=0;step<16;++step)
    {
        unsigned mask=blend?15-step:step;
        state.pixel_shader=program?Gpu::Shader{shader,sizeof(shader)/4,3}:Gpu::Shader{};
        state.render[27]=blend;state.render[168]=mask;
        assert(gpu->Begin(surface,nullptr));gpu->Clear(1,0xa1b2c3d4,1,0,nullptr,0,state);
        gpu->Draw(draw,state);gpu->End();gpu->Sync(surface,false);
        unsigned channels=((mask&1)?0xff0000:0)|((mask&2)?0xff00:0)|((mask&4)?0xff:0)|((mask&8)?0xff000000:0);
        unsigned expected=(0x78563412&channels)|(0xa1b2c3d4&~channels);
        bool okay=true;for(unsigned pixel:pixels)okay=okay&&pixel==expected;
        if(!okay)++failures;
        printf("program=%u blend=%u mask=%u actual=%08x expected=%08x pass=%u\n",program,blend,mask,pixels[32*64+32],expected,unsigned(okay));++cases;
    }
    gpu->Release(surface.bits,pixels.size()*4);gpu->Release(texture.bits,4);
    printf("COLOR MASK cases=%u failures=%u\n",cases,failures);
    const bool baseline=argc>1&&!strcmp(argv[1],"--expect-broken");
    return failures==(baseline?60u:0u)?0:1;
}
