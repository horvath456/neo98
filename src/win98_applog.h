// SPDX-License-Identifier: GPL-2.0-or-later
// KERNEL32 APPLOG index maintenance.
#pragma once
namespace Win98AppLog {
inline unsigned Read(const unsigned char*p){return unsigned(p[0])|(unsigned(p[1])<<8)|(unsigned(p[2])<<16)|(unsigned(p[3])<<24);}
inline void Write(unsigned char*p,unsigned v){for(unsigned i=0;i<4;++i)p[i]=v>>(8*i);}
struct Index {
 unsigned count=0,string_bytes=0,stamp[2]={},groups[86]={};
 unsigned char *records=nullptr,*strings=nullptr;
};
// Native file-provider validation, before the original unchecked algorithms.
inline bool Load(Index&out,unsigned char*bytes,unsigned size){
 if(size<408)return false;
 const char magic[]="APPLOG1.1";for(unsigned i=0;i<sizeof magic;++i)if(bytes[i]!=magic[i])return false;
 unsigned count=Read(bytes+16),strings=Read(bytes+20);
 if(count>(size-408)/32||strings>size-408-count*32)return false;
 Index result;result.count=count;result.string_bytes=strings;
 result.stamp[0]=Read(bytes+24);result.stamp[1]=Read(bytes+28);
 for(unsigned i=0;i<86;++i)result.groups[i]=Read(bytes+64+i*4);
 unsigned used=0;
 for(unsigned i=0;i<86;i+=2){unsigned n=result.groups[i+1];if(!n)continue;
  if(result.groups[i]!=used||n>count-used)return false;used+=n;
 }
 if(used!=count)return false;
 result.records=bytes+408;result.strings=result.records+count*32;
 for(unsigned i=0;i<count;++i){unsigned offset=Read(result.records+i*32+8);
  if(offset>=strings)return false;
  unsigned n=0;for(;n<246&&offset+n<strings&&result.strings[offset+n];++n){}
  if(n>=246||offset+n>=strings)return false;
 }
 out=result;return true;
}
template<class E>void Prune(Index&s,E&e){
 const unsigned now=e.NowHigh();unsigned removed=0,processed=0;
 for(unsigned group=0;group<43;++group){
  unsigned&first=s.groups[group*2],&count=s.groups[group*2+1];if(!count)continue;
  first-=removed;unsigned position=first*32,dropped=0;
  for(unsigned i=0;i<count;++i,++processed){
   unsigned char*record=s.records+position;const unsigned char*dir=s.strings+Read(record+8);
   char path[260];unsigned n=0;while(dir[n]){path[n]=dir[n];++n;}path[n++]='\\';
   for(unsigned j=0;j<8;++j)path[n+j]=record[j];path[n+8]=0;
   while(path[n])++n;for(unsigned j=0;j<5;++j)path[n+j]=".EXE"[j];
   unsigned seen=Read(record+16);
   if(!e.Exists(path)||(seen<=now&&now-seen>=0x137f6)){
    const unsigned bytes=(s.count-processed-1)*32;
    for(unsigned j=0;j<bytes;++j)record[j]=record[j+32];++dropped;
   }else position+=32;
  }
  removed+=dropped;count-=dropped;
 }
 s.count-=removed;if(removed&&!s.count){s.string_bytes=0;s.strings=nullptr;}
}
template<class E>void Save(const Index&s,E&e){
 if(!e.Open())return;
 struct Close{E&e;~Close(){e.Close();}}close{e};
 if(!s.count||!s.records)return;
 // Reserved bytes of the original stack header are undefined. Zero them
 // at the native boundary; only the magic and DWORDs 16..31 carry data.
 unsigned char header[64]={},groups[344];const char magic[]="APPLOG1.1";
 for(unsigned i=0;i<sizeof magic;++i)header[i]=magic[i];
 Write(header+16,s.count);Write(header+20,s.string_bytes);Write(header+24,s.stamp[0]);Write(header+28,s.stamp[1]);
 for(unsigned i=0;i<86;++i)Write(groups+4*i,s.groups[i]);
 if(e.Write(header,64)!=64||e.Write(groups,344)!=344||e.Write(s.records,s.count*32)!=s.count*32)return;
 e.Write(s.strings,s.string_bytes);
}
}
