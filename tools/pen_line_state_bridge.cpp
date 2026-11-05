// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_pen_line.h"
#include <cstring>
extern "C" unsigned pen_line_state(const unsigned*a,unsigned*out){using namespace Win98PenLine;memset(out,0,9*4);Point current{Word(a[1]),Word(a[2])};U mode=a[3]&65535,result=0;if(a[0]==0)result=SetRop(mode,a[4]);else if(a[0]==1){Point before=Move(current,{int(a[4]),int(a[5])});out[3]=U(before.x)&65535;out[4]=U(before.y)&65535;result=1;}else{struct Provider{unsigned*out;void Draw(const Point*p,unsigned){for(unsigned i=0;i<2;++i){out[5+i*2]=U(p[i].x)&65535;out[6+i*2]=U(p[i].y)&65535;}}}e{out};result=Line(e,current,{int(a[4]),int(a[5])});}out[0]=U(current.x)&65535;out[1]=U(current.y)&65535;out[2]=mode;return result;}
