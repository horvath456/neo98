// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98Menu {
using U=unsigned;using W=unsigned short;using S=short;using P=__UINTPTR_TYPE__;
static_assert(sizeof(U)==4&&sizeof(W)==2&&sizeof(S)==2,"Win98 menu scalar widths");
constexpr U Missing=0xffffffffu;
struct Item {
 W type=0,state=0,id=0,reserved=0;
 U submenu=0;
 W checked=0,unchecked=0;
 P text=0;
 U data=0;
 W length=0,x=0,y=0,width=0,height=0,layout22=0,layout24=0,layout26=0,bitmap=0,bitmap_width=0xffff,bitmap_height=0xffff;
};
struct Menu {
 U next=0;W flags=0,handle=0,task=0,width=0,height=0,count=0;
 U window=0;Item*items=nullptr;U help=0;W capacity=0,max_height=0;U data=0;W brush=0,first_visible=0,last_visible=0,scroll=0,status=0;
};
struct Info {U size=32,mask=0;W type=0,state=0,id=0,submenu=0,checked=0,unchecked=0;U data=0;P text=0;W length=0,bitmap=0;};
struct MenuInfo {U size=28,mask=0,style=0,max_height=0,brush=0,help=0,data=0;};
struct State {U head=0;};
struct Tracking {
 S selected=-1,keyboard=-1;W flags=0,window=0;
 W first_window=0;S first_index=-1;W second_window=0;S second_index=-1;
};
struct Environment {
 virtual ~Environment()=default;
 virtual Menu*Find(U)=0;
 virtual U AllocateMenu()=0; // zeroed original 46-byte object, public handle assigned by heap provider
 virtual void FreeMenu(U)=0;
 virtual bool ResizeItems(U,W)=0; // preserve old storage on failure; capacity is committed by core
 virtual P AllocateText(U)=0;
 virtual void FreeText(P)=0;
 virtual W TextLength(P)=0;
 virtual void Copy(P destination,P source,W count)=0;
 virtual bool Writable(P,W)=0;
 virtual void PutZero(P)=0;
 virtual bool ModernTask()=0;
 virtual bool Bitmap(W)=0;
 virtual Tracking*Active(U)=0;
 virtual void Refresh(U,U,bool)=0; // popup tracking/render path USER remains provider
 virtual bool PreserveSystemClose(U,W,W)=0; // USER window/system-menu test
 virtual void Enabled(U)=0; // USER associated system-menu caption refresh
};
struct Found {U menu=0;W index=0;explicit operator bool()const{return menu!=0;}};
inline Found Lookup(Environment&e,U h,W item,W by_position){ // USER
 Menu*m=e.Find(h);if(!m||!m->count||item==0xffff)return {};
 if(by_position)return item<m->count?Found{h,item}:Found{};
 Found popup{};
 for(W i=0;i<m->count;++i){const Item&v=m->items[i];
  if(v.submenu){if(v.id==item)popup={h,i};Found f=Lookup(e,v.submenu,item,0);if(f)return f;}
  else if(v.id==item)return {h,i};
 }
 return popup;
}
inline U Count(Environment&e,U h){Menu*m=e.Find(h);return m?m->count:Missing;}
inline U Id(Environment&e,U h,W index){Menu*m=e.Find(h);return !m||index>=m->count||m->items[index].submenu?Missing:m->items[index].id;}
inline U Submenu(Environment&e,U h,W index){Menu*m=e.Find(h);if(!m||index>=m->count)return 0;U v=m->items[index].submenu;return e.Find(v)?v:0;}
inline U Flags(Environment&e,U h,W item,W flags){ // B358..B3C6
 Found f=Lookup(e,h,item,flags&0x400);if(!f)return Missing;Item&v=e.Find(f.menu)->items[f.index];W result=v.type|v.state;
 if(v.bitmap&&!v.text)result|=4;
 if(v.submenu){Menu*s=e.Find(v.submenu);result=W((result&0xff)|0x10|(W(s?s->count:0)<<8));}
 return result;
}
inline U ChangeFlags(Environment&e,U h,W item,W flags,W mask){ // B3C6..B41C
 Found f=Lookup(e,h,item,flags&0x400);if(!f)return Missing;Item&v=e.Find(f.menu)->items[f.index];W old=v.state&mask;
 if(flags&mask)v.state|=flags&mask;else v.state&=W(~mask);return old;
}
inline U Check(Environment&e,U h,W item,W flags){return ChangeFlags(e,h,item,flags,8);}
inline U Enable(Environment&e,U h,W item,W flags){U r=ChangeFlags(e,h,item,flags,3);e.Enabled(h);return r;}
inline U Radio(Environment&e,U h,W first,W last,W check,W flags){ // B4AA..B536
 U root=0,result=0;
 // Original WORD loop wraps at FFFF; the native bounded traversal ends after that item.
 for(U i=first;i<=last;++i){Found f=Lookup(e,h,W(i),flags&0x400);if(!f)continue;Item&v=e.Find(f.menu)->items[f.index];if(v.type&0x800)continue;
  if(!root)root=f.menu;else if(root!=f.menu)continue;
  if(i==check){v.type|=0x200;v.state|=8;result=1;}else v.state&=W(~8);
 }return result;
}
inline U Default(Environment&e,U h,W by_position,W flags){ // B536..B5E2
 Menu*m=e.Find(h);if(!m)return Missing;
 for(S i=0;i<S(m->count);++i){Item&v=m->items[W(i)];if(!(v.state&0x1000))continue;if(!(flags&1)&&(v.state&3))continue;
  if((flags&2)&&v.submenu){U r=Default(e,v.submenu,by_position,flags);if(r!=Missing)return r;}
  return by_position?U(i):v.id;
 }return Missing;
}
inline U SetDefault(Environment&e,U h,W item,W by_position){ // B5E2..B6B4
 Menu*m=e.Find(h);if(!m)return 0;Found target{};
 if(item!=0xffff){target=Lookup(e,h,item,by_position);if(!target||target.menu!=h||(m->items[target.index].type&0x800))return 0;}
 for(S i=0;i<S(m->count);++i){Item&v=m->items[W(i)];if((v.state&0x1000)&&(!target||target.index!=W(i))){v.state&=W(~0x1000);v.layout24=0x7fff;v.layout26=0;}}
 if(target){Item&v=m->items[target.index];if(!(v.state&0x1000)){v.state|=0x1000;v.layout24=0x7fff;v.layout26=0;}}return 1;
}
inline bool Validate(Environment&e,const Info&i,bool get){ // USER + 3F44
 if(i.size!=32||(i.mask&~0x1ffu))return false;if(get)return true;
 if((i.mask&0x100)&&(i.type&0x949b))return false;
 if((i.mask&1)&&(i.state&0xe074))return false;
 auto bitmap=[&](W h){return h==0xffff||h<12||e.Bitmap(h);};
 if((i.mask&8)&&(!bitmap(i.checked)||!bitmap(i.unchecked)))return false;
 if((i.mask&4)&&i.submenu&&!e.Find(i.submenu))return false;
 if((i.mask&0x80)&&!bitmap(i.bitmap))return false;
 return true; // source string read-probe belongs to the native pointer provider
}
inline bool Normalize16(const Info&in,Info&out){ // 4816..48BE
 if(in.size!=30&&in.size!=32)return false;out=in;out.size=32;
 if(!(out.mask&0xc0)&&(in.mask&0x10)){
  if(in.type&4){out.type&=W(~4);out.mask|=0xc0;out.bitmap=W(in.text);out.text=0;out.length=0;}
  else if(!(in.type&0x904)){out.mask|=0xc0;out.bitmap=0;}
 }return true;
}
inline Info Legacy(W flags,W id,P text){ // 58DC..5980
 Info i;i.mask=0x103;if(flags&0x10){i.mask=0x107;i.submenu=id;}
 if(flags&0x100){i.mask|=0x20;i.data=U(text);}i.text=text;
 if(flags&4){flags&=W(~4);i.mask|=0xc0;i.bitmap=W(text);i.text=0;i.length=0;}
 else if(!(flags&0x904)){i.mask|=0xc0;i.bitmap=0;}
 i.state=flags&0x8b;i.type=flags&0x4b64;i.id=id;return i;
}
inline void FreeText(Environment&e,Item&v){if(v.text)e.FreeText(v.text);v.text=0;v.length=0;}
inline bool Destroy(Environment&,State&,U);
inline void FreeItem(Environment&e,State&s,Item&v,bool submenu){FreeText(e,v);if(submenu){if(v.submenu)Destroy(e,s,v.submenu);v.submenu=0;}}
inline bool Apply(Environment&e,State&s,U h,W index,const Info&i,U owner=0){ // 5586..5808; ordering preserved on allocation failure
 Menu*target=e.Find(h);if(!target||index>=target->count)return false;Item&v=target->items[index];if(owner)h=owner;Menu*m=e.Find(h);bool geometry=false,repaint=false;
 if(i.mask&0x100){v.type=W((v.type&0x949b)|i.type);if(i.type&0x800)v.state|=3;geometry=true;}
 if(i.mask&2)v.id=i.id;if(i.mask&0x20)v.data=i.data;
 if(i.mask&1){v.state=W((v.state&0x1080)|i.state);if(v.type&0x800)v.state|=3;repaint=true;}
 if(i.mask&8){v.checked=i.checked;v.unchecked=i.unchecked;repaint=true;}
 if((i.mask&4)&&v.submenu!=i.submenu){if(bool(v.submenu)!=bool(i.submenu))repaint=true;
  if(v.submenu){Destroy(e,s,v.submenu);v.submenu=0;}
  if(Menu*sub=e.Find(i.submenu)){v.submenu=i.submenu;sub->flags|=1;}
 }
 if(i.mask&0x40){P text=0;W length=0;if(i.text){length=e.TextLength(i.text);text=e.AllocateText(U(length)+1);if(!text)return false;e.Copy(text,i.text,W(length+1));}
  FreeText(e,v);v.text=text;v.length=length;geometry=true;
 }
 if(i.mask&0x80){v.bitmap=i.bitmap;v.bitmap_width=v.bitmap_height=0xffff;geometry=true;}
 if(geometry){if(!(v.type&0x900)&&!v.bitmap&&!v.text){v.type|=0x800;v.state|=3;}v.layout24=0x7fff;v.layout22=v.layout26=0;m->width=m->height=0;}
 if((geometry||repaint)&&e.Active(h))e.Refresh(h,index,false);return true;
}
inline bool SetInfo(Environment&e,State&s,U h,W item,W by_position,const Info&i){Found f=Lookup(e,h,item,by_position);if(!f)return !by_position&&item==0xf130;return Apply(e,s,f.menu,f.index,i,h);}
inline bool GetInfo(Environment&e,U h,W item,W by_position,Info&i){ // 53C4..5586
 Found f=Lookup(e,h,item,by_position);if(!f)return false;const Item&v=e.Find(f.menu)->items[f.index];
 if(i.mask&1)i.state=v.state&0x1f8b;if(i.mask&2)i.id=v.id;if(i.mask&4)i.submenu=W(v.submenu);
 if(i.mask&8){i.checked=v.checked;i.unchecked=v.unchecked;}if(i.mask&0x20)i.data=v.data;
 if(i.mask&0x100)i.type=v.type&0x6b64;if(i.mask&0x80)i.bitmap=v.bitmap;
 if(i.mask&0x40){if(!i.length)i.length=v.length;else{
  W copied=0;if(v.text){S n=S(W(i.length-1));if(n>S(v.length))n=S(v.length);copied=W(n);
   if(!e.Writable(i.text,i.length)&&(e.ModernTask()||!e.Writable(i.text,copied)))return false;
   e.Copy(i.text,v.text,copied);e.PutZero(i.text+copied);
  }else if(e.Writable(i.text,1))e.PutZero(i.text);
  i.length=copied;
 }}return true;
}
inline U String(Environment&e,U h,W item,W flags,P buffer,W count){Info i;i.mask=0x40;i.text=buffer;i.length=count;if(count)e.PutZero(buffer);return GetInfo(e,h,item,flags&0x400,i)?i.length:0;}
inline void AdjustIndex(S&value,S index){if(value==index)value=-1;else if(index<value)--value;}
inline bool Insert(Environment&e,State&s,U h,W item,W by_position,const Info&i){ // 48F0..4CB6
 Menu*m=e.Find(h);if(!m)return false;Found f{};if(item!=0xffff){f=Lookup(e,h,item,by_position);if(f){h=f.menu;m=e.Find(h);}else item=0xffff;}
 W bitmap=(i.mask&0x80)?i.bitmap:0;W position=f?f.index:m->count;
 if(!(m->flags&1)&&m->count&&(!bitmap||bitmap>=12)){
  W di=by_position?item:(f?f.index:item);
  if(!di){if(m->items[0].bitmap==1){item=1;position=1;}}
  else{if(di==0xffff)di=m->count;--di;while(di&&m->items[di].bitmap&&m->items[di].bitmap<12){item=di;position=di;--di;}}
 }
 if(m->count>=m->capacity){W capacity=W(m->capacity+10);if(!e.ResizeItems(h,capacity))return false;m=e.Find(h);m->capacity=capacity;}
 ++m->count;for(W j=W(m->count-1);j>position;--j)m->items[j]=m->items[j-1];m->items[position]=Item{};
 if(Tracking*t=e.Active(h)){S index=S(position/46);if(index<=t->selected)++t->selected;if((t->flags&0x20)&&t->keyboard>=index)++t->keyboard;if(t->first_window==t->window&&t->first_index>=index)++t->first_index;if(t->second_window==t->window&&t->second_index>=index)++t->second_index;}
 if(Apply(e,s,h,position,i))return true;
 FreeItem(e,s,m->items[position],true);
 if(m->count==1){e.ResizeItems(h,0);m=e.Find(h);m->capacity=0;}
 else{for(W j=position;j+1<m->count;++j)m->items[j]=m->items[j+1];if(W(m->capacity-m->count)==9){W capacity=W(m->capacity-10);if(e.ResizeItems(h,capacity)){m=e.Find(h);m->capacity=capacity;}}}
 --m->count;return false;
}
inline bool Remove(Environment&e,State&s,U h,W item,W flags,bool destroy){ // 4CD4..4E7A; 4EFE/4F1C wrappers
 if(destroy&&e.PreserveSystemClose(h,item,flags))return true;
 Found f=Lookup(e,h,item,flags&0x400);if(!f)return false;h=f.menu;Menu*m=e.Find(h);Tracking*t=e.Active(h);S tracked=S(f.index/46);
 if(t){AdjustIndex(t->selected,tracked);if(t->flags&0x20)AdjustIndex(t->keyboard,tracked);if(t->first_window==t->window)AdjustIndex(t->first_index,tracked);if(t->second_window==t->window)AdjustIndex(t->second_index,tracked);}
 FreeItem(e,s,m->items[f.index],destroy);
 if(m->count==1){e.ResizeItems(h,0);m=e.Find(h);m->capacity=0;}
 else{for(W j=f.index;j+1<m->count;++j)m->items[j]=m->items[j+1];if(W(m->capacity-m->count)==9){W capacity=W(m->capacity-10);if(!e.ResizeItems(h,capacity)){--m->count;return false;}m=e.Find(h);m->capacity=capacity;}}
 --m->count;m->width=m->height=0;if(t)e.Refresh(h,U(tracked),true);return true;
}
inline U Create(Environment&e,State&s,W task,bool popup){ // 6444..6540; heap/task provider supplies native object identity
 U h=e.AllocateMenu();if(!h)return 0;Menu*m=e.Find(h);m->flags=popup?1:0;m->task=task;m->next=s.head;s.head=h;return h;
}
inline bool Destroy(Environment&e,State&s,U h){ // 6540..6642
 U*link=&s.head;while(*link){Menu*m=e.Find(*link);if(!m)return false;if(*link!=h){link=&m->next;continue;}*link=m->next;
  for(U j=m->count;j;--j)FreeItem(e,s,m->items[j-1],true);e.ResizeItems(h,0);m=e.Find(h);m->count=m->capacity=0;m->handle=0;e.FreeMenu(h);return true;
 }return false;
}
inline void GetMenuInfo(Environment&e,U h,MenuInfo&i){Menu&m=*e.Find(h);
 if(i.mask&0x10){i.style=0;const W bits[]={8,0x20,0x40,0x80,0x100,0x400};for(U n=0;n<6;++n)if(m.flags&bits[n])i.style|=0x80000000u>>n;}
 if(i.mask&1)i.max_height=m.max_height;if(i.mask&2)i.brush=m.brush;if(i.mask&4)i.help=m.help;if(i.mask&8)i.data=m.data;
}
inline void SetMenuInfo(Environment&e,U h,const MenuInfo&i){Menu&m=*e.Find(h);bool geometry=false,repaint=false;
 if(i.mask&0x10){const W bits[]={8,0x20,0x40,0x80,0x100,0x400};for(U n=0;n<6;++n)if(i.style&(0x80000000u>>n))m.flags|=bits[n];else m.flags&=W(~bits[n]);geometry=true;}
 if(i.mask&1){m.max_height=W(i.max_height);geometry=true;}if(i.mask&2){m.brush=W(i.brush);repaint=true;}if(i.mask&4)m.help=i.help;if(i.mask&8)m.data=i.data;
 if(i.mask&0x80000000u)for(U n=0;n<m.count;++n)if(m.items[n].submenu)SetMenuInfo(e,m.items[n].submenu,i);
 if(geometry)m.width=m.height=0;if((geometry||repaint)&&e.Active(h))e.Refresh(h,Missing,false);
}
} // namespace Win98Menu
