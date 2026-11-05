// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_menu.h"
namespace Win98Menu {
// USER. The caller maps a STRING pointer after this conversion.
inline Info From32(const U*p){
 Info i;i.size=p[0];if(i.size!=44&&i.size!=48)return i;
 if(i.size==48)i.bitmap=W(p[11]);i.size=32;i.mask=p[1];i.type=W(p[2]);
 if(i.mask&0x10){i.mask|=0x100;if(i.type&4){i.mask|=0x80;i.bitmap=W(p[9]);}if(!(i.type&0x904))i.mask|=0x40;}
 i.state=W(p[3]);i.id=W(p[4]);i.submenu=W(p[5]);i.checked=W(p[6]);i.unchecked=W(p[7]);i.data=p[8];i.text=p[9];i.length=W(p[10]);return i;
}
inline void PrepareGet32(U*p){if(p[1]&0x10)p[1]|=0x1c0;}
// USER. It copies narrowed fields even when the inner call failed.
inline void To32(U*p,const Info&i){
 p[2]=i.type;p[3]=i.state;p[4]=i.id;p[5]=i.submenu;p[6]=i.checked;p[7]=i.unchecked;p[8]=i.data;
 if(i.mask&0x10){p[1]&=0xfffffe3fu;if(i.bitmap){p[9]=i.bitmap;p[2]|=4;}else if(!i.length)p[9]=0;}
 p[10]=(i.mask&0x10)&&i.bitmap?W(i.text):i.length;
 if(!(i.mask&0x10)&&p[0]>=48&&(p[1]&0x80))p[11]=i.bitmap;
}
} // namespace Win98Menu
namespace Win98Menu {
// Same original copy-back order with guest reads preserved between writes. A
// string buffer may overlap MENUITEMINFO, so cbSize and fMask are read again.
template<class Read,class Write>bool Publish32(U address,const Info&i,Read read,Write write){
 if(address>~0u-48)return false;
 const U fields[]={i.type,i.state,i.id,i.submenu,i.checked,i.unchecked,i.data};
 for(U n=0;n<7;++n)if(!write(address+8+4*n,fields[n]))return false;
 if(i.mask&0x10){U mask;if(!read(address+4,mask)||!write(address+4,mask&0xfffffe3fu))return false;
  if(i.bitmap){if(!write(address+36,i.bitmap)||!write(address+8,i.type|4))return false;}
  else if(!i.length&&!write(address+36,0))return false;
 }
 if(!write(address+40,(i.mask&0x10)&&i.bitmap?W(i.text):i.length))return false;
 if(!(i.mask&0x10)){U size,mask;if(!read(address,size)||!read(address+4,mask))return false;if(size>=48&&(mask&0x80)&&!write(address+44,i.bitmap))return false;}
 return true;
}
}
