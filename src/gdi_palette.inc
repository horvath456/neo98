// SPDX-License-Identifier: GPL-2.0-or-later
// Native guest-memory/DC providers. Displays at eight bits or below have
// RC_PALETTE. GDI realizes the palette (src/win98_gdi_realize.h); USER sends
// WM_PALETTECHANGED when realization changes the system palette.
unsigned last_realize_changed=0;
unsigned LastRealizeChanged(){return last_realize_changed;}
// GDI: the palette of the DC is the one of the foreground.
bool IsDCCurrentPalette(HDC h){auto*c=DC(h);return c&&PaletteDisplay()&&c->current.palette==system_palette.foreground;}
unsigned DefaultPalette(){if(!default_palette){default_palette=NewPalette(20,Win98GdiPalette::DefaultColors,true);MapDefaultPalette();}return default_palette;}
// GDI expands original WORD count to DWORD, including partial copies.
unsigned PaletteGetObject(unsigned h,unsigned count,unsigned output,BitmapIO&io){auto*p=Find(h,PaletteObject);if(!p)return 0;if(!output)return 4;unsigned bytes=count<4?count:4;if(!bytes){io.Error(87);return 0;}unsigned value=p->palette.count;return io.Write(output,&value,bytes)?bytes:0;}
bool HasPalette(unsigned h){return Find(h,PaletteObject)!=nullptr;}
bool IsPaletteApi(ApiName n){switch(n.id){API_CASE("CreatePalette") API_CASE("CreateHalftonePalette") API_CASE("GetPaletteEntries") API_CASE("SetPaletteEntries") API_CASE("ResizePalette") API_CASE("GetNearestPaletteIndex") API_CASE("SelectPalette") API_CASE("RealizePalette") API_CASE("AnimatePalette") API_CASE("GetSystemPaletteEntries") API_CASE("GetSystemPaletteUse") API_CASE("SetSystemPaletteUse") return true;default:return false;}}
unsigned PaletteApi(ApiName n,const unsigned*a,BitmapIO&io){
 switch (n.id)
 {
 API_CASE("CreatePalette"){
  unsigned short header[2];if(!a[0]||!io.Read(a[0],header,4))return 0;
  if(!Win98GdiPalette::CreateValid(header[0],header[1]))return 0;
  auto*colors=new unsigned[header[1]];if(!colors)return 0;
  bool ok=io.Read(a[0]+4,colors,header[1]*4);unsigned h=0;
  if(ok){for(unsigned i=0;i<header[1];++i)if(colors[i]&0x02000000){colors[i]=Win98GdiPalette::Normalize(colors[i]);unsigned char green=0;if(!io.Write(a[0]+5+i*4,&green,1)){ok=false;break;}}
   if(ok)h=NewPalette(header[1],colors);}
  delete[]colors;return h;
 }
 API_CASE("CreateHalftonePalette"){
  // A DC without RC_PALETTE uses the default palette.
  if(a[0]&&(!DC(a[0])||DC(a[0])->device_info||!PaletteDisplay()))return DC(a[0])?DefaultPalette():0;
  unsigned h=NewPalette(256,Win98GdiPalette::HalftoneColors);return h?h:DefaultPalette();
 }
 API_CASE("SelectPalette"){
  auto*c=DC(a[0]);auto*p=Find(a[1],PaletteObject);if(!c||!p)return 0;
  unsigned previous=c->current.palette;
  Retain(a[1],PaletteObject,a[0]);Release(previous,PaletteObject);c->current.palette=a[1];
  // Without bForceBackground, select the foreground palette. USER checks
  // whether the window may do this.
  if(PaletteDisplay()&&!c->device_info)Win98GdiRealize::Select(system_palette,a[1],a[2]!=0);
  return previous;
 }
 // Palette operations require RC_PALETTE. Indexed display modes provide a
 // device palette shared by all non-printer DCs, including memory DCs. Other
 // devices return before changing palette state.
 API_CASE("RealizePalette"){
  auto*c=DC(a[0]);last_realize_changed=0;if(!c||c->device_info||!PaletteDisplay())return 0;
  auto*p=Find(c->current.palette,PaletteObject);
  if(!p||!p->palette.entries)return 0;
  // Realize the palette and send the whole table to the driver if the system
  // palette changed.
  const auto result=Win98GdiRealize::RealizePalette(system_palette,c->current.palette,p->palette,
      c->current.palette==default_palette,io.ExpectedVersion()>=0x400);
  last_realize_changed=result.changed;
  if(result.changed)ProgramDisplay(0,system_palette.size);
  return result.mapped;
 }
 API_CASE("GetSystemPaletteUse") API_CASE("SetSystemPaletteUse"){
  auto*c=DC(a[0]);if(!c)return 0;
  if(c->device_info||display_depth>8)return 0;
  if(API_IS(n,"GetSystemPaletteUse"))return system_palette_use;
  // SYSPAL_NOSTATIC releases all but black and white; other values select
  // SYSPAL_STATIC and restore fixed colors. Send the full resulting table to
  // the driver.
  const unsigned previous=Win98GdiRealize::SetUse(system_palette,a[1]&65535,system_palette_use);
  ProgramDisplay(0,system_palette.size);
  return previous;
 }
 // The system palette is the device's current realized color table. Without
 // an indexed device table, return the fixed halftone table.
 API_CASE("GetSystemPaletteEntries"){
  auto*c=DC(a[0]);if(!c)return 0;if(!a[3])return Find(c->current.palette,PaletteObject)->palette.count;
  if(!c->device_info&&display_depth<=8){
   const unsigned start=a[1]&65535;
   unsigned count=Win98GdiPalette::Range(1u<<unsigned(display_depth),start,a[2]&65535);
   if(!count)return 0;
   auto*entries=new unsigned[count];
   for(unsigned i=0;i<count;++i){
    const unsigned value=display_colors[start+i];
    entries[i]=((value>>16)&255)|(value&0xff00)|((value&255)<<16);
   }
   const bool ok=io.Write(a[3],entries,count*4);delete[]entries;return ok?count:0;
  }
  unsigned count=Win98GdiPalette::Range(256,a[1]&65535,a[2]&65535);
  return count&&io.Write(a[3],Win98GdiPalette::HalftoneColors+(a[1]&65535),count*4)?count:0;
 }
 default:
     break;
 }
 auto*object=Find(a[0],PaletteObject);if(!object)return 0;auto&p=object->palette;
 if(API_IS(n,"ResizePalette")){PaletteStorage storage;return Win98GdiPalette::Resize(storage,p,a[1]&65535);}
 if(API_IS(n,"GetNearestPaletteIndex"))return Win98GdiPalette::Nearest(p,a[1]);
 unsigned start=a[1]&65535,count=a[2]&65535;
 if(API_IS(n,"GetPaletteEntries")){
  if(!a[3])return p.count;count=Win98GdiPalette::Range(p.count,start,count);
  if(!count)return 0;auto*colors=new unsigned[count];if(!colors)return 0;
  Win98GdiPalette::Get(p,start,count,colors);bool ok=io.Write(a[3],colors,count*4);delete[]colors;return ok?count:0;
 }
 bool animate=API_IS(n,"AnimatePalette");
 if(p.stock)return animate?1:0;
 count=Win98GdiPalette::Range(p.count,start,count);if(!count)return animate?1:0;
 auto*colors=new unsigned[count];if(!colors)return 0;if(!a[3]||!io.Read(a[3],colors,count*4)){delete[]colors;return 0;}
 if(animate){
  // Update PC_RESERVED entries and their reserved system slots, then send the
  // changed slot range to the driver.
  unsigned low=0,high=0;
  if(PaletteDisplay()&&object!=Find(default_palette,PaletteObject)){
   if(Win98GdiRealize::Animate(system_palette,p,start,count,colors,low,high))ProgramDisplay(low,high-low+1);
  }else Win98GdiPalette::Animate(p,start,count,colors);
  delete[]colors;return 1;
 }
 // SMapLS maps the caller's pages; original PC_EXPLICIT normalization is observable.
 for(unsigned i=0;i<count;++i)if(colors[i]&0x02000000){unsigned char green=0;if(!io.Write(a[3]+i*4+1,&green,1)){delete[]colors;return 0;}}
 unsigned result=Win98GdiPalette::Set(p,start,count,colors);delete[]colors;return result;
}
