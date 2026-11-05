#include "win98_gdi_palette_tables.h"
// SPDX-License-Identifier: GPL-2.0-or-later
#include "user_draw.h"
#if !defined(USER_DRAW_NO_VECTORS) && !defined(NEO98_RUNTIME_CONTENT)
#include "../build/generated/user_vectors.h"
#endif

namespace Win98Draw {
namespace {
int W(int value) { return (short)(unsigned short)value; }
void Fill(Painter &p,int x,int y,int width,int height,unsigned color)
{
    p.fill(p.context,W(x),W(y),W(width),W(height),color);
}
// The original staircase uses integer ratios, not Bresenham lines.
Gdi::SIZE Diagonal(Painter &p,const Gdi::RECT &r,unsigned flags,unsigned color)
{
    if(r.left>=r.right||r.top>=r.bottom)return {0,0};
    int width=W(r.right-r.left),height=W(r.bottom-r.top);
    // The Win16 routine can divide by zero after overflowing an extent. Reject
    // those native inputs instead of causing a processor exception.
    if(width<=0||height<=0)return {0,0};
    int dx,dy;
    const bool wide=height<width;
    if(wide){dx=width/height;dy=p.border_height;}
    else {dx=p.border_width;dy=height/width;}
    const int step_x=dx;
    const unsigned orientation=flags&31;
    const int direction=orientation==0x13||orientation==0x1c?-1:1;
    const int step_y=W(dy*direction);
    if(wide)dx=W(dx*p.border_height);else dy=W(dy*p.border_height);
    int x=r.left,top=r.top,bottom=W(r.bottom-dy),right=W(r.right-dx);
    // Valid screen rectangles advance monotonically; the bound also prevents
    // a malformed 16-bit wraparound from hanging a native process.
    for(unsigned count=0;x<=right&&top<=bottom&&count<65536;++count){
        int y=direction<0?top:bottom,fx=x,fy=y,fw=dx,fh=dy;
        if(flags&0x800){
            if(p.border_height<dy){
                if(flags&1){fy=r.top;fh=W(y-r.top+dy);}
                else fh=W(r.bottom-y);
            }else{
                if(flags&2)fw=W(r.right-x);
                else {fx=r.left;fw=W(dx-r.left+x);}
            }
        }
        Fill(p,fx,fy,fw,fh,color);
        x=W(x+step_x);
        if(direction<0)top=W(top-step_y);else bottom=W(bottom-step_y);
    }
    return {dx,dy};
}
}

bool DrawEdge(Painter &p,Gdi::RECT *rectangle,unsigned edge,unsigned flags)
{
    if(!rectangle||!p.fill||p.border_width<1||p.border_height<1)return false;
    Gdi::RECT r={W(rectangle->left),W(rectangle->top),W(rectangle->right),W(rectangle->bottom)};
    flags&=65535;edge&=255;
    if(p.depth==1)flags|=0x8000;
    if(flags&0x8000)flags|=0x4000;
    unsigned part=edge&3;
    for(unsigned pass=0;pass<2;++pass){
        if(pass)part=edge&12;
        if(!part)continue;
        unsigned br,tl;
        if(flags&0x4000){
            br=tl=flags&0x8000?(part&3?p.frame:p.window):(part&3?p.shadow:p.face);
        }else{
            switch(part){
            case 1:br=p.dark_shadow;tl=flags&0x1000?p.highlight:p.light;break;
            case 2:br=p.highlight;tl=flags&0x1000?p.dark_shadow:p.shadow;break;
            case 4:br=p.shadow;tl=flags&0x1000?p.light:p.highlight;break;
            case 8:br=p.light;tl=flags&0x1000?p.shadow:p.dark_shadow;break;
            default:return false;
            }
        }
        if(flags&16){
            const auto step=Diagonal(p,r,flags&~0x800u,flags&8?br:tl);
            if(flags&2)r.left=W(r.left+step.cx);else r.right=W(r.right-step.cx);
            if(flags&4)r.top=W(r.top+step.cy);else r.bottom=W(r.bottom-step.cy);
        }else{
            if(flags&4){r.right=W(r.right-p.border_width);Fill(p,r.right,r.top,p.border_width,r.bottom-r.top,br);}
            if(flags&8){r.bottom=W(r.bottom-p.border_height);Fill(p,r.left,r.bottom,r.right-r.left,p.border_height,br);}
            if(flags&1){Fill(p,r.left,r.top,p.border_width,r.bottom-r.top,tl);r.left=W(r.left+p.border_width);}
            if(flags&2){Fill(p,r.left,r.top,r.right-r.left,p.border_height,tl);r.top=W(r.top+p.border_height);}
        }
    }
    if(flags&0x800){
        const unsigned color=flags&0x8000?p.window:p.face;
        if(flags&16)Diagonal(p,r,flags,color);
        else Fill(p,r.left,r.top,r.right-r.left,r.bottom-r.top,color);
    }
    if(flags&0x2000)*rectangle=r;
    return true;
}

bool ButtonFrame(Painter &p,Gdi::RECT *rectangle,unsigned state,unsigned flags)
{
    if(!rectangle)return false;
    Gdi::RECT r=*rectangle;
    if(!DrawEdge(p,&r,state&0x600?10:5,(flags&0xf00f)|0x200f))return false;
    if(state&0x400){
        if(p.depth<8||p.highlight==0xffffff){
            if(!p.pattern)return false;
            p.pattern(p.context,r.left,r.top,W(r.right-r.left),W(r.bottom-r.top),p.highlight,p.face);
        }else Fill(p,r.left,r.top,r.right-r.left,r.bottom-r.top,p.highlight);
    }else Fill(p,r.left,r.top,r.right-r.left,r.bottom-r.top,p.face);
    if(flags&0x2000)*rectangle=r;
    return true;
}

namespace {
struct Native { Gdi::HDC dc; bool success=true; };
void NativeFill(void *context,int x,int y,int width,int height,unsigned rgb)
{
    if(!width||!height)return;
    auto &n=*static_cast<Native *>(context);
    auto brush=Gdi::CreateSolidBrush(Gdi::RGB(rgb>>16,rgb>>8,rgb));
    if(!brush){n.success=false;return;}
    // USER's edge strips are PatBlt calls. A tiny frame can produce
    // negative extents, which draw on the other side of the origin.
    const auto old=Gdi::SelectObject(n.dc,brush);
    if(!Gdi::PatBlt(n.dc,x,y,width,height,Gdi::PATCOPY))n.success=false;
    Gdi::SelectObject(n.dc,old);
    Gdi::DeleteObject(brush);
}
void NativePattern(void *context,int x,int y,int width,int height,unsigned background,unsigned foreground)
{
    if(!width||!height)return;
    auto &n=*static_cast<Native *>(context);
    Gdi::BITMAPINFO info={{40,8,-8,1,32,0,0,0,0,0,0}};void *bits=nullptr;
    auto bitmap=Gdi::CreateDIBSection(n.dc,&info,0,&bits);
    if(!bitmap){n.success=false;return;}
    for(unsigned row=0;row<8;++row)for(unsigned col=0;col<8;++col)static_cast<unsigned *>(bits)[row*8+col]=(row+col)&1?background:foreground;
    auto brush=Gdi::CreatePatternBrush(bitmap);
    if(!brush)n.success=false;
    else{
        const auto old=Gdi::SelectObject(n.dc,brush);
        if(!Gdi::PatBlt(n.dc,x,y,width,height,Gdi::PATCOPY))n.success=false;
        Gdi::SelectObject(n.dc,old);
    }
    if(brush)Gdi::DeleteObject(brush);Gdi::DeleteObject(bitmap);
}
}
}
namespace Win98Frame { const unsigned *SystemColors(); }
namespace Win98Draw {
namespace {
// USER draws edges with its system colour brushes (Windows 98 selects hbrGray and
// the others), so the painter takes the colours of the system colour table,
// the ones the display shows.
void SystemPainter(Painter &p)
{
    const unsigned *c=Win98Frame::SystemColors();
    p.window=c[5];p.frame=c[6];p.face=c[15];p.shadow=c[16];p.highlight=c[20];p.dark_shadow=c[21];p.light=c[22];
}
}
bool DrawEdge(Gdi::HDC dc,Gdi::RECT *r,unsigned edge,unsigned flags)
{
    Native n={dc};Painter p={&n,NativeFill};SystemPainter(p);return DrawEdge(p,r,edge,flags)&&n.success;
}
bool ButtonFrame(Gdi::HDC dc,Gdi::RECT *r,unsigned state,unsigned flags)
{
    Native n={dc};Painter p={&n,NativeFill};SystemPainter(p);p.pattern=NativePattern;return ButtonFrame(p,r,state,flags)&&n.success;
}
namespace {
#if !defined(USER_DRAW_NO_VECTORS) && !defined(NEO98_RUNTIME_CONTENT)
struct TestTarget { unsigned char pixels[96*80*4];unsigned trace; };
void TestFill(void *context,int x,int y,int width,int height,unsigned rgb)
{
    auto &t=*static_cast<TestTarget *>(context);
    const unsigned values[]={(unsigned)x,(unsigned)y,(unsigned)width,(unsigned)height,rgb};
    for(unsigned value:values)for(unsigned byte=0;byte<4;++byte)t.trace=(t.trace^((value>>(byte*8))&255))*16777619u;
    if(width<=0||height<=0)return;
    for(int row=y<0?0:y;row<y+height&&row<80;++row)for(int col=x<0?0:x;col<x+width&&col<96;++col)
        for(unsigned b=0;b<4;++b)t.pixels[(row*96+col)*4+b]=rgb>>(b*8);
}
void TestPattern(void *context,int x,int y,int w,int h,unsigned background,unsigned foreground)
{
    auto &t=*static_cast<TestTarget *>(context);
    const unsigned values[]={0x504154u,(unsigned)x,(unsigned)y,(unsigned)w,(unsigned)h,background,foreground};
    for(unsigned value:values)for(unsigned b=0;b<4;++b)t.trace=(t.trace^((value>>(b*8))&255))*16777619u;
    if(w<=0||h<=0)return;
    for(int row=y<0?0:y;row<y+h&&row<80;++row)for(int col=x<0?0:x;col<x+w&&col<96;++col){
        const auto rgb=(row+col)&1?background:foreground;
        for(unsigned b=0;b<4;++b)t.pixels[(row*96+col)*4+b]=rgb>>(b*8);
    }
}
unsigned Crc(const unsigned char *bytes,unsigned count)
{
    static unsigned table[256];static bool ready=false;
    if(!ready){for(unsigned i=0;i<256;++i){unsigned c=i;for(unsigned b=0;b<8;++b)c=(c>>1)^((c&1)?0xedb88320u:0);table[i]=c;}ready=true;}
    unsigned c=~0u;for(unsigned i=0;i<count;++i)c=table[(c^bytes[i])&255]^(c>>8);return ~c;
}
#endif
}
#if !defined(USER_DRAW_NO_VECTORS) && !defined(NEO98_RUNTIME_CONTENT)
unsigned OriginalTestCount(){return sizeof UserEdgeVectors/sizeof UserEdgeVectors[0]+sizeof UserButtonVectors/sizeof UserButtonVectors[0];}
unsigned OriginalSelfTest()
{
    auto *target=new TestTarget;
    for(unsigned i=0;i<OriginalTestCount();++i){
        const unsigned edges=sizeof UserEdgeVectors/sizeof UserEdgeVectors[0];
        const bool button=i>=edges;const auto &v=button?UserButtonVectors[i-edges]:UserEdgeVectors[i];
        for(unsigned pixel=0;pixel<96*80;++pixel){target->pixels[pixel*4]=0x56;target->pixels[pixel*4+1]=0x34;target->pixels[pixel*4+2]=0x12;target->pixels[pixel*4+3]=0;}
        target->trace=2166136261u;Painter p={target,TestFill};p.depth=v[6];p.border_width=v[7];p.border_height=v[8];p.pattern=TestPattern;
        Gdi::RECT r={(int)v[0],(int)v[1],(int)v[2],(int)v[3]};
        const bool result=button?ButtonFrame(p,&r,v[4],v[5]):DrawEdge(p,&r,v[4],v[5]);
        if(result!=bool(v[13])||r.left!=(int)v[9]||r.top!=(int)v[10]||r.right!=(int)v[11]||r.bottom!=(int)v[12]||
           Crc(target->pixels,sizeof target->pixels)!=v[14]||target->trace!=v[15]){delete target;return i+1;}
    }
    delete target;return 0;
}
#endif
}

