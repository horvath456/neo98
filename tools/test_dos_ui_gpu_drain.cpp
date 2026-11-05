// SPDX-License-Identifier: GPL-2.0-or-later
// GPU refusal must reach the state supplier even though Draw also returns
// false for a legitimate frame without a changed monitor picture.
#include "../src/dos_voodoo.h"
#include "../src/dos_vm.h"
#include <cassert>
#include <cstdio>
struct Backend final : Gpu::Backend {
 bool refuse=false;
 const char*Name()const override{return "test";}
 bool CompressedTextures()const override{return false;}
 bool TransformAndLight()const override{return false;}
 bool DriverModel8()const override{return false;}
 bool DriverModel9()const override{return false;}
 bool PixelShaders()const override{return false;}
 bool VertexShaders()const override{return false;}
 bool Glide()const override{return true;}
 bool Begin(const Gpu::Surface&,const Gpu::Surface*)override{return !refuse;}
 void Clear(unsigned,unsigned,float,unsigned,const int*,unsigned,const Gpu::State&)override{}
 void SetTexture(unsigned,const Gpu::Surface*)override{}
 void Draw(const Gpu::Draw&,const Gpu::State&)override{}
 void End()override{}
 void Forget(unsigned,unsigned)override{}
 void Sync(const Gpu::Surface&,bool)override{}
 void Release(const unsigned char*,unsigned long)override{}
 void FillDepth(const Gpu::Surface&,const int*,float)override{}
 void SyncDepth(const Gpu::Surface&)override{}
 void DrawGlide(unsigned,const Gpu::GlideVertex*,unsigned,const Gpu::GlideState&)override{}
};
struct Queue {
 DosVoodooFrame frame;
 DosVoodooReadback readback;
 unsigned available=0,taken=0,ack=0;
 unsigned short pixels[4][DosVoodooReadback::Pitch*2]{};
 Queue(){for(unsigned i=0;i<3;++i)readback.color[i]=pixels[i];readback.depth=pixels[3];}
 DosVm::Provider Provider(){DosVm::Provider p{};p.owner=this;
  p.VoodooFrame=[](void*owner,DosVm::Session*,DosVoodooFrame&out){auto&q=*static_cast<Queue*>(owner);if(!q.available)return false;--q.available;++q.taken;out=q.frame;return true;};
  p.VoodooReadback=[](void*owner,DosVm::Session*){return &static_cast<Queue*>(owner)->readback;};
  p.VoodooDrawn=[](void*owner,DosVm::Session*){++static_cast<Queue*>(owner)->ack;};return p;}
};
int main(){Backend gpu;Queue q;auto provider=q.Provider();auto*session=reinterpret_cast<DosVm::Session*>(&q);DosVoodoo::Renderer*renderer=nullptr;
 q.available=10;assert(DosVoodoo::Drain(provider,session,gpu,123,renderer)&&q.taken==4&&q.available==6&&!renderer);puts("ok bounded drain");
 q.frame.active=true;q.frame.width=q.frame.height=2;q.available=1;
 assert(DosVoodoo::Drain(provider,session,gpu,123,renderer)&&renderer&&renderer->Healthy());puts("ok valid GPU owner");
 q.frame.read_mask=DosVoodooReadback::Color;q.available=1;
 assert(DosVoodoo::Drain(provider,session,gpu,123,renderer)&&q.ack==1&&renderer->Healthy());puts("ok unchanged picture still answers readback");
 DosVoodooCommand fill;fill.type=DosVoodooCommand::Fill;fill.fbz_mode=1u<<9;
 q.frame.commands=&fill;q.frame.command_count=1;q.available=1;gpu.refuse=true;
 assert(!DosVoodoo::Drain(provider,session,gpu,123,renderer)&&!renderer->Healthy()&&q.ack==2);puts("ok failed Begin refuses capture and releases producer");
 q.frame.commands=nullptr;q.frame.command_count=0;q.available=1;gpu.refuse=false;
 assert(!DosVoodoo::Drain(provider,session,gpu,123,renderer)&&q.ack==3);puts("ok failed renderer never becomes a successful capture");
 delete renderer;return 0;}
