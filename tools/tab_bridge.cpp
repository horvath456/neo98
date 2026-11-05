// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_tab.h"
#include <cstring>
static unsigned char memory[65536];
struct E {
 unsigned Read32(unsigned p){unsigned v=0;memcpy(&v,memory+p-0x100000,4);return v;}
 void Write32(unsigned p,unsigned v){memcpy(memory+p-0x100000,&v,4);}
 void Copy(unsigned to,unsigned from,unsigned n){memmove(memory+to-0x100000,memory+from-0x100000,n);}
 unsigned Allocate(unsigned n){unsigned p=Read32(0x10f000);Write32(0x10f000,p+((n+15)&~15u));memset(memory+p-0x100000,0,n);return p;}
 void Free(unsigned){}
 unsigned Array(unsigned op,unsigned a,unsigned index,unsigned){if(op!=332)return 0;return int(index)>=0&&index<Read32(a)?Read32(Read32(a+4)+index*4):0;}
 bool SetString(unsigned field,unsigned source){if(!source){Write32(field,0);return true;}unsigned n=0;while(memory[source-0x100000+n]||memory[source-0x100000+n+1])n+=2;unsigned text=Allocate(n+2);Copy(text,source,n+2);Write32(field,text);return true;}
 void CopyText(unsigned out,unsigned from,unsigned count){if(!count)return;unsigned n=0;for(;n+1<count;++n){Copy(out+n*2,from+n*2,2);if(!(memory[from-0x100000+n*2]|memory[from-0x100000+n*2+1]))return;}memory[out-0x100000+n*2]=memory[out-0x100000+n*2+1]=0;}
 bool Alive(unsigned){return true;}
 void Dirty(unsigned t,bool layout){if(layout){Write32(t+0x44,0x7fffffff);Write32(t+0x4c,0x7fffffff);}}
 void Selection(unsigned,unsigned,unsigned){}
 unsigned Notify(unsigned,unsigned code){unsigned n=Read32(0x10e000);Write32(0x10e004+n*4,code);Write32(0x10e000,n+1);return code==0xfffffdd8?Read32(0x10e040):0;}
 int TextWidth(unsigned,unsigned item){return Read32(item+0x1c);}
}e;
extern "C" {
void tab_reset(const void*p){memcpy(memory,p,sizeof memory);}
void*tab_memory(){return memory;}
unsigned tab_provider(unsigned op,const unsigned*a){switch(op){case 1:return e.Array(332,a[0],a[1],0);case 2:e.Copy(a[0],a[1],a[2]);return a[0];case 3:return e.SetString(a[0],a[1]);case 4:e.CopyText(a[0],a[1],a[2]);return a[0];case 5:return e.Allocate(a[0]);case 6:Win98Tab::FreeItem(e,a[1]);return 0;case 7:return e.Notify(a[1],a[2]);}return 0;}
unsigned tab_run(unsigned op,unsigned t,unsigned index,unsigned p){switch(op){case 0:return Win98Tab::Get(e,t,index,p);case 1:return Win98Tab::Make(e,t,p);case 2:return Win98Tab::Set(e,t,index,p);case 3:return Win98Tab::Extra(e,t,index);case 4:return Win98Tab::Select(e,t,index,p!=0);}return 0;}
}
