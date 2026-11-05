// SPDX-License-Identifier: GPL-2.0-or-later
// DXT cube faces, mip chains, alpha and cache transitions.
#include "host_metal.h"
#include "../src/gpu_backend.h"
#include <array>
#include <vector>
#include <cassert>
#include <cstdio>

int main()
{
    auto *gpu=HostMetal::Install();assert(gpu && gpu->CompressedTextures());
    constexpr unsigned width=96,height=16;
    std::vector<unsigned> pixels(width*height);
    Gpu::Surface target;target.bits=(unsigned char*)pixels.data();target.width=width;target.height=height;target.pitch=width*4;
    target.bit_count=32;target.r_mask=0xff0000;target.g_mask=0xff00;target.b_mask=0xff;target.a_mask=0xff000000;target.owner=7;target.handle=1;
    unsigned texel=0xffffffff;auto white=target;white.bits=(unsigned char*)&texel;white.width=white.height=1;white.pitch=4;white.handle=2;
    const unsigned rgb[]={0xff0000,0x00ff00,0x0000ff,0xffff00,0x00ffff,0xff00ff};
    const unsigned short rgb565[]={0xf800,0x07e0,0x001f,0xffe0,0x07ff,0xf81f};
    const float directions[6][3]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    const unsigned ps1[]={0xffff0101,66,0xb00f0000,1,0x800f0000,0xb0e40000,0xffff};
    const unsigned ps2[]={0xffff0200,0x0200001f,0x80000000,0xb00f0000,
        0x0200001f,0x98000000,0xa00f0800,0x03000042,0x800f0000,0xb0e40000,0xa0e40800,
        0x02000001,0x800f0800,0x80e40000,0xffff};
    std::vector<unsigned> ps2flat(ps2,ps2+sizeof(ps2)/4);ps2flat[5]=0x90000000; // dcl_2d s0
    Gpu::State state;state.viewport_width=width;state.viewport_height=height;state.render[22]=1;
    state.render[15]=1;state.render[24]=127;state.render[25]=5;
    for(unsigned s=0;s<8;++s){state.stage[s][11]=s;state.stage[s][13]=state.stage[s][14]=1;state.stage[s][16]=state.stage[s][17]=1;state.stage[s][18]=1;}
    struct Vertex{float x,y,z,w;unsigned color;float u,v,q;};
    unsigned cases=0;
    for(unsigned format=1;format<=5;++format)for(unsigned levels:{0u,3u})for(unsigned program=0;program<2;++program)
    {
        std::array<Gpu::Surface,6> faces;
        std::array<std::array<std::vector<unsigned char>,4>,6> data;
        const unsigned block=format==1?8:16;
        for(unsigned f=0;f<6;++f)
        {
            auto &face=faces[f];face.width=face.height=8;face.pitch=2*block;face.fourcc=0x30545844u+(format<<24);
            face.owner=7;face.handle=3;face.generation=++cases;face.level_count=levels;
            for(unsigned l=0;l<=levels;++l)
            {
                const unsigned side=8>>l,blocks=((side+3)/4)*((side+3)/4);
                data[f][l].resize(blocks*block);
                for(unsigned b=0;b<blocks;++b)
                {
                    auto *p=data[f][l].data()+b*block;
                    if(format==2||format==3)for(unsigned k=0;k<8;++k)p[k]=255;
                    if(format==4||format==5)p[0]=255;
                    auto *color=p+(format==1?0:8);color[0]=rgb565[f]&255;color[1]=rgb565[f]>>8;
                }
                if(!l)face.bits=data[f][l].data();
                else face.levels[l-1]={data[f][l].data(),int(side),int(side),int(((side+3)/4)*block),0};
            }
        }
        auto texture=faces[0];texture.faces=faces.data();
        state.pixel_shader=program?Gpu::Shader{ps2,sizeof(ps2)/4,2}:Gpu::Shader{ps1,sizeof(ps1)/4,1};
        for(unsigned pass=0;pass<4;++pass)
        {
            // Reuse the cube, switch this handle to 2D, then back to the cube.
            texture.faces=pass==2?nullptr:faces.data();
            if(program) state.pixel_shader=pass==2?Gpu::Shader{ps2flat.data(),unsigned(ps2flat.size()),3}:Gpu::Shader{ps2,sizeof(ps2)/4,2};
            assert(gpu->Begin(target,nullptr));gpu->Clear(1,0xff404040,1,0,nullptr,0,state);
            for(unsigned s=0;s<8;++s)gpu->SetTexture(s,s?&white:&texture);
            for(unsigned f=0;f<6;++f)
            {
                Vertex vertices[4];for(unsigned i=0;i<4;++i)vertices[i]={float(f*16+((i&1)?16:0))-.5f,float((i&2)?16:0)-.5f,.5f,1,0xffffffff,directions[f][0],directions[f][1],directions[f][2]};
                Gpu::Draw draw;draw.primitive=Gpu::TriangleStrip;draw.fvf=0x10144;draw.stride=sizeof(Vertex);draw.vertices=(unsigned char*)vertices;draw.vertex_count=4;draw.primitive_count=2;
                gpu->Draw(draw,state);
            }
            gpu->End();gpu->Sync(target,false);
            unsigned wrong=0;for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x)
                if(pixels[y*width+x]!=(0xff000000u|rgb[pass==2?0:x/16]))++wrong;
            printf("COMPRESSED CUBE dxt=%u levels=%u shader=%u pass=%u wrong=%u pixels=%zu\n",format,levels,program+1,pass,wrong,pixels.size());
            assert(!wrong);
        }
        for(auto &face:data)for(auto &level:face)if(!level.empty())gpu->Release(level.data(),level.size());
    }
    gpu->Release(target.bits,pixels.size()*4);gpu->Release(white.bits,4);
    puts("PASS: 80 compressed texture cases, every face and alpha pixel correct");
}
