// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_pen_line.h"
#include <cstring>
struct Provider {const unsigned*a;unsigned*events;unsigned AllocatePen(const Win98PenLine::Pen&p){unsigned values[]={p.style,p.width_x,p.width_y,p.color,p.flags};memcpy(events,values,sizeof values);return a[8];}};
extern "C" unsigned pen_line_objects(const unsigned*a,unsigned*events){Provider e{a,events};memset(events,0,20);if(a[0]==0)return Win98PenLine::CreateDirect(e,a[1],a[2],a[3]);if(a[0]==1)return Win98PenLine::Create(e,{a[1],a[2],a[3],a[4],0});return Win98PenLine::CreateExtended(e,a[1],a[2],a[3],a[4]);}
