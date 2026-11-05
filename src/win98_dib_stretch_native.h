// SPDX-License-Identifier: GPL-2.0-or-later
// Included by gdi.cpp after the native DIB provider. Native storage/driver seam.
namespace {
bool ScaleDib(Context &context,DibFormat &format,const DibEng::Bitmap &source,
              RECT destination,RECT source_area,unsigned rop,bool mirror_x=false,bool mirror_y=false,unsigned *failure=nullptr){
 if(failure)*failure=50;
 // GDI9 normalizes HALFTONE to COLORONCOLOR before its software fallback.
 switch(Win98DibStretchObjects::SoftwareMode(context.current.stretch_mode)){case 1:case 2:case 3:break;default:return false;}
 auto *object=Find(context.current.bitmap,BitmapObject);auto &target=object->bitmap;
 if(source.depth!=target.depth||(source.depth!=16&&source.depth!=32&&source.depth>8)||rop!=SRCCOPY)return false;
 if(source.depth==16&&format.rgb565!=bool(object->dib&&object->dib->format.rgb565))return false;
 const int width=destination.right-destination.left,height=destination.bottom-destination.top;
 if(width<=0||height<=0)return true;
 if(width>32767||height>32767)return false;
 DibTemporary samples;if(!samples.Allocate(unsigned(width+height)*sizeof(int))){if(failure)*failure=8;return false;}
 struct Provider {
  const DibEng::Bitmap &source;DibEng::Bitmap &target;int *indices;unsigned capacity;bool mirror_x,mirror_y;
  int *AllocateSamples(unsigned n){return n<=capacity?indices:nullptr;}void FreeSamples(int*){}
  void Pixel(int x,int y,int sx,int sy){
   if(mirror_x)sx=source.width-1-sx;if(mirror_y)sy=source.height-1-sy;
   DibEng::WritePixel(target,x,y,DibEng::ReadPixel(source,sx,sy));
  }
 } e{source,target,reinterpret_cast<int*>(samples.data),unsigned(width+height),mirror_x,mirror_y};
 if(mirror_x)source_area={source.width-source_area.right,source_area.top,source.width-source_area.left,source_area.bottom};
 if(mirror_y)source_area={source_area.left,source.height-source_area.bottom,source_area.right,source.height-source_area.top};
 for(unsigned i=0;i<context.current.clips;++i){
  RECT clip;if(!Intersect(clip,ClipAt(context.current,i),{0,0,target.width,target.height}))continue;
  Win98DibStretch::Request r{source.width,source.height,
   {source_area.left,source_area.top,source_area.right,source_area.bottom},
   {destination.left,destination.top,destination.right,destination.bottom},
   {clip.left,clip.top,clip.right,clip.bottom}};
  if(!Win98DibStretch::Raster(e,r))return false;
 }
 return true;
}
// GDI and 0FE4..119E: last source index assigned to
// each destination pixel. Shrink accumulates raw pixels in these bins;
// COLORONCOLOR keeps the last. Vertical ties use <=, horizontal ties <.
void GdiStretchSamples(int source,int destination,bool horizontal,int *indices){
 int error=(source>destination?source:destination)-((source<destination?source:destination)/2);
 if(source>=destination){
  int out=0;
  for(int i=0;i<source;++i){
   error-=destination;
   if(horizontal?error<0:error<=0){if(out<destination)indices[out++]=i;error+=source;}
  }
  if(out<destination)indices[out]=source-1;
 }else{
  int sample=0;
  for(int i=0;i<destination;++i){
   indices[i]=sample;error-=source;
   if(horizontal?error<0:error<=0){error+=destination;++sample;}
  }
 }
}
// StretchDIBits between formats the direct path above does not pair, or with
// another raster operation. The DIB fallback combines source pixels
// before colour conversion for BLACKONWHITE/WHITEONBLACK reductions. The
// DIBENG fast paths keep their sample-only behavior (native stretch_modes).
// An RLE image's coverage is stretched along, so skipped runs stay unpainted.
bool StretchThroughSource(Context &context,DibFormat &format,const DibEng::Bitmap &source,RECT destination,
                          RECT source_area,unsigned rop,const DibEng::Bitmap *coverage,unsigned *failure,
                          bool source_is_dib){
 if(failure)*failure=50;
 switch(Win98DibStretchObjects::SoftwareMode(context.current.stretch_mode)){case 1:case 2:case 3:break;default:return false;}
 const int width=destination.right-destination.left,height=destination.bottom-destination.top;
 if(width<=0||height<=0)return true;
 if(width>32767||height>32767)return false;
 const unsigned stride=((unsigned(width)*unsigned(source.depth)+31)/32)*4;
 const unsigned mask_stride=coverage?((unsigned(width)*unsigned(coverage->depth)+31)/32)*4:0;
 DibTemporary pixels,mask,samples;
 if(!pixels.Allocate(stride*unsigned(height))||(coverage&&!mask.Allocate(mask_stride*unsigned(height)))||
    !samples.Allocate(unsigned(width+height)*sizeof(int))){if(failure)*failure=8;return false;}
 memset(pixels.data,0,stride*unsigned(height));
 if(coverage)memset(mask.data,0,mask_stride*unsigned(height));
 DibEng::Bitmap scaled{pixels.data,width,height,int(stride),source.depth};
 DibEng::Bitmap scaled_mask{coverage?mask.data:nullptr,width,height,int(mask_stride),coverage?coverage->depth:1};
 const auto *target=Find(context.current.bitmap,BitmapObject);
 const unsigned requested_mode=Win98DibStretchObjects::SoftwareMode(context.current.stretch_mode);
 const bool generic=source_is_dib&&rop==SRCCOPY&&
   !(source.depth==8&&(target->bitmap.depth==8||target->bitmap.depth==16));
 const unsigned reduce_mode=generic?requested_mode:3;
 const int sw=source_area.right-source_area.left,sh=source_area.bottom-source_area.top;
 // GDI copies the common rectangle and repeats its final
 // row/column when both extents differ by at most one. No reduction occurs.
 const bool near_copy=generic&&sw>=width-1&&sw<=width+1&&sh>=height-1&&sh<=height+1;
 struct Provider {
  const DibEng::Bitmap &source,*coverage;DibEng::Bitmap &scaled,&mask;int *indices;unsigned capacity;
  RECT area;int width,height;unsigned mode;
  int *AllocateSamples(unsigned n){return n<=capacity?indices:nullptr;}void FreeSamples(int*){}
  void Pixel(int x,int y,int sx,int sy){
   const int sw=area.right-area.left,sh=area.bottom-area.top;
   if(mode==3||(sw<=width&&sh<=height)){
    DibEng::WritePixel(scaled,x,y,DibEng::ReadPixel(source,sx,sy));
    if(coverage)DibEng::WritePixel(mask,x,y,DibEng::ReadPixel(*coverage,sx,sy));
    return;
   }
   int left=sw>width?area.left+(x?indices[x-1]+1:0):sx;
   int right=sx+1;
   int top=sh>height?area.top+(y?indices[width+y-1]+1:0):sy;
   int bottom=sy+1;
   if(left<0)left=0;if(top<0)top=0;
   if(right>source.width)right=source.width;if(bottom>source.height)bottom=source.height;
   unsigned value=mode==1?~0u:0u;bool covered=false;
   for(int row=top;row<bottom;++row)for(int column=left;column<right;++column){
    if(coverage&&!DibEng::ReadPixel(*coverage,column,row))continue;
    const unsigned pixel=DibEng::ReadPixel(source,column,row);
    if(mode==1)value&=pixel;else value|=pixel;
    covered=true;
   }
   if(covered)DibEng::WritePixel(scaled,x,y,value);
   if(coverage)DibEng::WritePixel(mask,x,y,covered);
  }
 } e{source,coverage,scaled,scaled_mask,reinterpret_cast<int*>(samples.data),unsigned(width+height),source_area,width,height,reduce_mode};
 Win98DibStretch::Request r{source.width,source.height,
  {source_area.left,source_area.top,source_area.right,source_area.bottom},
  {0,0,width,height},{0,0,width,height}};
 if(near_copy){
  for(int y=0;y<height;++y)for(int x=0;x<width;++x){
   const int sx=source_area.left+(x<sw?x:sw-1),sy=source_area.top+(y<sh?y:sh-1);
   if(sx<0||sy<0||sx>=source.width||sy>=source.height)continue;
   DibEng::WritePixel(scaled,x,y,DibEng::ReadPixel(source,sx,sy));
   if(coverage)DibEng::WritePixel(scaled_mask,x,y,DibEng::ReadPixel(*coverage,sx,sy));
  }
 }else if(generic&&sw>0&&sh>0){
  int *columns=reinterpret_cast<int*>(samples.data),*rows=columns+width;
  GdiStretchSamples(sw,width,true,columns);GdiStretchSamples(sh,height,false,rows);
  for(int y=0;y<height;++y)for(int x=0;x<width;++x){
   const int sx=source_area.left+columns[x],sy=source_area.top+rows[y];
   if(sx<0||sy<0||sx>=source.width||sy>=source.height)continue;
   e.Pixel(x,y,sx,sy);
  }
 }else if(!Win98DibStretch::Raster(e,r))return false;
 return BlitDib(context,format,scaled,destination,0,0,rop,nullptr,coverage?&scaled_mask:nullptr,source_is_dib);
}
unsigned CompatibleBitmap(DibIO &io,unsigned dc,unsigned width,unsigned height,bool discard){
 auto *context=DC(dc);if(!context){io.Error(87);return 0;}
 auto *selected=Find(context->current.bitmap,BitmapObject);
 struct Provider {
  enum : unsigned { DcToken=0xff000000,HeaderToken=0xfff00000,OutputToken=0xfff01000 };
  DibIO &io;unsigned dc;Gdi::Object &selected;DibFormat header;
  Provider(DibIO &i,unsigned d,Gdi::Object &o):io(i),dc(d),selected(o){if(o.dib)header=o.dib->format;}
  unsigned DcObject(unsigned){return DcToken;}
  // DC+9Dh, the bits per pixel of the DC's device. A display DC has those of
  // the display mode, whatever surface the port draws it on: a window surface
  // is 32 bits even on a 256-colour desktop, and a program measures the mode
  // with a compatible bitmap. A memory DC has those of its bitmap.
  unsigned Read8(unsigned p){if(p==DcToken+15)return selected.dib&&!selected.dib->colours_only?0x80:0;if(p==DcToken+0x9c)return 1;if(p==DcToken+0x9d)return DisplayLikeDC(dc)?unsigned(DisplayDepth()):InformationDC(dc)?32:selected.bitmap.depth;return 0;}
  unsigned Read16(unsigned p){return bitmap_store.Read16(p);}
  unsigned Read32(unsigned p){return header.Read32(0x100+(p-HeaderToken));}
  void Write32(unsigned p,unsigned value){header.Write32(0x100+(p-HeaderToken),value);}
  unsigned DibHeader(unsigned){return HeaderToken;}
  unsigned Object(unsigned h){auto *o=Find(h,BitmapObject);return o?bitmap_store.Object(o->original_bitmap):0;}
  bool MakeDiscardable(unsigned){return false;} // Native storage stays resident; original ignores a failed GMEM_MODIFY.
  unsigned AllocateBitmap(const Win98Bitmap::Bitmap &b,unsigned,unsigned){
   const unsigned a[]={b.width,b.height,b.planes,b.bpp,0};return BitmapApi("CreateBitmap",a,io);
  }
  unsigned CreateSection(unsigned,unsigned){
   struct Clone final:DibIO {
    Provider &e;explicit Clone(Provider &p):e(p){}
    bool Read(unsigned p,void*out,unsigned n)override{if(p<HeaderToken||p-HeaderToken>sizeof e.header.info||n>sizeof e.header.info-(p-HeaderToken))return false;memcpy(out,e.header.info+(p-HeaderToken),n);return true;}
    bool Write(unsigned p,const void*,unsigned n)override{return p==OutputToken&&n==4;}
    bool Probe(unsigned p,bool write)override{return write?p==OutputToken:p>=HeaderToken&&p-HeaderToken<sizeof e.header.info;}
    void Error(unsigned code)override{e.io.Error(code);}void Fault(const char*s)override{e.io.Fault(s);}
    bool AllocateDib(unsigned n,DibLease&lease)override{return e.io.AllocateDib(n,lease);}
   } clone(*this);
   const unsigned a[]={dc,HeaderToken,0,OutputToken,0,0};return DibApi("CreateDIBSection",a,clone);
  }
 } e{io,dc,*selected};
 const unsigned handle=Win98DibStretchObjects::Compatible(e,1,width,height,discard);
 // On a palettised display a device bitmap holds indices into the system
 // palette. The port's DDB has no table of its own, so it gets the one the
 // display has now; without it neither a colour nor a blit onto a 32-bit
 // window surface could resolve its pixels.
 if(handle&&DisplayLikeDC(dc)&&DisplayDepth()<=8)AttachDisplayColors(handle);
 return handle;
}
bool StretchStorageOverlap(const DibEng::Bitmap &a,const DibEng::Bitmap &b){
 using Address=__UINTPTR_TYPE__;
 const long long ad=static_cast<long long>(a.height-1)*a.stride,bd=static_cast<long long>(b.height-1)*b.stride;
 const Address ab=reinterpret_cast<Address>(a.bits)+(ad<0?ad:0),bb=reinterpret_cast<Address>(b.bits)+(bd<0?bd:0);
 const Address ae=ab+Address(ad<0?-ad:ad)+Address(a.width)*(a.depth/8),be=bb+Address(bd<0?-bd:bd)+Address(b.width)*(b.depth/8);
 return ab<be&&bb<ae;
}
unsigned StretchBitmap(DibIO &io,const unsigned *a){
 auto *destination=DC(a[0]),*source_dc=DC(a[5]);
 if(!destination||!source_dc){io.Error(87);return 0;}
 auto *src_object=Find(source_dc->current.bitmap,BitmapObject),*dst_object=Find(destination->current.bitmap,BitmapObject);
 // Original GDI transforms endpoints before normalizing mirror rectangles.
 const auto dp=DevicePoint(destination->current,int(a[1]),int(a[2]));
 const auto de=DevicePoint(destination->current,Win98DcMap::S(a[1]+a[3]),Win98DcMap::S(a[2]+a[4]));
 const auto sp=DevicePoint(source_dc->current,int(a[6]),int(a[7]));
 const auto se=DevicePoint(source_dc->current,Win98DcMap::S(a[6]+a[8]),Win98DcMap::S(a[7]+a[9]));
 Win98DibStretchObjects::Rect dr{dp.x,dp.y,de.x,de.y},sr{sp.x,sp.y,se.x,se.y};
 unsigned flags=Win98DibStretchObjects::Normalize(sr,0);flags=Win98DibStretchObjects::Normalize(dr,flags);
 if(dr.right<=dr.left||dr.bottom<=dr.top||sr.right<=sr.left||sr.bottom<=sr.top)return 1;
 if(!flags&&dr.right-dr.left==sr.right-sr.left&&dr.bottom-dr.top==sr.bottom-sr.top)
  return BitBlt(a[0],int(a[1]),int(a[2]),int(a[3]),int(a[4]),a[5],int(a[6]),int(a[7]),a[10]);
 // A stretch that is not a copy inside one direct-colour format: a DIB section
 // onto the palettized display goes through the DC's palette (GDI, the
 // same hand-over as BitBlt), a copy between two bitmaps of one depth keeps its
 // pixels, and any other pair converts after sampling (StretchThroughSource).
 const bool source_section=src_object->dib&&!src_object->dib->colours_only;
 const bool same_depth=src_object->bitmap.depth==dst_object->bitmap.depth;
 const bool device_target=dst_object->bitmap.depth<=8&&dst_object->dib&&dst_object->dib->colours_only&&PaletteDisplay();
 const int depth=src_object->bitmap.depth;
 const bool direct=a[10]==SRCCOPY&&same_depth&&!(source_section&&device_target)&&(depth==16||depth==32||depth<=8);
 // Every other pair takes the converting path: a palettized side, a 24-bit
 // bitmap (the picture a Delphi program decodes from a JPEG and draws with
 // StretchDraw), two direct-colour depths or another operation. Only
 // a mirrored stretch on that path stays refused.
 if(!direct){
  if(flags){io.Error(50);return 0;}
  BitmapAccess source_access(src_object,false),destination_access(dst_object,true);
  DibFormat format;
  if(src_object->dib)format=src_object->dib->format;
  else{
   format.depth=unsigned(src_object->bitmap.depth);format.width=unsigned(src_object->bitmap.width);format.height=unsigned(src_object->bitmap.height);
   // A monochrome bitmap expands through the text and background colours
   // of the destination, as in every blit.
   if(format.depth==1){format.colors=2;format.Write32(0x100+format.palette,destination->current.text_mode.foreground);
    format.Write32(0x104+format.palette,destination->current.text_mode.background);}
  }
  DibTemporary snapshot;DibEng::Bitmap source=src_object->bitmap;
  if(StretchStorageOverlap(source,dst_object->bitmap)){
   const unsigned stride=((unsigned(source.width)*unsigned(source.depth)+31)/32)*4;
   if(!snapshot.Allocate(stride*unsigned(source.height))){io.Error(8);return 0;}
   DibEng::Bitmap copy{snapshot.data,source.width,source.height,int(stride),source.depth};
   for(int row=0;row<source.height;++row)for(int col=0;col<source.width;++col)DibEng::WritePixel(copy,col,row,DibEng::ReadPixel(source,col,row));
   source=copy;
  }
  unsigned failure=50;
  if(!StretchThroughSource(*destination,format,source,{dr.left,dr.top,dr.right,dr.bottom},{sr.left,sr.top,sr.right,sr.bottom},
                           a[10],nullptr,&failure,source_section)){io.Error(failure);return 0;}
  return 1;
 }
 BitmapAccess source_access(src_object,false),destination_access(dst_object,true);
 DibEng::Bitmap source=src_object->bitmap;DibTemporary snapshot;
 // Snapshot also covers distinct DCs that refer to the same bitmap storage.
 if(StretchStorageOverlap(source,dst_object->bitmap)){
  const unsigned stride=unsigned(source.width)*unsigned(source.depth)/8;
  if(!snapshot.Allocate(stride*unsigned(source.height))){io.Error(8);return 0;}
  for(int row=0;row<source.height;++row)memcpy(snapshot.data+row*stride,source.bits+DibEng::Offset(row)*source.stride,stride);
  source={snapshot.data,source.width,source.height,int(stride),source.depth};
 }
 DibFormat format;if(src_object->dib)format=src_object->dib->format;
 unsigned failure=50;
 if(!ScaleDib(*destination,format,source,{dr.left,dr.top,dr.right,dr.bottom},{sr.left,sr.top,sr.right,sr.bottom},a[10],flags&8,flags&4,&failure)){io.Error(failure);return 0;}
 return 1;
}
}
bool IsStretchApi(ApiName name){switch(name.id){API_CASE("StretchBlt") API_CASE("GetStretchBltMode") API_CASE("SetStretchBltMode") API_CASE("CreateCompatibleBitmap") API_CASE("CreateDiscardableBitmap") return true;default:return false;}}
unsigned StretchApi(ApiName name,const unsigned*a,DibIO &io){
 if(API_IS(name,"CreateCompatibleBitmap")||API_IS(name,"CreateDiscardableBitmap"))return CompatibleBitmap(io,a[0],a[1],a[2],name[6]=='D');
 if(API_IS(name,"StretchBlt"))return StretchBitmap(io,a);
 auto *context=DC(a[0]);if(!context){io.Error(87);return 0;}
 if(API_IS(name,"GetStretchBltMode"))return context->current.stretch_mode;
 if(API_IS(name,"SetStretchBltMode"))return Win98DibStretchObjects::SetMode(context->current.stretch_mode,a[1]);
 return 0;
}
