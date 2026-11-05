// SPDX-License-Identifier: GPL-2.0-or-later
// New native storage and guest ABI around implemented GDI/DIBENG decisions.
namespace {
unsigned AllocatePen(const Win98PenLine::Pen &pen){
 const unsigned handle=Allocate(PenObject);if(handle)Find(handle,PenObject)->pen=pen;return handle;
}
bool DeletePenObject(unsigned handle){
 auto *object=Find(handle,PenObject);if(!object)return false;
 struct E{unsigned handle;void Diagnostic(){} void Free(){Gdi::Free(handle);}}e{handle};
 return Win98PenLine::Delete(e,object->references*4,object->private_default?0xcf47:0x4f47);
}
struct PenAllocation {unsigned AllocatePen(const Win98PenLine::Pen&p){return Gdi::AllocatePen(p);}};
// Wide strokes: the wide stroker. GDI
// routes effective widths above one to GDI's own stroker, which widens the
// polyline into polygons with the round caps and round joins of a CreatePen
// pen and fills them with the pen colour and ROP2. That stroker is not
// implemented. This provider fills the same shape directly: a pixel belongs
// to the stroke when its centre lies closer to a segment than half the width
// (for an even width the centre is the pixel's middle, so a vertical line of
// width 4 at x=30 covers 28..31 like the polygon's top-left rule; for an odd
// width it is the lattice point, so width 3 covers 29..31). Every pixel is
// written once, so an XOR ROP2 does not cancel at the joins.
bool DrawWidePolyline(Context &dc,const Win98PenLine::Point*points,unsigned count,int width,BitmapIO &io){
 auto *object=Find(dc.current.bitmap,BitmapObject);
 const auto *pen=Find(dc.current.pen,PenObject);
 unsigned foreground;
 if(!RealizeDeviceColor(*object,pen->pen.color,foreground,&dc.current)){io.Error(50);return false;}
 BitmapAccess access(object,true);auto &bitmap=object->bitmap;
 int left=points[0].x,top=points[0].y,right=left,bottom=top;
 for(unsigned i=1;i<count;++i){
  if(points[i].x<left)left=points[i].x;if(points[i].x>right)right=points[i].x;
  if(points[i].y<top)top=points[i].y;if(points[i].y>bottom)bottom=points[i].y;
 }
 left-=width;top-=width;right+=width+1;bottom+=width+1;
 const bool odd=width&1;const long long limit=odd?(long long)(width-1)*(width-1):(long long)width*width;
 for(unsigned c=0;c<dc.current.clips;++c){
  RECT clip;if(!Intersect(clip,ClipAt(dc.current,c),{0,0,bitmap.width,bitmap.height}))continue;
  const int x0=clip.left>left?clip.left:left,x1=clip.right<right?clip.right:right;
  const int y0=clip.top>top?clip.top:top,y1=clip.bottom<bottom?clip.bottom:bottom;
  for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x){
   // Doubled coordinates keep the half-pixel centre in integers.
   const long long sx=2ll*x+(odd?0:1),sy=2ll*y+(odd?0:1);
   bool inside=false;
   for(unsigned i=1;i<count&&!inside;++i){
    const long long ax=2ll*points[i-1].x,ay=2ll*points[i-1].y,bx=2ll*points[i].x,by=2ll*points[i].y;
    const long long dx=bx-ax,dy=by-ay,vx=sx-ax,vy=sy-ay,length=dx*dx+dy*dy,along=vx*dx+vy*dy;
    __int128 distance;
    if(!length||along<=0)distance=vx*vx+vy*vy;
    else if(along>=length){const long long wx=sx-bx,wy=sy-by;distance=wx*wx+wy*wy;}
    else{
     // |v|^2 - along^2/length, compared after multiplying by length.
     const __int128 scaled=(__int128)(vx*vx+vy*vy)*length-(__int128)along*along;
     inside=odd?scaled<=(__int128)limit*length:scaled<(__int128)limit*length;
     continue;
    }
    inside=odd?distance<=limit:distance<limit;
   }
   if(inside)DibEng::WritePixel(bitmap,x,y,Win98PenLine::Rop(dc.current.text_mode.rop2,foreground,DibEng::ReadPixel(bitmap,x,y)));
  }
 }
 return true;
}
bool DrawPenPolyline(Context &dc,const Win98PenLine::Point*points,unsigned count,BitmapIO &io){
 const auto *pen=Find(dc.current.pen,PenObject);const unsigned style=Win98PenLine::RealizedStyle(pen->pen);
 if(style==5)return true;
 if(pen->pen.style>6){io.Error(87);return false;}
 // GDI routes effective widths above one to its separate wide stroker
 // (the wide stroker above), after GDI realizes the logical width.
 const int width=Win98PenWidth::Device(pen->pen.width_x,dc.current.mapping.viewport_extent.x,
                                      dc.current.mapping.window_extent.x,dc.current.mapping.shifts.x);
 if(width>1||width<-1)return DrawWidePolyline(dc,points,count,width<0?-width:width,io);
 auto *object=Find(dc.current.bitmap,BitmapObject);
 // DIBENG's pen realization maps true-color COLORREF through the driver.

 unsigned foreground,background;
 if(!RealizeDeviceColor(*object,pen->pen.color,foreground,&dc.current)||
    !RealizeDeviceColor(*object,Pixel(dc.current.text_mode.background),background)){io.Error(50);return false;}
 BitmapAccess access(object,true);auto &bitmap=object->bitmap;
 struct PixelProvider {DibEng::Bitmap&bitmap;unsigned Read(int x,int y){return DibEng::ReadPixel(bitmap,x,y);}void Pixel(int x,int y,unsigned v){DibEng::WritePixel(bitmap,x,y,v);}} e{bitmap};
 for(unsigned i=0;i<dc.current.clips;++i){
  RECT clip;if(!Intersect(clip,ClipAt(dc.current,i),{0,0,bitmap.width,bitmap.height}))continue;
  if(!Win98PenLine::Raster(e,points,count,style,dc.current.text_mode.rop2,foreground,background,dc.current.text_mode.background_mode,{clip.left,clip.top,clip.right,clip.bottom}))return false;
 }
 return true;
}
Win98PenLine::Point PenDevicePoint(const State&s,unsigned x,unsigned y){
 const auto point=DevicePoint(s,Win98PenLine::Word(x),Win98PenLine::Word(y));
 return {Win98PenLine::Word(point.x),Win98PenLine::Word(point.y)};
}
}
unsigned CreatePen(unsigned style,int width,COLORREF color){PenAllocation provider;return Win98PenLine::CreateDirect(provider,style,unsigned(width),color);}
bool HasPen(unsigned handle){return Find(handle,PenObject);}
unsigned PenGetObject(unsigned handle,unsigned count,unsigned output,BitmapIO&io){
 auto *object=Find(handle,PenObject);if(!object){io.Error(87);return 0;}
 struct Memory{BitmapIO&io;bool Write8(unsigned p,unsigned v){unsigned char byte=v;return io.Write(p,&byte,1);}void Error(unsigned v){io.Error(v);}} memory{io};
 return Win98PenLine::Query(memory,object->pen,count,output);
}
bool IsPenApi(ApiName name){switch(name.id){API_CASE("CreatePen") API_CASE("CreatePenIndirect") API_CASE("ExtCreatePen") API_CASE("MoveToEx") API_CASE("GetCurrentPositionEx") API_CASE("LineTo") API_CASE("Polyline") API_CASE("SetROP2") API_CASE("GetROP2") return true;default:return false;}}
unsigned PenApi(ApiName name,const unsigned*a,BitmapIO&io){
 PenAllocation provider;
 // The Win32 wrapper accepts PS_USERSTYLE/PS_ALTERNATE as solid cosmetic
 // pens; GDI's Win16 CreateDirect entry still rejects styles above 6.
 if(API_IS(name,"CreatePen")){return CreatePen(a[0]==7||a[0]==8?0:a[0],int(a[1]),a[2]);}
 if(API_IS(name,"CreatePenIndirect")){
  unsigned data[4];if(!a[0]||!io.Read(a[0],data,sizeof data)){io.Error(87);return 0;}
  return Win98PenLine::Create(provider,{data[0],data[1],data[2],data[3],0});
 }
 if(API_IS(name,"ExtCreatePen")){
  unsigned brush[3];if(!a[2]||!io.Read(a[2],brush,sizeof brush)){io.Error(87);return 0;}
  return Win98PenLine::CreateExtended(provider,a[0],a[1],brush[0],brush[1]);
 }
 auto *dc=DC(a[0]);if(!dc){io.Error(87);return 0;}
 switch (name.id)
 {
 API_CASE("GetROP2")return dc->current.text_mode.rop2;
 API_CASE("SetROP2")return Win98PenLine::SetRop(dc->current.text_mode.rop2,a[1]);
 API_CASE("GetCurrentPositionEx"){
  const int values[]={dc->current.position.x,dc->current.position.y};if(!a[1]||!io.Write(a[1],values,sizeof values)){io.Error(87);return 0;}return 1;
 }
 API_CASE("MoveToEx"){
  if(a[3]&&(!io.Probe(a[3],true)||!io.Probe(a[3]+7,true))){io.Error(87);return 0;}
  const int before[]={dc->current.position.x,dc->current.position.y};
  Win98PenLine::Move(dc->current.position,{int(a[1]),int(a[2])});
  if(a[3]&&!io.Write(a[3],before,sizeof before))return 0;return 1;
 }
 API_CASE("LineTo"){
  struct LineProvider {Context&dc;BitmapIO&io;void Draw(const Win98PenLine::Point*p,unsigned){Win98PenLine::Point device[]={PenDevicePoint(dc.current,p[0].x,p[0].y),PenDevicePoint(dc.current,p[1].x,p[1].y)};DrawPenPolyline(dc,device,2,io);}} line{*dc,io};
  return Win98PenLine::Line(line,dc->current.position,{int(a[1]),int(a[2])});
 }
 API_CASE("Polyline"){
  const unsigned count=a[2]&65535;if(count<2||(count&0xc000)){io.Error(87);return 0;}
  Win98PenLine::Point local[40];DibTemporary temporary;Win98PenLine::Point *points=local;
  if(count>40){if(!temporary.Allocate(count*sizeof *points)){io.Error(8);return 0;}points=reinterpret_cast<Win98PenLine::Point*>(temporary.data);}
  for(unsigned i=0;i<count;++i){unsigned pair[2];if(!io.Read(a[1]+i*8,pair,sizeof pair)){io.Error(87);return 0;}points[i]=PenDevicePoint(dc->current,pair[0],pair[1]);}
  return DrawPenPolyline(*dc,points,count,io);
 }
 default:
     break;
 }
 return 0;
}
