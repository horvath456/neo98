// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_menu.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
using namespace Win98Menu;
struct WI {U type,state,id,reserved,submenu,checked,unchecked,text,data,length,x,y,width,height,l22,l24,l26,bitmap,bw,bh;};
struct WM {U alive,next,flags,handle,task,width,height,count,help,capacity,maxheight,data,brush,first,last,scroll,status,window;WI items[24];};
struct WT {U active;int selected,keyboard;U flags,window,first_window;int first_index;U second_window;int second_index;};
struct Wire {WM menus[4];WT tracking[4];U head,next_text,allocations,fail_at,modern,badwrite,refreshes,freed,enabled;unsigned char text[4096];};
struct InfoWire {U size,mask,type,state,id,submenu,checked,unchecked,data,text,length,bitmap;};
struct Env:Environment {
 Wire&w;Menu menus[4]{};Item items[4][24]{};Tracking tracks[4]{};
 Env(Wire&v):w(v){for(U n=0;n<4;++n){WM&a=w.menus[n];Menu&m=menus[n];m.next=a.next;m.flags=a.flags;m.handle=a.handle;m.task=a.task;m.width=a.width;m.height=a.height;m.count=a.count;m.help=a.help;m.capacity=a.capacity;m.max_height=a.maxheight;m.data=a.data;m.brush=a.brush;m.first_visible=a.first;m.last_visible=a.last;m.scroll=a.scroll;m.status=a.status;m.window=a.window;m.items=items[n];
  for(U j=0;j<24;++j){WI&x=a.items[j];Item&i=items[n][j];i.type=x.type;i.state=x.state;i.id=x.id;i.reserved=x.reserved;i.submenu=x.submenu;i.checked=x.checked;i.unchecked=x.unchecked;i.text=x.text;i.data=x.data;i.length=x.length;i.x=x.x;i.y=x.y;i.width=x.width;i.height=x.height;i.layout22=x.l22;i.layout24=x.l24;i.layout26=x.l26;i.bitmap=x.bitmap;i.bitmap_width=x.bw;i.bitmap_height=x.bh;}
  WT&t=w.tracking[n];tracks[n]={S(t.selected),S(t.keyboard),W(t.flags),W(t.window),W(t.first_window),S(t.first_index),W(t.second_window),S(t.second_index)};
 }}
 ~Env(){for(U n=0;n<4;++n){WM&a=w.menus[n];Menu&m=menus[n];a.next=m.next;a.flags=m.flags;a.handle=m.handle;a.task=m.task;a.width=m.width;a.height=m.height;a.count=m.count;a.help=m.help;a.capacity=m.capacity;a.maxheight=m.max_height;a.data=m.data;a.brush=m.brush;a.first=m.first_visible;a.last=m.last_visible;a.scroll=m.scroll;a.status=m.status;a.window=m.window;
  for(U j=0;j<24;++j){WI&x=a.items[j];Item&i=items[n][j];x={i.type,i.state,i.id,i.reserved,i.submenu,i.checked,i.unchecked,U(i.text),i.data,i.length,i.x,i.y,i.width,i.height,i.layout22,i.layout24,i.layout26,i.bitmap,i.bitmap_width,i.bitmap_height};}
  Tracking&t=tracks[n];w.tracking[n]={w.tracking[n].active,t.selected,t.keyboard,t.flags,t.window,t.first_window,t.first_index,t.second_window,t.second_index};
 }}
 Menu*Find(U h)override{return h>=0x80&&h<=0x8c&&!(h&3)&&w.menus[(h-0x80)/4].alive?&menus[(h-0x80)/4]:nullptr;}
 bool fail(){return ++w.allocations==w.fail_at;}
 U AllocateMenu()override{if(fail())return 0;for(U n=0;n<4;++n)if(!w.menus[n].alive){w.menus[n].alive=1;menus[n]=Menu{};menus[n].items=items[n];menus[n].handle=W(0x80+n*4);return menus[n].handle;}return 0;}
 void FreeMenu(U h)override{w.menus[(h-0x80)/4].alive=0;}
 bool ResizeItems(U,W count)override{if(count>24)return false;return !count||!fail();}
 P AllocateText(U size)override{if(fail()||w.next_text+size>sizeof(w.text))return 0;U p=0xa000+w.next_text;w.next_text+=size;return p;}
 void FreeText(P)override{++w.freed;}
 W TextLength(P p)override{U n=0;while(p>=0xa000&&p+n<0xb000&&w.text[p+n-0xa000])++n;return W(n);}
 void Copy(P d,P s,W n)override{if(n){if(d<0xa000||d+n>0xb000||s<0xa000||s+n>0xb000)std::abort();std::memmove(w.text+d-0xa000,w.text+s-0xa000,n);}}
 bool Writable(P p,W n)override{return !w.badwrite&&p>=0xa000&&p+n<=0xb000;}
 void PutZero(P p)override{if(p<0xa000||p>=0xb000)std::abort();w.text[p-0xa000]=0;}
 bool ModernTask()override{return w.modern;}
 bool Bitmap(W h)override{return h==0x200;}
 Tracking*Active(U h)override{return Find(h)&&w.tracking[(h-0x80)/4].active?&tracks[(h-0x80)/4]:nullptr;}
 void Refresh(U,U,bool)override{++w.refreshes;}
 bool PreserveSystemClose(U,W,W)override{return false;}
 void Enabled(U)override{++w.enabled;}
};
extern "C" U menu_family(Wire*w,U op,U h,U a,U b,U c,InfoWire*info){Env e(*w);State s{w->head};Info i{info->size,info->mask,W(info->type),W(info->state),W(info->id),W(info->submenu),W(info->checked),W(info->unchecked),info->data,info->text,W(info->length),W(info->bitmap)};U r=0;
 switch(op){case 0:{Found f=Lookup(e,h,W(a),W(b));r=f?f.menu|(U(f.index)<<16):0;break;}case 1:r=Flags(e,h,a,b);break;case 2:r=ChangeFlags(e,h,a,b,c);break;case 3:r=Radio(e,h,a,b,c,W(info->mask));break;case 4:r=Default(e,h,a,b);break;case 5:r=SetDefault(e,h,a,b);break;case 6:r=GetInfo(e,h,a,b,i);break;case 7:r=SetInfo(e,s,h,a,b,i);break;case 8:r=Insert(e,s,h,a,b,i);break;case 9:r=Remove(e,s,h,a,b,c);break;case 10:r=Destroy(e,s,h);break;case 11:r=Create(e,s,a,b);break;case 12:r=Validate(e,i,a);break;case 13:i=Legacy(a,b,c);r=1;break;case 14:{Info out{};r=Normalize16(i,out);i=out;break;}case 15:{MenuInfo m{i.size,i.mask,U(i.type)|(U(i.state)<<16),i.id,i.submenu,i.data,U(i.text)};GetMenuInfo(e,h,m);i.type=W(m.style);i.state=W(m.style>>16);i.id=W(m.max_height);i.submenu=W(m.brush);i.data=m.help;i.text=m.data;r=1;break;}}
 *info={i.size,i.mask,i.type,i.state,i.id,i.submenu,i.checked,i.unchecked,i.data,U(i.text),i.length,i.bitmap};w->head=s.head;return r;
}
#ifdef MENU_REPLAY
static void Clean(Wire&w){for(auto&m:w.menus){if(!m.alive){std::memset(&m,0,sizeof(m));continue;}for(U j=m.count;j<24;++j)std::memset(&m.items[j],0,sizeof(WI));}}
int main(int argc,char**argv){if(argc!=2)return 1;FILE*f=fopen(argv[1],"rb");if(!f)return 2;U count=0;Wire before,expected;InfoWire info,after;U args[6];while(fread(&before,sizeof(before),1,f)==1){if(fread(args,sizeof(args),1,f)!=1||fread(&info,sizeof(info),1,f)!=1||fread(&expected,sizeof(expected),1,f)!=1||fread(&after,sizeof(after),1,f)!=1)return 3;U result=menu_family(&before,args[0],args[1],args[2],args[3],args[4],&info);Clean(before);if(result!=args[5]||std::memcmp(&before,&expected,sizeof(before))||std::memcmp(&info,&after,sizeof(info))){fprintf(stderr,"menu replay mismatch %u\n",count);return 4;}++count;}fclose(f);printf("MENU ORIGINAL REPLAY PASS: %u cases\n",count);}
#endif
#include "../src/win98_menu_public.h"
extern "C" void menu_convert(unsigned *p,InfoWire*out,bool get,bool back){if(get)PrepareGet32(p);Info i=From32(p);if(back){i={out->size,out->mask,W(out->type),W(out->state),W(out->id),W(out->submenu),W(out->checked),W(out->unchecked),out->data,out->text,W(out->length),W(out->bitmap)};To32(p,i);}else*out={i.size,i.mask,i.type,i.state,i.id,i.submenu,i.checked,i.unchecked,i.data,U(i.text),i.length,i.bitmap};}
extern "C" U menu_details(Wire*w,U h,MenuInfo*i,U set){Env e(*w);if(set)SetMenuInfo(e,h,*i);else GetMenuInfo(e,h,*i);return 1;}
