// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_toolbar.h"
#include <cstdio>
#include <cstring>
#include <cassert>
using U=unsigned;static unsigned char memory[65536];static U config,events[1024],event_count,next_text;
static U(*outside_read)(U)=nullptr;static void(*outside_write)(U,U)=nullptr;
struct E{
 unsigned char temporary[1024];E(){memset(temporary,0xa5,sizeof temporary);}
 U temporary_used=0;struct Scratch{E&e;U address,bytes;Scratch(E&x,U n):e(x),address(0x20000000+x.temporary_used),bytes(n){assert(x.temporary_used+n<=1024);x.temporary_used+=n;}~Scratch(){e.temporary_used-=bytes;}};
 void Event(U a,U b=0,U c=0,U d=0,U f=0){assert(event_count+5<=1024);events[event_count++]=a;events[event_count++]=b;events[event_count++]=c;events[event_count++]=d;events[event_count++]=f;}

 static constexpr U Base=0x100000;
 U Read8(U p){if(p>=0x20000000&&p<0x20000400)return temporary[p-0x20000000];if(p>=Base&&p<Base+sizeof memory)return memory[p-Base];assert(outside_read);return outside_read(p);}
 U Read16(U p){return Read8(p)|(Read8(p+1)<<8);}U Read32(U p){return Read16(p)|(Read16(p+2)<<16);}
 void Write8(U p,U v){if(p>=0x20000000&&p<0x20000400)temporary[p-0x20000000]=v;else if(p>=Base&&p<Base+sizeof memory)memory[p-Base]=v;else {assert(outside_write);outside_write(p,v);}}void Write16(U p,U v){Write8(p,v);Write8(p+1,v>>8);}void Write32(U p,U v){Write16(p,v);Write16(p+2,v>>16);}
 void Invalidate(U h,U rect,U erase){Event(10,h,rect,erase);}
 void Layout(U toolbar){Event(11,toolbar);}
 void Accessibility(U event,U h,U object,U child){Event(12,event,h,object,child);}
 void InvalidateButton(U toolbar,U button,U erase){Event(13,toolbar,button,erase);}
 U ImageList(U toolbar,U index,U kind){U result=(config&4)?0:0x3344;Event(14,toolbar,index,kind,result);return result;}
 U ImageCount(U list){Event(15,list,6);return 6;}
 void Tooltip(U tool,U message,U info){Event(18,tool,message,Read32(info+8),Read32(info+12));if(message==0x435)Write32(info+4,0x11223344);}
 void CopyText(U out,U text,U count){Event(17,out,text,count);if(!count)return;U i=0;while(i+1<count&&Read16(text+i*2)){Write16(out+i*2,Read16(text+i*2));++i;}Write16(out+i*2,0);}
 U SetString(U field,U text){Event(16,field,text,Read32(field));if(config&2)return 0;U destination=0x10c000+next_text;next_text+=128;if(text){U i=0;do{U value=Read16(text+i);Write16(destination+i,value);i+=2;if(!value)break;}while(i<128);Write32(field,destination);}else Write32(field,0);return 1;}
 void ConvertAtoW(U source,U out,U count){Event(32,source,count);for(U i=0;i<count;++i){U c=Read8(source+i);Write16(out+i*2,c);if(!c)break;}}
 void ConvertWtoA(U source,U out,U count){Event(33,source,count);for(U i=0;i<count;++i){U c=Read16(source+i*2);Write8(out+i,c<256?c:63);if(!c)break;}}
 U BorderY(){return 2;}U SeparatorWidth(){return 8;}
 U AnsiToWide(U module,U text){Event(20,module,text);if(config&8)return 0;U out=0x10b000;for(U i=0;i<64;++i){U c=Read8(text+i);Write16(out+i*2,c);if(!c)break;}return out;}
 void FreeLocal(U p){Event(21,p);}
 void Wrap(U toolbar){Event(22,toolbar);}
 U CancelCapture(U toolbar){U result=!(config&16);Event(23,toolbar,result);return result;}
 void Notify(U toolbar,U message,U note){Event(24,toolbar,message,Read32(note+12));for(U i=0;i<20;i+=4)events[event_count++]=Read32(note+16+i);}
 U Parent(U window){Event(25,window,0x9876);return 0x9876;}
 void ClientRect(U window,U rect){Event(26,window);Write32(rect,3);Write32(rect+4,5);Write32(rect+8,303);Write32(rect+12,105);}
 void WrapWidth(U toolbar,U width,U rect,U flags){Event(27,toolbar,width,flags);for(U i=0;i<16;i+=4)events[event_count++]=Read32(rect+i);}
 void WrapRows(U toolbar,U rows,U rect,U flags){Event(28,toolbar,rows,flags);for(U i=0;i<16;i+=4)events[event_count++]=Read32(rect+i);}
 U AllocateLocal(U flags,U bytes){U next=(config&1)||bytes>4096?0:0x108000;Event(29,flags,bytes,next);if(next)memset(memory+(next-Base),0,bytes);return next;}
 U ReallocateLocal(U old,U bytes,U flags){U next=(config&1)||bytes>4096?0:0x108000;Event(30,old,bytes,flags,next);if(next)memmove(memory+(next-Base),memory+(old-Base),bytes);return next;}
 U Resize(U old,U bytes){U next=(config&1)||bytes>4096?0:0x108000;events[event_count++]=old;events[event_count++]=bytes;events[event_count++]=next;if(next&&old)memmove(memory+(next-Base),memory+(old-Base),bytes);return next;}
};
extern "C" void toolbar_reset(const unsigned char*p,U c){memcpy(memory,p,sizeof memory);config=c;event_count=next_text=0;}
extern "C" const unsigned char*toolbar_memory(){return memory;}
extern "C" U toolbar_events(U*out){memcpy(out,events,event_count*4);return event_count;}
extern "C" U toolbar_run(U op,const U*a){E e;using namespace Win98Toolbar;switch(op){case 0:return CommandIndex(e,a[0],a[1]);case 1:return ToPublic(e,a[0],a[1],a[2]);case 2:return FromPublic(e,a[0],a[1],a[2]);case 3:return Reserve(e,a[0],a[1]);case 4:return GetInfo(e,a[0],a[1],a[2]);case 5:return SetInfo(e,a[0],a[1],a[2]);case 6:return Insert(e,a[0],a[1],a[2],a[3],a[4]);case 7:return Delete(e,a[0],a[1]);case 8:return Message(e,a[0],a[1],a[2],a[3]);case 9:return GetInfoA(e,a[0],a[1],a[2]);case 10:return SetInfoA(e,a[0],a[1],a[2]);}assert(false);return 0;}
extern "C" void toolbar_callbacks(U(*read)(U),void(*write)(U,U)){outside_read=read;outside_write=write;}
extern "C" U toolbar_provider(U op,const U*a){E e;switch(op){case 10:e.Invalidate(a[0],a[1],a[2]);return 0;case 11:e.Layout(a[0]);return 0;case 12:e.Accessibility(a[0],a[1],a[2],a[3]);return 0;case 13:e.InvalidateButton(a[0],a[1],a[2]);return 0;case 14:return e.ImageList(a[0],a[1],a[2]);case 15:return e.ImageCount(a[0]);case 16:return e.SetString(a[0],a[1]);case 17:e.CopyText(a[0],a[1],a[2]);return 0;case 18:e.Tooltip(a[0],a[1],a[2]);return 0;case 19:return e.Resize(a[0],a[1]);case 20:return e.AnsiToWide(a[0],a[1]);case 21:e.FreeLocal(a[0]);return 0;case 22:e.Wrap(a[0]);return 0;case 23:return e.CancelCapture(a[0]);case 24:e.Notify(a[0],a[1],a[2]);return 0;case 25:return e.Parent(a[0]);case 26:e.ClientRect(a[0],a[1]);return 0;case 27:e.WrapWidth(a[0],a[1],a[2],a[3]);return 0;case 28:e.WrapRows(a[0],a[1],a[2],a[3]);return 0;case 29:return e.AllocateLocal(a[0],a[1]);case 30:return e.ReallocateLocal(a[0],a[1],a[2]);case 32:e.ConvertAtoW(a[0],a[1],a[2]);return 1;case 33:e.ConvertWtoA(a[0],a[1],a[2]);return 1;}assert(false);return 0;}
#ifdef TOOLBAR_STANDALONE
U word(FILE*f){U n;assert(fread(&n,4,1,f)==1);return n;}
void delta(FILE*f,unsigned char*m){for(U n=word(f);n;--n){U p=word(f),bytes=word(f);assert(p<=65536&&bytes<=65536-p);assert(fread(m+p,1,bytes,f)==bytes);}}
int main(int argc,char**argv){assert(argc==2);FILE*f=fopen(argv[1],"rb");assert(f);U total=word(f);unsigned char before[65536],after[65536];for(U n=0;n<total;++n){U op=word(f),args[5],failure=word(f),expected=word(f);assert(fread(args,4,5,f)==5);memset(before,0xa5,sizeof before);delta(f,before);memcpy(after,before,sizeof after);delta(f,after);U count=word(f),want[1024],got[1024];assert(count<=1024&&fread(want,4,count,f)==count);toolbar_reset(before,failure);U actual=toolbar_run(op,args);assert(actual==expected);assert(!memcmp(after,memory,sizeof after));assert(toolbar_events(got)==count&&!memcmp(want,got,count*4));}fclose(f);printf("TOOLBAR CORE ASAN/UBSAN PASS: %u cases\n",total);}
#endif
