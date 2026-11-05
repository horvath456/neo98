// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/gdi.h"
#include <cstdio>
#include <cstring>
#define CHECK(c) do { if(!(c)){std::fprintf(stderr,"GDI contract line %d: %s\n",__LINE__,#c);return 1;} } while(0)
struct IO : Gdi::BitmapIO {
    unsigned char memory[8192]{}; unsigned error=0;
    bool Read(unsigned p,void *v,unsigned n) override {if(!p||p>sizeof memory||n>sizeof memory-p)return false;memcpy(v,memory+p,n);return true;}
    bool Write(unsigned p,const void *v,unsigned n) override {if(!p||p>sizeof memory||n>sizeof memory-p)return false;memcpy(memory+p,v,n);return true;}
    bool Probe(unsigned p,bool) override {return p&&p<sizeof memory;}
    void Error(unsigned e) override {error=e;}
    void Fault(const char *s) override {std::fprintf(stderr,"unexpected GDI fault: %s\n",s);}
    unsigned Path(const char *name,unsigned dc,unsigned b=0,unsigned c=0,unsigned d=0) {
        unsigned a[]={dc,b,c,d};return Gdi::PathApi(name,a,*this);
    }
    unsigned Region(const char *name,unsigned h,unsigned b=0,unsigned c=0) {
        unsigned a[]={h,b,c};return Gdi::RegionApi(name,a,*this);
    }
};
int main()
{
    IO io;const auto dc=Gdi::CreateCompatibleDC(0);CHECK(dc);
    const auto original=Gdi::GetColorSpace(dc,"C:\\WINDOWS\\SYSTEM\\COLOR\\sRGB Color Space Profile.icm");
    unsigned char logical[328];CHECK(original&&Gdi::GetLogColorSpace(original,logical));
    const auto custom=Gdi::CreateColorSpace(logical);CHECK(custom&&custom!=original);
    CHECK(Gdi::ColorMode(dc,2)==2);CHECK(Gdi::SetColorProfile(dc,"C:\\test.icm"));
    CHECK(Gdi::SetColorSpace(dc,custom,"")==original);CHECK(!Gdi::DeleteColorSpace(custom));
    const int saved=Gdi::SaveDC(dc);CHECK(saved);
    CHECK(Gdi::SetColorSpace(dc,original,"")==custom);
    CHECK(!Gdi::DeleteColorSpace(custom)); // saved selection also owns a reference
    CHECK(Gdi::ColorMode(dc,1)==1);CHECK(Gdi::SetColorProfile(dc,"C:\\other.icm"));
    CHECK(Gdi::RestoreDC(dc,saved));CHECK(Gdi::ColorMode(dc,3)==2);
    CHECK(!std::strcmp(Gdi::ColorProfile(dc),"C:\\test.icm"));CHECK(Gdi::GetColorSpace(dc,"")==custom);
    CHECK(Gdi::SetColorSpace(dc,original,"")==custom);CHECK(Gdi::DeleteColorSpace(custom));
    CHECK(!Gdi::GetLogColorSpace(custom,logical));
    CHECK(io.Path("SetArcDirection",dc,2)==1);CHECK(Gdi::SaveDC(dc));
    CHECK(io.Path("SetArcDirection",dc,1)==2);CHECK(Gdi::RestoreDC(dc,-1));
    CHECK(io.Path("GetArcDirection",dc)==2);
    const unsigned widths[]={1,10};
    for(unsigned width: widths) {
        const auto pen=Gdi::CreatePen(0,width,0),old=Gdi::SelectObject(dc,pen);CHECK(pen&&old);
        CHECK(io.Path("BeginPath",dc));unsigned result=0;
        unsigned move[]={dc,20,20,0},line[]={dc,60,20};
        CHECK(Gdi::PathRecord("MoveToEx",move,io,result)&&result);
        CHECK(Gdi::PathRecord("LineTo",line,io,result)&&result);
        CHECK(!io.Path("WidenPath",dc));CHECK(io.Path("EndPath",dc));
        CHECK(io.Path("WidenPath",dc));CHECK(io.Path("GetPath",dc)>2);
        const auto region=io.Path("PathToRegion",dc);CHECK(region);
        CHECK(io.Region("PtInRegion",region,40,20));
        CHECK(!io.Region("PtInRegion",region,40,14));
        CHECK(io.Region("GetRgnBox",region,64));
        int box[4];memcpy(box,io.memory+64,sizeof box);
        CHECK(box[1]==(width==1?20:15)&&box[3]==(width==1?21:25));
        CHECK(Gdi::DeleteObject(region));CHECK(Gdi::SelectObject(dc,old));CHECK(Gdi::DeleteObject(pen));
    }
    Gdi::BITMAPINFO info{};info.bmiHeader.biSize=40;info.bmiHeader.biWidth=160;
    info.bmiHeader.biHeight=-100;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    void *pixels=nullptr;const auto bitmap=Gdi::CreateDIBSection(dc,&info,0,&pixels);CHECK(bitmap&&pixels);
    const auto old_bitmap=Gdi::SelectObject(dc,bitmap);CHECK(old_bitmap);
    for(unsigned i=0;i<16000;++i)static_cast<unsigned*>(pixels)[i]=0x00ff00ff;
    const auto brush=Gdi::CreateSolidBrush(0xffffff),old_brush=Gdi::SelectObject(dc,brush);
    CHECK(brush&&old_brush);
    CHECK(Gdi::PatBlt(dc,3,2,-2,-1,0xabf0cdef));
    CHECK(Gdi::GetPixel(dc,1,1)==0xffffff&&Gdi::GetPixel(dc,2,1)==0xffffff);
    CHECK(Gdi::GetPixel(dc,3,1)==0xff00ff&&Gdi::GetPixel(dc,2,2)==0xff00ff);
    CHECK(!Gdi::PatBlt(dc,0,0,1,1,Gdi::SRCCOPY));
    CHECK(Gdi::SelectObject(dc,old_brush));CHECK(Gdi::DeleteObject(brush));
    unsigned mode[]={dc,8},window[]={dc,10,10,0},viewport[]={dc,20,30,0};
    CHECK(Gdi::MapApi("SetMapMode",mode,io));CHECK(Gdi::MapApi("SetWindowExtEx",window,io));
    CHECK(Gdi::MapApi("SetViewportExtEx",viewport,io));
    unsigned move[]={dc,10,10,0};CHECK(Gdi::PenApi("MoveToEx",move,io));
    Gdi::SetTextAlign(dc,1);Gdi::SetTextColor(dc,0);Gdi::SetBkColor(dc,0xffffff);
    const Gdi::RECT box={5,5,60,30};const int advances[]={5,4,3,2,1,5};
    CHECK(Gdi::ExtTextOutA(dc,0,0,6,&box,"Sample",6,advances));
    Gdi::POINT position{};CHECK(Gdi::CurrentPosition(dc,position)&&position.x==30&&position.y==10);
    CHECK((static_cast<unsigned*>(pixels)[15*160+10]&0xffffff)==0xffffff);
    CHECK(static_cast<unsigned*>(pixels)[0]==0x00ff00ff);
    unsigned ink=0;for(unsigned i=0;i<16000;++i)ink+=(static_cast<unsigned*>(pixels)[i]&0xffffff)==0;
    CHECK(ink>0);CHECK(Gdi::SelectObject(dc,old_bitmap));CHECK(Gdi::DeleteObject(bitmap));
    CHECK(Gdi::DeleteDC(dc));Gdi::ShutdownBitmaps();
    puts("PASS: colour-space ownership, saved ICM/profile/arc state, fillable stroke outlines, and mapped text/clip/current position");
}
