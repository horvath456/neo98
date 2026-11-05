// SPDX-License-Identifier: GPL-2.0-or-later
// Recovered USER6 caption decisions and USER13 end-ellipsis fit.
#pragma once
namespace Win98Caption {
using U=unsigned;inline int W(int x){U v=U(x)&65535;return v<32768?int(v):int(v)-65536;}
struct Rect{int left,top,right,bottom;};
struct State{U colors[29]={},brushes[29]={},dither=0,font=0,smallfont=0;int height=0,smallheight=0,depth=32,margin=2,iconwidth=16,iconheight=16,border=1,cacheheight=18;};
struct PublicArgs{U window,dc,rect,font,icon,text,flags;};
inline PublicArgs Public(const U*a,bool temporary){return {a[0],a[1],a[2],temporary?a[3]:0,temporary?a[4]:0,temporary?a[5]:0,temporary?a[6]:a[3]};}
struct Args{U window=0,dc=0,font=0,icon=0,flags=0;const char*text=nullptr;};
inline int Half(int v){v=W(v);return v/2;}
// USER. Fixed-point 7-bit RGB steps and the short final stripe.
template<class E> void Strips(E&e,U dc,Rect r,int step,U first,U last){
 int width=W(r.right-r.left),height=W(r.bottom-r.top);if(width<=0||height<=0)return;
 int n=W(width+step-1)/step;if(n<=0){e.Limit();return;}int value[3],delta[3];
 for(U k=0;k<3;++k){int a=(first>>(k*8))&255,b=(last>>(k*8))&255;value[k]=W(a*128);delta[k]=W((b-a)*128)/n;}
 for(int x=0;x<width;){if(step>width-x)step=width-x;U c=0;for(U k=0;k<3;++k)c|=(U(value[k]>>7)&255)<<(8*k);
  e.Stripe(dc,{W(r.left+x),r.top,W(r.left+x+step),W(r.top+height)},c);for(U k=0;k<3;++k)value[k]=W(value[k]+delta[k]);x+=step;}
}
// USER. Strict/inclusive binary-search choices are preserved.
template<class E> bool EllipsisFit(E&e,const char*s,U&n,int width,int overhang){
 if(!n||e.Extent(s,n)<=width)return false;U low=0,high=n;width=W(width-W(e.Extent("...",3)-overhang));
 if(width>0){while(high>low){U mid=(high+low+1)/2;int measured=e.Extent(s,mid);if(measured<width)low=mid;else if(measured>width)high=mid-1;else{low=mid;break;}}}
 n=low;return true;
}
// USER end-only branch132F..13E4, using owned native scratch storage.
template<class E> bool EndEllipsis(E&e,char*s,U&length,U capacity,int width,int overhang){
 U kept=length;if(!EllipsisFit(e,s,kept,width,overhang))return true;
 if(!kept)kept=1;if(kept+4>capacity)return false;
 s[kept]='.';s[kept+1]='.';s[kept+2]='.';s[kept+3]=0;length=kept+3;return true;
}
// USER cache eligibility, followed by the original32C8..3349 fallback.
template<class E> void Icon(E&e,const State&s,U dc,Rect&r,U icon,U brush,U flags){
 int height=W(r.bottom-r.top);bool cached=height==s.cacheheight&&brush!=s.dither&&e.CachedIcon(dc,r,icon,flags);
 if(!cached){e.Fill(dc,{r.left,r.top,W(r.left+height),r.bottom},brush);e.DrawIcon(dc,W(r.left+Half(height-s.iconwidth)+s.border),W(r.top+Half(height-s.iconheight)),icon,s.iconwidth,s.iconheight);}
 r.left=W(r.left+height);
}
// USER. Object lookup, system data, text and graphics are providers.
template<class E> U Draw(E&e,const State&s,Args a,Rect&r){
 U flags=a.flags&65535,result=1;bool gradient=false;int oldmode=0;char caption[256]={};
 if(a.window){if((flags&8)&&!a.text){e.Caption(a.window,caption,255,(flags&0x2000)!=0);if(!(flags&0x2000)&&!e.ValidWindow(a.window)){a.window=0;a.icon=0;}a.text=caption;}
  if(a.window&&!a.icon&&e.EligibleIcon(a.window)&&!(flags&2)&&e.SystemMenu(a.window)){a.icon=e.SmallIcon(a.window,(flags&0x2000)!=0);if(!e.ValidWindow(a.window)){a.icon=0;caption[0]=0;}}}
 U text,bk,brush;
 if(flags&1){if(flags&0x10){bk=s.colors[20];if(s.depth>=8&&bk==s.colors[0]&&s.colors[5]!=bk){text=s.colors[18];brush=s.brushes[20];}else{text=s.colors[15];brush=s.dither;oldmode=e.BkMode(a.dc,1);}}
 else{text=s.colors[9];bk=s.colors[2];brush=s.brushes[2];if(flags&0x20){gradient=true;oldmode=e.BkMode(a.dc,1);}}}
 else if(flags&0x10){text=s.colors[18];bk=s.colors[15];brush=s.brushes[15];}
 else{text=s.colors[19];bk=s.colors[3];brush=s.brushes[3];if(flags&0x20){gradient=true;oldmode=e.BkMode(a.dc,1);}}
 U oldtext=e.TextColor(a.dc,text),oldbk=e.BkColor(a.dc,bk);
 if(a.icon&&!(flags&2)){if(flags&4)Icon(e,s,a.dc,r,a.icon,brush,flags);else r.left=W(r.left+W(r.bottom-r.top));}
 if((flags&8)&&r.right>r.left){int height;U oldfont;
  if(!a.font){a.font=flags&2?s.smallfont:s.font;height=flags&2?s.smallheight:s.height;oldfont=e.Select(a.dc,a.font);}else{oldfont=e.Select(a.dc,a.font);height=e.Height(a.dc);}
  int y=Half(W(r.top+r.bottom-height));U length=a.text?e.Length(a.text):0;
  if(gradient){U index=flags&1?2:3;if(s.colors[index]==s.colors[index+25])e.Fill(a.dc,r,s.brushes[index]);else if(!e.CachedGradient(a.dc,r,flags&1))Strips(e,a.dc,r,s.depth>8?2:4,s.colors[index],s.colors[index+25]);}
  else e.Fill(a.dc,r,brush);
  if(brush==s.dither){e.TextColor(a.dc,s.colors[18]);e.BkColor(a.dc,s.colors[17]);}
  if((flags&0x4000)||(length&&e.Extent(a.text,length)>W(r.right-r.left-s.margin))){e.Ellipsis(a.dc,a.text,length,r,0x8824|((flags&0x4000)?1:0),s.margin);result=0;}
  else e.Output(a.dc,W(r.left+s.margin),y,r,a.text,length);
  if(oldfont)e.Select(a.dc,oldfont);
 }
 e.TextColor(a.dc,oldtext);e.BkColor(a.dc,oldbk);if(brush==s.dither||gradient)e.BkMode(a.dc,oldmode);return result;
}
}
