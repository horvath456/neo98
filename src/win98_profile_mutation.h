// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_profile_buffer.h"
namespace Win98Profile {
struct Buffer {unsigned char*text;U size,capacity,flags;};
struct MutationEnvironment {virtual ~MutationEnvironment()=default;virtual bool Reserve(Buffer&,int change)=0;};
inline bool Move(Buffer&b,U at,int change){if(at>b.size||change<0&&U(-change)>b.size-at||change>0&&U(change)>b.capacity-b.size)return false;
 if(change<0){U source=at+U(-change);for(U n=source;n<b.size;++n)b.text[n+change]=b.text[n];}
 else if(change>0){for(U n=b.size;n>at;--n)b.text[n-1+change]=b.text[n-1];}b.size+=change;return true;
}
// KRNL386 preserves comment lines while deleting a section.
inline void DeleteSection(Buffer&b,U at){if(at==Missing||at>=b.size)return;while(at&&b.text[at]!='[')--at;U dst=at,src=at+1;
 while(src<b.size){unsigned char c=b.text[src++];if(c==';'){b.text[dst++]=c;while(src<b.size&&c!='\n'){c=b.text[src++];b.text[dst++]=c;}continue;}
  if(c=='['){--src;break;}if(!c){src=src>=3?src-3:0;break;}while(src<b.size&&c!='\n')c=b.text[src++];
 }if(src>=dst)Move(b,dst,-int(src-dst));
}
// KRNL386.
inline void DeleteKey(Buffer&b,U value){if(value==Missing||value>=b.size)return;U start=value;while(start&&b.text[start-1]!='\n')--start;U end=NextLine(b.text,value,b.size);Move(b,start,-int(end-start));}
inline U FillSection(Buffer&b,U at,const unsigned char*section,U n){if(at){b.text[at++]='\r';b.text[at++]='\n';}b.text[at++]='[';for(U i=0;i<n;++i)b.text[at++]=section[i];b.text[at++]=']';b.text[at++]='\r';b.text[at++]='\n';return at;}
inline U FillKey(Buffer&b,U at,const unsigned char*key,U n){for(U i=0;i<n;++i)b.text[at++]=key[i];b.text[at++]='=';return at;}
inline U FillValue(Buffer&b,U at,const unsigned char*value,U n){for(U i=0;i<n;++i)b.text[at++]=value[i];b.text[at++]='\r';b.text[at++]='\n';return at;}
inline bool SameValue(const unsigned char*old,const unsigned char*value){for(U n=0;;++n){if(old[n]=='\r')return !value[n];if(old[n]!=value[n])return false;}}
// KRNL386. Cache acquisition, allocation and disk-space checks are
// provider responsibilities. This body mutates the original normalized byte layout.
inline bool WriteString(MutationEnvironment&e,Buffer&b,unsigned char*section,unsigned char*key,unsigned char*value){
 if(!section&&!key&&!value){b.flags=0;return false;}if(!section)return false;if(!b.text||b.flags&8)return false;
 U section_at=Section(b.text,b.size,section),at=b.size-1,mode=0,section_length=0,key_length=0;int change=0;
 if(section_at==Missing){if(!key||!value)return true;mode=0;}
 else{at=section_at;if(!key){DeleteSection(b,at);b.flags|=2;return true;}
  U found=Key(b.text,b.size,section_at,key);if(found==Missing){if(!value)return true;mode=1;while(at<b.size&&b.text[at]){if(b.text[at]=='['&&at&&b.text[at-1]=='\n')break;++at;}}
  else{at=found;if(!value){DeleteKey(b,at);b.flags|=2;return true;}if(SameValue(b.text+at,value))return true;mode=2;U end=at;while(end<b.size&&b.text[end]!='\r')++end;change=-int(end-at);}
 }
 if(mode<2){if(!mode){section_length=ValueLength(section);change=int(section_length)+4;}key_length=ValueLength(key);change+=int(key_length)+3;
  while(at){--at;if(b.text[at]!='\r'&&b.text[at]!='\n')break;}if(at)at+=3;if(at&&!mode)change+=2;
 }
 U value_length=ValueLength(value);change+=int(value_length);if(!e.Reserve(b,change))return false;if(!Move(b,at,change))return false;
 if(!mode)at=FillSection(b,at,section,section_length);if(mode<2)at=FillKey(b,at,key,key_length);FillValue(b,at,value,value_length);b.flags|=2;return true;
}
// KRNL386. MULTI_SZ strings become CR/LF lines in the cache.
inline bool WriteSection(MutationEnvironment&e,Buffer&b,unsigned char*section,const unsigned char*strings){
 if(!section||!b.text||b.flags&8)return false;U at=Section(b.text,b.size,section),mode=0,section_length=0;int change=0;
 if(at==Missing){if(!strings)return true;at=b.size-1;section_length=ValueLength(section);change=int(section_length)+4;}
 else{if(!strings){DeleteSection(b,at);b.flags|=2;return true;}mode=2;U end=at;while(end<b.size&&b.text[end]&&b.text[end]!='['){while(end<b.size&&b.text[end]!='\r'){++end;--change;}if(end<b.size){end+=2;change-=2;}}}
 for(U p=0;strings[p];){while(strings[p]){++p;++change;}++p;change+=2;}change+=2;
 if(!e.Reserve(b,change)||!Move(b,at,change))return false;if(!mode)at=FillSection(b,at,section,section_length);
 for(U p=0;strings[p];){while(strings[p])b.text[at++]=strings[p++];++p;b.text[at++]='\r';b.text[at++]='\n';}b.flags|=2;return true;
}

}
