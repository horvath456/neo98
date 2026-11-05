// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_menu_geometry.h"
namespace Win98MenuPlacement {
using Win98Menu::W;using Win98Menu::S;using Win98MenuGeometry::Rect;
struct Point {S x,y;};
inline bool Intersection(Rect&a,const Rect&b){a.left=a.left>b.left?a.left:b.left;a.top=a.top>b.top?a.top:b.top;a.right=a.right<b.right?a.right:b.right;a.bottom=a.bottom<b.bottom?a.bottom:b.bottom;if(a.left>=a.right||a.top>=a.bottom){a={};return false;}return true;}
// USER. Each directional candidate checks its moved axis before
// testing overlap. WORD sums deliberately retain original signed overflow.
inline bool Candidate(const Rect&work,const Rect&exclude,S width,S height,Point desired,W direction,Point&out){
 switch(direction){case 0:desired.x=S(exclude.left-width);if(desired.x<work.left)return false;break;
 case 1:desired.y=S(exclude.top-height);if(desired.y<work.top)return false;break;
 case 2:desired.x=exclude.right;if(S(desired.x+width)>work.right)return false;break;
 case 3:desired.y=exclude.bottom;if(S(desired.y+height)>work.bottom)return false;break;}
 out=desired;Rect area{desired.x,desired.y,S(desired.x+width),S(desired.y+height)};return !Intersection(area,exclude);
}
// USER, work-area and exclusion placement after alignment.
inline Point Place(const Rect&work,Point position,S width,S height,const Rect*avoid,W flags,W&animation){
 Point original=position;Rect exclude=avoid?*avoid:Rect{position.x,position.y,position.x,position.y};if(avoid)Intersection(exclude,work);
 if(S(position.x+width)>work.right){if(!(flags&4)&&S(position.x-width)>=work.left)position.x=S(position.x-width);else position.x=S(work.right-width);if(animation&0xc00)animation=W((animation&~0xc00)^0x800);}
 if(position.x<work.left){if(!(flags&4)&&S(position.x+width)<=work.right)position.x=S(position.x+width);else position.x=work.left;if(animation&0xc00)animation=W((animation&~0xc00)^0x400);}
 if(S(position.y+height)>work.bottom){if(!(flags&0x10)&&S(position.y-height)>=work.top)position.y=S(position.y-height);else position.y=S(work.bottom-height);if(animation&0x3000)animation=W((animation&~0x3000)^0x2000);}
 if(position.y<work.top){if(!(flags&0x10)&&S(position.y+height)<=work.bottom)position.y=S(position.y+height);else position.y=work.top;if(animation&0x3000)animation=W((animation&~0x3000)^0x1000);}
 Point candidate=position;if(!Candidate(work,exclude,width,height,position,4,candidate)){
  W directions[4];W base=(flags&0x40)?2:0;directions[base]=(flags&8)?0:2;directions[base+1]=W(2-directions[base]);base=W(2-base);directions[base]=(flags&0x20)?1:3;directions[base+1]=W(4-directions[base]);
  for(W n=0;n<4;++n)if(Candidate(work,exclude,width,height,position,directions[n],candidate)){position=candidate;const W animations[]={0x800,0x2000,0x400,0x1000};animation=animations[directions[n]];break;}
 }
 if(flags&2){if(animation&0xc00){if(original.y==position.y)animation|=S(position.y+height)<work.bottom?0x1000:0x2000;}
  else if(original.x==position.x)animation|=S(position.x+width)<work.right?0x400:0x800;}
 return position;
}
// USER performs alignment before Place.
inline Point Align(Point p,S width,S height,W flags,W tracking,W&animation){
 if(flags&8){p.x=S(p.x-width);animation=0x800;}else if(flags&4)p.x=S(p.x-width/2);else animation=(tracking&0x10)?0x800:0x400;
 if(flags&0x20){p.y=S(p.y-height);animation|=0x2000;}else if(flags&0x10)p.y=S(p.y-height/2);else animation|=0x1000;
 if(flags&0x3c00)animation=flags&0x3c00;return p;
}
}
