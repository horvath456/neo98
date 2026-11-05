// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98ComboBox {
using U=unsigned;
struct State {U flags=0,width=0,drop_width=0,height=0,list_height=0,font=0;};
// USER and 1AFC..1B8B. Window style and private combo
// flags are different layouts; native HWNDs never pass through a WORD thunk.
// USER. An open popup consumes Enter/Escape key messages.
inline U DialogCode(U flags,U key,bool has_message,U message){return has_message&&message==0x100&&(flags&0x80)&&(key==13||key==27)?0x85:0x81;}
// USER: the selection field carries ODS_COMBOBOXEDIT for
// Win40 controls. ComboBoxEx uses it to omit list indentation and focus.
inline U SelectionState(U flags,bool enabled,bool win40){return ((flags&4)&&!(flags&0x80)?0x11u:0u)|(enabled?0u:4u)|(win40?0x1000u:0u);}
inline U NormalizeStyle(U style){return (style&0x30)?style:style|0x200;}
// USER keeps the list scrollbar bits before stripping
// the combo window's border/scrollbars and nonclient extended edges.
inline U WindowStyle(U style){return NormalizeStyle(style)&~0x00b00000u;}
inline U WindowExtendedStyle(U extended){return extended&~0x300u;}
inline U ListScrollStyle(U style){return style&0x00300000u;}
inline U Flags(U style,U flags){
 if((style&3)==3)flags=(flags&~1u)|0x12;else if(style&2)flags|=3;else flags=(flags&~2u)|1;
 if(style&0x2000)flags=(flags&~0x2000u)|0x1000;else if(style&0x4000)flags=(flags&~0x1000u)|0x2000;else flags&=~0x3000u;
 if(style&0x20)flags=(flags&~0x100u)|0x200;if(style&0x10)flags=(flags&~0x200u)|0x100;return flags;
}
// USER, native popup position is supplied later.
inline U ListStyle(U style,U flags){U r=(style&0x300000)|0x54008001;if(style&0x8000000)r|=0x8000000;if(style&0x400)r|=0x100;if(style&0x100)r|=2;if(style&0x200)r|=0x40;if(style&0x800)r|=0x1000;if((flags&0x300)==0x200)r|=0x20;else if((flags&0x300)==0x100)r|=0x10;if(flags&2)r|=0x800000;return r;}
inline U EditStyle(U style,U flags){U r=0x50000300;if(style&0x8000000)r|=0x8000000;if(style&0x40)r|=0x80;if(style&0x80)r|=0x400;if(flags&0x3000)r|=(flags&0x1000)?8:16;return r;}
// USER. Win16 CB400-series and LB400-series are mapped
// to their Win32 message numbers at this ABI boundary.
template<class E> U Message(E&e,State&s,U m,U wp,U lp){
 switch(m){
 case 0x140:return(s.flags&0x10)?~0u:e.Edit(0xb0,wp,lp);
 case 0x141:return(s.flags&0x10)?~0u:e.Edit(0xc5,wp,lp);
 case 0x142:return(s.flags&0x10)?~0u:e.Edit(0xb1,unsigned(short(lp)),unsigned(short(lp>>16)));
 case 0x143:return e.List((s.flags&0x3000)?((s.flags&0x1000)?0x1ac:0x1ad):0x180,wp,lp);
 case 0x144:return e.List(0x182,wp,lp);
 case 0x145:return e.List(0x18d,wp,lp);
 case 0x146:return e.List(0x18b,wp,lp);
 case 0x147:return e.List(0x188,wp,lp);
 case 0x148:return e.List(0x189,wp,lp);
 case 0x149:return e.List(0x18a,wp,lp);
 case 0x14a:return e.List((s.flags&0x3000)?((s.flags&0x1000)?0x1aa:0x1ab):0x181,wp,lp);
 case 0x14b:e.List(0x184,0,0);e.Sync();return 1;
 case 0x14c:return e.List(0x18f,wp,lp);
 case 0x14d:{U r=e.List(0x18c,wp,lp);e.Sync();return r;}
 case 0x14e:{U r=e.List(0x186,wp,lp);if(r!=~0u)e.List(0x197,wp,0);e.Sync();return r;}
 case 0x14f:if(wp&&!(s.flags&0x80))e.Drop(true);else if(!wp&&(s.flags&0x80))e.Drop(false);return 1;
 case 0x150:return e.List(0x199,wp,lp);
 case 0x151:return e.List(0x19a,wp,lp);
 case 0x153:if((wp&65535)!=65535)return e.List(0x1a0,wp,lp);if(lp>>16)return ~0u;return e.SelectionHeight(lp&65535);
 case 0x154:return(wp&65535)==65535?s.height:e.List(0x1a1,wp,lp);
 case 0x155:if(!(s.flags&2)||wp>1)return ~0u;s.flags=(s.flags&~0x800u)|(wp?0x800:0);return 0;
 case 0x156:return(s.flags&2)&&(s.flags&0x800)?1:0;
 case 0x157:return(s.flags>>7)&1;
 case 0x158:return e.List(0x1a2,wp,lp);
 case 0x159:return e.List(0x1a5,wp,lp);
 case 0x15a:return e.List(0x1a6,wp,lp);
 case 0x15b:return e.List(0x18e,wp,lp);
 case 0x15c:return e.List(0x197,wp,lp);
 case 0x15d:return e.List(0x193,wp,lp);
 case 0x15e:return e.List(0x194,wp,lp);
 case 0x160:if(s.flags&2){U next=wp&65535;if(next&&int(short(s.width))>int(short(next)))next=s.width;if(next!=s.drop_width){s.drop_width=next;e.Layout();}}[[fallthrough]];
 case 0x15f:return(s.flags&2)?(int(short(s.width))>int(short(s.drop_width))?s.width:s.drop_width):~0u;
 case 0x161:return e.List(0x1a8,wp,lp);
 default:return e.Unsupported(m,wp,lp);
 }
}
} // namespace Win98ComboBox
