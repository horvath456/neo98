// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_message_box.h"
using namespace Win98MessageBox;
extern "C" U mb_run(const U*input,U*out){
 struct E {const U*input;U*out,used=0;
  U DefaultCaption(){return 0x01100abe;}bool DesktopReady(){return input[10]!=0;}
  U Capture(const Parameters&p,const Buttons&b,U kind){out[used++]=kind;
   const U fields[]={p.size,p.owner,p.instance,p.text,p.caption,p.style,p.icon,p.context,p.callback,p.language,b.selected,b.count,p.style&15};
   for(U x:fields)out[used++]=x;return input[kind==1?11:12];}
  U Dialog(const Parameters&p,const Buttons&b){return Capture(p,b,1);}
  U HardError(const Parameters&p,const Buttons&b){return Capture(p,b,2);}
 }e{input,out+1};
 Parameters p{input[0],input[1],input[2],input[3],input[4],input[5],input[6],input[7],input[8],input[9]};
 U result=Run(e,p);out[0]=e.used;return result;
}
extern "C" U mb_buttons(U style,U*out){Buttons b=SelectButtons(style);for(U i=0;i<b.count;++i){out[i*3]=b.ids[i];out[i*3+1]=b.labels[i];out[i*3+2]=i==b.selected;}return b.count;}
extern "C" U mb_command(U command,U present){return Command(command,present!=0);}
extern "C" U mb_icon(U style){return Icon(style);}
// USER, the box's own WM_INITDIALOG renumbering decision: which id
// it looks the button up under and which id it writes back.
extern "C" U mb_renumber(U style,U*out){out[0]=Lookup(style);out[1]=ActualButton(style,out[0]);return out[0]!=0;}
