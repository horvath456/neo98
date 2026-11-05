// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98MenuHelp {
using U=unsigned;using W=unsigned short;
struct Environment {
 virtual ~Environment()=default;
 virtual U CommonModule()=0;virtual bool Read(U,U&)=0;
 virtual bool ItemType(U,U,U&)=0;virtual bool LastIsRestore(U)=0;
 virtual void LoadString(U,U,W(&)[256])=0;
 virtual void Text(U,U,const W*)=0;virtual void Simple(U,bool)=0;virtual void Update(U)=0;
};
// COMCTL32; cold branches.
// Menu lookup, resources and status-window messaging remain native providers.
inline void Help(Environment&e,const U*a){
 if(a[0]!=0x11f)return;U flags=a[1]>>16,item=W(a[1]);
 if(flags==0xffff&&!a[2]){e.Simple(a[5],false);return;}
 W text[256]={};U type=0,style=0;if(e.ItemType(a[2],item,type))style=(type>>3)&0x400;
 bool update=true,load=false;U resource=0,module=a[4];
 if(!(flags&0x800)){
  if(flags&0x10){update=false;bool found=false;
   // Provider bound for a malformed, unterminated help-ID table.
   for(U n=0;n<128;++n){U id=0,index=0;if(!e.Read(a[6]+8+n*8,id))return;if(!id)break;if(!e.Read(a[6]+12+n*8,index))return;if(index==item){resource=id;found=load=true;break;}}
   if(!found){if(a[2]==a[3]){if(e.LastIsRestore(a[3])){if(!item){resource=0xffff8ff0;module=e.CommonModule();load=true;}else --item;}if(!load){if(!e.Read(a[6]+4,resource))return;resource+=item;load=true;}}
    else if(flags&0x2000){resource=0xffff8ff0;module=e.CommonModule();load=true;}}
  }else{if(item>=0xf000){resource=item-0x7000;module=e.CommonModule();}else{if(!e.Read(a[6],resource))return;resource+=item;}load=true;}
 }
 if(load)e.LoadString(module,resource,text);e.Text(a[5],style|0x1ff,text);e.Simple(a[5],true);if(update)e.Update(a[5]);
}
}
