// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Original Win98 window regions. USER and are the validation
// entries behind USER32 SetWindowRgn/GetWindowRgn, is the SetWindowRgn
// body, the region step of the window-position apply loop (reached from
// ) and the GetWindowRgn body. Window objects, GDI regions, the
// DeferWindowPos machinery, the current task and the parameter-validation gate
// are explicit providers.
namespace Win98WindowRegion {
using U=unsigned;
inline U W(U x){return x&65535u;}
inline int S(U x){return short(x);}
// Modeled fields of the original WND: region WORD at +54h, state bytes at +33h
// and +39h, window rectangle origin WORDs at +10h/+12h and the owning task
// selector WORD at +20h.
struct Window{U region=0,task=0,state33=0,state39=0,left=0,top=0;};
// SWP_NOSIZE|NOMOVE|NOZORDER|NOACTIVATE|SWP_FRAMECHANGED|0800h|1000h.
constexpr U MoveFlags=0x1837,NoRedraw=8,CopyMode=5,RegionObject=6,ValidationError=0x6067,HandleError=0x6040;
// USER. The desktop and windows on a foreign message queue are
// rejected before any DeferWindowPos work. A null region becomes the entry
// sentinel 1, which the apply step reads as "remove".
template<class E>U SetBody(E&e,U w,U hrgn,U redraw){
 if(w==e.Desktop())return 0;
 if(e.Queue(e.CurrentTask())!=e.Queue(e.Read(w).task))return 0;
 const U defer=e.BeginDefer(1);if(!defer)return 0;
 if(!e.Defer(defer,w,0,0,0,0,0,MoveFlags|(W(redraw)?0:NoRedraw)))return 0;
 e.DeferRegion(defer,hrgn?hrgn:1);
 return e.EndDefer(defer,0);
}
// USER. GetWindowRgn's validates its destination
// unconditionally; SetWindowRgn skips the check for a null region.
template<class E>U Set(E&e,U hwnd,U hrgn,U redraw){
 const U w=e.Resolve(hwnd);if(!w){e.Error(hwnd,HandleError);return 0;}
 if(hrgn&&e.ObjectType(hrgn)!=RegionObject){e.Error(hrgn,ValidationError);return 0;}
 return W(SetBody(e,w,hrgn,redraw));
}
// USER. Only a non-zero entry field reaches this step.
// A borrowed region (state39 bit 2) is released, not deleted; value 2 keeps an
// owned region in place and otherwise borrows the class region.
template<class E>void Apply(E&e,U w,U value){
 value=W(value);auto s=e.Read(w);
 if(s.region){
  if(s.state39&4){s.state39&=~4u;e.Write(w,s);}
  else{if(value==2)return;e.Delete(s.region);}
  s=e.Read(w);s.region=0;e.Write(w,s);
 }
 if(value<=1)return;
 if(value==2){
  s=e.Read(w);
  if(!(s.state33&1)||!(s.state39&2))return;
  const U record=e.ClassRegion(w);if(!record)return;
  value=W(e.BorrowedRegion(record));
  s=e.Read(w);s.state39|=4;e.Write(w,s);
 }else{
  s=e.Read(w);
  if(w!=e.Desktop())e.Offset(value,s.left,s.top);
  e.SetOwner(value,e.UserInstance());
  e.MakePrivate(value,1);
 }
 s=e.Read(w);s.region=value;e.Write(w,s);
}
// USER. The stored region is in screen coordinates; the public
// result is shifted back by the window origin. A borrowed region is invisible
// here. The desktop without its own region answers with the global screen
// region at ds:1112h. The desktop *with* a region returns the uninitialized
// local at bp-2: that original value is an explicit provider.
template<class E>U GetBody(E&e,U w,U hrgn){
 const auto s=e.Read(w);
 if(s.region&&!(s.state39&4)){
  if(!e.Combine(hrgn,s.region,0,CopyMode))return 0;
  U result=e.Undefined();
  if(w!=e.Desktop())result=e.Offset(hrgn,W(0u-s.left),W(0u-s.top));
  return W(result);
 }
 if(w==e.Desktop())return W(e.Combine(hrgn,e.DesktopRegion(),0,CopyMode));
 return 0;
}
// USER.
template<class E>U Get(E&e,U hwnd,U hrgn){
 const U w=e.Resolve(hwnd);if(!w){e.Error(hwnd,HandleError);return 0;}
 if(e.ObjectType(hrgn)!=RegionObject){e.Error(hrgn,ValidationError);return 0;}
 return GetBody(e,w,hrgn);
}
// USER32 and narrow every argument to a WORD and sign-extend
// the WORD result with CWDE.
inline int Result(U ax){return S(ax);}
template<class E>int SetPublic(E&e,U hwnd,U hrgn,U redraw){return Result(Set(e,W(hwnd),W(hrgn),W(redraw)));}
template<class E>int GetPublic(E&e,U hwnd,U hrgn){return Result(Get(e,W(hwnd),W(hrgn)));}
}
