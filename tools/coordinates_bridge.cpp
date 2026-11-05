// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_coordinates.h"
using namespace Win98Coordinates;
struct E{
 U*a;void Lock(){}void Unlock(){}
 U Validate(U h){return h==1?0x30002000:h==2?0x30004000:h==3?0x30003000:0;}
 U Desktop(){return 0x30004000;}U Version(){return a[15];}U Metric(U n){return a[n==0?16:n==1?17:n==57?18:19];}
 Window Read(U h){if(h==0x30003000)return {{},{S(a[20]),S(a[21]),0,0},0,0};return {{S(a[5]),S(a[6]),S(a[7]),S(a[8])},{S(a[9]),S(a[10]),S(a[11]),S(a[12])},a[13],a[14]};}
};
extern "C" U coordinates_run(U*a,U*out){E e{a};for(U i=0;i<6;++i)out[i]=a[24+i];
 if(a[0]<2)return PointQuery(e,a[1],a[2]?reinterpret_cast<Point*>(out):nullptr,a[0]==0);
 if(a[0]<4)return RectQuery(e,a[1],a[2]?reinterpret_cast<Rect*>(out):nullptr,a[0]==2);
 return Map(e,a[1]?e.Validate(a[1]):0,a[22]?e.Validate(a[22]):0,reinterpret_cast<Point*>(out),a[23]);
}
