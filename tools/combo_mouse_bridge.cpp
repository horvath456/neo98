/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/win98_combo_mouse.h"
struct E{unsigned flags,header,kill,alive=1,*out,count=0;void Event(unsigned op,unsigned value=0){out[3+count++]=op;out[3+count++]=value;}
 bool Header04(){return header&4;}bool Win40(){return header&64;}bool Alive(){Event(2,alive);return alive;}
 void Notify(unsigned code){Event(1,code);if(code==kill)alive=0;}
 void EndList(bool accept){Event(3,accept);}void HideList(){Event(4);}void InvalidateSelection(){Event(5);}
 void Position(unsigned f){Event(6,f);}void Update(){Event(7);}void SyncEdit(){Event(8);}void SelectionChanged(){Event(9);}
 void Pressed(bool p){Event(10,p);flags=p?flags|64:flags&~64u;}void ReleaseCapture(){Event(11);}void BeginList(bool p,unsigned){Event(12,p);}void MatchEdit(){Event(13);}
};
extern "C" void combo_mouse(unsigned op,unsigned flags,unsigned header,unsigned first,unsigned second,unsigned kill,unsigned*out){E e{flags,header,kill,1,out};unsigned result=0;if(op==0)result=Win98ComboMouse::Close(e,e.flags,first,second);if(op==1)result=Win98ComboMouse::ListCommand(e,e.flags,first);if(op==2)Win98ComboMouse::ReleaseButton(e,e.flags);out[0]=result;out[1]=e.flags;out[2]=e.count;out[3+e.count]=e.alive;}
struct L{int selection,saved,caret,first;unsigned combo,header,access,*out,count=0;void Event(unsigned o,unsigned v=0){out[3+count++]=o;out[3+count++]=v;}void FinishRange(){Event(14);}bool IsCombo(){return combo;}int Selection(){return selection;}void PaintSelection(bool value){Event(15,value);}void RestoreSelection(int value){selection=value;}bool AccessibilityEnabled(){return access;}void AccessibilitySelection(){Event(16);}void ListNotify(unsigned code){Event(1,code);}void KillScrollTimer(){Event(17);}void ReleaseCapture(){Event(11);}int Caret(){return caret;}int First(){return first;}int Page(){Event(18);return 5;}void EnsureCaret(){Event(19);}bool Header04(){return header&4;}};
extern "C" void combo_end_list(unsigned flags,unsigned reason,int selection,int saved,int caret,int first,unsigned combo,unsigned header,unsigned access,unsigned*out){L e{selection,saved,caret,first,combo,header,access,out};Win98ComboMouse::ListState s{flags,saved};Win98ComboMouse::EndTracking(e,s,reason);out[0]=unsigned(e.selection);out[1]=s.flags;out[2]=e.count;}
