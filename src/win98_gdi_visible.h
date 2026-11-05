// SPDX-License-Identifier: GPL-2.0-or-later
// RectVisible and PtVisible against a device context's effective clip region.
#pragma once
namespace Win98GdiVisible {
using U=unsigned;
struct Rect{int left,top,right,bottom;};
// GDI32. The caller's own rectangle is never written.
inline void Normalize(Rect&r){
 if(r.left>r.right){int t=r.left;r.left=r.right;r.right=t;}
 if(r.top>r.bottom){int t=r.top;r.top=r.bottom;r.bottom=t;}}
// GDI, shared by both entries and by the other DC queries.
inline bool DcGate(U signature){return ((signature&0x5fff)-0x4f4d&65535)<=3;}
// GDI. Windows 3.0 applications get an inclusive right and
// bottom edge; 3.1 and every Win32 caller keep the exclusive edge.
inline bool Inclusive(U expected_version){return expected_version<=0x300;}
inline Rect Edges(Rect r,U expected_version){
 if(Inclusive(expected_version)){++r.right;++r.bottom;}
 return r;}
// GDI normalizes the transformed rectangle a second time before the
// band walk, so a mapping mode that mirrors an axis still queries a valid box.
inline Rect Prepare(Rect device,U expected_version){Normalize(device);return Edges(device,expected_version);}
// The band enumerator answers an intersection test. E supplies Bands and
// Band(i); an empty region and an empty rectangle are both invisible.
template<class E>bool Visible(E&e,Rect r){
 if(r.left>=r.right||r.top>=r.bottom)return false;
 for(U i=0,n=e.Bands();i<n;++i){
  const Rect b=e.Band(i);
  if(r.left<b.right&&b.left<r.right&&r.top<b.bottom&&b.top<r.bottom)return true;}
 return false;}
// GDI.103 PTVISIBLE tests a single device point against the same region.
template<class E>bool Point(E&e,int x,int y){
 for(U i=0,n=e.Bands();i<n;++i){
  const Rect b=e.Band(i);
  if(x>=b.left&&x<b.right&&y>=b.top&&y<b.bottom)return true;}
 return false;}
}
