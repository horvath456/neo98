// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_pen_line.h"
#include <cstring>
extern "C" unsigned pen_line_lifetime(const unsigned*a,unsigned*out){using namespace Win98PenLine;memset(out,0,8*4);if(a[0]==0){struct E{unsigned*out;void Diagnostic(){out[1]=1;}void Free(){out[0]=1;}}e{out};return Delete(e,a[1],a[2]);}
 U current=a[1]?0x3002:0x2002,old=a[3]&65535,fresh=a[4]&65535;struct E{U&old,&fresh;const U*a;U*out;bool Realize(U h){out[3]=h;return a[5];}void AddReference(U h){U&r=h==0x2002?old:fresh;r=Win98PenLine::AddReference(r);}void RemoveReference(U h){U&r=h==0x2002?old:fresh;r=Win98PenLine::RemoveReference(r);}}e{old,fresh,a,out};U result=Select(e,current,0x3002);out[0]=current;out[1]=old;out[2]=fresh;return result;}
