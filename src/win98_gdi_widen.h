// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_gdi_path.h"
// Stroke outlines for flattened paths. The caller owns the resulting path,
// so a failed allocation never replaces the original with a partial outline.
namespace Win98GdiWiden {
using Point = Win98GdiPath::Point;
struct Vector { double x, y; };
inline double Abs(double v) { return v < 0 ? -v : v; }
inline Vector Unit(Vector v)
{
    const double square = v.x*v.x + v.y*v.y;
    if (!square) return {0,0};
    double length = Abs(v.x) + Abs(v.y);
    for (unsigned i=0;i<8;++i) length = (length + square/length)*.5;
    return {v.x/length,v.y/length};
}
inline Vector Direction(Point a, Point b) { return Unit({double(b.x)-a.x,double(b.y)-a.y}); }
inline Vector Normal(Vector v) { return {-v.y,v.x}; }
inline int Round(double v) {
    if(v>=2147483646.5)return 2147483647;
    if(v<=-2147483648.0)return (-2147483647-1);
    const double n=v+.5;const int i=int(n);return n<i?i-1:i;
}
template<class Emit> bool Offset(Emit &emit, Point p, Vector v, double radius)
{ return emit(Point{Round(p.x+v.x*radius),Round(p.y+v.y*radius)},false); }
// Subdivide a minor circular arc without requiring a platform math library.
template<class Emit> bool Arc(Emit &emit, Point p, Vector a, Vector b, double r, unsigned depth=0)
{
    const double dot=a.x*b.x+a.y*b.y;
    if (depth<8 && dot<.70710678)
    {
        const Vector middle=Unit({a.x+b.x,a.y+b.y});
        if (!Arc(emit,p,a,middle,r,depth+1)) return false;
        return Arc(emit,p,middle,b,r,depth+1);
    }
    return Offset(emit,p,b,r);
}
template<class Emit> bool Join(Emit &emit,Point p,Vector before,Vector after,double r,unsigned flags,double limit)
{
    const Vector a=Normal(before), b=Normal(after);
    if (!Offset(emit,p,a,r)) return false;
    const double cross=before.x*after.y-before.y*after.x;
    const double dot=before.x*after.x+before.y*after.y;
    if (cross>0)
    {
        // The inner contour returns through the vertex; this also handles
        // a short segment whose two offset intersections would cross.
        if (!emit(p,false)) return false;
    }
    else if (flags&16)
    {
        const double denominator=1+dot;
        if (denominator>1e-9)
        {
            const Vector m={(a.x+b.x)/denominator,(a.y+b.y)/denominator};
            if (m.x*m.x+m.y*m.y<=limit*limit && !Offset(emit,p,m,r)) return false;
        }
    }
    else if ((flags&32) && dot>-.999999)
        return Arc(emit,p,a,b,r);
    return Offset(emit,p,b,r);
}
template<class Emit> bool Cap(Emit &emit,Point p,Vector direction,double r,unsigned flags)
{
    const Vector a=Normal(direction), b={-a.x,-a.y};
    if (flags&4)
    {
        if (!Offset(emit,p,{a.x+direction.x,a.y+direction.y},r) ||
            !Offset(emit,p,{b.x+direction.x,b.y+direction.y},r)) return false;
    }
    else if (flags&2)
    {
        if (r<1) return Offset(emit,p,b,r);
        if (!Arc(emit,p,a,direction,r) || !Arc(emit,p,direction,b,r)) return false;
    }
    return Offset(emit,p,b,r);
}
template<class Emit> bool Figure(const Point *points,unsigned count,bool closed,double radius,unsigned flags,double limit,Emit &emit)
{
    if(count<2)return true;
    if(closed && points[count-1].x==points[0].x && points[count-1].y==points[0].y)--count;
    if(count<2)return true;
    if(closed)
    {
        for(unsigned side=0;side<2;++side)
        {
            const unsigned first=side?count-1:0;
            const unsigned prev=side?0:count-1;
            Vector d=Direction(points[prev],points[first]);
            const Vector n=Normal(d);
            if(!emit(Point{Round(points[first].x+n.x*radius),Round(points[first].y+n.y*radius)},true))return false;
            for(unsigned j=0;j<count;++j)
            {
                const unsigned i=side?count-1-j:j;
                const unsigned next=side?(i?i-1:count-1):(i+1)%count;
                const Vector after=Direction(points[i],points[next]);
                if(!Join(emit,points[i],d,after,radius,flags,limit))return false;
                d=after;
            }
        }
        return true;
    }
    Vector d=Direction(points[0],points[1]);
    const Vector n=Normal(d);
    if(!emit(Point{Round(points[0].x+n.x*radius),Round(points[0].y+n.y*radius)},true))return false;
    for(unsigned i=1;i+1<count;++i)
    {
        Vector after=Direction(points[i],points[i+1]);
        if(!Join(emit,points[i],d,after,radius,flags,limit))return false;
        d=after;
    }
    if(!Offset(emit,points[count-1],Normal(d),radius) || !Cap(emit,points[count-1],d,radius,flags))return false;
    d={-d.x,-d.y};
    for(unsigned i=count-1;i>1;--i)
    {
        const Vector after=Direction(points[i-1],points[i-2]);
        if(!Join(emit,points[i-1],d,after,radius,flags,limit))return false;
        d=after;
    }
    return Offset(emit,points[0],Normal(d),radius) && Cap(emit,points[0],d,radius,flags);
}
}
