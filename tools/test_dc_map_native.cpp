// SPDX-License-Identifier: GPL-2.0-or-later
// This fixture uses raster fonts only. An unexpected TrueType request must fail.
#include "../src/gdi_truetype.h"
#include <cstdlib>
namespace GdiTrueType {
bool AbcWidths(const Font &, unsigned, unsigned, int *) { std::abort(); }
}
#include "../src/gdi.h"
#include "../src/user.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
using U=unsigned;
struct Memory final:Gdi::BitmapIO
{
    std::vector<unsigned char> bytes=std::vector<unsigned char>(0x200000);
    U error=0;bool fail_page=false;
    bool Range(U p,U n)const{return p>=0x1000 && p-0x1000<bytes.size() && n<=bytes.size()-(p-0x1000);}
    bool Read(U p,void *d,U n)override{if(!Range(p,n))return false;memcpy(d,bytes.data()+p-0x1000,n);return true;}
    bool Write(U p,const void *d,U n)override{if(!Range(p,n))return false;memcpy(bytes.data()+p-0x1000,d,n);return true;}
    bool Probe(U p,bool)override{return Range(p,1) && !(fail_page && (p&~4095u)==0x2000);}
    void Error(U value)override{error=value;}
    void Fault(const char *why)override{fprintf(stderr,"Mapping fault: %s\n",why);assert(false);}
    U Call(const char *name,U a=0,U b=0,U c=0,U d=0,U e=0,U f=0)
    {const U args[]={a,b,c,d,e,f};return Gdi::MapApi(name,args,*this);}
    Gdi::POINT Pair(){Gdi::POINT p;assert(Read(0x1000,&p,8));return p;}
    bool Pair(int x,int y){const auto p=Pair();return p.x==x && p.y==y;}
};
int main()
{
    Memory m;assert(!Gdi::LiveObjects());
    unsigned pixels[64*64]={};
    U dc=Gdi::CreateSurfaceDC({reinterpret_cast<unsigned char *>(pixels),64,64,256,32});assert(dc);
    assert(m.Call("SetMapMode",dc,8)==1);
    assert(m.Call("SetWindowExtEx",dc,1,1,0x1000) && m.Pair(1,1));
    assert(m.Call("SetViewportExtEx",dc,2,3,0x1000) && m.Pair(1,1));
    U brush=Gdi::CreateSolidBrush(0x123456);assert(brush);
    const Gdi::RECT r={1,1,4,3};assert(Gdi::FillRect(dc,&r,brush));
    for(U y=0;y<64;++y)for(U x=0;x<64;++x)
        assert(pixels[y*64+x]==(x>=2 && x<8 && y>=3 && y<9 ? 0x563412u : 0u));
    assert(Gdi::GetPixel(dc,2,2)==0x123456);
    // GDI receives an already realized physical font. Mapping changes the
    // text origin and converts measurements back into logical coordinates.
    unsigned saved_pixels[64*64], reference_pixels[64*64];
    memcpy(saved_pixels,pixels,sizeof pixels);memcpy(reference_pixels,pixels,sizeof pixels);
    U reference=Gdi::CreateSurfaceDC({reinterpret_cast<unsigned char *>(reference_pixels),64,64,256,32});assert(reference);
    assert(Gdi::TextOutA(dc,1,1,"A",1) && Gdi::TextOutA(reference,2,3,"A",1));
    assert(!memcmp(pixels,reference_pixels,sizeof pixels));
    assert(memcmp(pixels,saved_pixels,sizeof pixels));
    memcpy(pixels,saved_pixels,sizeof pixels);
    Gdi::SIZE text_size,physical_size;Gdi::TEXTMETRICA metrics,physical_metrics;int widths[1];
    assert(Gdi::GetTextExtentPoint32A(reference,"A",1,&physical_size));
    assert(Gdi::GetTextExtentPoint32A(dc,"A",1,&text_size));
    assert(text_size.cx==(physical_size.cx+1)/2 && text_size.cy==(physical_size.cy+1)/3);
    assert(Gdi::GetTextMetricsA(reference,&physical_metrics) && Gdi::GetTextMetricsA(dc,&metrics));
    assert(metrics.tmHeight==(physical_metrics.tmHeight+1)/3 &&
           metrics.tmAveCharWidth==(physical_metrics.tmAveCharWidth+1)/2);
    assert(!Gdi::GetCharWidth32A(dc,'A','A',widths));
    assert(Gdi::DeleteDC(reference));
    unsigned source_pixels[64*64]={};
    U source=Gdi::CreateSurfaceDC({reinterpret_cast<unsigned char *>(source_pixels),64,64,256,32});assert(source);
    assert(!Gdi::BitBlt(dc,0,0,4,4,source,0,0,Gdi::SRCCOPY));
    assert(!Gdi::BitBlt(source,0,0,4,4,dc,0,0,Gdi::SRCCOPY));
    assert(Gdi::DeleteDC(source));
    assert(Gdi::SaveDC(dc)==1);
    assert(m.Call("SetMapMode",dc,1)==8);
    assert(Gdi::SetDeviceOrigin(dc,37,41,true));
    assert(m.Call("GetDCOrgEx",dc,0x1000) && m.Pair(37,41));
    assert(m.Call("GetViewportOrgEx",dc,0x1000) && m.Pair(0,0));
    assert(Gdi::GetPixel(dc,3,4)==0x123456); // Local backing origin is still zero.
    const Gdi::POINT point={3,4};m.Write(0x1000,&point,8);
    assert(m.Call("LPtoDP",dc,0x1000,1) && m.Pair(3,4));
    assert(Gdi::RestoreDC(dc,-1));
    assert(m.Call("GetMapMode",dc)==8 && m.Call("GetDCOrgEx",dc,0x1000) && m.Pair(0,0));
    assert(Gdi::GetPixel(dc,2,2)==0x123456);
    // Mirrored point/rectangle geometry is supported independently of the
    // deliberately unavailable mirrored bitmap/font realization.
    assert(m.Call("SetViewportExtEx",dc,unsigned(-2),3));
    assert(m.Call("SetViewportOrgEx",dc,20,0));
    const Gdi::RECT mirrored={1,4,4,6};assert(Gdi::FillRect(dc,&mirrored,brush));
    for(U y=12;y<18;++y)for(U x=12;x<18;++x)assert(pixels[y*64+x]==0x563412u);
    m.fail_page=true;m.error=0;
    const auto saved=m.bytes;
    assert(!m.Call("LPtoDP",dc,0x1000,1024) && m.error==87 && m.bytes==saved);
    m.fail_page=false;
    // The stock DEFAULT_PALETTE the DC borrowed is system-owned; it goes with the provider.
    assert(Gdi::DeleteDC(dc) && Gdi::DeleteObject(brush));Gdi::ShutdownBitmaps();assert(!Gdi::LiveObjects());

    assert(User::Initialize());
    assert(User::RegisterClass("DcMapNative",1,0,0));
    // Visible: a window DC draws on the screen inside the window's visible
    // region, which is empty for a hidden window (the one-screen design).
    const U h=User::CreateWindow("DcMapNative","GDI mapping",0x90cf0000u,40,60,180,120,0,0,0);
    assert(h);User::CompleteCreation(h);
    dc=User::GetDC(h);assert(dc);
    Gdi::POINT origin={0,0};assert(User::ScreenToClient(h,&origin));origin.x=-origin.x;origin.y=-origin.y;
    assert(m.Call("GetDCOrgEx",dc,0x1000) && m.Pair(origin.x,origin.y));
    brush=Gdi::CreateSolidBrush(0xa1b2c3);assert(brush);
    const Gdi::RECT local={2,3,6,7};assert(Gdi::FillRect(dc,&local,brush));
    // The GDI dispatcher reports every output call to USER (x86_gdi.cpp);
    // this direct call reports its drawing the same way.
    User::DrawingChanged(dc);
    assert(Gdi::GetPixel(dc,3,4)==0xa1b2c3);
    assert(User::ReleaseDC(h,dc));
    assert(User::MoveWindow(h,90,100,180,120));
    dc=User::GetDC(h);assert(dc);
    Gdi::POINT moved={0,0};assert(User::ScreenToClient(h,&moved));moved.x=-moved.x;moved.y=-moved.y;
    assert(moved.x==origin.x+50 && moved.y==origin.y+40);
    assert(m.Call("GetDCOrgEx",dc,0x1000) && m.Pair(moved.x,moved.y));
    assert(m.Call("GetViewportOrgEx",dc,0x1000) && m.Pair(0,0));
    assert(Gdi::GetPixel(dc,3,4)==0xa1b2c3);
    assert(Gdi::SaveDC(dc));assert(Gdi::SetViewportOrgEx(dc,7,8));
    assert(m.Call("GetDCOrgEx",dc,0x1000) && m.Pair(moved.x,moved.y));
    assert(Gdi::RestoreDC(dc,-1) && m.Call("GetViewportOrgEx",dc,0x1000) && m.Pair(0,0));
    assert(User::ReleaseDC(h,dc) && Gdi::DeleteObject(brush) && User::DestroyWindow(h));
    User::Shutdown();Gdi::ShutdownBitmaps();assert(!Gdi::LiveObjects());
    puts("DC MAP NATIVE PASS: scaled raster pixels, full state restore, range failure, moved window keeps its pixels, device/client origins and cleanup");
}
