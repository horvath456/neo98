// SPDX-License-Identifier: GPL-2.0-or-later
#define main EarlierDibTest
#include "test_dib_api_native.cpp"
#undef main
static unsigned Call(Memory&m,const char*name,std::initializer_list<unsigned>values){unsigned a[8]{},i=0;for(auto v:values)a[i++]=v;return Gdi::PenApi(name,a,m);}
int main(){using namespace Gdi;Memory m;unsigned pixels[32*32]{};unsigned dc=CreateSurfaceDC({reinterpret_cast<unsigned char*>(pixels),32,32,128,32});CHECK(dc);unsigned pen=Call(m,"CreatePen",{0,1,0xb74a12}),old=SelectObject(dc,pen);CHECK(pen&&old&&HasPen(pen));CHECK(SaveDC(dc)==1&&DeleteObject(pen)&&HasPen(pen));CHECK(Call(m,"SetROP2",{dc,7})==13);CHECK(Call(m,"MoveToEx",{dc,1,1,0x10000}));CHECK(m.R(0x10000)==0&&m.R(0x10004)==0);CHECK(Call(m,"LineTo",{dc,7,4}));CHECK(pixels[1*32+1]==0x124ab7&&pixels[1*32+2]==0x124ab7&&pixels[4*32+7]==0);CHECK(Call(m,"GetCurrentPositionEx",{dc,0x10000})&&m.R(0x10000)==7&&m.R(0x10004)==4);CHECK(RestoreDC(dc,-1)&&Call(m,"GetROP2",{dc})==13);CHECK(Call(m,"GetCurrentPositionEx",{dc,0x10000})&&!m.R(0x10000)&&!m.R(0x10004));CHECK(SelectObject(dc,old)==pen&&DeleteObject(pen));
 // Logical pens retain widths/styles for queries; DIBENG realizes wide dash pens solid.
 unsigned descriptor[]={0x10003,0xfffffff9,0x8001,0x124ab7};memcpy(m.data,descriptor,sizeof descriptor);pen=Call(m,"CreatePenIndirect",{0x10000});CHECK(pen);CHECK(PenGetObject(pen,16,0x10100,m)==16&&m.R(0x10100)==3&&m.R(0x10104)==0xfffffff9&&m.R(0x10108)==0xffff8001);CHECK(!Call(m,"CreatePen",{7,0,0}));CHECK(DeleteObject(pen));
 // ExtCreatePen rejects original unsupported brushes and large geometric widths.
 unsigned brush[]={0,0x331122,0};memcpy(m.data,brush,sizeof brush);CHECK(!Call(m,"ExtCreatePen",{0x10000,0x4001,0x10000,0,0}));pen=Call(m,"ExtCreatePen",{0x11203,3,0x10000,9,1});CHECK(pen);CHECK(PenGetObject(pen,0,0,m)==16);CHECK(DeleteObject(pen));
 // Polyline does not alter the current point and cannot draw partly on read/OOM failure.
 for(unsigned i=0;i<41;++i){m.W(0x10000+i*8,i%32);m.W(0x10004+i*8,(i*3)%32);}unsigned before[1024];memcpy(before,pixels,sizeof before);fail_after=0;CHECK(!Call(m,"Polyline",{dc,0x10000,41})&&m.error==8&&!memcmp(before,pixels,sizeof before));CHECK(Call(m,"Polyline",{dc,0x10000,41}));CHECK(Call(m,"GetCurrentPositionEx",{dc,0x11000})&&!m.R(0x11000)&&!m.R(0x11004));CHECK(!Call(m,"Polyline",{dc,0x1fff0,10}));
 // Wide geometry has a separate original stroker. Keep LineTo's original
 // result/current-point behavior while reporting the missing drawing provider.
 unsigned wide=Call(m,"CreatePen",{0,3,0x123456});CHECK(wide&&SelectObject(dc,wide)==old);memcpy(before,pixels,sizeof before);m.error=0;
 CHECK(Call(m,"LineTo",{dc,20,20})&&m.error==50&&!memcmp(before,pixels,sizeof before));CHECK(Call(m,"GetCurrentPositionEx",{dc,0x11000})&&m.R(0x11000)==20&&m.R(0x11004)==20);
 CHECK(!Call(m,"Polyline",{dc,0x10000,2})&&m.error==50);CHECK(SelectObject(dc,old)==wide&&DeleteObject(wide));
 // Saved states keep deselected pens alive, including during DC destruction.
 pen=Call(m,"CreatePen",{2,1,0x332211});CHECK(SelectObject(dc,pen)==old&&SaveDC(dc)==1&&SelectObject(dc,old)==pen&&DeleteObject(pen)&&HasPen(pen));// The stock DEFAULT_PALETTE the DC borrowed is system-owned; it goes with the provider.
 CHECK(DeleteDC(dc)&&DeleteObject(pen));ShutdownBitmaps();CHECK(LiveObjects()==0);puts("Pen native PASS: original raster, ROP2/current point, pen records, saved references, OOM and cleanup");return 0;}
