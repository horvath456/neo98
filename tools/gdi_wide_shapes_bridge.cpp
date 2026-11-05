// SPDX-License-Identifier: GPL-2.0-or-later
#include <initializer_list>
#include "../src/win98_gdi_shapes.h"
using namespace Win98GdiShapes;
extern "C" U wide_shapes(U ellipse,const int *a,int *out) {
  if(ellipse) {
    Point p[8192]; U inner=0,n=WideEllipseContours(p,8192,{a[0],a[1],a[2],a[3]},a[4],inner);
    for(U i=0;i<n;++i) {out[2*i]=p[i].x;out[2*i+1]=p[i].y;}
    return 2*n;
  }
  struct Sink {
    int *out; U used=0;
    void Fill(Rect r) {for(int v:{1,r.left,r.top,r.right,r.bottom}) out[used++]=v;}
    void PenSpan(int y,int l,int r) {if(l!=r) for(int v:{3,y,l,r}) out[used++]=v;}
  } sink{out};
  WideRectangle(sink,{a[0],a[1],a[2],a[3]},a[4],true);
  return sink.used;
}
