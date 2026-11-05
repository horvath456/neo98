// SPDX-License-Identifier: GPL-2.0-or-later
// Windows 98 CBT-hook event decisions.
#pragma once
namespace Win98CbtHooks {
using U=unsigned;
enum Code {MoveSize=0,MinMax=1,Create=3,Destroy=4,Activate=5,Focus=9};
// USER,1A48; USER,97F4,98A6; USER test AX.
// The interactive move/size producer USER instead tests DX:AX.
inline bool Veto(unsigned code,U result){return code==MoveSize?result!=0:(result&65535)!=0;}
// USER jump table, 5C8E..5CA9. Visibility-only requests skip the
// min/max routine; normal/noactivate/restore enter it only for iconic/zoomed windows.
inline bool MinMaxRequest(U style,U command){
 return command==2||command==3||command==6||command==7||
        ((command==1||command==4||command==9)&&(style&0x21000000));
}
// Native Win32 callback ABI records. The original segmented records are
// converted by USER's callback gate; pointer/handle translation is a provider.
struct CreateInfo {U create,after;};
struct ActivateInfo {U mouse,previous;};
}
namespace Win98CbtHooks {
struct DestroyEnvironment {
 virtual bool Valid(U)=0;virtual U Flags(U)=0;virtual void Mark(U,U)=0;
 virtual U First(U)=0;virtual U Next(U)=0;virtual U DetachFirst(U)=0;
 virtual void Send(U,U)=0;virtual void Free(U)=0;
 // USER; standalone tree-order providers may have no input queue.
 virtual void ClearFocus(U) {}
};
// USER: parent WM_DESTROY precedes all descendant messages;
// callbacks may remove a child, in which case restart from the live first child.
inline void DestroyMessages(DestroyEnvironment&e,U h){
 e.ClearFocus(h);
 e.Send(h,2);
 while(e.Valid(h)){
  U child=e.First(h);
  while(child){
   if(!(e.Flags(child)&0x20)){
    e.Mark(child,0x20);DestroyMessages(e,child);
    if(!e.Valid(child))break;
   }
   child=e.Next(child);
  }
  if(!child){e.ClearFocus(h);break;}
 }
}
// USER. Unlink each child before descending, then notify the
// parent. Resource disposal after WM_NCDESTROY is supplied by the native port.
inline void DestroyNonClient(DestroyEnvironment&e,U h){
 if(!e.Valid(h))return;
 e.Mark(h,0x10);
 while(U child=e.DetachFirst(h))DestroyNonClient(e,child);
 e.Send(h,0x82);
 if(e.Valid(h))e.Free(h);
}
}
