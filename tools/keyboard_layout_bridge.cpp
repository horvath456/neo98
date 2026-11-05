// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_keyboard_layout.h"
using U=unsigned;using namespace Win98KeyboardLayout;
struct Fixture {State state;Window windows[9];U layouts[5],current,focus,excluded,excluded_class,shell,first,reentry;};
extern "C" U keyboard_layout_case(Fixture *f,U op,U arg,U flags,unsigned char *output,U *trace){
 struct E{Fixture&f;unsigned char*out;U*t;void Event(U kind,U a=0,U b=0,U c=0){U n=t[0];t[1+n++]=kind;t[1+n++]=a;t[1+n++]=b;t[1+n++]=c;t[0]=n;}
  U CurrentTask(){Event(1);return f.current;}U ResolveThread(U id){Event(2,id);return id>=0x101&&id<=0x104?id-0x100:0;}
  U ThreadLayout(U task){return f.layouts[task];}void SetThreadLayout(U task,U hkl){f.layouts[task]=hkl;}
  void StoreLayout(U p,U hkl){for(U i=0;i<4;++i)out[p+i]=hkl>>(8*i);}
  U FirstWindow(){return f.first;}Window ReadWindow(U h){return f.windows[h];}U ExcludedWindow(){return f.excluded;}U ExcludedClass(){return f.excluded_class;}
  U Focus(U){return f.focus;}bool ValidWindow(U h){Event(3,h);return h&&h<9;}
  bool ShellInterested(){Event(4);return f.shell;}
  void ShellNotify(U w,U h){Event(5,w,h);}
  void DriverActivate(U h){Event(6,h);}
  void LanguageMessage(U w,U c,U h){Event(7,w,c,h);if(f.reentry)f.layouts[f.current]=f.reentry;}
  void HexName(U p,U value){Event(8,value);for(U i=0;i<8;++i)out[p+i]="0123456789abcdef"[(value>>(28-i*4))&15];out[p+8]=0;}
  U VariantName(U variant,U){Event(9,variant);return 0;}
 }e{*f,output,trace};trace[0]=0;
 switch(op){case 0:return Get(e,f->state,arg);case 1:return List(e,f->state,arg,flags);case 2:return Neighbor(f->state,arg,int(flags));case 3:return Change(e,f->state,1,f->current,arg,flags);case 4:return Activate(e,f->state,arg,flags);case 5:return Name(e,f->state,arg);default:return 0;}
}
