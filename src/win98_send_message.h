// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98SendMessage {
// USER. Native task identities replace 16-bit queue selectors.
inline bool CrossTask(unsigned owner,unsigned current){return owner!=current;}
struct Record{unsigned sender=0,result=0,flags=0;};
// USER and 24F8..2569.
inline unsigned InSend(const Record*r){return r!=nullptr;}
inline unsigned InSendEx(const Record*r){if(!r)return 0;unsigned result=r->sender?1:(r->flags&0x300)?4:2;return result|((r->flags&1)?8:0);}
// USER. Synchronous reply stores the result, marks replied and wakes.
template<class E> unsigned Reply(E&e,Record*r,unsigned result){if(!r)return 0;if(r->sender){r->result=result;r->flags|=0x401;e.Wake();}else if(r->flags&0x100)e.CallbackReply(result);return 1;}
// USER. A normal return cannot replace an explicit reply.
template<class E> void Finish(E&e,Record&r,unsigned result){if(r.flags&0xc){e.Dispose();return;}if(!(r.flags&1)){r.result=result;r.flags|=1;e.Wake();}else r.flags&=~0x400u;}
}
