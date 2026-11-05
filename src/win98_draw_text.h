// SPDX-License-Identifier: GPL-2.0-or-later
// Original USER13 DrawText layout and USER6 prefix scan.
#pragma once
namespace Win98DrawText {
using U=unsigned;inline int W(int x){U v=U(x)&65535;return v<32768?int(v):int(v)-65536;}
struct Rect{int left,top,right,bottom;};
struct Params{U size=12;int tab=8,left=0,right=0;U drawn=0;};
struct Metrics{int height=0,external=0,average=0,overhang=0,xsign=1,ysign=1;};
struct State{Rect rect;int tab,xsign,ysign,height,width,maximum=0,right_margin,overhang;};
struct Prefix{U count=0,removed=0;int underline=-1;};
// USER, including last-prefix index and escaped ampersands.
inline Prefix Strip(const char*s,U n,char*out=nullptr,U capacity=32767){Prefix r;U p=0;while(p<n&&capacity&&s[p]){--capacity;char c=s[p++];if(c=='&'){++r.removed;if(s[p]!='&'){r.underline=int(r.count);continue;}++p;if(out)out[r.count]='&';++r.count;}else{if(out)out[r.count]=c;++r.count;}}if(out)out[r.count]=0;return r;}
// USER: hidden prefixes remove ampersand advance, not arbitrary glyph width.
template<class E> int PrefixWidth(E&e,const char*s,U n,U flags,int overhang){int removed=0;if(!(flags&0x800)){auto p=Strip(s,n);if(p.removed)removed=W(int(p.removed)*W(e.Extent("&",1)-overhang));}return W(e.Extent(s,n)-removed);}
// USER. Every tab advances to the next tabstop relative to rect.left.
template<class E> int Measure(E&e,State&st,const char*s,U n,U flags,int x,int y,bool draw){
 if(!(flags&0x40)){if(draw&&!(flags&0x400))e.Draw(x,y,s,n,flags);return W(PrefixWidth(e,s,n,flags,st.overhang)-st.overhang);}
 int start=x;U p=0;while(p<n){U count=0;while(p+count<n&&s[p+count]!='\t')++count;
  if(count){if(draw&&!(flags&0x400))e.Draw(x,y,s+p,count,flags);x=W(x+PrefixWidth(e,s+p,count,flags,st.overhang)-st.overhang);}
  p+=count;if(p==n)break;++p;if(st.tab){x=W(st.rect.left+W((W(x-st.rect.left)/st.tab+1)*st.tab));}
 }return W(x-start);
}
// USER. Center uses arithmetic shift, unlike single-line vertical centering.
template<class E> void Line(E&e,State&st,const char*s,U n,U flags,int y){int x=st.rect.left;if(flags&3){int width=W(Measure(e,st,s,n,flags,x,y,false)+st.overhang);if(flags&1){int d=W(st.rect.right-st.rect.left-width);x=W(st.rect.left+(d>=0?d/2:-((-d+1)/2)));}else x=W(st.rect.right-width);}int width=W(Measure(e,st,s,n,flags,x,y,true)+st.overhang);if(st.maximum<width)st.maximum=width;}
// USER. A whitespace byte at the start is one candidate itself.
inline U Word(const char*s,U at,U end,U flags){U initial=1;while(at<end){char c=s[at];if(c=='\r'||c=='\n')return at;if((c==' '||c=='\t')&&(flags&0x10))return at+initial;++at;initial=0;}return at;}
// USER. An overlong first word still consumes at least one byte.
template<class E> U Fit(E&e,State&st,const char*s,U n,U flags){U low=0,high=n;while(high-low>1){U mid=low+(high-low)/2;if(PrefixWidth(e,s,mid,flags,st.overhang)>st.width)high=mid;else low=mid;}if(!low&&n)low=1;return low;}
struct Break{U count,next;};
// USER and whitespace helper0DAC..0E17.
template<class E> Break Next(E&e,State&st,const char*s,U start,U end,U flags){
 U previous=start,candidate=start,next=start;bool wrapped=false;
 while(previous<end){candidate=Word(s,previous,end,flags);int width=Measure(e,st,s+start,candidate-start,flags,st.rect.left,0,false);
  if((flags&0x10)&&W(width+st.overhang)>st.width){wrapped=true;if(previous!=start)candidate=previous;else if(flags&0x2000)candidate=start+Fit(e,st,s+start,candidate-start,flags);next=candidate;break;}
  if(candidate<end&&(s[candidate]=='\r'||s[candidate]=='\n')){next=candidate+1;if(next<end&&s[next]==(s[candidate]^7))++next;break;}
  previous=candidate;next=candidate;
 }
 U count=candidate-start;
 if(wrapped&&next<end){U align=flags&3;if((align==1||align==2)&&count&&(s[next-1]==' '||s[next-1]=='\t'))--count;if((align==0||align==1)&&(s[next]==' '||s[next]=='\t'))++next;}
 return {count,next};
}
// USER for one line of a multi-line DrawText with DT_END_ELLIPSIS:
// the text is cut where it and "..." still fit (0FE8..1097) and drawn with
// the ellipsis. Only the end ellipsis without prefix processing is provided.
template<class E> void EllipsisLine(E&e,State&st,const char*s,U n,U flags,int y){
 if(!n||e.Extent(s,n)<=st.width){Line(e,st,s,n,flags,y);return;}
 char line[1024];if(n>sizeof line-4)n=sizeof line-4;for(U i=0;i<n;++i)line[i]=s[i];
 U low=0,high=n;int width=W(st.width-W(e.Extent("...",3)-st.overhang));
 if(width>0){while(high>low){U mid=(high+low+1)/2;int measured=e.Extent(line,mid);if(measured<width)low=mid;else if(measured>width)high=mid-1;else{low=mid;break;}}}
 n=low?low:1;line[n]='.';line[n+1]='.';line[n+2]='.';n+=3;
 Line(e,st,line,n,flags,y);
}
// USER initialization and1414..177A main body. Rendering/metrics,
// DBCS callbacks, ellipsis and device clip implementation are provider boundaries.
template<class E> int Layout(E&e,const char*s,U length,bool explicit_zero,Rect&r,U flags,Params*p){
 if(p&&p->size!=12)return 0;if(explicit_zero)return 1;
 Metrics m=e.MetricsFor(flags);State st{r,W((p&&(flags&0x80)?p->tab:8)*m.average),m.xsign,m.ysign,W((m.height+((flags&0x200)?m.external:0))*m.ysign),0,0,0,m.overhang};
 if(p){st.rect.left=W(st.rect.left+p->left*m.xsign);st.right_margin=W(p->right*m.xsign);st.rect.right=W(st.rect.right-st.right_margin);}st.width=W((st.rect.right-st.rect.left)*m.xsign);
 if(st.width<=0&&(flags&0x10))return 1;
 U consumed=0;int bottom=r.top;
 for(U pass=0;;++pass){int y=r.top,lines=0;consumed=0;
  if(flags&0x20){if((flags&12)==4)y=W(r.top+W(r.bottom-r.top-st.height)/2);else if((flags&12)==8)y=W(r.bottom-st.height);Line(e,st,s,length,flags,y);consumed=length;bottom=W(y+st.height);lines=1;}
  else{bool stop=false;int last_extra=flags&0x2000?st.height:0;while(consumed<length&&!stop){if(!(flags&0x500)&&W((y+st.height+last_extra)*st.ysign)>W(r.bottom*st.ysign))stop=true;
    // DT_END_ELLIPSIS in multi-line mode (15EE..160E, 163B..1675): the last
    // visible line takes the rest of the text through 1224, and so does a
    // line that reaches the end of the text.
    if((flags&0x8000)&&stop){U end=consumed;while(end<length&&s[end]!='\r'&&s[end]!='\n')++end;EllipsisLine(e,st,s+consumed,end-consumed,flags,y);consumed=length;++lines;y=W(y+st.height);break;}
    auto b=Next(e,st,s,consumed,length,flags);
    if((flags&0x8000)&&b.next>=length)EllipsisLine(e,st,s+consumed,b.count,flags,y);else Line(e,st,s+consumed,b.count,flags,y);
    if(b.next<=consumed){e.NoProgress();return 0;}consumed=b.next;++lines;y=W(y+st.height);
   }bottom=y;if(!(flags&0x2000)&&length&&(s[length-1]=='\r'||s[length-1]=='\n'))bottom=W(bottom+st.height);
  }
  if(flags&0x400){r.right=W(st.rect.left+st.maximum*st.xsign+st.right_margin);if(lines>1&&st.width<st.maximum){if(pass>=63){e.NoProgress();return 0;}st.width=st.maximum;continue;}r.bottom=bottom;}
  break;
 }
 if(p)p->drawn=consumed;int result=W(bottom-r.top);return result?result:1;
}
}
