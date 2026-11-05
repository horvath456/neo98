// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98Caret {
using U=unsigned;
struct State{U owner=0,visible=0,phase=0,hides=0,x=0,y=0,height=0,width=0,bitmap=0,timer=0;};
inline int Signed(U v){return v&0x8000?int(v&65535)-65536:int(v&65535);}
// USER: current queue selection precedes this owner check.
template<class E> bool Valid(E&e,const State&s,U requested){
    return s.owner&&(!requested||requested==s.owner)&&
        (e.OwnerTask(s.owner)==e.CurrentTask()||(e.SameProcess(s.owner)&&(e.TaskFlags()&4)));
}
// USER, native drawing is supplied by the framebuffer provider.
template<class E> void Draw(E&e,State&s){
    if(!e.Visible(s.owner)){s.visible=0;return;}
    if(!e.PendingPaint(s))e.Draw(s);
}
template<class E> void Hide(E&e,State&s){ //
    if(s.visible){Draw(e,s);s.visible=0;}
    s.hides=(s.hides+1)&65535;if(e.Accessibility()&&s.hides==1)e.Notify(0x8003,s.owner);
}
template<class E> void Show(E&e,State&s){ //
    const bool becoming_visible=s.hides==1;
    bool redraw=s.hides==0?s.visible<1:((s.hides=(s.hides-1)&65535)<1);
    if(!redraw)return;s.visible=s.phase;
    if(s.visible)Draw(e,s);
    if(e.Accessibility()&&becoming_visible)e.Notify(0x8002,s.owner);
}
template<class E> void Destroy(E&e,State&s){ //
    Hide(e,s);e.KillTimer(s.owner);if(e.Accessibility())e.Notify(0x8001,s.owner);
    s.timer=0;s.owner=0;if(s.bitmap>1)e.BitmapReference(s.bitmap,false);s.bitmap=0;s.hides=0;
}
template<class E> U Create(E&e,State&s,U owner,U bitmap,U width,U height){ //
    if(!e.CanCreate(owner))return 0;if(s.owner)Destroy(e,s);
    s.owner=owner;s.phase=1;s.hides=1;s.visible=0;width&=65535;height&=65535;
    if(!width)width=e.BorderWidth();if(!height)height=e.BorderHeight();s.bitmap=bitmap;
    if(bitmap>1){e.BitmapReference(bitmap,true);e.BitmapSize(bitmap,width,height);}
    s.width=width&65535;s.height=height&65535;s.timer=e.StartTimer(owner);
    if(e.Accessibility())e.Notify(0x8000,owner);return 1;
}
template<class E> U Position(E&e,State&s,U x,U y){ //
    if(!Valid(e,s,0))return 0;x&=65535;y&=65535;if(s.x==x&&s.y==y)return 1;
    if(s.visible)Draw(e,s);s.x=x;s.y=y;e.KillTimer(s.owner);s.timer=e.StartTimer(s.owner);
    s.phase=1;s.visible=s.hides<1;if(s.visible)Draw(e,s);
    if(e.Accessibility())e.Notify(0x800b,s.owner);return 1;
}
template<class E> void Tick(E&e,State&s,U owner){ //
    if(!s.owner||s.owner!=owner)return;s.phase^=1;if(!s.hides){s.visible^=1;Draw(e,s);}
}
template<class E> U Blink(E&e,State&s,U&interval,U value){ //
    interval=value&65535;if(s.owner){e.KillTimer(s.owner);s.timer=e.StartTimer(s.owner);}return 1;
}
} // namespace Win98Caret