// ---------------------------------------------------------------------------
// DrawFrameControl: the recorded target shared by the host comparison and the
// retained replay, plus that replay.
#include "win98_frame_control.h"
#ifndef NEO98_RUNTIME_CONTENT
#include "../build/generated/marlett_glyphs.h"
#endif
#if !defined(USER_DRAW_NO_VECTORS) && !defined(NEO98_RUNTIME_CONTENT)
#include "../build/generated/frame_control_vectors.h"
#endif

namespace Win98Frame {
namespace {
void Record(Probe &p,const unsigned *values,unsigned count)
{
    for(unsigned i=0;i<count;++i)for(unsigned b=0;b<4;++b)p.trace=(p.trace^((values[i]>>(b*8))&255))*16777619u;
}
void Plot(Probe &p,int x,int y,unsigned rgb)
{
    if(!p.pixels||x<0||y<0||x>=p.width||y>=p.height)return;
    unsigned char *pixel=p.pixels+(y*p.width+x)*4;
    for(unsigned b=0;b<4;++b)pixel[b]=rgb>>(b*8);
}
void ProbeFill(void *context,int x,int y,int w,int h,unsigned rgb)
{
    auto &p=*static_cast<Probe *>(context);
    const unsigned values[]={0x464c4c46u,(unsigned)x,(unsigned)y,(unsigned)w,(unsigned)h,rgb};
    Record(p,values,6);
    if(w<=0||h<=0)return;
    for(int row=y;row<y+h;++row)for(int col=x;col<x+w;++col)Plot(p,col,row,rgb);
}
void ProbePattern(void *context,int x,int y,int w,int h,unsigned background,unsigned foreground)
{
    auto &p=*static_cast<Probe *>(context);
    const unsigned values[]={0x54544150u,(unsigned)x,(unsigned)y,(unsigned)w,(unsigned)h,background,foreground};
    Record(p,values,7);
    if(w<=0||h<=0)return;
    for(int row=y;row<y+h;++row)for(int col=x;col<x+w;++col)Plot(p,col,row,(row+col)&1?background:foreground);
}
unsigned ProbeCreateFont(void *context,int height)
{
    auto &p=*static_cast<Probe *>(context);
    const unsigned values[]={0x544e4f46u,(unsigned)height};Record(p,values,2);
    ++p.created;const unsigned handle=p.next_font++;
    if(handle<64)p.sizes[handle]=height;
    return handle;
}
void ProbeDeleteFont(void *context,unsigned handle)
{
    auto &p=*static_cast<Probe *>(context);
    const unsigned values[]={0x464c4544u,(unsigned)(handle<64?p.sizes[handle]:0)};Record(p,values,2);++p.deleted;
}
unsigned ProbeSelectFont(void *context,unsigned handle)
{
    auto &p=*static_cast<Probe *>(context);
    const unsigned values[]={0x464c4553u,(unsigned)(handle<64?p.sizes[handle]:0)};Record(p,values,2);
    const unsigned previous=(unsigned)p.current;p.current=handle<64?(int)handle:0;return previous;
}
int ProbeBkMode(void *context,int mode)
{
    auto &p=*static_cast<Probe *>(context);
    const unsigned values[]={0x444d4b42u,(unsigned)mode};Record(p,values,2);return 2;
}
void ProbeCell(void *context,int x,int y,char code,unsigned rgb,int height)
{
    auto &p=*static_cast<Probe *>(context);
    const unsigned values[]={0x4c4c4543u,(unsigned)x,(unsigned)y,(unsigned)(unsigned char)code,rgb,(unsigned)height};
    Record(p,values,6);++p.cells_drawn;
#ifndef NEO98_RUNTIME_CONTENT
    for(int row=0;row<height;++row){
        const unsigned bits=Win98Marlett::Row(height,code,row);
        for(int column=0;column<height;++column)if((bits>>column)&1)Plot(p,x+column,y+row,rgb);
    }
#endif
}
Environment probe_environment;
// USER's font handles outlive one call, so the recorded target does too.
Probe probe_state;
}

bool Run(Probe &p,Gdi::RECT *rect,unsigned type,unsigned state,int depth,
         int border_width,int border_height,unsigned menu_background,
         unsigned check_background,bool keep)
{
    if(!keep){probe_environment=Environment{};probe_state=Probe{};}
    p.next_font=probe_state.next_font;p.current=probe_state.current;
    for(unsigned i=0;i<sizeof p.sizes/sizeof p.sizes[0];++i)p.sizes[i]=probe_state.sizes[i];
    auto &e=probe_environment;
    e.painter.context=&p;e.painter.fill=ProbeFill;e.painter.pattern=ProbePattern;
    e.painter.depth=depth;e.painter.border_width=border_width;e.painter.border_height=border_height;
    e.painter.window=p.colors[ColorWindow];e.painter.frame=p.colors[ColorWindowFrame];
    e.painter.face=p.colors[ColorBtnFace];e.painter.shadow=p.colors[ColorBtnShadow];
    e.painter.highlight=p.colors[ColorBtnHighlight];e.painter.dark_shadow=p.colors[21];
    e.painter.light=p.colors[22];
    e.context=&p;e.colors=p.colors;e.menu_background=menu_background;e.check_background=check_background;
    e.create_font=ProbeCreateFont;e.delete_font=ProbeDeleteFont;e.select_font=ProbeSelectFont;
    e.set_bk_mode=ProbeBkMode;e.cell=ProbeCell;
    const bool result=DrawFrameControl(e,rect,type,state);
    probe_state.next_font=p.next_font;probe_state.current=p.current;
    for(unsigned i=0;i<sizeof p.sizes/sizeof p.sizes[0];++i)probe_state.sizes[i]=p.sizes[i];
    return result;
}

#if !defined(USER_DRAW_NO_VECTORS) && !defined(NEO98_RUNTIME_CONTENT)
namespace {
// The classic Win98 table the original comparison installed in USER's data.
const unsigned ProbeColors[ColorCount]={
    0xc0c0c0,0x008080,0x000080,0x808080,0xc0c0c0,0xffffff,0,0,0,0xffffff,
    0xc0c0c0,0xc0c0c0,0x808080,0x000080,0xffffff,0xc0c0c0,0x808080,0x808080,0,0xc0c0c0,
    0xffffff,0x404040,0xc0c0c0,0,0xffffe1,0xc0c0c0,0x0000ff,0x1084d0,0xb5b5b5};
unsigned ProbeCrc(const unsigned char *bytes,unsigned count)
{
    static unsigned table[256];static bool ready=false;
    if(!ready){for(unsigned i=0;i<256;++i){unsigned c=i;for(unsigned b=0;b<8;++b)c=(c>>1)^((c&1)?0xedb88320u:0);table[i]=c;}ready=true;}
    unsigned c=~0u;for(unsigned i=0;i<count;++i)c=table[(c^bytes[i])&255]^(c>>8);return ~c;
}
}
unsigned OriginalTestCount(){return sizeof FrameControlVectors/sizeof FrameControlVectors[0];}
unsigned OriginalSelfTest()
{
    auto *pixels=new unsigned char[96*80*4];
    for(unsigned i=0;i<OriginalTestCount();++i){
        const auto &v=FrameControlVectors[i];
        for(unsigned pixel=0;pixel<96*80;++pixel){
            pixels[pixel*4]=0x56;pixels[pixel*4+1]=0x34;pixels[pixel*4+2]=0x12;pixels[pixel*4+3]=0;}
        Probe p;p.pixels=pixels;
        for(unsigned index=0;index<ColorCount;++index)p.colors[index]=ProbeColors[index];
        Gdi::RECT r={(int)v[0],(int)v[1],(int)v[2],(int)v[3]};
        const bool result=Run(p,&r,v[4],v[5],(int)v[6],(int)v[7],(int)v[8],v[9],v[10],v[11]!=0);
        if(result!=bool(v[16])||r.left!=(int)v[12]||r.top!=(int)v[13]||r.right!=(int)v[14]||
           r.bottom!=(int)v[15]||ProbeCrc(pixels,96*80*4)!=v[17]||p.trace!=v[18]){
            delete[] pixels;return i+1;}
    }
    delete[] pixels;return 0;
}
#endif
}

