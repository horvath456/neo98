// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_toolbar_input.h"
#include <cassert>
#include <cstring>
using U=unsigned;static unsigned char arena[65536],scratch[4096];static U count,events[8192],scratch_top;static U(*reader)(U);static void(*writer)(U,U);
struct E{
 struct Scratch{U address,old;Scratch(E&,U n):old(scratch_top){address=0x20000000+scratch_top;scratch_top+=n;assert(scratch_top<=sizeof scratch);}~Scratch(){scratch_top=old;}};
 U Read8(U p){if(p>=0x100000&&p<0x110000)return arena[p-0x100000];if(p>=0x20000000&&p<0x20001000)return scratch[p-0x20000000];assert(reader);return reader(p);}
 U Read32(U p){U v=0;for(U i=0;i<4;++i)v|=Read8(p+i)<<(i*8);return v;}
 void Write8(U p,U v){if(p>=0x100000&&p<0x110000)arena[p-0x100000]=v;else if(p>=0x20000000&&p<0x20001000)scratch[p-0x20000000]=v;else{assert(writer);writer(p,v);}}
 void Write32(U p,U v){for(U i=0;i<4;++i)Write8(p+i,v>>(i*8));}
 void Event(U a,U b=0,U c=0,U d=0){assert(count+4<=8192);events[count++]=a;events[count++]=b;events[count++]=c;events[count++]=d;}
 U Hit(U t,int x,int y){Event(1,t,x,y);return Read32(0x10f000);}
 U Capture(){Event(2);return Read32(0x10f004);}
 void SetCapture(U h){Event(3,h);Write32(0x10f004,h);}
 void ReleaseCapture(){Event(4);Write32(0x10f004,0);}
 U Alive(U h){Event(5,h);return Read32(0x10f008);}
 U Focus(){Event(6);return Read32(0x10f00c);}
 void InvalidateButton(U t,U b,U erase){Event(7,t,b,erase);}
 void Update(U h){Event(8,h);}
 void Accessibility(U code,U h,U object,U child){Event(9,code,h,object);Event(10,child);}
 void Cursor(U h,U out){Event(11,h);Write32(out,12);Write32(out+4,13);}
 void Relay(U t,U m,U k,U p){Event(12,t,m,k);Event(13,p);}
 void TrackLeave(U h,bool cancel){Event(14,h,cancel);}
 void Unpress(U t,U command){Event(15,t,command);U i=Win98Toolbar::CommandIndex(*this,t,command);if(i!=~0u){U b=Win98ToolbarInput::Button(*this,t,i);Write8(b+8,Read8(b+8)&~2u);}}
 void Command(U parent,U command,U h){Event(16,parent,command,h);if(Read32(0x10f010)==0x111)Write32(0x10f008,0);}
 U Notify(U t,U code,U p){Write32(p,Read32(t));Write32(p+4,77);Write32(p+8,code);Event(17,t,code);U n=code==0xfffffd37?24:(code==~1u||code==~19u)?32:code==~15u?12:60;for(U i=0;i<n;i+=4)Event(18,Read32(p+i));if(Read32(0x10f010)==code)Write32(0x10f008,0);return Read32(0x10f014)==code;}
 U ButtonNotify(U t,U id,U code){Scratch s(*this,60);Win98ToolbarInput::Zero(*this,s.address,60);Write32(s.address+12,id);return Notify(t,code,s.address);}
 void Invalidate(U h,U p,U erase){Event(20,h,p,erase);}
 void ChangeStyle(U t,U style){Event(21,t,style);}
 void Layout(U t){Event(22,t);}
 void Reflow(U t){Event(23,t);}
 void Dropdown(U,U,U){assert(false);}bool AdjustRequested(U,U){Event(24,0x12);return false;}void Adjust(U,U){assert(false);}
};
extern "C" void input_reset(const unsigned char*p){memcpy(arena,p,sizeof arena);count=scratch_top=0;}
extern "C" unsigned char*input_memory(){return arena;}
extern "C" U input_events(U*out){memcpy(out,events,count*4);return count;}
extern "C" void input_callbacks(U(*r)(U),void(*w)(U,U)){reader=r;writer=w;}
extern "C" void input_run(U op,const U*a){E e;using namespace Win98ToolbarInput;switch(op){case 0:Move(e,a[0],a[1],a[2],a[3],a[4]);break;case 1:Down(e,a[0],a[1],a[2],a[3],a[4]);break;case 2:Up(e,a[0],a[1],a[2],a[3],a[4]);break;case 3:Hot(e,a[0],a[1],a[2]);break;case 4:Release(e,a[0]);break;case 5:Group(e,a[0],a[1]);break;case 6:Press(e,a[0],a[1],a[2]);break;case 7:Extended(e,a[0],a[1],a[2]);break;}}
extern "C" U input_provider(U op,const U*a){E e;switch(op){case 1:return e.Hit(a[0],int(a[1]),int(a[2]));case 2:return e.Capture();case 3:e.SetCapture(a[0]);return 0;case 4:e.ReleaseCapture();return 1;case 5:return e.Alive(a[0]);case 6:return e.Focus();case 7:e.InvalidateButton(a[0],a[1],a[2]);return 0;case 8:e.Update(a[0]);return 1;case 9:e.Accessibility(a[0],a[1],a[2],a[3]);return 0;case 11:e.Cursor(a[0],a[1]);return 1;case 12:e.Relay(a[0],a[1],a[2],a[3]);return 0;case 14:e.TrackLeave(e.Read32(a[0]+8),e.Read32(a[0]+4)&0x80000000u);return 1;case 16:if(a[1]==0x111)e.Command(a[0],a[2],a[3]);else {assert(a[1]==0x403);e.Unpress(0x100100,a[2]);}return 0;case 17:return e.Notify(a[0],a[1],a[2]);case 19:return e.ButtonNotify(a[0],a[1],a[2]);case 20:e.Invalidate(a[0],a[1],a[2]);return 1;case 21:e.ChangeStyle(a[0],a[1]);return 0;case 22:e.Layout(a[0]);return 0;case 23:e.Reflow(a[0]);return 0;case 24:e.Event(24,a[0]);return 0;}assert(false);return 0;}
