// SPDX-License-Identifier: GPL-2.0-or-later
// Native bitmap/DC/clip providers around Windows 98-compatible pixel decisions.
namespace {
unsigned PixelAccess(unsigned op,HDC h,int x,int y,unsigned color,BitmapIO*io){
 auto*c=DC(h);if(!c)return op==2?0:~0u;
 if(op!=3&&!Win98DcInformation::DrawingAllowed(c->signature))return op==2?0:~0u;
 auto*object=Find(c->current.bitmap,BitmapObject);if(!object||(!DibEng::Valid(object->bitmap)&&!(op==3&&InformationDC(h))))return op==2?0:~0u;
 if(op!=0){unsigned resolved;if(!ResolveLogicalColor(c->current,color,resolved)){if(io)io->Error(50);return op==2?0:~0u;}}
 if(object->bitmap.depth&&object->bitmap.depth<=8&&object->bitmap.depth!=1&&!object->dib){if(io)io->Error(50);return op==2?0:~0u;}
 BitmapAccess access(object,op==1||op==2);
 struct Provider{
  Context&c;Object&o;
  void Update(){}unsigned Record(unsigned){return ~0u;}void Dirty(Win98GdiPixel::Point){}
  Win98GdiPixel::Point Map(Win98GdiPixel::Point p){auto r=DevicePoint(c.current,p.x,p.y);return {short(r.x),short(r.y)};}
  bool Clip(Win98GdiPixel::Point p){auto&b=o.bitmap;if(p.x<0||p.y<0||p.x>=b.width||p.y>=b.height)return false;for(unsigned i=0;i<c.current.clips;++i){auto r=ClipAt(c.current,i);if(p.x>=r.left&&p.y>=r.top&&p.x<r.right&&p.y<r.bottom)return true;}return false;}
  unsigned Tagged(unsigned color,unsigned&){unsigned resolved=0;ResolveLogicalColor(c.current,color,resolved);return resolved;}
  unsigned ForwardColor(unsigned v){return v;}unsigned ReverseColor(unsigned v){return v;}
  unsigned ManagedColor(unsigned v,unsigned&p){return ColorInfo(v,p,true);}
  unsigned PrepareDevice(){return 1;}void RestoreDevice(){}
  unsigned Count(){return o.dib?o.dib->format.colors:2;}
  unsigned Palette(unsigned i){if(o.dib)return o.dib->format.Read32(0x100+o.dib->format.palette+i*4);return i?0xffffff:0;}
  unsigned Read32(unsigned address){return Palette(address/4);}
  unsigned ColorInfo(unsigned value,unsigned&physical,bool write){return Win98GdiPixel::DriverColor(*this,!Win98DcInformation::DrawingAllowed(c.signature)?32:o.bitmap.depth,o.dib&&o.dib->format.rgb565,value,physical,write,!o.dib&&o.bitmap.depth==1);}
  unsigned SystemColor(unsigned){return 0;}unsigned PaletteColor(unsigned){return 0;}bool LogicalColor(unsigned,unsigned&){return false;}
  unsigned Read(unsigned x,unsigned y){return DibEng::ReadPixel(o.bitmap,int(x),int(y));}
  void Write(unsigned x,unsigned y,unsigned v){DibEng::WritePixel(o.bitmap,int(x),int(y),v);}
  void Begin(Win98GdiPixel::Point){}void End(){}
  unsigned Pixel(Win98GdiPixel::Point p,unsigned value,bool write){return Win98GdiPixel::DriverPixel(*this,o.bitmap.width,o.bitmap.height,o.bitmap.depth,4,p,value,c.current.text_mode.rop2,write);}
 }e{*c,*object};Win98GdiPixel::State state;state.signature=c->signature;Win98GdiPixel::Point p{x,y};
 if(!op)return Win98GdiPixel::Get(e,state,p);
 if(op==3)return Win98GdiPixel::Nearest(e,state,color);
 unsigned result=Win98GdiPixel::Set(e,state,p,color);return op==2?Win98GdiPixel::SetV(result):result;
}
}
bool IsPixelApi(ApiName name){switch(name.id){API_CASE("GetPixel") API_CASE("SetPixel") API_CASE("SetPixelV") API_CASE("GetNearestColor") return true;default:return false;}}
unsigned PixelApi(ApiName name,const unsigned*a,BitmapIO&io){unsigned op=API_IS(name,"GetPixel")?0:API_IS(name,"SetPixel")?1:API_IS(name,"SetPixelV")?2:3;return PixelAccess(op,a[0],op==3?0:int(a[1]),op==3?0:int(a[2]),op==0?0:op==3?a[1]:a[3],&io);}
