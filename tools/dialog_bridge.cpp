// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_dialog.h"
extern "C" unsigned dialog_next(const Win98Dialog::Node *nodes,unsigned count,unsigned start,bool previous,bool group)
{
    struct View {const Win98Dialog::Node *nodes;unsigned count;} view{nodes,count};
    Win98Dialog::Tree tree{&view,[](void *p,unsigned h){auto &v=*static_cast<View*>(p);return h&&h<=v.count?v.nodes[h-1]:Win98Dialog::Node{};}};
    return group?Win98Dialog::NextGroup(tree,1,start,previous):Win98Dialog::NextTab(tree,1,start,previous);
}
extern "C" void dialog_rect(Gdi::RECT *r,int x,int y){Win98Dialog::MapRect(r,x,y);}
