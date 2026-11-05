// SPDX-License-Identifier: GPL-2.0-or-later
// Ordinary modal controller decisions from COMCTL32 /.
#pragma once
#include "win98_property_sheet.h"
namespace Win98PropertySheet {
// positive SETACTIVE results identify a page template, -1 requests
// the next index. The original allows count as the intermediate next index.
template<class E> U NextPage(E&e,U state,U current,U name,U direction){
 if(!name){U next=current+direction;return next>e.Read(state+0x1c)?~0u:next;}
 // The provider names the template the page was made from (a 16-bit page
 // keeps the identifier of its own module, not the converted copy).
 for(U i=0;i<e.Read(state+0x1c);++i)if(e.PageTemplateName(e.Read(e.Read(state+0x24)+4*i))==name)return i;
 return ~0u;
}
// no KILLACTIVE notification exists before a page has been shown.
template<class E> U KillActive(E&e,U state){return e.Read(state+0x38)?Notify(e,state,e.Read(state+0x40),~200u,0):0;}
// /..389 ordinary-sheet branch; E owns actual page windows.
template<class E> bool Activate(E&e,U state,U direction){
 if(!e.Alive()||!e.Read(state+0x1c))return false;
 U attempts=1;
 while(attempts<=e.Read(state+0x1c)&&e.Alive()){
  U index=e.Selection();if(static_cast<int>(index)<0||index>=e.Read(state+0x1c))return false;
  U hwnd=e.PageWindow(index);
  if(!hwnd){hwnd=e.CreatePageWindow(index);if(!e.Alive())return false;
   if(!hwnd){e.RemovePage(index);if(!e.Alive())return false;e.Select(0);++attempts;continue;}}
  e.PositionPage(hwnd);U result=Notify(e,state,index,~199u,0);if(!e.Alive())return false;
  if(result){U next=NextPage(e,state,index,result==~0u?0:result,direction);
   if(result==~0u&&(next==index||next>=e.Read(state+0x1c)))next=e.Read(state+0x40);
   if(next!=~0u){e.Select(next);e.ShowPage(hwnd,false);++attempts;continue;}}
  U page=e.Read(e.Read(state+0x24)+index*4);
  if(e.Read(state+8)&0x200)e.EnableHelp((e.Read(page+4)&0x20)!=0);
  // a wizard takes the title of the page's tab item as its caption.
  if(e.Read(state+8)&0x1402020)e.WizardTitle(index);
  e.Write(state+0x60,e.Read(state+0x60)|1);e.ShowPage(hwnd,true);
  U previous=e.Read(state+0x38);if(previous&&previous!=hwnd)e.ShowPage(previous,false);
  if(!e.Alive())return false;
  e.Write(state+0x60,e.Read(state+0x60)&~1u);e.Write(state+0x38,hwnd);e.Write(state+0x40,index);e.FocusPage(hwnd);return true;
 }return false;
}
// /..57D. IDOK=1, Cancel=2, Close=8, Apply=0x3021.
// Pages are notified only after real creation. Error2 preserves the selection;
// other nonzero results select the failing page before reactivation.
template<class E> bool Apply(E&e,U state,U command){
 bool close=false,parameter=false;U code=~201u,result=e.Read(state+0x48);
 if(command==1){close=true;parameter=true;}
 else if(command==2||command==8){close=true;parameter=command==8;code=~202u;}
 else if(command!=0x3021)return false;
 if(code==~201u){if(Notify(e,state,e.Read(state+0x40),~200u,0)||!e.Alive())return false;result=1;}
 U failure=0,index=0;bool ok=true;
 for(;index<e.Read(state+0x1c)&&e.Alive();++index){if(!e.PageWindow(index))continue;
  failure=Notify(e,state,index,code,parameter);if(!e.Alive())return false;
  if(failure){ok=false;close=false;break;}
  if(code==~201u&&!close&&e.Read(state+0x4c))close=true;e.SetChanged(index,false);
 }
 if(ok)e.Write(state+0x38,0);else if(failure!=2)e.Select(index);
 if(ok)e.Write(state+0x48,result);
 if(!close){if(ok)e.Applied();if(failure!=2)e.ActivateCurrent(state);}
 return ok;
}
}
