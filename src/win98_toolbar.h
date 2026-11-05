// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// COMCTL32 5.80 toolbar object helpers. Work in progress: not runtime-connected.
namespace Win98Toolbar {
using U=unsigned;
// command -1 is never a valid lookup target.
template<class E> U CommandIndex(E&e,U toolbar,U command){
 if(!toolbar||command==0xffffffff)return 0xffffffff;
 const int count=int(e.Read32(toolbar+0x84));U item=e.Read32(toolbar+0x30)+4;
 for(int i=0;i<count;++i,item+=28)if(e.Read32(item)==command)return U(i);
 return 0xffffffff;
}
// The original zeros the caller-declared byte count, then
// writes twelve fixed bytes even for a smaller declared size.
template<class E> U ToPublic(E&e,U toolbar,U source,U output){
 const U bytes=e.Read32(toolbar+0x64);for(U i=0;i<bytes;++i)e.Write8(output+i,0);
 e.Write32(output,e.Read32(source));e.Write32(output+4,e.Read32(source+4));
 e.Write8(output+8,e.Read8(source+8));e.Write8(output+9,e.Read8(source+9));e.Write16(output+10,e.Read16(source+10));
 U result=source;
 if(bytes>=20){e.Write32(output+12,e.Read32(source+12));result=e.Read32(source+16);e.Write32(output+16,result);}
 return result; // EAX is observable even though the helper is internally void.
}
// public reserved bytes are not imported.
template<class E> U FromPublic(E&e,U toolbar,U output,U source){
 e.Write32(output,e.Read32(source));e.Write32(output+4,e.Read32(source+4));
 e.Write8(output+8,e.Read8(source+8));e.Write8(output+9,e.Read8(source+9));e.Write16(output+10,0);
 if(e.Read32(toolbar+0x64)>=20){e.Write32(output+12,e.Read32(source+12));e.Write32(output+16,e.Read32(source+16));}
 else {e.Write32(output+12,0);e.Write32(output+16,0xffffffff);}
 return output;
}
// the toolbar's LocalAlloc/LocalReAlloc/LocalFree wrapper.
template<class E> U ResizeAllocation(E&e,U old,U bytes){
 if(!bytes){if(old)e.FreeLocal(old);return 0;}
 return old?e.ReallocateLocal(old,bytes,0x42):e.AllocateLocal(0x40,bytes);
}
// Capture keeps its relative button offset
// when the allocation provider moves the array. Count is changed by insertion.
template<class E> U Reserve(E&e,U toolbar,U count){
 if(!toolbar||!e.Read32(toolbar+0x64))return 0;
 const U capture=e.Read32(toolbar+0x34),old=e.Read32(toolbar+0x30);
 const U next=ResizeAllocation(e,old,count*28);if(!next)return 0;
 if(capture)e.Write32(toolbar+0x34,capture+next-e.Read32(toolbar+0x30));
 e.Write32(toolbar+0x30,next);return 1;
}
// invalidate computed button rectangles without reallocating them.
template<class E> void Dirty(E&e,U toolbar){e.Write32(toolbar+0xec,(e.Read32(toolbar+0xec)&0xfffeffff)|0x10);}
// shared state transition for TB_SETSTATE and INFO.
template<class E> void SetState(E&e,U toolbar,U button,U state,U index){
 const U changed=(state^e.Read8(button+8))&255;e.Write8(button+8,state);if(!changed)return;
 const U flags=e.Read32(toolbar+0xec);if(flags&0x800){e.Write32(toolbar+0xec,flags|0x3000);return;}
 if(changed&8){e.Invalidate(e.Read32(toolbar),0,1);e.Layout(toolbar);}
 else e.InvalidateButton(toolbar,button,1);
 e.Accessibility(0x800a,e.Read32(toolbar),0xfffffffc,index+1);
}
// /: image-list and bitmap-group bounds precede mutation.
template<class E> U SetImage(E&e,U toolbar,U button,U image){
 U flags=e.Read32(toolbar+0xec);
 if(!(flags&2)){
  U count=0;
  if(flags&1){if(!e.ImageList(toolbar,0,0))return 0;count=e.ImageCount(e.ImageList(toolbar,0,0));}
  else{int n=int(e.Read32(toolbar+0x50));U p=e.Read32(toolbar+0x54);while(n-->0){count+=e.Read32(p);p+=12;}}
  if(int(image)>=int(count))return 0;
 }
 e.Write32(button,image);e.InvalidateButton(toolbar,button,e.Read32(toolbar+8)&0x8000);return 1;
}
// replacing a command also transfers its tooltip registration.
template<class E> void SetCommand(E&e,U toolbar,U button,U command){
 U old=e.Read32(button+4);e.Write32(button+4,command);const U tool=e.Read32(toolbar+0x40);if(!tool)return;
 typename E::Scratch info(e,44);e.Write32(info.address,44);e.Write32(info.address+8,e.Read32(toolbar));e.Write32(info.address+12,old);
 e.Tooltip(tool,0x435,info.address);e.Tooltip(e.Read32(toolbar+0x40),0x433,info.address);
 e.Write32(info.address+12,command);e.Tooltip(e.Read32(toolbar+0x40),0x432,info.address);
}
// By-index bounds are checked before any field copy.
template<class E> U GetInfo(E&e,U toolbar,U command,U info){
 if(e.Read32(info)!=32)return 0xffffffff;
 U index=(e.Read32(info+4)&0x80000000)?command:CommandIndex(e,toolbar,command);
 if(int(index)<0||int(index)>=int(e.Read32(toolbar+0x84)))return 0xffffffff;
 const U button=e.Read32(toolbar+0x30)+index*28,mask=e.Read32(info+4);
 if(mask&8)e.Write8(info+17,e.Read8(button+9));
 if(mask&4)e.Write8(info+16,e.Read8(button+8));
 if(mask&1)e.Write32(info+12,e.Read32(button));
 if(mask&64)e.Write16(info+18,e.Read16(button+10));
 if(mask&2){U text=e.Read32(button+16);if(text!=0xffffffff&&(text&0xffff0000))e.CopyText(e.Read32(info+24),text,e.Read32(info+28));}
 const U current=e.Read32(info+4);
 if(current&16)e.Write32(info+20,e.Read32(button+12));
 if(current&32)e.Write32(info+8,e.Read32(button+4));return index;
}
// The original only excludes index -1 on writes;
// the native public boundary must enforce allocation bounds before this core.
template<class E> U SetInfo(E&e,U toolbar,U command,U info){
 if(e.Read32(info)!=32)return 0;
 U index=(e.Read32(info+4)&0x80000000)?command:CommandIndex(e,toolbar,command);if(index==0xffffffff)return 0;
 const U button=e.Read32(toolbar+0x30)+index*28;bool full=false,changed=false;
 if(e.Read8(info+4)&8){U difference=e.Read8(button+9)^e.Read8(info+17);if(difference&0x88)full=true;if(difference&16)e.Write16(button+10,0);e.Write8(button+9,e.Read8(info+17));changed=true;}
 if(e.Read8(info+4)&4)SetState(e,toolbar,button,e.Read8(info+16),index);
 if(e.Read8(info+4)&1)SetImage(e,toolbar,button,e.Read32(info+12));
 if(e.Read8(info+4)&64){e.Write16(button+10,e.Read16(info+18));changed=full=true;}
 if(e.Read8(info+4)&2){
  U style=e.Read8(button+9);
  if((style&16)||((e.Read8(toolbar+0x24)&8)&&(e.Read8(toolbar+9)&16)&&!(style&1))){e.Write16(button+10,0);full=true;}
  e.Write32(toolbar+0xec,e.Read32(toolbar+0xec)|8);U text=e.Read32(button+16);
  if(text==0xffffffff||!(text&0xffff0000))e.Write32(button+16,0);
  e.SetString(button+16,e.Read32(info+24));changed=true;
 }
 if(e.Read8(info+4)&16)e.Write32(button+12,e.Read32(info+20));
 if(e.Read8(info+4)&32)SetCommand(e,toolbar,button,e.Read32(info+8));
 if(full||changed){Dirty(e,toolbar);if(full)e.Invalidate(e.Read32(toolbar),0,1);else e.InvalidateButton(toolbar,button,1);}
 return 1;
}

// /: A/W batch insertion shares one object.
template<class E> U Insert(E&e,U toolbar,U index,U count,U source,U wide){
 if(!Reserve(e,toolbar,count+e.Read32(toolbar+0x84)))return 0;
 U old_count=e.Read32(toolbar+0x84);if(index>old_count)index=old_count;
 U hot=0xffffffff;int h=int(e.Read32(toolbar+0xcc));
 if(h>=0&&h<int(old_count))hot=e.Read32(e.Read32(toolbar+0x30)+U(h)*28+4);
 U from=e.Read32(toolbar+0x30)+old_count*28-28,to=from+count*28,trailing=old_count-index;
 while(trailing){for(U i=0;i<28;i+=4)e.Write32(to+i,e.Read32(from+i));from-=28;to-=28;--trailing;}
 bool resize=false;
 if(e.Read32(toolbar+0x5c)||(e.Read8(toolbar+0xec)&8))
  resize=(e.Read8(toolbar+9)&16)||int(e.Read32(toolbar+0x98)+e.Read32(toolbar+0x6c)+e.BorderY())<int(e.Read32(toolbar+0x74));
 U input=source+(count-1)*e.Read32(toolbar+0x64);e.Write32(toolbar+0x84,e.Read32(toolbar+0x84)+count);
 for(U remaining=count;remaining;--remaining){
  FromPublic(e,toolbar,to,input);if(e.Read8(to+9)&1)e.Write32(to+16,0xffffffff);
  U text=e.Read32(to+16);
  if(text!=0xffffffff&&(text&0xffff0000)){
   U converted=wide?text:e.AnsiToWide(e.Read32(toolbar+0x14),text);e.Write32(to+16,0);e.SetString(to+16,converted);
   if(!wide&&converted&&converted!=0xffffffff)e.FreeLocal(converted);
   U flags=e.Read32(toolbar+0xec);if(!(flags&8))resize=true;e.Write32(toolbar+0xec,flags|8);
  }
  U tool=e.Read32(toolbar+0x40);
  if(tool&&!(e.Read8(input+9)&1)){
   typename E::Scratch info(e,44);e.Write32(info.address,44);e.Write32(info.address+4,0);e.Write32(info.address+8,e.Read32(toolbar));e.Write32(info.address+12,e.Read32(input+4));e.Write32(info.address+36,0xffffffff);e.Tooltip(tool,0x432,info.address);
  }
  if((e.Read8(to+9)&1)&&int(e.Read32(to))<=0)e.Write32(to,e.SeparatorWidth());
  to-=28;input-=e.Read32(toolbar+0x64);
 }
 e.Write32(toolbar+0xa4,0xffffffff);e.Write32(toolbar+0xa8,0xffffffff);
 if((e.Read8(toolbar+0x24)&2)||(e.Read8(toolbar+9)&2))e.Wrap(toolbar);
 Dirty(e,toolbar);if(resize&&!e.Read32(toolbar+0x48))e.Layout(toolbar);
 e.Accessibility(count==1?0x8000:0x8004,e.Read32(toolbar),0xfffffffc,count==1?trailing+1:0);
 if(hot!=0xffffffff)e.Write32(toolbar+0xcc,CommandIndex(e,toolbar,hot));
 Dirty(e,toolbar);e.Invalidate(e.Read32(toolbar),0,1);return 1;
}
// /: notification precedes freeing owned text.
template<class E> U Delete(E&e,U toolbar,U index){
 if(index>=e.Read32(toolbar+0x84))return 0;const U offset=index*28;
 if(e.Read32(toolbar+0x30)+offset==e.Read32(toolbar+0x34)){
  if(e.Read32(toolbar+0x64)==20)e.Write8(toolbar+0xed,e.Read8(toolbar+0xed)|0x40);
  if(!e.CancelCapture(toolbar))return 0;e.Write32(toolbar+0x34,0);
 }
 U hot=0xffffffff;int h=int(e.Read32(toolbar+0xcc));
 if(h>=0&&h<int(e.Read32(toolbar+0x84)))hot=e.Read32(e.Read32(toolbar+0x30)+U(h)*28+4);
 e.Accessibility(0x8001,e.Read32(toolbar),0xfffffffc,index+1);
 {
  typename E::Scratch note(e,60);for(U i=0;i<60;++i)e.Write8(note.address+i,0);
  U button=e.Read32(toolbar+0x30)+offset;e.Write32(note.address+12,e.Read32(button+4));ToPublic(e,toolbar,button,note.address+16);
  e.Notify(toolbar,0xfffffd35,note.address);
 }
 U button=e.Read32(toolbar+0x30)+offset,text=e.Read32(button+16);
 if(text!=0xffffffff&&(text&0xffff0000))e.SetString(button+16,0);
 if(U tool=e.Read32(toolbar+0x40)){
  typename E::Scratch info(e,44);e.Write32(info.address,44);e.Write32(info.address+8,e.Read32(toolbar));e.Write32(info.address+12,e.Read32(e.Read32(toolbar+0x30)+offset+4));e.Tooltip(tool,0x433,info.address);
 }
 button=e.Read32(toolbar+0x30)+offset;e.Write32(toolbar+0x84,e.Read32(toolbar+0x84)-1);
 U wrap=e.Read8(button+8)&0x20,next=button+28;
 while(index<e.Read32(toolbar+0x84)){
  wrap|=e.Read8(next+8)&0x20;for(U i=0;i<28;i+=4)e.Write32(button+i,e.Read32(next+i));++index;next+=28;button+=28;
 }
 U style=e.Read32(toolbar+8);e.Write32(toolbar+0xa4,0xffffffff);e.Write32(toolbar+0xa8,0xffffffff);
 if(((style&0x200)||(e.Read8(toolbar+0x24)&2))&&wrap){
  U window=e.Read32(toolbar);if(!(style&12))window=e.Parent(window);
  typename E::Scratch rect(e,16);e.ClientRect(window,rect.address);
  if(e.Read32(toolbar+8)&0x200)e.WrapWidth(toolbar,e.Read32(rect.address+8)-e.Read32(rect.address),rect.address,0);
  else e.WrapRows(toolbar,e.Read32(toolbar+0x104),rect.address,0);
 }
 if(hot!=0xffffffff)e.Write32(toolbar+0xcc,CommandIndex(e,toolbar,hot));
 e.Invalidate(e.Read32(toolbar),0,1);Dirty(e,toolbar);return 1;
}

// /BB66: ANSI INFO wrappers retain their original asymmetry.
// GET clears the text mask in-place and restores it only after successful lookup.
template<class E> U GetInfoA(E&e,U toolbar,U command,U info){
 U mask=e.Read32(info+4);e.Write32(info+4,mask&~2u);U index=GetInfo(e,toolbar,command,info);
 if(index!=0xffffffff){
  U button=e.Read32(toolbar+0x30)+index*28;e.Write32(info+4,mask);
  if(mask&2){U text=e.Read32(button+16);if(text!=0xffffffff&&(text&0xffff0000))e.ConvertWtoA(text,e.Read32(info+24),e.Read32(info+28));else e.Write8(e.Read32(info+24),0);}
 }
 return index;
}
template<class E> U SetInfoA(E&e,U toolbar,U command,U info){
 typename E::Scratch temp(e,544);U local=temp.address+512;
 for(U i=0;i<32;i+=4)e.Write32(local+i,e.Read32(info+i));
 if(e.Read8(info+4)&2){e.Write32(local+24,temp.address);e.Write32(local+28,256);e.ConvertAtoW(e.Read32(info+24),temp.address,256);}
 return SetInfo(e,toolbar,command,local);
}
// Checking a grouped toolbar button clears the first other checked button in
// the contiguous group. Search both directions while the group-style bit
// remains set. Repaint the cleared button without emitting an accessibility
// notification.
template<class E> void CheckGroup(E&e,U toolbar,U command){
 const U count=e.Read32(toolbar+0x84),buttons=e.Read32(toolbar+0x30);
 const U index=CommandIndex(e,toolbar,command);if(int(index)<0)return;
 auto style=[&](U i){return e.Read8(buttons+i*28+9);};
 auto state=[&](U i){return e.Read8(buttons+i*28+8);};
 if(!(state(index)&1))return;
 U first=index;
 if(int(index)>0)
  for(;;){
   if(!(style(first)&4))break;
   --first;
   if(int(first)<=0)break;
  }
 if(!(style(first)&4))++first;
 U last=index;
 if(int(index)<int(count)-1)
  for(;;){
   if(!(style(last)&4))break;
   ++last;
   if(int(last)>=int(count)-1)break;
  }
 if(!(style(last)&4))--last;
 if(int(first)>int(last))return;
 for(U at=first;int(at)<=int(last);++at){
  if(at==index)continue;
  if(!(state(at)&1))continue;
  e.Write8(buttons+at*28+8,state(at)&~1u);
  e.InvalidateButton(toolbar,buttons+at*28,1);
  return;
 }
}
// Selected original WndProc branches; layout and drawing stay outside
// this button-object family. The caller only passes messages accepted here.
inline bool ObjectMessage(U message){
 switch(message){case 0x401:case 0x402:case 0x403:case 0x404:case 0x405:case 0x406:case 0x411:case 0x412:case 0x414:case 0x415:case 0x416:case 0x417:case 0x418:case 0x419:case 0x41e:case 0x424:case 0x425:case 0x42b:case 0x43f:case 0x440:case 0x441:case 0x442:case 0x443:case 0x444:case 0x449:case 0x44a:return true;default:return message>=0x409&&message<=0x40e;}
}
template<class E> U Message(E&e,U toolbar,U message,U wp,U lp){
 if(message>=0x409&&message<=0x40e){
  static const U mask[]={4,1,2,8,16,128};U i=CommandIndex(e,toolbar,wp);
  return int(i)<0?0xffffffff:e.Read8(e.Read32(toolbar+0x30)+i*28+8)&mask[message-0x409];
 }
 switch(message){
 // The six state setters that are one bit each: TB_ENABLEBUTTON (0x401),
 // TB_CHECKBUTTON (0x402), TB_PRESSBUTTON (0x403), TB_HIDEBUTTON (0x404),
 // TB_INDETERMINATE (0x405) and TB_MARKBUTTON (0x406). The bit of each one
 // stands in the byte table that Windows 98 reads with the message
 // number, and it is the bit its reader above uses; the low word of lParam
 // says whether it goes in or out, and the rest is the path of TB_SETSTATE.
 // a check that went in also runs the group rule below.
 case 0x401:case 0x402:case 0x403:case 0x404:case 0x405:case 0x406:{
  static const U bits[]={4,1,2,8,16,128};U i=CommandIndex(e,toolbar,wp);if(int(i)<0)return 0;
  U entry=e.Read32(toolbar+0x30)+i*28,previous=e.Read8(entry+8),bit=bits[message-0x401];
  U wanted=(lp&65535)?(previous|bit):(previous&~bit);
  e.Write8(entry+8,wanted);
  if(wanted==previous)return 1;
  // Checking one button clears the other checked button in its group.
  if(message==0x402&&(e.Read8(entry+9)&4))CheckGroup(e,toolbar,wp);
  // Hiding an item changes layout, so invalidate the whole toolbar and
  // request layout recalculation.
  if(message==0x404){e.Invalidate(e.Read32(toolbar),0,1);Dirty(e,toolbar);}
  else e.InvalidateButton(toolbar,entry,1);
  e.Accessibility(0x800a,e.Read32(toolbar),0xfffffffc,i+1);
  return 1;}
 case 0x411:{U i=CommandIndex(e,toolbar,wp);if(int(i)<0)return 0;e.Write32(toolbar+0xa4,0xffffffff);e.Write32(toolbar+0xa8,0xffffffff);SetState(e,toolbar,e.Read32(toolbar+0x30)+i*28,lp,i);Dirty(e,toolbar);return 1;}
 case 0x412:{U i=CommandIndex(e,toolbar,wp);return int(i)<0?0xffffffff:e.Read8(e.Read32(toolbar+0x30)+i*28+8);}
 case 0x414:case 0x444:return Insert(e,toolbar,0xffffffff,wp,lp,message==0x444);
 case 0x415:case 0x443:return Insert(e,toolbar,wp,1,lp,message==0x443);
 case 0x416:return Delete(e,toolbar,wp);
 case 0x417:if(wp>=e.Read32(toolbar+0x84))return 0;ToPublic(e,toolbar,e.Read32(toolbar+0x30)+wp*28,lp);return 1;
 case 0x418:return e.Read32(toolbar+0x84);
 case 0x419:return CommandIndex(e,toolbar,wp);
 case 0x41e:if(!e.Read32(toolbar+0x84))e.Write32(toolbar+0x64,wp);return 0;
 case 0x424:e.Write32(toolbar+0x40,wp);return 0;
 case 0x425:{U old=e.Read32(toolbar+4);e.Write32(toolbar+4,wp);return old;}
 case 0x42b:{U i=CommandIndex(e,toolbar,wp);return int(i)<0?0:SetImage(e,toolbar,e.Read32(toolbar+0x30)+i*28,lp&65535);}
 case 0x43f:return GetInfo(e,toolbar,wp,lp);
 case 0x440:return SetInfo(e,toolbar,wp,lp);
 case 0x441:return GetInfoA(e,toolbar,wp,lp);
 case 0x442:return SetInfoA(e,toolbar,wp,lp);
 case 0x449:{U old=e.Read32(toolbar+0xec);e.Write32(toolbar+0xec,(old&~128u)|((wp&1)<<7));return (old>>7)&1;}
 case 0x44a:return (e.Read32(toolbar+0xec)>>7)&1;
 default:return 0;
 }
}

}
