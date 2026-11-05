// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_serialui_pages.h"
#include <cstring>
struct Wire{unsigned mode,reason,flags,applied,page,accepted,result,used;unsigned char memory[1024];unsigned trace[32];};
struct E{
 Wire&w;
 void Event(unsigned event,unsigned arg=0){w.trace[w.used++]=event;w.trace[w.used++]=arg;}
 unsigned Read16(unsigned p){return w.memory[p]|(w.memory[p+1]<<8);}
 unsigned Read32(unsigned p){return Read16(p)|(Read16(p+2)<<16);}
 void Write16(unsigned p,unsigned v){w.memory[p]=v;w.memory[p+1]=v>>8;}
 void Write32(unsigned p,unsigned v){Write16(p,v);Write16(p+2,v>>16);}
 unsigned Linear(unsigned p){return p&65535;}
 unsigned Create(){Event(1);return w.page;}
 unsigned Callback(unsigned p){Event(2,p);return w.accepted;}
 void Destroy(unsigned p){Event(3,p);}
 void CopyDcb(){Event(1,28);memcpy(w.memory+0x308,w.memory+0x182,28);}
 void Save(){Event(2,Read32(0x300));}
 void FreeState(){Event(3,0x300);Event(3,0x180);}
};
extern "C" void run(Wire*w){E e{*w};w->result=w->mode==0?Win98SerialUi::OfferPage(e):w->mode==1?Win98SerialUi::ReleasePage(e,w->reason,w->flags,w->applied):Win98SerialUi::CollectPage(e,0x100,w->page);}
