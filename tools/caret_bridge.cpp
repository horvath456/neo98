// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_caret.h"
using namespace Win98Caret;
extern "C" U caret_call(State*s,U*globals,const U*a,U*trace){
 struct E{State&s;U*g,*t;void Event(U k,U a=0,U b=0,U c=0){U n=t[0];t[1+n++]=k;t[1+n++]=a;t[1+n++]=b;t[1+n++]=c;t[0]=n;}
 U CurrentTask(){return g[0];}U OwnerTask(U){return g[1];}bool SameProcess(U){return g[2];}U TaskFlags(){return g[3];}
 bool CanCreate(U){return g[4]&&g[2];}bool Visible(U){return g[5];}bool PendingPaint(const State&){return g[6];}
 void Draw(const State&s){Event(1,s.owner,s.x,s.y);Event(2,s.visible,s.width,s.height);}
 bool Accessibility(){return g[7];}void Notify(U event,U owner){Event(3,event,owner);}
 U BorderWidth(){return g[8];}U BorderHeight(){return g[9];}
 void BitmapReference(U h,bool add){Event(4,h,add);}void BitmapSize(U h,U&w,U&height){Event(5,h);w=g[10];height=g[11];}
 void KillTimer(U owner){Event(6,owner);}U StartTimer(U owner){Event(7,owner,g[12]);return g[13];}
 }e{*s,globals,trace};trace[0]=0;
 switch(a[0]){
 case 0:return Create(e,*s,a[1],a[2],a[3],a[4]);
 case 1:if(!Valid(e,*s,0))return 0;Destroy(e,*s);return 1;
 case 2:if(!Valid(e,*s,a[1]))return 0;Show(e,*s);return 1;
 case 3:if(!Valid(e,*s,a[1]))return 0;Hide(e,*s);return 1;
 case 4:return Position(e,*s,a[1],a[2]);
 case 5:Tick(e,*s,a[1]);return 0;
 case 6:return Blink(e,*s,globals[12],a[1]);
 }
 return 0;
}
