// SPDX-License-Identifier: GPL-2.0-or-later
#include "x86_app.h"
#include <string.h>
#include <stdlib.h>
#include "win98_image_resource.h"
#include "win98_image_probe.h"
#include "win98_image_file.h"
#include "win98_image_copy.h"
#include "win98_icon_info.h"
#ifdef NEO98_RUNTIME_CONTENT
#include "windows_runtime_data.h"
#else
#include "win98_gdi_palette_tables.h"
#endif
#include "win98_image_resample.h"
#include "win98_oem_bitmaps.h"
#include "win98_frame_control.h"
#ifdef NEO98_RUNTIME_CONTENT
#include "windows_runtime_data.h"
#else
#include "../build/image-resource/system_assets.h"
#endif
#include <new>
namespace X86 {
Application::Icon *Application::SharedIcons() {
  struct Store {
    Icon icons[1023];
    ~Store(){for(auto &icon:icons)delete icon.data;}
  };
  static Store store;
  return store.icons;
}
void Application::FreeIcons() {
  auto *icons=SharedIcons();
  for(unsigned i=0;i<1023;++i)if(icons[i].owner==this){
    auto &icon=icons[i];delete icon.data;
    unsigned generation=icon.generation%0x3ffff+1;
    icon=Icon{};icon.generation=generation;
  }
}
Application::Icon *Application::FindIcon(unsigned h) {
  auto *icons=SharedIcons();
  unsigned i = (h & 1023) - 1;
  return (h & 0xf0000000) == 0x30000000 && i < 1023 && icons[i].data &&
                 icons[i].generation == ((h >> 10) & 0x3ffff)
             ? &icons[i]
             : nullptr;
}
unsigned Application::NewIcon(WinImage::Image *data, unsigned resource,
                              bool shared) {
  if(!data){last_error=8;return 0;}
  auto *icons=SharedIcons();
  for (unsigned i = 0; i < 1023; ++i)
    if (!icons[i].data) {
      auto &icon = icons[i];
      unsigned generation=icon.generation;
      icon=Icon{};icon.generation=generation;
      icon.owner = this;
      icon.data = data;
      icon.resource = resource;
      icon.shared = shared;
      return 0x30000000 | (icon.generation << 10) | (i + 1);
    }
  delete data;
  last_error = 8;
  return 0;
}
Application::ImageList *Application::SharedImageLists() {
  struct Store {
    ImageList lists[16];
    ~Store(){for(auto &list:lists)for(auto *item:list.items)delete item;}
  };
  static Store store;
  return store.lists;
}
Application::ImageList *Application::FindImageList(unsigned h) {
  const unsigned kind=h&0xf0000000u,i=(h&255)-1;
  if(kind!=0x40000000u&&kind!=0x60000000u)return nullptr;
  auto *lists=kind==0x60000000u?SharedImageLists():image_lists;
  return i<16&&lists[i].used&&lists[i].generation==((h>>8)&0xfffff)?&lists[i]:nullptr;
}

namespace {
namespace IR=Win98ImageResource;
using IW=IR::Word;
constexpr unsigned SystemImageInstance=0x7ffffffd,SystemImageModule=0x7ffffffe;
bool EqualImageName(const char*a,const char*b){while(*a&&*b){unsigned x=(unsigned char)*a++,y=(unsigned char)*b++;if(x>='a'&&x<='z')x-=32;if(y>='a'&&y<='z')y-=32;if(x!=y)return false;}return *a==*b;}
}
struct Application::ImageResources final:IR::Environment {
 Application&app;bool ready=false,initializing=false;bool failed=false;unsigned startup_images[22]{},startup_count=0;IR::Record records[256]{};bool used[256]{};unsigned heads[3]{};
 struct Resource {bool used=false;unsigned address=0,size=0,type=0;const unsigned char*system=nullptr;unsigned char*copy=nullptr;} resources[16];
 explicit ImageResources(Application&a):app(a){}
 Win98ImageDirectory::Display Display()override{return {IW(app.SystemMetric(11)),IW(app.SystemMetric(12)),IW(app.SystemMetric(13)),IW(app.SystemMetric(14)),0x41,32};}
 unsigned SystemInstance()override{return SystemImageInstance;}unsigned SystemModule()override{return SystemImageModule;}
 unsigned CompatibilityInstance()override{const unsigned i=app.FindModule("USER32.DLL");return i<Application::ModuleCapacity?app.modules[i].handle:0;}
 IW ExpectedVersion()override{const unsigned v=app.image.SubsystemVersion();return IW(((v>>8)&0xff00)|(v&255));}
 unsigned ApplicationTable()override{return 1;}unsigned TaskTable()override{return 1;}unsigned SystemTable()override{return 2;}
 bool Win32Module(unsigned)override{return true;}
 unsigned ModuleIdentity(unsigned h)override{unsigned i=app.ModuleIndex(h);return i<Application::ModuleCapacity?app.modules[i].order*Application::ModuleCapacity+i:0;}
 unsigned Head(unsigned t)override{return heads[t];}void Head(unsigned t,unsigned h)override{heads[t]=h;}
 IR::Record&Item(unsigned h)override{return records[h-1];}
 unsigned AllocateRecord()override{for(unsigned i=0;i<256;++i)if(!used[i]){used[i]=true;records[i]={};return i+1;}app.last_error=8;return 0;}
 void ReleaseRecord(unsigned h)override{used[h-1]=false;records[h-1]={};}
 IW FindAtom(IR::Name n)override{return IW(app.GlobalAtomSearch(n.value,false));}
 IW AddAtom(IR::Name n)override{return IW(app.GlobalAtomSearch(n.value,true));}
 void DeleteAtom(IW n)override{app.GlobalAtomDelete(n);}
 unsigned FindResource(unsigned module,IR::Name name,IW type)override{
  Resource*r=nullptr;unsigned slot=0;for(;slot<16;++slot)if(!resources[slot].used){r=&resources[slot];break;}
  if(!r){app.last_error=8;return 0;}
  Pe32::Image::ResourceKey key;unsigned short wide[260];if(!app.ResourceName(name.value,key,wide))return 0;
  if(module==SystemImageInstance){
   const char*text=name.text?app.ReadString(name.value):nullptr;
   for(const auto&v:OriginalImageResources)if(v.type==type&&((key.name&&EqualImageName(v.name,text))||(!key.name&&!v.name[0]&&v.id==key.id))){*r={true,0,v.Size(),type,v.data,nullptr};return slot+1;}
   app.last_error=1814;return 0;
  }
  const unsigned index=app.ModuleIndex(module);if(index>=Application::ModuleCapacity){app.last_error=6;return 0;}
  const auto*pe=app.modules[index].pe;if(!pe){app.last_error=1812;return 0;}
  Pe32::Image::ResourceEntry entry;Pe32::Image::ResourceKey tk;tk.id=type;unsigned error=0;
  bool found=pe->FindResource(tk,key,0,entry,&error);
  if(!found&&error==1815)found=pe->FindResource(tk,key,0x409,entry,&error)||pe->FindResource(tk,key,~0u,entry,&error);
  if(!found){app.last_error=error;return 0;}
  if(!entry.size){app.last_error=13;return 0;}
  *r={true,pe->Base()+entry.data,entry.size,type,nullptr,nullptr};return slot+1;
 }
 unsigned LoadResource(unsigned,unsigned h)override{return h;}
 IR::Block LockResource(unsigned h)override{
  if(!h||h>16||!resources[h-1].used)return {};auto&r=resources[h-1];
  if(!r.system&&!r.copy){r.copy=static_cast<unsigned char*>(malloc(r.size));if(!r.copy){app.last_error=8;return {};}
   if(!app.Read(r.address,r.copy,r.size)){free(r.copy);r.copy=nullptr;return {};}}
  const auto*p=r.system?r.system:r.copy;
  if((r.type==12||r.type==14)&&(r.size<6||(Win98ImageDirectory::Read(p)==0&&(Win98ImageDirectory::Read(p+2)==1||Win98ImageDirectory::Read(p+2)==2)&&(!Win98ImageDirectory::Read(p+4)||Win98ImageDirectory::Read(p+4)>(r.size-6)/14)))){app.last_error=13;return {};}
  return {p,r.size};
 }
 void UnlockResource(unsigned)override{}
 bool FreeResource(unsigned h)override{if(h&&h<=16){auto&r=resources[h-1];free(r.copy);r={};}return false;}
 unsigned GlobalHandle(unsigned)override{return 0;}IW GlobalFlags(unsigned)override{return 0;}
 unsigned CreateBitmap(IR::Block data,IW cx,IW cy,IW flags)override{
  // LR_LOADTRANSPARENT (20h) and LR_LOADMAP3DCOLORS (1000h) change the colour
  // table of a DIB of eight bits or fewer before the bitmap is made: the
  // entry of the first pixel of the bits becomes COLOR_WINDOW, and the grey
  // shades 808080h, C0C0C0h and DFDFDFh become COLOR_3DSHADOW, COLOR_3DFACE
  // and COLOR_3DLIGHT. COMCTL32's ImageList_LoadImage asks for the first
  // (the check boxes of APPWIZ.CPL's MS-DOS options).
  struct Patched{unsigned char*bytes=nullptr;~Patched(){free(bytes);}}patched;
  if((flags&0x1020)&&data.size>=40&&Win98ImageProbe::Read32(data.bytes)==40){
   const unsigned bpp=Win98ImageDirectory::Read(data.bytes+14);
   unsigned colors=Win98ImageProbe::Read32(data.bytes+32);if(!colors&&bpp<=8)colors=1u<<bpp;
   if(bpp<=8&&40+colors*4<data.size&&(patched.bytes=static_cast<unsigned char*>(malloc(data.size)))){
    memcpy(patched.bytes,data.bytes,data.size);
    auto rgb=[&](unsigned index){return unsigned(patched.bytes[40+index*4+2])<<16|unsigned(patched.bytes[40+index*4+1])<<8|patched.bytes[40+index*4];};
    auto put=[&](unsigned index,unsigned system){const unsigned a[]={system};const unsigned c=app.Api("GetSysColor",a);
     patched.bytes[40+index*4]=(c>>16)&255;patched.bytes[40+index*4+1]=(c>>8)&255;patched.bytes[40+index*4+2]=c&255;};
    const unsigned first=(patched.bytes[40+colors*4]>>(8-bpp))&((1u<<bpp)-1);
    const unsigned first_colour=first<colors?rgb(first):~0u;
    if(flags&0x1000)for(unsigned i=0;i<colors;++i){const unsigned c=rgb(i);
     if(c==0x808080)put(i,16);else if(c==0xc0c0c0)put(i,15);else if(c==0xdfdfdf)put(i,22);}
    if((flags&0x20)&&first<colors){for(unsigned i=0;i<colors;++i)if(rgb(i)==first_colour||i==first)put(i,5);}
    data.bytes=patched.bytes;
   }
   flags&=IW(~0x1020u);
  }
  if(flags&~IW(0xa0d1)){app.last_error=50;return 0;} // LR_DEFAULTSIZE (40h) is ignored for bitmaps
  if(data.size<12){app.last_error=13;return 0;}
  const unsigned header=Win98ImageProbe::Read32(data.bytes);
  const unsigned w=Win98ImageDirectory::Read(data.bytes+4),h=Win98ImageDirectory::Read(data.bytes+(header==12?6:8));
  const bool resized=(cx&&cx!=w)||(cy&&cy!=h);
  if(flags&0x2000){
   // LR_CREATEDIBSECTION: USER 18:xxxx hands the packed DIB to CreateDIBSection
   // and copies the pixels into the section; the caller then owns a DIB
   // whose bits and colour table it can read and edit (COMCTL32's
   // CreateMappedBitmap-style colour mapping in BROWSEUI's toolbars).
   if(header!=40&&header!=12){app.last_error=50;return 0;}
   const unsigned bpp=Win98ImageDirectory::Read(data.bytes+(header==12?10:14));
   unsigned colors=header==40?Win98ImageProbe::Read32(data.bytes+32):0;
   if(!colors&&bpp<=8)colors=1u<<bpp;
   if(bpp>8)colors=header==40&&Win98ImageProbe::Read32(data.bytes+16)==3?3:0; // BI_BITFIELDS masks
   const unsigned table=header+colors*(header==12?3:4);
   if(table>data.size){app.last_error=13;return 0;}
   // fills a zero cx or cy with the DIB's own size, so USER always
   // takes the path below that clears the compression in the header and
   // draws the resource into the section. For an uncompressed DIB at its own
   // size that is a plain copy, which the port makes directly; an RLE
   // resource has to be decoded (INTL.CPL 50, the world map of Regional
   // Settings).
   const unsigned compression=header==40?Win98ImageProbe::Read32(data.bytes+16):0;
   const bool redraw=resized||compression==1||compression==2;
   const unsigned block=app.Allocate(table+4);if(!block){app.last_error=8;return 0;}
   app.Store(block,data.bytes,table);
   if(redraw){
    // Windows 98 writes cx and cy into the header before CreateDIBSection;
    // a 40-byte header also loses its compression unless it is BI_BITFIELDS.
    if(header==12){const unsigned tx=cx?cx:w,ty=cy?cy:h;const unsigned char size[]={(unsigned char)tx,(unsigned char)(tx>>8),(unsigned char)ty,(unsigned char)(ty>>8)};app.Store(block+4,size,4);}
    else{app.Write32(block+4,cx?cx:w);app.Write32(block+8,cy?cy:h);if(app.Read32(block+16)!=3){app.Write32(block+16,0);app.Write32(block+20,0);}}
   }
   unsigned args[]={0,block,0,block+table,0,0};
   const unsigned bitmap=app.GdiApi("CreateDIBSection",args);
   const unsigned bits=bitmap?app.Read32(block+table):0;
   app.Free(block);
   if(!bitmap||!bits){if(bitmap)Gdi::DeleteObject(bitmap);app.last_error=8;return 0;}
   const unsigned source_width=header==40?Win98ImageProbe::Read32(data.bytes+4):w;
   const int source_rows=header==40?int(Win98ImageProbe::Read32(data.bytes+8)):int(h);
   const unsigned source_height=source_rows<0?unsigned(-(long long)source_rows):unsigned(source_rows);
   if(redraw){
    // Draw the original DIB into the selected section. Going through a
    // device bitmap loses a monochrome resource's colour table.
    if(!app.DrawResourceBitmap(bitmap,data.bytes,data.size,cx?cx:w,cy?cy:h,flags)){
     Gdi::DeleteObject(bitmap);app.last_error=8;return 0;
    }
    return bitmap;
   }
   // A BMP file may carry bytes after the last scan line. USER copies the
   // DIB's scan lines into the allocation returned by CreateDIBSection; the
   // file tail is not part of that allocation. Copying every remaining byte
   // wrote past Kaggz's 640 x 480 x 24-bit section when skin.bmp ended with
   // two trailing bytes.
   const unsigned long long stride=((unsigned long long)source_width*bpp+31)/32*4;
   const unsigned long long pixels64=stride*source_height;
   if(pixels64>data.size-table){Gdi::DeleteObject(bitmap);app.last_error=13;return 0;}
   const unsigned pixels=unsigned(pixels64);
   if(pixels&&!app.Store(bits,data.bytes+table,pixels)){Gdi::DeleteObject(bitmap);app.last_error=8;return 0;}
   return bitmap;
  }
  const unsigned bitmap=app.OwnGdi(app.CreateResourceBitmap(data.bytes,data.size),1);
  if(!bitmap)return 0;
  if(!resized&&!(flags&0x81))return bitmap;
  // The allocation follows the shared USER18 bitmap-format decision, but
  // LoadImage stretches the original DIB, whose depth controls filtering.
  const unsigned result=app.CopyBitmapImage(bitmap,cx?cx:w,cy?cy:h,flags&0x81,false);
  if(result&&!app.DrawResourceBitmap(result,data.bytes,data.size,cx?cx:w,cy?cy:h,flags)){
   const unsigned drop[]={result,bitmap};
   app.GdiApi("DeleteObject",drop);app.GdiApi("DeleteObject",drop+1);
   app.last_error=8;return 0;
  }
  const unsigned drop[]={bitmap};
  app.GdiApi("DeleteObject",drop);
  if(!result)app.last_error=8;
  return result;
 }
 unsigned CreateIcon(IR::Block data,bool icon,unsigned,IW cx,IW cy,IW flags)override{
  const unsigned skip=icon?0:4;if(data.size<skip+12){app.last_error=13;return 0;}
  if(flags&~IW(0x80d1)){app.last_error=50;return 0;}
  const unsigned header=Win98ImageProbe::Read32(data.bytes+skip);if(header!=12&&(header!=40||data.size<skip+40)){app.last_error=13;return 0;}const unsigned bpp=Win98ImageDirectory::Read(data.bytes+skip+(header==12?10:14));
  if(((flags&1)&&bpp!=1)||((flags&0x80)&&bpp>4)){app.last_error=50;return 0;}
  auto*image=new WinImage::Image;if(!image){app.last_error=8;return 0;}
  if(!image->DecodeIcon(data.bytes+skip,data.size-skip,cx,cy)){app.last_error=image->failure;delete image;return 0;}
  const unsigned h=app.NewIcon(image);if(auto*p=app.FindIcon(h)){p->cursor=!icon;if(icon){p->hotspot_x=image->width/2;p->hotspot_y=image->height/2;}else{
   const unsigned sw=Win98ImageDirectory::Read(data.bytes+skip+4),sh=Win98ImageDirectory::Read(data.bytes+skip+(header==12?6:8))/2;
   if(sw&&sh){p->hotspot_x=unsigned(int(Win98ImageDirectory::Read(data.bytes))*int(image->width)/int(sw));p->hotspot_y=unsigned(int(Win98ImageDirectory::Read(data.bytes+2))*int(image->height)/int(sh));}
  }}return h;
 }
 unsigned LoadFile(IR::Name name,IW type,IW cx,IW cy,IW flags)override{
  if(!name.text){app.last_error=87;return 0;}const char*path=app.ReadString(name.value);char absolute[260];unsigned id=0;
  unsigned error=app.files.FullPath(path,absolute);if(!error)error=app.files.store.Lookup(absolute,id);if(error){app.last_error=error;return 0;}
  const auto*file=app.files.store.Get(id);
  // Bytes: a file of the read-only tree or of a host volume is fetched here.
  const auto*p=file?app.files.store.Bytes(id):nullptr;if(!file||!p){app.last_error=13;return 0;}
  const unsigned size=file->size;
  if(type==2){if(size<26||IW(p[0]|unsigned(p[1])<<8)!=0x4d42){app.last_error=13;return 0;}
   const unsigned offset=Win98ImageProbe::Read32(p+10);const unsigned header=Win98ImageProbe::Read32(p+14);unsigned colors=0;
   if(header==40&&size>=54){colors=Win98ImageProbe::Read32(p+46);const unsigned b=Win98ImageDirectory::Read(p+28);if(!colors&&b<=8)colors=1u<<b;}
   else if(header==12){const unsigned b=Win98ImageDirectory::Read(p+24);if(b<=8)colors=1u<<b;}else{app.last_error=50;return 0;}
   if(offset!=14+header+colors*(header==12?3:4)){app.last_error=50;return 0;}
   return CreateBitmap({p+14,size-14},cx,cy,flags);
  }
  if(size>=4&&Win98ImageProbe::Read32(p)==0x46464952){app.last_error=50;return 0;}
  if(size<8||Win98ImageDirectory::Read(p)||!(Win98ImageDirectory::Read(p+2)==1||Win98ImageDirectory::Read(p+2)==2)){app.last_error=13;return 0;}
  const unsigned count=Win98ImageDirectory::Read(p+4);if(!count||count>(size-6)/16||count>4096){app.last_error=13;return 0;}
  auto*dir=static_cast<unsigned char*>(calloc(6+count*14,1));if(!dir){app.last_error=8;return 0;}
  const unsigned index=Win98ImageFile::Select(Display(),p,IW(count),type==3,cx,cy,flags,1,32,dir);free(dir);
  if(index>=count){app.last_error=13;return 0;}
  const auto*entry=p+6+index*16;const unsigned bytes=Win98ImageProbe::Read32(entry+8),offset=Win98ImageProbe::Read32(entry+12);
  if(!bytes||offset>size||bytes>size-offset){app.last_error=13;return 0;}
  if(bytes>65535){app.last_error=50;return 0;}
  if(type==3)return CreateIcon({p+offset,bytes},true,0,cx,cy,flags);
  auto*cursor=static_cast<unsigned char*>(malloc(bytes+4));if(!cursor){app.last_error=8;return 0;}
  memcpy(cursor,entry+4,4);memcpy(cursor+4,p+offset,bytes);const unsigned h=CreateIcon({cursor,bytes+4},false,0,cx,cy,flags);free(cursor);return h;
 }
 unsigned DrawSystemBitmap(IW id,IW,IW,IW)override{
  // USER maps OEM resource IDs to cells in its cached bitmap strip.
  // ignores LoadImage dimensions/flags for this path. Draw the same
  // cells through the implemented frame painter instead of loading a DLL.
  static const unsigned short mapping[][2]={
   {0x7ff6,0x47},{0x7ff9,0x2d},{0x7ff8,0x3f},{0x7ff7,0x48},
   {0x7fe2,0x32},{0x7ff0,0x32},{0x7fe6,0x33},{0x7fe0,0x35},
   {0x7fee,0x3a},{0x7fe4,0x3b},{0x7fde,0x3d},{0x7ffc,0x32},
   {0x7ffa,0x3a},{0x7ff5,0x16},{0x7ff3,0x1a},{0x7ffb,0x36},
   {0x7ffd,0x2e},{0x7ff4,0xe},{0x7fe3,0x3e},{0x7fed,0x16},
   {0x7fea,0x17},{0x7feb,0x1a},{0x7fe8,0x1b},{0x7fef,0x36},
   {0x7fe5,0x37},{0x7fdf,0x39},{0x7ffe,0x2d},{0x7ff1,0x2e},
   {0x7fe7,0x2f},{0x7fe1,0x31},{0x7fec,0xe},{0x7fe9,0xf}};
  unsigned index=~0u;for(const auto &entry:mapping)if(entry[0]==id){index=entry[1];break;}
  const bool close=id==0x7ff2||id==0x7fff,checks=id==0x7ff7;
  if(index==~0u&&!close){app.last_error=50;return 0;}
  auto metric=[&](unsigned n){const unsigned a[]={n};return int(app.Api("GetSystemMetrics",a));};
  Win98Oem::Metrics m;
  m.scroll_width=metric(2);m.scroll_height=metric(3);
  m.scroll_cross_y=metric(20);m.scroll_cross_x=metric(21);
  m.size_x=metric(30);m.size_y=metric(31);m.caption_x=metric(54);m.caption_y=metric(55);
  m.small_x=metric(52);m.small_y=metric(53);
  m.edge_x=metric(45);m.edge_y=2*metric(46);m.border_y=metric(6);
  m.menu_bias=metric(72);m.menu_extra=0;
  int width=0,height=0;
  if(close){width=2*(m.edge_x+m.caption_x);height=m.caption_y+m.edge_y;}
  else{
   Win98Oem::Size(m,index,width,height);
   if(!width&&height>=0&&height<int(Win98UserResources::StripCount))Win98Oem::Size(m,unsigned(height),width,height);
  }
  const bool padded=index==0x16||index==0x1a;
  const bool combo=id==0x7fe2,mono=id==0x7fe3||id==0x7ff8;
  if(padded)height+=m.edge_y;
  if(combo){width-=2*m.edge_x;height-=m.edge_y;}
  if(checks){width*=4;height*=3;}
  if(width<=0||height<=0){app.last_error=87;return 0;}
  const unsigned make[]={unsigned(width),unsigned(height),1,mono?1u:unsigned(Gdi::DisplayDepth()),0};
  const unsigned bitmap=app.GdiApi("CreateBitmap",make);if(!bitmap)return 0;
  const auto dc=Gdi::CreateCompatibleDC();
  if(!dc){Gdi::DeleteObject(bitmap);app.last_error=8;return 0;}
  const auto previous=Gdi::SelectObject(dc,bitmap);
  Gdi::SetBkColor(dc,0xffffff);Gdi::SetTextColor(dc,0);
  Gdi::RECT rect{0,0,width,height};
  if(mono)Gdi::PatBlt(dc,0,0,width,height,0x00ff0062);
  else{
   const auto brush=Gdi::CreateSolidBrush(Win98Frame::SystemColors()[padded||close?15:5]);
   Gdi::FillRect(dc,&rect,brush);Gdi::DeleteObject(brush);
  }
  if(padded){rect.top=metric(46);rect.bottom-=m.edge_y-metric(46);}
  if(combo){rect.left=-metric(45);rect.top=-metric(46);rect.right+=metric(45);rect.bottom+=metric(46);}
  Win98Oem::Cell strip[Win98UserResources::StripCount];
  if(checks)Win98Oem::Pack(m,300,strip); // USER: 300-pixel-wide source strip.
  auto cell=[&](unsigned at,Gdi::HDC target,Gdi::RECT box){
   // USER copies the source rectangle for aliases and never paints
   // their nominal state. This includes two three-state checkbox cells.
   int cx=0,cy=0;Win98Oem::Size(m,at,cx,cy);
   if(!cx&&cy>=0&&cy<int(Win98UserResources::StripCount))at=unsigned(cy);
   const auto *call=Win98UserResources::StripCalls[at];
   Gdi::POINT previous_origin{};
   if(checks)Gdi::SetBrushOrgEx(target,-strip[at].x,-strip[at].y,&previous_origin);
   const bool result=Win98Frame::DrawFrameControl(target,&box,call[0],combo?1:call[1],Win98Frame::ColorWindow+1,Win98Frame::ColorWindowFrame+1);
   if(checks)Gdi::SetBrushOrgEx(target,previous_origin.x,previous_origin.y);
   return result;};
  bool drawn=true;
  if(close){
   // USER creates its separate small logo from resource 105 at
   // SM_CYSMICON, then draws that image, not the shared large icon.
   const unsigned small=unsigned(metric(50));
   const unsigned logo=IR::Load(*this,SystemInstance(),{105,false},1,small,small,0);
   for(unsigned part=0;part<2;++part){
    const unsigned draw[]={dc,unsigned(m.edge_x+part*(width/2)),unsigned(metric(46)),logo,
       unsigned(width/2-2*m.edge_x),unsigned(m.caption_y-metric(46)),0,0,3};
    drawn=app.Api("DrawIconEx",draw)&&drawn;
   }
   if(logo)FreeImage(logo);
  }else if(checks){
   const int cw=width/4,ch=height/3;
   const unsigned make_cell[]={unsigned(cw),unsigned(ch),1,unsigned(Gdi::DisplayDepth()),0};
   const unsigned bits=app.GdiApi("CreateBitmap",make_cell);const auto temp=Gdi::CreateCompatibleDC();
   if(!bits||!temp)drawn=false;
   else{
    const auto old=Gdi::SelectObject(temp,bits);unsigned entry=index;
    for(unsigned row=0;row<3;++row){
     for(unsigned col=0;col<4;++col,++entry){
      const Gdi::RECT box{0,0,cw,ch};
      if(row==1){
       drawn=cell(0x47,temp,box)&&drawn;
       drawn=Gdi::BitBlt(dc,col*cw,row*ch,cw,ch,temp,0,0,0x008800c6)&&drawn;
      }
      drawn=cell(entry,temp,box)&&drawn;
      drawn=Gdi::BitBlt(dc,col*cw,row*ch,cw,ch,temp,0,0,row==1?0x00660046:0x00cc0020)&&drawn;
     }
     if(row!=1)++entry;
    }
    Gdi::SelectObject(temp,old);
   }
   if(temp)Gdi::DeleteDC(temp);if(bits)Gdi::DeleteObject(bits);
  }else drawn=cell(index,dc,rect);
  Gdi::SelectObject(dc,previous);Gdi::DeleteDC(dc);
  if(!drawn){Gdi::DeleteObject(bitmap);app.last_error=50;return 0;}
  return bitmap;
 }
 void BeforeDestroy(unsigned h)override{if(app.cursor==h)app.cursor=0;for(auto&t:app.threads)if(t.cursor_handle==h)t.cursor_handle=0;}
 unsigned FreeImage(unsigned h)override{auto*p=app.FindIcon(h);if(!p){app.last_error=6;return h;}delete p->data;p->data=nullptr;p->shared=false;p->generation=(p->generation%0x3ffff)+1;return 0;}
 void FixSystemImage(unsigned h)override{if(auto*p=app.FindIcon(h)){p->shared=true;if(startup_count<22)startup_images[startup_count++]=h;}else failed=true;}
 // USER keeps the loaded OEM cursors in its own table; the index is the one
 // InitializeSystemImages walks, so entry i is resource 100+i.
 void PublishSystemImage(bool icon,unsigned index,unsigned h)override{
  if(icon){if(!index)app.default_application_icon=h;return;}
  if(index<16)app.system_cursors[index]=h;
  if(!index&&!app.cursor){app.cursor=h;User::SetPointerShape(h);}
 }
 void CustomizeSystemImages(bool)override{} // Registry-supplied themes/ANI are not implemented.
};
bool Application::InitializeImageResources(){
 if(image_resources)return image_resources->ready||image_resources->initializing;
 void*p=malloc(sizeof(ImageResources));if(!p){last_error=8;return false;}image_resources=new(p)ImageResources(*this);auto&e=*image_resources;e.initializing=true;
 IR::InitializeSystemImages(e,false);IR::InitializeSystemImages(e,true);e.initializing=false;
 if(e.failed||e.startup_count!=22){unsigned handles[22];memcpy(handles,e.startup_images,sizeof(handles));FreeImageResources();for(unsigned h:handles)if(auto*i=FindIcon(h)){delete i->data;i->data=nullptr;i->generation=(i->generation%0x3ffff)+1;}cursor=0;last_error=8;return false;}e.ready=true;return true;
}
void Application::FreeImageResources(){if(!image_resources)return;auto&e=*image_resources;
 for(unsigned t=1;t<3;++t)while(e.Head(t))IR::SharedRemove(e,t,0);
 for(unsigned i=1;i<=16;++i)e.FreeResource(i);
 e.~ImageResources();free(image_resources);image_resources=nullptr;
}
unsigned Application::LookupResourceIcon(const unsigned char*data,unsigned size,bool icon,int cx,int cy,unsigned flags){
 if(!data||size<20||!Win98ImageDirectory::Read(data+4)||Win98ImageDirectory::Read(data+4)>(size-6)/14){last_error=87;return 0;}
 if(IW(flags)&0xf00){last_error=87;return 0;}
 const Win98ImageDirectory::Display d{IW(SystemMetric(11)),IW(SystemMetric(12)),IW(SystemMetric(13)),IW(SystemMetric(14)),0x41,32};
 return unsigned(int(int16_t(Win98ImageDirectory::Lookup(d,data,icon,IW(cx),IW(cy),IW(flags)))));
}
unsigned Application::CreateResourceIcon(const unsigned char*data,unsigned size,bool icon,unsigned version,int cx,int cy,unsigned flags){
 // Host resource spans have no guest address-space probe. A cursor staging span
 // supplies the four-byte over-probe used by USER; declared cb is unchanged.
 struct Probe {const unsigned char*p;unsigned n;const unsigned char*Data(){return p;}bool Readable(unsigned at,unsigned bytes){return p&&at<=n&&bytes<=n-at;}} probe{data,size+(icon?0:4)};
 if(!data||size<(icon?40u:44u)){last_error=87;return 0;}
 if((IW(flags)&0xf00)||!Win98ImageProbe::Icon(probe,size,icon,version)){last_error=87;return 0;}
 ImageResources e(*this);return IR::Create(e,{data,size},icon,version,IW(cx),IW(cy),IW(flags));
}

unsigned Application::ImageApi(ApiName name, const unsigned *a) {
  switch (name.id)
  {
  API_CASE("LoadBitmapA") API_CASE("LoadIconA") API_CASE("LoadCursorA"){
    const unsigned type=API_IS(name,"LoadBitmapA")?0:API_IS(name,"LoadIconA")?1:2;
    unsigned v[]={a[0],a[1],type,0,0,type?0x8040u:0u};return ImageApi("LoadImageA",v);
  }
  API_CASE("LoadCursorFromFileA"){unsigned v[]={0,a[0],2,0,0,0x50};return ImageApi("LoadImageA",v);}
  API_CASE("LoadImageA"){
    if(IW(a[2])>2||(IW(a[5])&0xf00)){last_error=87;return 0;}
    if(a[0]&&ModuleIndex(a[0])>=ModuleCapacity){last_error=6;return 0;}
    if(!LockLoader())return 0;struct Unlock{Application&a;~Unlock(){a.UnlockLoader();}}unlock{*this};
    if(!InitializeImageResources())return 0;
    return IR::Load(*image_resources,a[0],{a[1],a[1]>65535},IW(a[2]),IW(a[3]),IW(a[4]),IW(a[5]));
  }
  API_CASE("LookupIconIdFromDirectory") API_CASE("LookupIconIdFromDirectoryEx"){
    const bool extended=API_IS(name,"LookupIconIdFromDirectoryEx");unsigned char header[6];
    if(!virtual_memory.Access(a[0],6,false)||!Read(a[0],header,6)){last_error=87;return 0;}
    const unsigned count=Win98ImageDirectory::Read(header+4),size=6+count*14;
    if(!count||!virtual_memory.Access(a[0],size,false)){last_error=87;return 0;}
    auto*bytes=static_cast<unsigned char*>(malloc(size));if(!bytes){last_error=8;return 0;}
    unsigned result=0;if(Read(a[0],bytes,size))result=LookupResourceIcon(bytes,size,IW(a[1])!=0,extended?int(a[2]):0,extended?int(a[3]):0,extended?a[4]:0x40);free(bytes);return result;
  }
  API_CASE("CreateIconFromResource") API_CASE("CreateIconFromResourceEx"){
    const bool extended=API_IS(name,"CreateIconFromResourceEx");
    if(!a[1]||a[1]>16*1024*1024||!virtual_memory.Access(a[0],a[1],false)){last_error=87;return 0;}
    auto*bytes=static_cast<unsigned char*>(malloc(a[1]));if(!bytes){last_error=8;return 0;}
    unsigned result=0;if(Read(a[0],bytes,a[1])){
     struct GuestProbe {Application&app;const unsigned char*p;unsigned base;const unsigned char*Data(){return p;}bool Readable(unsigned at,unsigned n){return at<=~base&&app.virtual_memory.Access(base+at,n,false);}}probe{*this,bytes,a[0]};
     if(a[1]>=(IW(a[2])?40u:44u)&&Win98ImageProbe::Icon(probe,a[1],IW(a[2]),a[3]))result=CreateResourceIcon(bytes,a[1],IW(a[2])!=0,a[3],extended?int(a[4]):0,extended?int(a[5]):0,extended?a[6]:0x40);
    }free(bytes);return result;
  }
  // USER CreateIcon and CreateCursor build the same twelve
  // byte header {xHot, yHot, width, height, cbWidthBytes, planes, bitsPixel}
  // and hand it with the two planes to, which lays the object out as
  // header, AND plane, XOR plane and returns the global handle with bit 0 set
  // for an icon and cleared for a cursor. NEO98 keeps an icon as two 32-bit
  // rasters and a flag, so the planes are decoded here instead of copied.
  API_CASE("CreateIcon") API_CASE("CreateCursor"){
    const bool is_icon=API_IS(name,"CreateIcon");
    const int w=int(int(is_icon?a[1]:a[3])),h=int(int(is_icon?a[2]:a[4]));
    int hot_x=0,hot_y=0;unsigned planes=1,depth=1;
    if(is_icon){
      // The hot spot of an icon is its middle, rounded
      // toward zero, and the two depth arguments are single bytes.
      hot_x=w/2;hot_y=h/2;planes=a[3]&255;depth=a[4]&255;
    }else{
      // a hot spot outside the cursor answers zero, and
      // fixes the cursor at one plane of one bit.
      hot_x=int(a[1]);hot_y=int(a[2]);
      if(hot_x<0||hot_x>w||hot_y<0||hot_y>h)return 0;
    }
    if(w<=0||h<=0||w>256||h>256){last_error=87;return 0;}
    // The original copies the device dependent bits and lets the drawing code
    // read them. NEO98 has no device bitmap behind an icon, so it decodes the
    // formats a caller can name portably: the documented monochrome plane and
    // the two true colour widths of the native surface. Other formats fail with error 87.
    if(planes!=1||(depth!=1&&depth!=4&&depth!=24&&depth!=32)){last_error=87;return 0;}
    // ((bits + 15) & ~8) >> 3, the word aligned row.
    const unsigned uw=unsigned(w),uh=unsigned(h);
    const unsigned and_row=((uw+15)&~8u)>>3,xor_row=((uw*depth+15)&~8u)>>3;
    const unsigned and_size=and_row*uh,xor_size=xor_row*uh;
    const unsigned and_bits=is_icon?a[5]:a[5],xor_bits=is_icon?a[6]:a[6];
    if(and_bits&&!virtual_memory.Access(and_bits,and_size,false)){last_error=87;return 0;}
    if(xor_bits&&!virtual_memory.Access(xor_bits,xor_size,false)){last_error=87;return 0;}
    auto*and_plane=static_cast<unsigned char*>(calloc(and_size?and_size:1,1));
    auto*xor_plane=static_cast<unsigned char*>(calloc(xor_size?xor_size:1,1));
    if(!and_plane||!xor_plane){free(and_plane);free(xor_plane);last_error=8;return 0;}
    // and skip the copy for a null plane and leave the zeros.
    if((and_bits&&!Read(and_bits,and_plane,and_size))||(xor_bits&&!Read(xor_bits,xor_plane,xor_size))){
      free(and_plane);free(xor_plane);last_error=87;return 0;}
    auto*image=new WinImage::Image;
    if(image){image->width=uw;image->height=uh;image->monochrome=depth==1;
      image->color=static_cast<unsigned*>(calloc(uw*uh,4));
      image->mask=static_cast<unsigned*>(calloc(uw*uh,4));}
    if(!image||!image->color||!image->mask){delete image;free(and_plane);free(xor_plane);last_error=8;return 0;}
    // The planes of a device dependent bitmap run top down, unlike a DIB.
    for(unsigned y=0;y<uh;++y)for(unsigned x=0;x<uw;++x){
      const unsigned set=(and_plane[y*and_row+x/8]>>(7-(x&7)))&1;
      image->mask[y*uw+x]=set?0xffffffffu:0u; // all ones is transparent, as for a resource icon
      const unsigned char*row=xor_plane+y*xor_row;unsigned color=0;
      if(depth==1)color=((row[x/8]>>(7-(x&7)))&1)?0xffffffu:0u;
      else if(depth==4){
        const unsigned index=(x&1)?(row[x/2]&15):(row[x/2]>>4);
        // The sixteen colour device palette: GDI holds the eight dark
        // entries first and the eight bright ones last.
        const unsigned bgr=Win98GdiPalette::DefaultColors[index<8?index:index+4];
        color=((bgr&255)<<16)|(bgr&0xff00)|((bgr>>16)&255);
      }
      else if(depth==24)color=(unsigned(row[x*3+2])<<16)|(unsigned(row[x*3+1])<<8)|row[x*3];
      else color=(unsigned(row[x*4+2])<<16)|(unsigned(row[x*4+1])<<8)|row[x*4];
      image->color[y*uw+x]=color;
    }
    free(and_plane);free(xor_plane);
    const unsigned handle=NewIcon(image,0,false);
    if(handle){auto*entry=FindIcon(handle);entry->cursor=!is_icon;
      entry->hotspot_x=unsigned(hot_x);entry->hotspot_y=unsigned(hot_y);}
    return handle;
  }
  API_CASE("CreateIconIndirect"){
    // ICONINFO {fIcon, xHotspot, yHotspot, hbmMask, hbmColor}. The two GDI
    // bitmaps are read pixel by pixel into a native image: mask white is
    // transparent; without a colour bitmap the mask carries AND above XOR.
    unsigned info[5];
    if(!virtual_memory.Access(a[0],20,false)||!Read(a[0],info,20)){last_error=87;return 0;}
    const unsigned mask=info[3],color=info[4];
    unsigned mb[6]={},cb[6]={};
    if(!mask)return 0; // USER leaves LastError unchanged for a missing mask.
    const unsigned scratch=Allocate(24);if(!scratch){last_error=8;return 0;}
    auto bitmap_info=[&](unsigned bitmap,unsigned *out){const unsigned args[]={bitmap,24,scratch};
      return GdiApi("GetObjectA",args)==24&&Read(scratch,out,24);};
    const bool valid=bitmap_info(mask,mb)&&(!color||bitmap_info(color,cb));
    Free(scratch);if(!valid)return 0;
    const unsigned w=color?cb[1]:mb[1],h=color?cb[2]:mb[2]/2;
    if(!w||!h||w>256||h>256){last_error=87;return 0;}
    auto*image=new WinImage::Image;if(!image){last_error=8;return 0;}
    image->width=w;image->height=h;image->monochrome=!color;
    image->color=static_cast<unsigned*>(calloc(w*h,4));image->mask=static_cast<unsigned*>(calloc(w*h,4));
    if(!image->color||!image->mask){delete image;last_error=8;return 0;}
    const auto dc=Gdi::CreateCompatibleDC(0);if(!dc){delete image;last_error=8;return 0;}
    const auto keep=Gdi::SelectObject(dc,mask);
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)image->mask[y*w+x]=(Gdi::GetPixel(dc,(int)x,(int)y)&0xffffff)?0xffffffffu:0u; // same convention as resource icons: all ones = transparent
    if(color){
      Gdi::SelectObject(dc,color);
      for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){const unsigned c=Gdi::GetPixel(dc,(int)x,(int)y);image->color[y*w+x]=((c&0xff)<<16)|(c&0xff00)|((c>>16)&0xff);}
    }else for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)image->color[y*w+x]=(Gdi::GetPixel(dc,(int)x,(int)(y+h))&0xffffff)?0xffffffu:0u;
    Gdi::SelectObject(dc,keep);Gdi::DeleteDC(dc);
    const unsigned handle=NewIcon(image,0,false);
    if(handle){auto*icon=FindIcon(handle);icon->cursor=!info[0];icon->hotspot_x=info[0]?w/2:info[1];icon->hotspot_y=info[0]?h/2:info[2];}
    return handle;
  }
  API_CASE("CopyImage")return CopyImageApi(a);
  API_CASE("GetIconInfo")return IconInfoApi(a);
  API_CASE("DestroyIcon") API_CASE("DestroyCursor"){
    if(!a[0]){last_error=6;return 0;}
    ImageResources temporary(*this);return IR::Destroy(image_resources?*image_resources:temporary,a[0],4);
  }
  API_CASE("DrawIcon") API_CASE("DrawIconEx") {
    auto *icon = FindIcon(a[3]);
    if (!icon)
      return 0;
    unsigned flags = 3, cx = 32, cy = 32, background = 0;
    if (API_IS(name,"DrawIconEx")) {
      flags = a[8];
      cx = a[4] ? a[4] : icon->data->width;
      cy = a[5] ? a[5] : icon->data->height;
      background = a[7];
      if (a[6] || (flags & ~3u)) {
        Fail("Unsupported DrawIconEx animation/flags");
        return 0;
      }
    }
    WinImage::Image scaled;
    if (!scaled.Copy(*icon->data, cx, cy))
      return 0;
    if (background) {
      const unsigned select[] = {a[0], background};
      const unsigned old = GdiApi("SelectObject", select);
      if (!old) return 0;
      const unsigned fill[] = {a[0], a[1], a[2], cx, cy, Gdi::PATCOPY};
      const unsigned filled = GdiApi("PatBlt", fill);
      const unsigned restore[] = {a[0], old};
      GdiApi("SelectObject", restore);
      if (!filled) return 0;
    }
    unsigned recorded = 0;
    if (MetafileIcon(a[0], int(a[1]), int(a[2]), scaled, flags, recorded))
      return recorded;
    bool result = scaled.Draw(a[0], a[1], a[2], flags);
    if (result)
      User::DrawingChanged(a[0]);
    return result;
  }
  API_CASE("ImageList_Create") {
    if (!a[0] || !a[1] || a[0] > 256 || a[1] > 256 || a[3] > 128) {
      last_error = 87;
      return 0;
    }
    if (a[2] & ~0x13fu) {
      Fail("Unsupported image list flags");
      return 0;
    }
    auto *lists=(a[2]&0x100)?SharedImageLists():image_lists;
    for (unsigned i = 0; i < 16; ++i)
      if (!lists[i].used) {
        auto &list = lists[i];
        list.used = true;
        list.width = a[0];
        list.height = a[1];
        list.count = 0;
        list.background = ~0u;
        return ((a[2]&0x100)?0x60000000u:0x40000000u) | (list.generation << 8) | (i + 1);
      }
    last_error = 8;
    return 0;
  }
  default:
      break;
  }
  auto *list = FindImageList(a[0]);
  if (!list) {
    last_error = 6;
    return API_IS(name,"ImageList_ReplaceIcon") ? ~0u : 0;
  }
  switch (name.id)
  {
  API_CASE("ImageList_Destroy") {
    for (auto &item : list->items) {
      delete item;
      item = nullptr;
    }
    list->used = false;
    list->count = 0;
    list->generation = (list->generation % 0xfffff) + 1;
    return 1;
  }
  // Native provider operations used by the original Shell cache bootstrap.
  API_CASE("ImageList_GetIconSize"){
    if(a[1])Write32(a[1],list->width);if(a[2])Write32(a[2],list->height);return 1;
  }
  API_CASE("ImageList_Remove"){
    if(a[1]==~0u){for(auto &item:list->items){delete item;item=nullptr;}list->count=0;return 1;}
    if(a[1]>=list->count)return 0;delete list->items[a[1]];
    for(unsigned i=a[1]+1;i<list->count;++i)list->items[i-1]=list->items[i];
    list->items[--list->count]=nullptr;return 1;
  }
  API_CASE("ImageList_SetIconSize"){
    if(!a[1]||!a[2]||a[1]>256||a[2]>256){last_error=87;return 0;}
    for(auto &item:list->items){delete item;item=nullptr;}list->count=0;
    list->width=a[1];list->height=a[2];return 1;
  }
  API_CASE("ImageList_SetBkColor") {
    unsigned old = list->background;
    list->background = a[1];
    return old;
  }
  API_CASE("ImageList_GetImageCount")
    return list->count;
  API_CASE("ImageList_ReplaceIcon") {
    auto *icon = FindIcon(a[2]);
    unsigned index = a[1] == ~0u ? list->count : a[1];
    if (!icon || index >= 128 || (a[1] != ~0u && index >= list->count))
      return ~0u;
    auto *copy = new WinImage::Image;
    if (!copy){last_error=8;return ~0u;}
    if (!copy->Copy(*icon->data, list->width, list->height)) {
      delete copy;
      return ~0u;
    }
    delete list->items[index];
    list->items[index] = copy;
    if (index == list->count)
      ++list->count;
    return index;
  }
  API_CASE("ImageList_Draw") {
    if (a[1] >= list->count)
      return 0;
    if (a[5] & ~1u) {
      Fail("Unsupported image list drawing flags");
      return 0;
    }
    if (!(a[5] & 1) && list->background != ~0u) {
      auto brush = Gdi::CreateSolidBrush(list->background);
      Gdi::RECT r = {(int)a[3], (int)a[4], (int)(a[3] + list->width),
                     (int)(a[4] + list->height)};
      Gdi::FillRect(a[2], &r, brush);
      Gdi::DeleteObject(brush);
    }
    bool result = list->items[a[1]]->Draw(a[2], a[3], a[4]);
    if (result)
      User::DrawingChanged(a[2]);
    return result;
  }
  default:
      break;
  }
  Fail("Unsupported image API");
  return 0;
}

