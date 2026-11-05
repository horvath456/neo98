// SPDX-License-Identifier: GPL-2.0-or-later
// Original SHELL32 shared icon-list initialization and its resource bootstrap.
#pragma once
namespace Win98ShellIconCache {
using U=unsigned;
struct State {
 U large=0,small=0,load_flags=0,large_x=0,small_x=0,max_icons=500,
   builtin_limit=0,large_y=0,small_y=0,bpp=0;
};
static constexpr U ResourceIds[]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,137,37,138,39,40,41,42,43,44,45};
// The query's status and type are intentionally ignored.
template<class E> U Number(E&e,U key,const char*name,U fallback){
 if(!key)return fallback;char text[20]={};e.QueryText(key,name,text,20);
 if(static_cast<unsigned char>(text[0])<'0'||static_cast<unsigned char>(text[0])>'9')return fallback;
 return e.Decimal(text,20);
}
//  and cold blocks7FCD5E1E..2E.
template<class E> U Maximum(E&e,State&s){
 U result=500,key=e.Open(0x80000002,"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer");
 if(key){result=Number(e,key,"Max Cached Icons",500);e.Close(key);if(int(result)<0)result=500;}
 if(s.max_icons==result)return 0;s.max_icons=result;return 1;
}
//  bootstrap branch: the caller already supplies a large icon.
// The separate filename-extraction branch is not reached by Bootstrap.
template<class E> U AddPair(E&e,State&s,U large,U small,const char*file,U resource,U flags){
 if(!large)return ~0u;if(!small)small=large;if(!s.large)return ~0u;
 e.Lock();U slot=e.NextSlot(),result=e.Replace(s.large,slot,large);
 if(int(result)>=0){U other=e.Replace(s.small,slot,small);if(int(other)<0){if(slot==~0u)e.Remove(s.large,result);result=~0u;}}
 if(int(result)>=0)e.RecordMetadata(file,resource,flags,result);
 e.Unlock();e.DestroyIcon(large);if(small&&small!=large)e.DestroyIcon(small);return result;
}
// ImageList and resource/cache-object operations are explicit providers.
template<class E> U Bootstrap(E&e,State&s,U lx,U ly,U sx,U sy,U bpp,bool icons){
 if(!s.large)s.large=e.Create(lx,ly,bpp|0x101,0,32);
 else {e.Remove(s.large,~0u);e.Resize(s.large,lx,ly);}
 if(!s.large)return 0;
 if(!s.small)s.small=e.Create(sx,sy,bpp|0x101,0,32);
 else {e.Remove(s.small,~0u);e.Resize(s.small,sx,sy);}
 if(!s.small){e.Destroy(s.large);s.large=0;return 0;}
 e.Background(s.large,e.Color(5));e.Background(s.small,e.Color(5));
 if(!icons)return 1;
 char module[260]={};e.ModuleFilename(module,260);U overrides=e.OpenExplorer("Shell Icons");
 for(U index=0;index<45;++index){
  U large=0,small=0;
  if(overrides){char value[260]={};e.QueryIndex(overrides,index,value,260);
   if(value[0]){U resource=e.IconLocation(value);e.Extract(value,resource,(s.large_x&65535)|(s.small_x<<16),(s.large_y&65535)|(s.small_y<<16),s.load_flags,large,small);}
  }
  if(!large){large=e.LoadResource(ResourceIds[index]&65535,lx,ly,s.load_flags);small=e.LoadResource(ResourceIds[index]&65535,sx,sy,s.load_flags);}
  if(large)e.AddPair(s,large,small,module,index,0);
 }
 if(overrides)e.Close(overrides);e.ListsChanged();return 1;
}
// The ABI argument is never read.
template<class E> U Initialize(E&e,State&s){
 Maximum(e,s);U lx=e.Metric(11),ly=e.Metric(12);
 U sx=U(int(e.Metric(11))/2),sy=U(int(e.Metric(12))/2),bpp=0;
 U key=e.Open(0x80000001,"Control Panel\\Desktop\\WindowMetrics");
 if(key){lx=Number(e,key,"Shell Icon Size",lx);ly=lx;sx=Number(e,key,"Shell Small Icon Size",sx);sy=sx;bpp=Number(e,key,"Shell Icon Bpp",0);e.Close(key);}
 U depth=e.Depth();if(int(bpp)>int(depth))bpp=0;if(int(depth)<=8)bpp=0;
 bool changed=false;U result=0;e.Lock();
 if(s.large){
  bpp=s.bpp; // Original overwrites EBX before comparing EBX with itself.
  if(s.large_x==lx&&s.large_y==ly&&s.small_x==sx&&s.small_y==sy)result=1;
  else {e.ClearIndex();e.ClearRelated();changed=true;}
 }
 if(!result){
  s.large_x=lx;s.large_y=ly;s.small_x=sx;s.small_y=sy;s.bpp=bpp;
  s.load_flags=int(depth)>4&&int(bpp)<=4?0x80:0;
  if(!s.builtin_limit)s.builtin_limit=46;
  if(!s.large&&e.LoadDiskCache(s,lx,ly,sx,sy,bpp))result=1;
  else result=Bootstrap(e,s,s.large_x,s.large_y,s.small_x,s.small_y,s.bpp,true);
 }
 e.Unlock();if(result&&changed)e.Notify(0x8000,3,~0u,0);return result;
}
// Original ordinal71; returns1 even if initialization returned0.
template<class E> U GetLists(E&e,State&s,U large,U small){Initialize(e,s);if(large)e.Write32(large,s.large);if(small)e.Write32(small,s.small);return 1;}
}
