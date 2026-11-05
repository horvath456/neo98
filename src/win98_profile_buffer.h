// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98Profile {
using U=unsigned;using W=unsigned short;constexpr U Missing=~0u;
inline unsigned char Lower(unsigned char c){
 // KRNL386 -> AB70 and original default table4:2162.
 if(c>='A'&&c<='Z')return c+32;
 if((c>=0xc0&&c<=0xd6)||(c>=0xd8&&c<=0xde))return c+32;
 if(c==0x8a||c==0x8c)return c+16;return c;
}
inline bool Space(unsigned char c){return c==' '||c=='\t';}
// KRNL386: compact in place, then append CR/LF/NUL.
inline U Normalize(unsigned char*text,U count,U capacity){
 if(count>capacity||capacity-count<3)return Missing;U src=0,dst=0,left=count;
 while(left){if(Space(text[src])){++src;--left;continue;}U last=dst;
  while(left){unsigned char c=text[src++];--left;text[dst++]=c;
   if(c=='='){dst=last;text[dst++]='=';while(left&&Space(text[src])){++src;--left;}while(left){c=text[src++];--left;text[dst++]=c;if(c=='\n')break;}break;}
   if(!left||c=='\n')break;if(!Space(c))last=dst;
  }
 }
 while(dst&&text[dst-1]==0x1a)--dst;text[dst++]='\r';text[dst++]='\n';text[dst++]=0;return dst;
}
struct Name {const unsigned char*text;U length;};
inline Name TrimName(const unsigned char*p){if(!p)return {p,0};while(Space(*p))++p;U length=0;for(U n=0;p[n];++n)if(!Space(p[n]))length=n+1;return {p,length};}
// Search callers preserve a case-sensitive final-byte fallback when the caller's
// key/section string contains trailing whitespace (5B38..4B and5C2E..41).
inline bool EqualName(const unsigned char*p,Name q){for(U n=0;n<q.length;++n)if(Lower(p[n])!=Lower(q.text[n]))return false;return !q.text[q.length]||(q.length&&p[q.length-1]==q.text[q.length-1]);}
inline U NextLine(const unsigned char*text,U at,U size){while(at<size&&text[at]!='\n')++at;return at<size?at+1:size;}
// KRNL386; the source buffer has passed Normalize.
inline U Section(const unsigned char*text,U size,const unsigned char*name){if(!name)return Missing;Name q=TrimName(name);U at=0;
 while(at<size&&text[at]){if(text[at]=='['){U end=at+1;while(end<size&&text[end]!=']')++end;if(end<size&&end-at-1==q.length&&EqualName(text+at+1,q)){U result=NextLine(text,at+1,size);return result<size?result:Missing;}}at=NextLine(text,at,size);}return Missing;
}
// KRNL386.
inline U Key(const unsigned char*text,U size,U section,const unsigned char*name){if(!name||section==Missing)return Missing;Name q=TrimName(name);U at=section;
 while(at<size&&text[at]&&text[at]!='['){if(text[at]!='\r'){U end=at;while(end<size&&text[end]!='=')++end;if(end<size&&end-at==q.length&&EqualName(text+at,q))return end+1;}at=NextLine(text,at,size);}return Missing;
}
// KRNL386. This writes the terminating control byte over trailing spaces.
inline U ValueLength(unsigned char*text){U n=0;while(text[n]>13)++n;unsigned char end=text[n];while(n&&text[n-1]==' ')--n;text[n]=end;return n;}
// KRNL386. Enumeration skips comment lines and counts key NULs.
inline U Keys(const unsigned char*text,U size,U section,unsigned char*out,W capacity){if(!capacity)return 0;if(section==Missing)return Missing;U at=section,used=0;const U limit=U(capacity)-1;
 while(at<size&&text[at]){U first=at;if(text[at]==';'){at=NextLine(text,at,size);continue;}while(at<size&&text[at]!='='&&text[at]!='\n'&&text[at]!='['&&text[at])++at;
  if(at==size||!text[at]||text[at]=='[')break;if(text[at]=='\n'){++at;continue;}
  for(U n=first;n<=at;++n){unsigned char c=n==at?0:text[n];out[used]=c;++used;if(used>=limit)--used;}
  at=NextLine(text,at+1,size);
 }
 out[used]=0;if(used)out[used-1]=0;return used;
}
// KRNL386, common string branch after cache acquisition.
inline U String(unsigned char*text,U size,const unsigned char*section,const unsigned char*key,unsigned char*fallback,unsigned char*out,W capacity){
 if(!capacity)return 0;U found=section?Section(text,size,section):Missing;
 if(!key){U count=Keys(text,size,found,out,capacity);if(count!=Missing)return count;}
 U value=key?Key(text,size,found,key):Missing;unsigned char*source=value!=Missing?text+value:fallback;if(!source)return 0;
 U n=ValueLength(source);if(n>=2&&source[0]==source[n-1]&&(source[0]=='\''||source[0]=='"')){source++;n-=2;}if(n>=capacity)n=capacity-1;for(U i=0;i<n;++i)out[i]=source[i];out[n]=0;return n;
}
// KERNEL32; the Win16 routine5836..58EA has the same parsing
// decisions but accumulates at WORD width. Minus toggles and x switches radix.
inline U Number(const unsigned char*text){while(Space(*text))++text;bool negative=false;U radix=10,result=0;for(int c=(signed char)*text;c;c=(signed char)*++text){if(c=='-'){negative=!negative;continue;}if(c>='A'&&c<='Z')c+=32;if(c=='x'){radix=16;continue;}c-='0';if(c>9)c-=0x27;if(c<0||U(c)>=radix)break;result=result*radix+U(c);}return negative?0-result:result;}
// KRNL386. The original copies raw CR-delimited section lines,
// including comments; LF-only input does not take the CR/LF conversion branch.
inline U SectionText(const unsigned char*text,U size,const unsigned char*section,unsigned char*out,W capacity){
 if(capacity)out[0]=0;U at=Section(text,size,section),used=0,dst=0;if(at==Missing){out[0]=out[1]=0;return 0;}
 while(at<size&&text[at]!='['){if(int(short(W(capacity-used)))<=2||!text[at])break;
  while(at<size&&text[at]!='\r'){if(int(short(W(capacity-used)))<=2||!text[at])break;out[dst++]=text[at++];++used;}
  int left=short(W(capacity-used));if(left>2){if(used&&out[dst-1]){out[dst++]=0;++used;}at+=2;}
  else if(left>=2)out[dst++]=0;
 }
 out[dst]=0;return used;
}
// KRNL386. Normal completion includes the final list terminator;
// overflow reports capacity-2 without copying a partial section name.
inline U SectionNames(const unsigned char*text,U size,unsigned char*out,W capacity){
 out[0]=0;U at=0,used=0;while(at<size&&text[at]){while(at<size&&Space(text[at]))++at;if(at==size||!text[at])break;
  if(text[at]!='['){at=NextLine(text,at,size);if(at==size||text[at]!='[')continue;}U start=++at;while(at<size&&text[at]&&text[at]!=']'&&text[at]!='\r')++at;
  if(at==size||!text[at]||text[at]=='\r')return 0;U length=at-start;if(capacity<2||U(W(capacity-2))<used+length)return W(capacity-2);
  for(U n=start;n<at;++n)out[used++]=text[n];at=NextLine(text,at,size);out[used++]=0;out[used]=0;
 }return used+1;
}

}
