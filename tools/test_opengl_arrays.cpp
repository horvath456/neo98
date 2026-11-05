// SPDX-License-Identifier: GPL-2.0-or-later
// Compare indexed/client-array draws with equivalent immediate-mode draws.
#include "../src/opengl_context.h"
#include "../src/win98_opengl_dispatch.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
using U = unsigned;
using S = Win98OpenGL::Slot;
struct Memory : OpenGL::Guest
{
    std::vector<unsigned char> data = std::vector<unsigned char>(2 * 1024 * 1024);
    bool spans = false;
    unsigned reads = 0, views = 0;
    bool Read(U a, void *p, U n) override
    {
        ++reads;
        if (uint64_t(a) + n > data.size()) { std::memset(p, 0, n); return false; }
        std::memcpy(p, data.data() + a, n); return true;
    }
    bool Write(U a, const void *p, U n) override
    {
        if (uint64_t(a) + n > data.size()) return false;
        std::memcpy(data.data() + a, p, n); return true;
    }
    U String(U) override { return 0; }
    const unsigned char *Span(U a, U n) override
    { ++views; return spans && uint64_t(a) + n <= data.size() ? data.data() + a : nullptr; }
};
struct Backend : Gpu::Backend
{
    using Vertex = std::array<U, 14>;
    std::vector<Vertex> vertices;
    unsigned syncs = 0;
    bool copy = true;
    Gpu::Surface target{};
    const char *Name() const override { return "array test"; }
    bool CompressedTextures() const override { return false; }
    bool TransformAndLight() const override { return false; }
    bool DriverModel8() const override { return false; }
    bool DriverModel9() const override { return false; }
    bool PixelShaders() const override { return false; }
    bool VertexShaders() const override { return false; }
    bool Glide() const override { return false; }
    bool Begin(const Gpu::Surface &s, const Gpu::Surface *) override { target=s; return true; }
    void Clear(U flags, U color, float, U, const int *, U, const Gpu::State &) override
    { if(flags&1)std::fill_n(reinterpret_cast<U *>(target.bits),target.width*target.height,color); }
    bool CopyColor(const Gpu::Surface &from, const Gpu::Surface &to) override
    {
        if(!copy)return false;
        std::memcpy(to.bits,from.bits,from.pitch*from.height);
        return true;
    }
    void SetTexture(U, const Gpu::Surface *) override {}
    void Draw(const Gpu::Draw &d, const Gpu::State &) override
    {
        assert(d.stride == sizeof(Vertex));
        const U count = d.indices ? d.index_count : d.primitive_count * 3;
        for (U i = 0; i < count; ++i)
        {
            const U index = d.indices ? d.indices[i] + d.base_vertex : d.first_vertex + i;
            assert(index < d.vertex_count);
            Vertex v; std::memcpy(v.data(), d.vertices + size_t(index) * d.stride, sizeof v);
            vertices.push_back(v);
        }
    }
    void End() override {}
    void Forget(U, U) override {}
    void Sync(const Gpu::Surface &, bool) override { ++syncs; }
    void Release(const unsigned char *, unsigned long) override {}
    void FillDepth(const Gpu::Surface &, const int *, float) override {}
    void SyncDepth(const Gpu::Surface &) override {}
    void DrawGlide(U, const Gpu::GlideVertex *, U, const Gpu::GlideState &) override {}
};
U bits(float f) { U u; std::memcpy(&u, &f, 4); return u; }
struct Input { float xyz[3]; unsigned char color[4]; float uv[2]; };
static const Input inputs[] = {
    {{-0.8f,-0.8f,0},{255,0,0,255},{0,0}}, {{0.8f,-0.8f,0},{0,255,0,255},{1,0}},
    {{0.8f,0.8f,0},{0,0,255,255},{1,1}}, {{-0.8f,0.8f,0},{255,255,0,255},{0,1}}
};
U call(OpenGL::Context &c, Memory &m, U slot, std::initializer_list<U> a = {})
{ std::array<U, 16> words{}; std::copy(a.begin(), a.end(), words.begin()); return c.Call(slot, words.data(), m); }
void immediate(OpenGL::Context &c, Memory &m, const std::vector<U> &indices, float shift)
{
    call(c,m,S::Begin,{4});
    for (U i : indices)
    {
        const auto &v=inputs[i%4];
        call(c,m,S::Color4ub,{v.color[0],v.color[1],v.color[2],v.color[3]});
        call(c,m,S::TexCoord2f,{bits(v.uv[0]),bits(v.uv[1])});
        call(c,m,S::Vertex3f,{bits(v.xyz[0]+shift),bits(v.xyz[1]),bits(v.xyz[2])});
    }
    call(c,m,S::End);
}
void compare(bool spans, U index_type, bool sparse, U shade, bool cull)
{
    Memory m; m.spans=spans;
    Backend backend;
    OpenGL::Context c(backend,1,{}); c.Drawable(64,64);
    const std::vector<U> indices = sparse ? std::vector<U>{0,32769,2,2,3,0} : std::vector<U>{2,3,0,0,1,2,2,1,0};
    call(c,m,S::ShadeModel,{shade});
    if(cull) call(c,m,S::Enable,{0xb44});
    call(c,m,S::VertexPointer,{3,0x1406,sizeof(Input),4096});
    call(c,m,S::ColorPointer,{4,0x1401,sizeof(Input),4096+12});
    call(c,m,S::TexCoordPointer,{2,0x1406,sizeof(Input),4096+16});
    for(U state : {0x8074u,0x8076u,0x8078u}) call(c,m,S::EnableClientState,{state});
    const U size=index_type==0x1401?1:index_type==0x1403?2:4;
    for(size_t i=0;i<indices.size();++i) assert(m.Write(1001+U(i)*size,&indices[i],size));
    for(float shift : {0.0f,0.1f})
    {
        for(U i : indices) { Input v=inputs[i%4]; v.xyz[0]+=shift; assert(m.Write(4096+i*sizeof(Input),&v,sizeof v)); }
        backend.vertices.clear();
        immediate(c,m,indices,shift); c.Flush();
        auto expected=backend.vertices;
        backend.vertices.clear(); m.reads=m.views=0;
        call(c,m,S::DrawElements,{4,U(indices.size()),index_type,1001}); c.Flush();
        assert(backend.vertices==expected);
        assert(!call(c,m,S::GetError));
        if(spans&&!sparse) { assert(m.views>0); assert(m.reads==0); }
    }
    backend.vertices.clear();
    call(c,m,S::DisableClientState,{0x8074});
    call(c,m,S::DrawElements,{4,U(indices.size()),index_type,1001}); c.Flush();
    assert(backend.vertices.empty());
    OpenGL::Picture picture;
    const U syncs=backend.syncs;
    assert(c.Swap(picture,false)); assert(backend.syncs==syncs);
    assert(c.Swap(picture,true)); assert(backend.syncs>syncs);
}
void locks()
{
    Memory m; Backend backend; OpenGL::Context c(backend,1,{}); c.Drawable(64,64);
    call(c,m,OpenGL::LockArraysEntry,{0,0}); assert(call(c,m,S::GetError)==0x501);
    call(c,m,OpenGL::UnlockArraysEntry); assert(call(c,m,S::GetError)==0x502);
    call(c,m,OpenGL::LockArraysEntry,{7,20}); assert(!call(c,m,S::GetError));
    call(c,m,S::GetIntegerv,{0x81a8,32}); call(c,m,S::GetIntegerv,{0x81a9,36});
    U values[2]; m.Read(32,values,sizeof values); assert(values[0]==7&&values[1]==20);
    call(c,m,OpenGL::LockArraysEntry,{7,20}); assert(call(c,m,S::GetError)==0x502);
    call(c,m,OpenGL::UnlockArraysEntry); assert(!call(c,m,S::GetError));
    call(c,m,S::Begin,{4}); call(c,m,OpenGL::LockArraysEntry,{0,4});
    call(c,m,S::End); assert(call(c,m,S::GetError)==0x502);
}
void multitexture()
{
    Memory m; m.spans = true; Backend b; OpenGL::Context c(b, 1, {}); c.Drawable(64,64);
    auto active = [&](U unit) { call(c,m,OpenGL::ActiveTextureEntry,{0x84c0+unit}); };
    auto client = [&](U unit) { call(c,m,OpenGL::ClientActiveTextureEntry,{0x84c0+unit}); };
    auto query = [&](U name) { call(c,m,S::GetIntegerv,{name,32}); U v=0; m.Read(32,&v,4); return v; };
    assert(query(0x84e2)==2);
    active(2); assert(call(c,m,S::GetError)==0x500); assert(query(0x84e0)==0x84c0);
    active(1); call(c,m,S::MatrixMode,{0x1702}); call(c,m,S::Translatef,{bits(.25f),0,0});
    call(c,m,S::BindTexture,{0xde1,29}); call(c,m,S::Enable,{0xde1});
    client(1); assert(query(0x84e0)==0x84c1&&query(0x84e1)==0x84c1);
    call(c,m,S::PushAttrib,{0x42001});
    active(0); call(c,m,S::Disable,{0xde1}); call(c,m,S::BindTexture,{0xde1,31});
    call(c,m,S::TexCoord2f,{bits(.5f),bits(.75f)});
    call(c,m,S::PopAttrib);
    assert(query(0x84e0)==0x84c1&&query(0x8069)==29);
    assert(call(c,m,S::IsEnabled,{0xde1})==1);
    active(0); assert(query(0x8069)==0);
    // Legacy texture coordinates still select unit zero while unit one is active.
    active(1); call(c,m,S::TexCoord2f,{bits(.5f),bits(.75f)});
    call(c,m,OpenGL::MultiTexCoordBase+10,{0x84c1,bits(.125f),bits(.375f)});
    call(c,m,S::GetFloatv,{0xb03,64}); float coord[4]; m.Read(64,coord,16);
    assert(coord[0]==.125f&&coord[1]==.375f);
    active(0); call(c,m,S::GetFloatv,{0xb03,64}); m.Read(64,coord,16);
    assert(coord[0]==.5f&&coord[1]==.75f);
    // Two independent arrays match immediate mode through distinct matrices.
    call(c,m,S::VertexPointer,{3,0x1406,sizeof(Input),4096}); call(c,m,S::EnableClientState,{0x8074});
    for (U unit=0;unit<2;++unit) {
        client(unit); call(c,m,S::TexCoordPointer,{2,0x1406,sizeof(Input),4096+16}); call(c,m,S::EnableClientState,{0x8078});
    }
    for (U i=0;i<4;++i) m.Write(4096+i*sizeof(Input),&inputs[i],sizeof(Input));
    b.vertices.clear(); call(c,m,S::Begin,{4});
    for (U i : {0u,1u,2u}) {
        call(c,m,S::TexCoord2f,{bits(inputs[i].uv[0]),bits(inputs[i].uv[1])});
        call(c,m,OpenGL::MultiTexCoordBase+10,{0x84c1,bits(inputs[i].uv[0]),bits(inputs[i].uv[1])});
        call(c,m,S::Vertex3f,{bits(inputs[i].xyz[0]),bits(inputs[i].xyz[1]),bits(inputs[i].xyz[2])});
    }
    call(c,m,S::End); c.Flush(); auto expected=b.vertices; b.vertices.clear();
    call(c,m,S::DrawArrays,{4,0,3}); c.Flush(); assert(b.vertices==expected);
    assert(expected[0][6]==bits(0)&&expected[0][10]==bits(.25f));
    // Vector data belongs to the compiled list, not to its original pointer.
    float coords[2]={.2f,.4f}; m.Write(96,coords,sizeof coords);
    call(c,m,S::NewList,{1,0x1300}); call(c,m,OpenGL::ActiveTextureEntry,{0x84c1});
    call(c,m,OpenGL::MultiTexCoordBase+11,{0x84c1,96}); call(c,m,S::EndList);
    coords[0]=.9f; m.Write(96,coords,sizeof coords); call(c,m,S::CallList,{1});
    call(c,m,S::GetFloatv,{0xb03,64}); m.Read(64,coord,16); assert(coord[0]==.2f&&coord[1]==.4f);
    // Array expansion into a display list retains both units too.
    call(c,m,S::NewList,{2,0x1300}); call(c,m,S::DrawArrays,{4,0,3}); call(c,m,S::EndList);
    b.vertices.clear(); call(c,m,S::CallList,{2}); c.Flush(); assert(b.vertices==expected);
    assert(!call(c,m,S::GetError));
}
void swap_copy()
{
    for(bool accelerated : {false,true})
    {
        Memory m; Backend b; b.copy=accelerated;
        OpenGL::Context c(b,1,{}); c.Drawable(8,8);
        call(c,m,S::ClearColor,{bits(0),bits(1),bits(0),bits(1)});
        call(c,m,S::Clear,{0x4000});
        OpenGL::Picture picture; assert(c.Swap(picture,false));
        const auto *back=b.target.bits;
        assert(picture.bits!=back);
        auto pixel=[](const unsigned char *p){U value;std::memcpy(&value,p,4);return value&0xffffff;};
        assert(pixel(picture.bits)==0x00ff00 && pixel(back)==0x00ff00);
        call(c,m,S::ClearColor,{bits(1),bits(0),bits(0),bits(1)});
        call(c,m,S::Clear,{0x4000}); c.Flush();
        assert(pixel(picture.bits)==0x00ff00 && pixel(back)==0xff0000);
        assert(c.Swap(picture,true));
        assert(pixel(picture.bits)==0xff0000 && pixel(back)==0xff0000);
        call(c,m,S::DrawBuffer,{0x404});
        call(c,m,S::ClearColor,{bits(0),bits(0),bits(1),bits(1)});
        call(c,m,S::Clear,{0x4000}); call(c,m,S::Flush);
        assert(c.TakePresent());assert(c.Front(picture));
        assert(pixel(picture.bits)==0x0000ff && pixel(back)==0xff0000);
    }
}
int main()
{
    unsigned cases=0;
    for(bool spans : {false,true}) for(U type : {0x1401u,0x1403u,0x1405u})
        for(U shade : {0x1d00u,0x1d01u}) for(bool cull : {false,true})
        { compare(spans,type,false,shade,cull); ++cases; }
    compare(true,0x1405,true,0x1d01,false); ++cases;
    locks();
    multitexture();
    swap_copy();
    std::printf("OpenGL arrays passed: %u geometry cases, changed client data, array locks, multitexture, display lists, disabled vertices, deferred swap.\n",cases);
}
