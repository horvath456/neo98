// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98ComboMouse {
using U=unsigned;
// USER. Provider calls retain the original order. Window header
// byte+2Eh bit04h and Win40 bit40h are separate from private ComboBox flags.
template<class E> bool Close(E&e,U&flags,bool notify,bool accept){
 if(notify&&e.Header04()&&((flags&2)||accept)){
  e.Notify(accept?9:10);if(!e.Alive())return false;
 }
 if(flags&2){
  e.EndList(accept);
  if(flags&0x80){
   const U position=e.Header04()?0x16:0x36;
   flags&=~0x80u;e.HideList();
   if(!(flags&1))e.InvalidateSelection();
   e.Position(position);e.Update();
   if(flags&1)e.SyncEdit();
   if(notify){e.Notify(8);if(!e.Alive())return false;}
  }
 }
 return true;
}
// USER. List selection and cancellation share the close/notify
// order; an internal synchronization flag suppresses exactly one close.
template<class E> U ListCommand(E&e,U&flags,U code){
 code&=65535;
 if(code==1||code==3){
  if(flags&0x400)flags&=~0x400u;
  else if(!Close(e,flags,true,true))return 0;
  e.Notify(1);if(e.Alive())e.SelectionChanged();
 }else if(code==2){e.Notify(2);e.Alive();}
 else if(code==65534){e.Notify(65535);e.Alive();}
 return 0;
}
// USER. Opening mouse-up ends the button capture and starts
// the list's tracking phase. A plain release does not close the dropdown.
template<class E> void ReleaseButton(E&e,U&flags){
 e.Pressed(false);
 if(!(flags&0x20))return;
 flags&=~0x20u;
 if((flags&3)==3){e.MatchEdit();e.SyncEdit();}
 e.ReleaseCapture();if(e.Win40())e.BeginList(false,0);
}
// USER. This shared termination core also covers the original
// multiple-selection flags; native ComboBox lists use the single-select case.
struct ListState{U flags=0;int saved=-1;};
template<class E> void EndTracking(E&e,ListState&s,U reason){
 if((s.flags&0x60)==0x40)e.FinishRange();
 if(e.IsCombo()&&(reason&2)){
  const int old=e.Selection();if(old>=0)e.PaintSelection(false);
  e.RestoreSelection(s.saved);e.PaintSelection(true);if(e.AccessibilityEnabled())e.AccessibilitySelection();
  if(!(reason&4)&&(s.flags&0x100)&&e.Selection()!=old)e.ListNotify(1);
 }
 e.KillScrollTimer();s.flags&=~0x200u;
 if(s.flags&0x400){s.flags&=~0x400u;if(reason&1)e.ReleaseCapture();}
 if(e.Caret()<e.First()||e.First()+e.Page()<e.Caret())e.EnsureCaret();
 if(!(s.flags&0x100))return;
 if(!(reason&4)){if(!(reason&0x10)||e.Selection()==s.saved)return;}
 else{
  if(!(reason&8)){e.ListNotify(3);return;}
  if((s.flags&0x800)&&!e.Header04())e.ListNotify(1);
  if(s.flags&0x800){e.ListNotify(2);return;}
 }
 e.ListNotify(1);
}
} // namespace Win98ComboMouse