// USER32 ordinal 70 through USER.EXE. The validation, size and route
// decisions come from win98_image_copy.h; the rasteriser, the scaler and the
// GDI blit are native providers.
unsigned Application::CopyImageApi(const unsigned *a)
{
  const unsigned handle=a[0],type=IW(a[1]);
  unsigned flags=IW(a[4]);
  if(Win98ImageCopy::Validate(type,flags)){
    // and raise a 16-bit parameter error; the Win32 caller
    // sees a null result. NEO98 reports the ordinary invalid-parameter code.
    last_error=87;
    return 0;
  }
  // NEO98 draws cursors from the same 32-bit images as icons, so the display
  // always has colour cursors and never forces monochrome.
  flags=Win98ImageCopy::CursorFlags(type,flags,true);
  if(type==Win98ImageCopy::Bitmap)return CopyBitmapImage(handle,IW(a[2]),IW(a[3]),flags);
  auto *icon=FindIcon(handle);
  if(!icon){last_error=6;return 0;}                       // handle check
  const unsigned source_width=icon->data->width,source_height=icon->data->height;
  const unsigned metric_x[]={type==Win98ImageCopy::Icon?11u:13u},metric_y[]={type==Win98ImageCopy::Icon?12u:14u};
  const unsigned width=Win98ImageCopy::Extent(IW(a[2]),flags,Api("GetSystemMetrics",metric_x),source_width),
                 height=Win98ImageCopy::Extent(IW(a[3]),flags,Api("GetSystemMetrics",metric_y),source_height);
                 // Without a resource re-read the original helper returns
  // zero and the worker continues on the ordinary path, which is what happens
  // here: NEO98 has no second resource lookup for an existing image handle.
  const unsigned route_flags=flags&~unsigned(Win98ImageCopy::CopyFromResource);
  // Native icon images are always one plane of 32-bit colour.
  const auto route=Win98ImageCopy::Select(route_flags,width,height,source_width,source_height,1,32);
  if(route==Win98ImageCopy::ReturnOriginal)return handle;             //
  if(route_flags&(Win98ImageCopy::Monochrome|Win98ImageCopy::VgaColor)){
    Fail("Unsupported CopyImage monochrome or VGA conversion");
    return 0;
  }
  auto *copy=new WinImage::Image;
  if(!copy){last_error=8;return 0;}
  if(!copy->Copy(*icon->data,width,height)){delete copy;last_error=8;return 0;}
  const unsigned result=NewIcon(copy);
  if(result){
    auto *made=FindIcon(result);
    made->cursor=icon->cursor;made->hotspot_x=icon->hotspot_x;made->hotspot_y=icon->hotspot_y;
  }
  if(Win98ImageCopy::DeleteSource(flags,handle,result)){              //
    const unsigned destroy[]={handle};
    ImageApi("DestroyIcon",destroy);
  }
  return result;
}
// Builds a packed DIB for Gdi::CreateResourceBitmap out of one native icon
// image. The 16-bit worker at USER and hands 15de the bits of
// the locked icon resource; NEO98 keeps decoded pixels instead, so the bits are
// packed here. Rows go bottom up, which is what a positive DIB height means.
static unsigned char *pack_icon_dib(const WinImage::Image &image, bool mask, unsigned &bytes)
{
  const unsigned bpp = mask ? 1u : 24u, colors = mask ? 2u : 0u;
  const unsigned pitch = ((image.width * bpp + 31) / 32) * 4;
  const unsigned header = 40 + colors * 4;
  bytes = header + pitch * image.height;
  auto *dib = static_cast<unsigned char *>(calloc(bytes, 1));
  if (!dib)
    return nullptr;
  auto put32 = [&](unsigned at, unsigned v) {
    dib[at] = v; dib[at + 1] = v >> 8; dib[at + 2] = v >> 16; dib[at + 3] = v >> 24;
  };
  put32(0, 40);
  put32(4, image.width);
  put32(8, image.height);
  dib[12] = 1;                                   // one plane, as the original requires
  dib[14] = (unsigned char)bpp;
  put32(20, pitch * image.height);
  if (mask) { put32(44, 0x00ffffff); }           // index 0 black, index 1 white
  for (unsigned y = 0; y < image.height; ++y) {
    auto *row = dib + header + (image.height - 1 - y) * pitch;
    for (unsigned x = 0; x < image.width; ++x) {
      const unsigned value = mask ? image.mask[y * image.width + x]
                                  : image.color[y * image.width + x];
      if (mask) { if (value) row[x / 8] |= (unsigned char)(128 >> (x & 7)); }
      else { row[x * 3] = value; row[x * 3 + 1] = value >> 8; row[x * 3 + 2] = value >> 16; }
    }
  }
  return dib;
}
// USER32 image handling delegates to USER.
// src/win98_icon_info.h; the icon store and the two bitmaps are native.
unsigned Application::IconInfoApi(const unsigned *a)
{
  auto *icon = FindIcon(a[0]);
  if (!icon || !icon->data || !icon->data->color || !icon->data->mask) {
    last_error = 6;                              // reports 0x600b here
    return 0;
  }
  if (!a[1] || !virtual_memory.Access(a[1], 20, true)) {
    // A null output is a legal call: USER32 pushes the null through
    // and skips its own widening. The worker then writes nothing
    // and its own result still stands.
    if (a[1]) { last_error = 87; return 0; }
    return 1;
  }
  Win98IconInfo::Info info;
  const auto &image = *icon->data;
  const bool monochrome = image.monochrome;
  const unsigned mask_height = Win98IconInfo::MaskHeight(image.height, monochrome);
  const unsigned stride = ((image.width + 15) / 16) * 2;
  const unsigned bytes = stride * mask_height;
  auto *bits = static_cast<unsigned char *>(calloc(bytes, 1));
  if (!bits) { last_error = 8; return 0; }
  for (unsigned y = 0; y < mask_height; ++y)
    for (unsigned x = 0; x < image.width; ++x) {
      const unsigned pixel = y < image.height ? image.mask[y * image.width + x]
          : image.color[(y - image.height) * image.width + x];
      if (pixel) bits[y * stride + x / 8] |= 128 >> (x & 7);
    }
  const unsigned scratch = Allocate(bytes);
  if (scratch && Store(scratch, bits, bytes)) {
    const unsigned create[] = {image.width, mask_height, 1, 1, scratch};
    info.mask = GdiApi("CreateBitmap", create);
  }
  if (scratch) Free(scratch);
  free(bits);
  unsigned color_bytes = 0;
  if (!info.mask) {                              // output stays zero
    unsigned zero[5] = {};
    Store(a[1], zero, sizeof zero);
    return 0;
  }
  if (!monochrome) {
    auto *color_dib = pack_icon_dib(*icon->data, false, color_bytes);
    if (color_dib) {
      info.color = OwnGdi(CreateResourceBitmap(color_dib, color_bytes), 1);
      free(color_dib);
    }
    if (!info.color) {                           // deletes the mask again
      Gdi::DeleteObject(info.mask);
      unsigned zero[5] = {};
      Store(a[1], zero, sizeof zero);
      last_error = 8;
      return 0;
    }
  }
  info.icon = Win98IconInfo::IsIcon(icon->cursor ? 1u : 0u);
  info.hotspot_x = icon->hotspot_x;
  info.hotspot_y = icon->hotspot_y;
  // USER32 : the first three widen with sign, the handles without.
  const unsigned out[5] = {Win98IconInfo::SignExtend(info.icon & 0xffffu),
                           Win98IconInfo::SignExtend(info.hotspot_x & 0xffffu),
                           Win98IconInfo::SignExtend(info.hotspot_y & 0xffffu),
                           info.mask, info.color};
  Store(a[1], out, sizeof out);
  return 1;
}
bool Application::DrawResourceBitmap(unsigned target,const unsigned char *data,unsigned bytes,unsigned width,unsigned height,unsigned flags)
{
  if(bytes<12||bytes>0x4000000||!width||!height||width>32767||height>32767)return false;
  const unsigned header=Win98ImageProbe::Read32(data);
  if(header!=12&&(header<40||header>bytes))return false;
  const unsigned source_width=header==12?Win98ImageDirectory::Read(data+4):Win98ImageProbe::Read32(data+4);
  const int rows=header==12?int(Win98ImageDirectory::Read(data+6)):int(Win98ImageProbe::Read32(data+8));
  const unsigned source_height=rows<0?unsigned(-static_cast<long long>(rows)):unsigned(rows);
  const unsigned depth=Win98ImageDirectory::Read(data+(header==12?10:14));
  const unsigned compression=header==12?0:Win98ImageProbe::Read32(data+16);
  unsigned colors=header==12?0:Win98ImageProbe::Read32(data+32);
  if(!colors&&depth<=8)colors=1u<<depth;
  if(depth>8)colors=header==40&&compression==3?3:0;
  const unsigned long long table64=header+static_cast<unsigned long long>(colors)*(header==12?3:4);
  if(table64>bytes||!source_width||!source_height||source_width>32767||source_height>32767)return false;
  const unsigned table=unsigned(table64),scratch=Allocate(24);
  if(!scratch)return false;
  unsigned info[6]={};const unsigned query[]={target,24,scratch};
  const bool queried=GdiApi("GetObjectA",query)&&Read(scratch,info,sizeof info);
  Free(scratch);if(!queried)return false;
  const unsigned target_depth=(info[4]&65535)*(info[4]>>16);
  unsigned char *filtered=nullptr;
  unsigned filtered_bytes=0,draw_width=source_width,draw_height=source_height;
  // USER filters only 4/8/24-bit RGB input. A selected palettized
  // section bypasses the interpolator; 16/32-bit input uses GDI stretching.
  if((!info[5]||target_depth>8)&&header==40&&!compression&&rows>0&&
     (depth==4||depth==8||depth==24)&&(depth==24||colors==(1u<<depth))&&
     (width!=source_width||height!=source_height)&&width>1&&height>1&&
     source_width/2<=width&&source_height/2<=height){
    const unsigned long long input_bytes=static_cast<unsigned long long>((source_width*depth+31)/32*4)*source_height;
    const unsigned long long output_bytes=static_cast<unsigned long long>((width*24+31)/32*4)*height;
    if(input_bytes<=bytes-table&&output_bytes<=0x4000000-40){
      filtered_bytes=40+unsigned(output_bytes);
      filtered=static_cast<unsigned char*>(calloc(filtered_bytes,1));
      if(filtered){
        unsigned palette[256]={};if(depth!=24)memcpy(palette,data+40,colors*4);
        const unsigned resized[]={40,width,height,1|(24u<<16),0,unsigned(output_bytes),0,0,0,0};
        memcpy(filtered,resized,sizeof resized);
        if(Win98ImageResample::Scale(data+table,source_width,source_height,depth,palette,filtered+40,width,height)){
          data=filtered;bytes=filtered_bytes;draw_width=width;draw_height=height;
        }else{free(filtered);filtered=nullptr;}
      }
    }
  }
  const unsigned block=Allocate(bytes);
  if(!block){free(filtered);return false;}
  const bool stored=Store(block,data,bytes);
  const unsigned bits_offset=filtered?40:table;
  free(filtered);
  const unsigned none[]={0};const unsigned dc=stored?GdiApi("CreateCompatibleDC",none):0;
  bool drawn=false;
  if(dc){
    const unsigned pick[]={dc,target};const unsigned old=GdiApi("SelectObject",pick);
    const unsigned mode[]={dc,(flags&1)?1u:3u};GdiApi("SetStretchBltMode",mode);
    const unsigned draw[]={dc,0,0,width,height,0,0,draw_width,draw_height,block+bits_offset,block,0,0x00cc0020u};
    drawn=GdiApi("StretchDIBits",draw)!=0;
    const unsigned restore[]={dc,old};GdiApi("SelectObject",restore);
    const unsigned drop[]={dc};GdiApi("DeleteDC",drop);
  }
  Free(block);return drawn;
}
// USER.EXE. The bitmap worker has no LR_COPYRETURNORG branch, always
// builds a new bitmap and stamps the caller's instance into its owner field.
unsigned Application::CopyBitmapImage(unsigned handle,unsigned cx,unsigned cy,unsigned flags,bool copy_pixels)
{
  // USER extracts a packed DIB, rounding the source depth to
  // 1/4/8/24. The section branch keeps that DIB format and ignores
  // LR_MONOCHROME; the ordinary branch uses 's device/section choice.
  const unsigned scratch=Allocate(1100);
  if(!scratch){last_error=8;return 0;}
  unsigned info[6]={};const unsigned query[]={handle,24,scratch};
  if(!GdiApi("GetObjectA",query)||!Read(scratch,info,sizeof info)){Free(scratch);last_error=6;return 0;}
  const unsigned source_width=info[1],source_height=info[2];
  const unsigned width=cx?cx:source_width,height=cy?cy:source_height;
  unsigned planes=0,bits=0;
  Win98ImageCopy::BitmapFormat(flags,info[4]&65535,info[4]>>16,planes,bits);
  const unsigned depth=(info[4]&65535)*(info[4]>>16);
  const unsigned dib_depth=depth<=1?1:depth<=4?4:depth<=8?8:24;
  const unsigned colors=dib_depth<=8?1u<<dib_depth:0;
  const unsigned pitch=((source_width*dib_depth+31)/32)*4;
  const unsigned long long total=static_cast<unsigned long long>(pitch)*source_height;
  if(!width||!height||width>32767||height>32767||total>0x4000000){Free(scratch);last_error=87;return 0;}
  const unsigned data=Allocate(unsigned(total));
  if(!data){Free(scratch);last_error=8;return 0;}
  unsigned header[266]={};header[0]=40;header[1]=source_width;header[2]=source_height;
  header[3]=1|(dib_depth<<16);header[5]=unsigned(total);header[8]=colors;
  Store(scratch,header,sizeof header);
  const unsigned none[]={0};
  const unsigned dc=GdiApi("CreateCompatibleDC",none);
  unsigned result=0;
  if(dc){
    const unsigned extract[]={dc,handle,0,source_height,data,scratch,0};
    if(GdiApi("GetDIBits",extract)==source_height&&Read(scratch,header,sizeof header)){
      const bool explicit_section=(flags&0x2000)!=0;
      const unsigned target_depth=explicit_section?dib_depth:planes*bits;
      const bool section=explicit_section||(target_depth!=1&&target_depth!=unsigned(Gdi::DisplayDepth()));
      if(section){
        unsigned target[266];memcpy(target,header,sizeof target);
        target[1]=width;target[2]=explicit_section?height:unsigned(-int(height));
        target[3]=1|(target_depth<<16);target[5]=0;target[8]=target_depth<=8?1u<<target_depth:0;
        if(!explicit_section && target_depth==4)
          for(unsigned i=0;i<16;++i){
            const unsigned rgb=Win98GdiPalette::DefaultColors[i<8?i:i+4];
            target[10+i]=((rgb&255)<<16)|(rgb&0xff00)|((rgb>>16)&255);
          }
        Store(scratch,target,sizeof target);
        const unsigned make[]={dc,scratch,0,scratch+1064,0,0};
        result=GdiApi("CreateDIBSection",make);
        Store(scratch,header,sizeof header);
      }else{
        const unsigned make[]={width,height,planes,bits,0};result=GdiApi("CreateBitmap",make);
      }
      if(result&&copy_pixels){
        const unsigned pick[]={dc,result};const unsigned old=GdiApi("SelectObject",pick);
        const unsigned mode[]={dc,planes*bits==1?1u:3u};GdiApi("SetStretchBltMode",mode);
        unsigned filtered=0;
        unsigned draw_width=source_width,draw_height=source_height;
        // USER bypasses its USER32 interpolator for a selected DIB
        // with a colour table, unchanged sizes, or a reduction below half.
        // A one-pixel destination would divide by zero in USER32; keep the
        // ordinary GDI stretch for that boundary instead.
        if((!section||target_depth>8) && dib_depth!=1 &&
           (width!=source_width||height!=source_height) && width>1 && height>1 &&
           source_width/2<=width && source_height/2<=height){
          const unsigned long long bytes=static_cast<unsigned long long>((width*24+31)/32*4)*height;
          if(bytes<=0x4000000){
            auto *input=static_cast<unsigned char*>(malloc(unsigned(total)));
            auto *output=static_cast<unsigned char*>(calloc(unsigned(bytes),1));
            if(input&&output&&Read(data,input,unsigned(total))&&
               Win98ImageResample::Scale(input,source_width,source_height,dib_depth,header+10,output,width,height)){
              filtered=Allocate(unsigned(bytes));
              if(filtered){
                Store(filtered,output,unsigned(bytes));
                unsigned resized[10]={40,width,height,1|(24u<<16),0,unsigned(bytes),0,0,0,0};
                Store(scratch,resized,sizeof resized);draw_width=width;draw_height=height;
              }
            }
            free(input);free(output);
          }
        }
        const unsigned draw[]={dc,0,0,width,height,0,0,draw_width,draw_height,filtered?filtered:data,scratch,0,0x00cc0020u};
        const bool drawn=GdiApi("StretchDIBits",draw)!=0;
        if(filtered)Free(filtered);
        const unsigned restore[]={dc,old};GdiApi("SelectObject",restore);
        if(!drawn){const unsigned drop[]={result};GdiApi("DeleteObject",drop);result=0;}
      }
    }
    const unsigned drop[]={dc};GdiApi("DeleteDC",drop);
  }
  Free(data);Free(scratch);
  if(result){
    OwnGdi(result,1);
    if(Win98ImageCopy::DeleteSource(flags,handle,result)){
      const unsigned drop[]={handle};GdiApi("DeleteObject",drop);
    }
  }
  return result;
}
} // namespace X86
