// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_clipboard.h"
#include <string.h>
using namespace Win98Clipboard;
using U = unsigned;
struct Blob { U used, size; unsigned char data[256]; };
struct Wire {
    U count, table, task, owner, viewer, opened, changed, notifying, saved, sequence;
    U current, version, locale, context, palette;
    Word special[2], pairs[2][2];
    Entry entries[256]; Blob blobs[128];
};
using Service = U (*)(U,U,U,U,U);
struct E final : Environment {
    Wire &w; State s; Service fn;
    E(Wire &v, Service f):w(v),fn(f){in();}
    void in(){s.entries=w.table?w.entries:nullptr;s.count=w.count;s.open_task=w.task;s.owner=w.owner;s.viewer=w.viewer;s.open_window=w.opened;s.changed=w.changed;s.notifying=w.notifying;s.saved_context=w.saved;s.sequence=w.sequence;memcpy(s.special_formats,w.special,sizeof w.special);memcpy(s.special_data,w.pairs,sizeof w.pairs);}
    void out(){w.table=s.entries!=nullptr;w.count=s.count;w.task=s.open_task;w.owner=s.owner;w.viewer=s.viewer;w.opened=s.open_window;w.changed=s.changed;w.notifying=s.notifying;w.saved=s.saved_context;w.sequence=s.sequence;memcpy(w.pairs,s.special_data,sizeof w.pairs);}
    U event(U op,U a=0,U b=0,U c=0,U d=0){out();U r=fn(op,a,b,c,d);in();return r;}
    Word CurrentTask()override{return w.current;}
    Word ExpectedVersion(Word)override{return w.version;}
    U Locale(Word)override{return w.locale;}
    U WindowHandle(U h)override{return h?Word(0x80+(h-0x4000)/0x100*4):0;}
    Word Context()override{return w.context;}
    void InputMode(bool x)override{event(20,x);}
    U Send(U h,Word msg,U wp,U lp,bool timeout)override{return event(timeout?22:21,h,msg,wp,lp);}
    Entry* ResizeTable(Entry*p,Word size)override{return event(1,p!=nullptr,size)?w.entries:nullptr;}
    void FreeTable(Entry*)override{event(2);}
    void ReferenceAtom(Word x)override{event(3,x);}
    void DeleteAtom(Word x)override{event(4,x);}
    Word AtomName(Word f,unsigned char*,Word n)override{return event(26,f,n);}
    Word GlobalAlloc(Word flags,U size)override{return event(5,flags,size);}
    U GlobalSize(Word h)override{return event(6,h);}
    unsigned char* GlobalData(Word h)override{return h>=0x400&&(h-0x400)/8<128?w.blobs[(h-0x400)/8].data:nullptr;}
    unsigned char* GlobalLock(Word h)override{return event(7,h)?GlobalData(h):nullptr;}
    void GlobalUnlock(Word h)override{event(8,h);}
    Word GlobalReAlloc(Word h,U size,Word flags)override{return event(9,h,size,flags);}
    void GlobalFree(Word h)override{event(10,h);}
    void SetObjectOwner(Word h)override{event(11,h);}
    void DeleteObject(Word h)override{event(12,h);}
    bool ValidMetaFile(Word h)override{return event(13,h)!=0;}
    Word CheckMetaFile(Word h)override{return event(14,h);}
    void DeleteMetaFile(Word h,bool x)override{event(15,h,x);}
    void DiscardMapping(Word h)override{event(16,h);}
    U To32(Word f,Word h)override{return event(17,f,h);}
    Word To16(Word f,U h,bool x)override{return event(18,f,h,x);}
    U ConvertSpecial(U h,bool x)override{return event(19,h,x);}
    bool PaletteDisplay()override{return w.palette;}
    Word CreateSystemPalette()override{return event(23);}
    Word ConvertGraphics(Word h,Word f,Word t)override{return event(24,h,f,t);}
    void TranslateText(const unsigned char*a,unsigned char*b,bool oem)override{
        unsigned x=0,y=0;for(unsigned i=0;i<128;++i){if(a==w.blobs[i].data)x=0x400+i*8;if(b==w.blobs[i].data)y=0x400+i*8;}
        event(25,x,y,oem);
    }
};
extern "C" U clipboard_call(Wire*w,Service fn,U op,U a,U b,U c){
    E e(*w,fn);auto&s=e.s;U r=0;
    switch(op){
    case 0:r=Open(e,s,a);break;case 1:r=Close(e,s);break;case 2:r=Empty(e,s);break;
    case 3:r=Set(e,s,a,b,c);break;case 4:r=Get(e,s,a,b);break;case 5:r=Enumerate(e,s,a);break;
    case 6:r=Available32(s,a);break;case 7:r=Priority(s,(const Word*)w->blobs[127].data,a);break;
    case 8:r=SetViewer(e,s,a);break;case 9:r=ChangeChain(e,s,a,b);break;
    case 10:OwnerDestroyed(e,s);break;case 11:NotifyViewer(e,s);break;
    case 12:r=Set32(e,s,a,b);break;case 13:r=Get32(e,s,a);break;
    case 14:r=Classify(s,{Word(a),Word(b)});break;case 15:Synthesize(e,s);break;
    case 16:FreeEntry(e,s,w->entries[a]);break;
    case 18:TaskExited(e,s,Word(a));break;case 17:r=Name(e,a,w->blobs[127].data,b);break;
    }
    e.out();return r;
}
