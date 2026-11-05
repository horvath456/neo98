// SPDX-License-Identifier: GPL-2.0-or-later
#include "class_bridge.cpp"
#include "../src/win98_class_public.h"
namespace {
struct PublicE:E
{
 void ProbeRead(U p){Read8(p);event(60,p);}
 U Module16(U v){event(61,v);return v&65535;}
 U Module32(U v){event(62,v);return v?v|0x80000000:0;}
 U Procedure16(U v){event(63,v);return v^0x20000000;}
 U Procedure32(U v){event(64,v);return v^0x40000000;}
 U MapString(U v){event(65,v);return v>65535?v^0x01000000:v;}
 void UnmapString(U v){event(66,v);}
 U UnmapPointer(U v){event(67,v);return v>65535?v^0x01000000:v;}
 U Register16(U source)
 {
  event(68,source?1:0);
  if(source)for(U i=0;i<34;++i)Write8(Data+0x5000+i,Read8(source+i));
  return config[9];
 }
 U GetInfo16(U instance,U name,U output)
 {
  event(69,instance,name);
  if(config[9])for(U i=4;i<34;++i)Write8(output+i,Read8(Data+0x4000+i));
  return config[9];
 }
 U Access16(U window,U index,U value,U flags)
 {events.insert(events.end(),{70,window,index,value,flags,0,0,0});return config[9];}
};
}
extern "C" U class_public_provider(U op,const U*a){PublicE e;switch(op){
case 60:e.ProbeRead(a[0]);return 0;case 61:return e.Module16(a[0]);case 62:return e.Module32(a[0]);case 63:return e.Procedure16(a[0]);case 64:return e.Procedure32(a[0]);case 65:return e.MapString(a[0]);case 66:e.UnmapString(a[0]);return 0;case 67:return e.UnmapPointer(a[0]);case 68:return e.Register16(a[0]);case 69:return e.GetInfo16(a[0],a[1],a[2]);case 70:return e.Access16(a[0],a[1],a[2],a[3]);}assert(false);return 0;}
extern "C" void class_public_run(const U*a,const U*c,U*out){PublicE e;class_begin(c);using namespace Win98ClassPublic;U r=0;switch(a[0]){
case 0:r=RegisterEx(e,a[1]);break;case 1:r=Register(e,a[1]);break;case 2:r=GetInfoEx(e,a[1],a[2],a[3]);break;case 3:r=Access(e,a[1],a[2],a[3],a[4]);break;
}out[0]=r;out[1]=last_error;}
