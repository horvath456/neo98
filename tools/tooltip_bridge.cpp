/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/win98_tooltip.h"
#include <cstring>
static unsigned char memory[65536];
struct Env{
 unsigned Read32(unsigned a){unsigned v;std::memcpy(&v,memory+a-0x100000,4);return v;}void Write32(unsigned a,unsigned v){std::memcpy(memory+a-0x100000,&v,4);}
 void Copy(unsigned a,unsigned b,unsigned n){std::memmove(memory+a-0x100000,memory+b-0x100000,n);}
 unsigned Resize(unsigned old,unsigned bytes){unsigned a=0x108000;if(old)Copy(a,old,Read32(0x100124)*44);return a;}
 void SetString(unsigned field,unsigned value){if(value){Copy(0x10c000,value,32);Write32(field,0x10c000);}else Write32(field,0);}
 unsigned ToWide(unsigned){return 0;}void CopyText(unsigned out,unsigned p,bool wide){if(wide){for(unsigned i=0;i<32;i+=2){Copy(out+i,p+i,2);if(!memory[p+i-0x100000]&&!memory[p+i+1-0x100000])break;}}else{for(unsigned i=0;i<16;++i){memory[out+i-0x100000]=memory[p+i*2-0x100000];if(!memory[out+i-0x100000])break;}}}
 bool ValidWindow(unsigned){return true;}unsigned NotifyFormat(unsigned,unsigned){return Read32(0x10e000);}void Hook(unsigned,unsigned,bool){Write32(0x10e004,Read32(0x10e004)+1);}
 void Hide(unsigned t){Write32(t+0x38,0);Write32(0x10e008,Read32(0x10e008)+1);}bool Alive(unsigned){return true;}void Refresh(unsigned){Write32(0x10e00c,Read32(0x10e00c)+1);}unsigned DoubleClick(){return 500;}
};
extern "C" void*tooltip_memory(){return memory;}
extern "C" unsigned tooltip_run(unsigned op,unsigned t,unsigned a,unsigned b,unsigned c){Env e;switch(op){case 0:return Win98Tooltip::Find(e,t,a);case 1:return Win98Tooltip::Add(e,t,a);case 2:Win98Tooltip::Delete(e,t,a);break;case 3:Win98Tooltip::SetDelay(e,t,a,b);break;case 4:return Win98Tooltip::Delay(e,t,a);case 5:return Win98Tooltip::Get(e,t,a,b);case 6:return Win98Tooltip::Hit(e,t,a,int(b),int(c));case 7:return Win98Tooltip::ToolAt(e,t,a,int(b),int(c));case 8:Win98Tooltip::NewRect(e,t,a);break;}return 0;}
