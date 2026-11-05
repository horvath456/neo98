// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_gdi_pixel.h"
using namespace Win98GdiPixel;
struct Input{State s;int mx,my;U clip,physical,color_result,raw,record,tagged,tag_original,aux,palette_result,logical_valid,logical_value,system,managed,forward,reverse,trace[256],count;};
struct Provider{Input&i;void Event(U op,U a=0,U b=0,U c=0){i.trace[i.count++]=op;i.trace[i.count++]=a;i.trace[i.count++]=b;i.trace[i.count++]=c;}
 void Update(){Event(1);}U Record(U n){Event(2,n);return i.record;}Point Map(Point p){Event(3,U(p.x),U(p.y));return {i.mx,i.my};}bool Clip(Point p){Event(4,U(p.x),U(p.y));return i.clip;}
 U Tagged(U v,U&o){Event(5,v);o=i.tag_original;return i.tagged;}U ForwardColor(U v){Event(6,v);return i.forward;}U ReverseColor(U v){Event(7,v);return i.reverse;}
 U ManagedColor(U v,U&p){Event(8,v);p=i.physical;return i.managed;}U PrepareDevice(){return i.aux;}void RestoreDevice(){}
 U ColorInfo(U v,U&p,bool write){Event(write?9:10,v);if(write)p=i.physical;return i.color_result;}
 U Pixel(Point p,U v,bool write){Event(write?11:12,U(p.x),U(p.y),v);return i.raw;}void Dirty(Point p){Event(13,U(p.x),U(p.y));}
 U PaletteColor(U v){Event(14,v);return i.palette_result;}bool LogicalColor(U,U&v){v=i.logical_value;return i.logical_valid;}U SystemColor(U){return i.system;}
};
extern "C" U pixel_core(Input*i,U operation,int x,int y,U color){Provider e{*i};if(operation==0)return Get(e,i->s,{x,y});if(operation==1)return Set(e,i->s,{x,y},color);if(operation==2)return Nearest(e,i->s,color);U physical=0;U result=Resolve(e,i->s,color,physical);return result;}