// ---------------------------------------------------------------------------
// Native adapter: the implemented body drawing through the project's GDI.
namespace Win98Frame {
namespace {
// The classic Win98 table; the public GetSysColor answers from the same one.
unsigned ClassicColors[ColorCount]={
    0xc0c0c0,0x008080,0x000080,0x808080,0xc0c0c0,0xffffff,0,0,0,0xffffff,
    0xc0c0c0,0xc0c0c0,0x808080,0x000080,0xffffff,0xc0c0c0,0x808080,0x808080,0,0xc0c0c0,
    0xffffff,0x000000,0xdfdfdf,0,0xffffe1,0xb4b4b4,0x0000ff,0x1084d0,0xb5b5b5};

struct NativeFrame {
    Gdi::HDC dc;
    bool success=true;
    unsigned next=1;
    int sizes[64]={};
    int current=0;
};
Gdi::HFONT (*frame_font_provider)(int)=nullptr;
void FrameFill(void *context,int x,int y,int width,int height,unsigned rgb)
{
    auto &n=*static_cast<NativeFrame *>(context);
    Win98Draw::Native painter{n.dc};
    Win98Draw::NativeFill(&painter,x,y,width,height,rgb);
    n.success=n.success&&painter.success;
}
void FramePattern(void *context,int x,int y,int width,int height,unsigned background,unsigned foreground)
{
    auto &n=*static_cast<NativeFrame *>(context);
    // A GDI pattern brush carries the DC's device-space brush origin. Local
    // pixel loops lost its phase when OEM cells were copied from USER's strip.
    Win98Draw::Native painter{n.dc};
    Win98Draw::NativePattern(&painter,x,y,width,height,background,foreground);
    n.success=n.success&&painter.success;
}
unsigned FrameCreateFont(void *context,int height)
{
    auto &n=*static_cast<NativeFrame *>(context);
    const unsigned handle=n.next++;
    if(handle<64)n.sizes[handle]=height;
    return handle;
}
void FrameDeleteFont(void *,unsigned){}
unsigned FrameSelectFont(void *context,unsigned handle)
{
    auto &n=*static_cast<NativeFrame *>(context);
    const unsigned previous=(unsigned)n.current;
    n.current=handle<64?(int)handle:0;
    return previous;
}
int FrameBkMode(void *context,int mode)
{
    auto &n=*static_cast<NativeFrame *>(context);
    return Gdi::SetBkMode(n.dc,mode);
}
// One Marlett cell, drawn as horizontal runs so a row costs one brush.
void FrameCell(void *context,int x,int y,char code,unsigned rgb,int height)
{
    auto &n=*static_cast<NativeFrame *>(context);
    if(frame_font_provider)if(const auto font=frame_font_provider(height)){
        const auto old=Gdi::SelectObject(n.dc,font);
        const auto color=Gdi::SetTextColor(n.dc,Gdi::RGB(rgb>>16,rgb>>8,rgb));
        if(!Gdi::TextOutA(n.dc,x,y,&code,1))n.success=false;
        Gdi::SetTextColor(n.dc,color);Gdi::SelectObject(n.dc,old);Gdi::DeleteObject(font);
        return;
    }
#ifdef NEO98_RUNTIME_CONTENT
    n.success=false; // Installed Marlett is missing or its driver failed.
#else
    auto brush=Gdi::CreateSolidBrush(Gdi::RGB(rgb>>16,rgb>>8,rgb));
    if(!brush){n.success=false;return;}
    for(int row=0;row<height;++row){
        const unsigned bits=Win98Marlett::Row(height,code,row);
        int column=0;
        while(column<height){
            if(!((bits>>column)&1)){++column;continue;}
            int end=column;
            while(end<height&&((bits>>end)&1))++end;
            const Gdi::RECT run={x+column,y+row,x+end,y+row+1};
            if(!Gdi::FillRect(n.dc,&run,brush))n.success=false;
            column=end;
        }
    }
    Gdi::DeleteObject(brush);
#endif
}
// USER keeps one Marlett font for the whole system; so does this adapter.
Environment native_environment;
}

const unsigned *SystemColors(){return ClassicColors;}
void SetFontProvider(Gdi::HFONT (*provider)(int)){frame_font_provider=provider;}
// USER. The one table every drawing path reads; SetSysColors writes
// through here. The original snaps the colour to the nearest one the display
// can show unless the index is one of the five that may dither; on a
// display without a palette GetNearestColor is the identity.
namespace {
// What the table was asked to hold; ClassicColors is what the display shows.
unsigned RequestedColors[ColorCount];
bool requested_ready=false, palette_display=false;
void RequestedReady(){if(!requested_ready){for(unsigned i=0;i<ColorCount;++i)RequestedColors[i]=ClassicColors[i];requested_ready=true;}}
// 1, 10, 11, 12 and 24 keep their colour and may dither.
bool Snapped(unsigned index){return index!=1&&index!=24&&(index<10||index>12);}
// Plain RGB on the palette display uses the sixteen unreserved colours.
// The four extra default-palette entries require a palette-tagged colour.
unsigned NearestStatic(unsigned colour)
{

    unsigned best=~0u,found=0;
    for(unsigned i=0;i<20;++i){
        if(i==8)i=12;
        const unsigned bgr=Win98GdiPalette::DefaultColors[i];
        unsigned c=((bgr&255)<<16)|(bgr&0xff00)|((bgr>>16)&255);
        const int r=int(c>>16)-int(colour>>16&255),g=int(c>>8&255)-int(colour>>8&255),b=int(c&255)-int(colour&255);
        const unsigned d=unsigned(r*r+g*g+b*b);
        if(d<best){best=d;found=c;}
        if(!d)break;
    }
    return found;
}
unsigned Shown(unsigned index,unsigned colour){return palette_display&&Snapped(index)?NearestStatic(colour):colour;}
}
bool SetSystemColor(unsigned index,unsigned colour)
{
    if(index>=ColorCount)return false;
    RequestedReady();
    RequestedColors[index]=colour&0xffffffu;
    ClassicColors[index]=Shown(index,colour&0xffffffu);
    return true;
}
void RefreshSystemColors(bool palette,void (*changed)(unsigned,unsigned))
{
    RequestedReady();
    palette_display=palette;
    for(unsigned i=0;i<ColorCount;++i){
        const unsigned shown=Shown(i,RequestedColors[i]);
        if(shown==ClassicColors[i])continue;
        ClassicColors[i]=shown;
        if(changed)changed(i,((shown&255)<<16)|(shown&0xff00)|((shown>>16)&255));
    }
}

bool DrawFrameControl(Gdi::HDC dc,Gdi::RECT *rectangle,unsigned type,unsigned state,
                      unsigned menu_background,unsigned check_background)
{
    NativeFrame n={dc};
    auto &e=native_environment;
    e.painter.context=&n;e.painter.fill=FrameFill;e.painter.pattern=FramePattern;
    e.painter.depth=Gdi::SurfaceDepth(dc);e.painter.border_width=1;e.painter.border_height=1;
    e.painter.window=ClassicColors[ColorWindow];e.painter.frame=ClassicColors[ColorWindowFrame];
    e.painter.face=ClassicColors[ColorBtnFace];e.painter.shadow=ClassicColors[ColorBtnShadow];
    e.painter.highlight=ClassicColors[ColorBtnHighlight];e.painter.dark_shadow=ClassicColors[21];
    e.painter.light=ClassicColors[22];
    e.context=&n;e.colors=ClassicColors;
    // The public mask/image backgrounds are white and black; OEM callers
    // can supply their strip's brush selectors explicitly.
    e.menu_background=menu_background;e.check_background=check_background;
    e.create_font=FrameCreateFont;e.delete_font=FrameDeleteFont;e.select_font=FrameSelectFont;
    e.set_bk_mode=FrameBkMode;e.cell=FrameCell;
    const bool result=DrawFrameControl(e,rectangle,type,state);
    return result&&n.success;
}
}
