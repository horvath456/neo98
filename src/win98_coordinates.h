// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98Coordinates {
using U=unsigned;
struct Export{const char*name;U ordinal,argc;};
inline constexpr Export Exports[]={{"ClientToScreen",61,2},{"GetClientRect",235,2},{"GetWindowRect",339,2},{"MapWindowPoints",426,4},{"ScreenToClient",494,2}};
inline int S(U value){return short(value);}
struct Point{U x=0,y=0;};
struct Rect{int left=0,top=0,right=0,bottom=0;};
struct Window{Rect outer,client;U style=0,flags=0;};
// USER32. The desktop returns its validated object identity,
// including when the caller supplies a null point. Ordinary points stay DWORDs.
template<class E> U PointQuery(E&e,U hwnd,Point*point,bool to_screen){
 e.Lock();U object=e.Validate(hwnd),result=object;
 if(object&&object!=e.Desktop()){
  result=0;
  if(point){auto w=e.Read(object);U x=U(S(w.client.left)),y=U(S(w.client.top));
   if(to_screen){point->x+=x;point->y+=y;}else{point->x-=x;point->y-=y;}
   result=y|1;
  }
 }
 e.Unlock();return result;
}
// USER32. Signed WORD dimensions and signed version compare.
template<class E> U RectQuery(E&e,U hwnd,Rect*out,bool client){
 e.Lock();U object=e.Validate(hwnd),result=0;
 if(object&&out){auto w=e.Read(object);bool desktop=object==e.Desktop();
  bool screen=desktop&&S(e.Version())<=0x400;
  bool icon=client&&!desktop&&!(w.flags&0x40)&&(w.style&0x20000000);
  if(screen||icon){*out={0,0,S(e.Metric(icon?57:0)),S(e.Metric(icon?58:1))};result=U(out->bottom)|1;}
  else if(client){*out={0,0,S(U(w.client.right)-U(w.client.left)),S(U(w.client.bottom)-U(w.client.top))};result=U(out->bottom)|1;}
  else{*out={S(w.outer.left),S(w.outer.top),S(w.outer.right),S(w.outer.bottom)};result=1;}
 }
 e.Unlock();return result;
}
// USER. A null source/target means desktop. Difference and every
// transformed coordinate wrap as WORDs; USER32 packs then sign-expands points.
template<class E> U Map(E&e,U from,U to,Point*points,U count){
 U x=0,y=0;
 if(from&&from!=e.Desktop()){auto w=e.Read(from);x=U(w.client.left);y=U(w.client.top);}
 if(to&&to!=e.Desktop()){auto w=e.Read(to);x-=U(w.client.left);y-=U(w.client.top);}
 for(U i=0;i<count;++i){points[i].x=U(S(points[i].x+x));points[i].y=U(S(points[i].y+y));}
 return(x&65535)|((y&65535)<<16);
}
}
