// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_shell_icon_cache.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using U=unsigned;using State=Win98ShellIconCache::State;
struct Wire {U operation,args[6];State state;U open_mask,metrics[2],depth,disk_ok,fail_create,load_fail,replace_fail,next_slot,override_icon,observer,query_error,counts[2],created,metadata,notifications,result,event_count,events[4096],output[2];char settings[4][20];};
static Wire *wire;static U hash(const char*p){U h=2166136261;while(*p)h=(h^static_cast<unsigned char>(*p++))*16777619;return h;}
struct Env {
 template<class...T>void Event(T...v){U data[]={U(v)...};for(U x:data){assert(wire->event_count<4096);wire->events[wire->event_count++]=x;}}
 U Open(U root,const char*path){U which=root==0x80000001?1:0;Event(1,root,hash(path));return wire->open_mask&(1u<<which)?0:which+1;}
 U OpenExplorer(const char*name){Event(2,hash(name));return wire->open_mask&4?0:3;}
 void Close(U h){Event(3,h);}
 void QueryText(U key,const char*name,char*out,U capacity){Event(4,key,hash(name),capacity,wire->query_error);const char*value="";
  if(key==3){if(wire->override_icon&&name[0]=='0'&&!name[1])value="override.dll,-3";}
  else {U index=!strcmp(name,"Max Cached Icons")?0:!strcmp(name,"Shell Icon Size")?1:!strcmp(name,"Shell Small Icon Size")?2:3;value=wire->settings[index];}
  U n=strlen(value)+1;if(n<=capacity)memcpy(out,value,n);
 }
 void QueryIndex(U key,U index,char*out,U capacity){char text[12];snprintf(text,sizeof text,"%u",index);Event(5,index);QueryText(key,text,out,capacity);}
 U Decimal(const char*text,U cap){Event(6,hash(text));U v=0;for(U i=0;i<cap&&text[i]>='0'&&text[i]<='9';++i)v=v*10+unsigned(text[i]-'0');return v;}
 U Metric(U index){Event(7,index);return wire->metrics[index-11];}U Depth(){Event(8);return wire->depth;}
 void Lock(){Event(9);}void Unlock(){Event(10);}void ClearIndex(){Event(11);wire->metadata=0;}void ClearRelated(){Event(12);}
 bool LoadDiskCache(State&s,U lx,U ly,U sx,U sy,U bpp){Event(13,lx,ly,sx,sy,bpp);if(wire->disk_ok){s.large=0x60000101;s.small=0x60000102;wire->counts[0]=wire->counts[1]=45;}return wire->disk_ok;}
 U Create(U x,U y,U flags,U initial,U grow){Event(14,x,y,flags,initial,grow);U n=++wire->created;if(n==wire->fail_create)return 0;U h=0x60000100+(n&1?1:2);wire->counts[(h&255)-1]=0;return h;}
 U Remove(U h,U slot){Event(15,h,slot);if(slot==~0u)wire->counts[(h&255)-1]=0;else if(wire->counts[(h&255)-1])--wire->counts[(h&255)-1];return 1;}
 U Resize(U h,U x,U y){Event(16,h,x,y);return 1;}U Destroy(U h){Event(17,h);wire->counts[(h&255)-1]=0;return 1;}
 U Background(U h,U color){Event(18,h,color);return ~0u;}U Color(U index){Event(19,index);return 0xc0c0c0;}
 void ModuleFilename(char*out,U n){Event(20,n);strcpy(out,"C:\\WINDOWS\\SYSTEM\\SHELL32.DLL");}
 U LoadResource(U id,U x,U y,U flags){Event(21,id,x,y,flags);return id==wire->load_fail?0:0x30000000|(x<<12)|id;}
 U IconLocation(char*text){Event(22,hash(text));char*p=strchr(text,',');if(p)*p=0;return U(-3);}
 void Extract(const char*path,U index,U cx,U cy,U flags,U&large,U&small){Event(23,hash(path),index,cx,cy,flags);large=0x30007777;small=0x30008888;}
 U NextSlot(){Event(24);return wire->next_slot;}
 U Replace(U list,U slot,U icon){Event(25,list,slot,icon);U index=(list&255)-1;if(wire->replace_fail==index+1)return ~0u;if(slot==~0u)return wire->counts[index]++;return slot;}
 void RecordMetadata(const char*file,U resource,U flags,U slot){Event(26,hash(file),resource,flags,slot);++wire->metadata;}
 void DestroyIcon(U icon){Event(27,icon);}
 void AddPair(State&s,U large,U small,const char*file,U resource,U flags){Win98ShellIconCache::AddPair(*this,s,large,small,file,resource,flags);}
 void ListsChanged(){if(wire->observer)Event(28);}void Notify(U event,U flags,U first,U second){Event(29,event,flags,first,second);++wire->notifications;}
 void Write32(U p,U value){assert(p==0x22000||p==0x22004);wire->output[(p-0x22000)/4]=value;}
}env;
extern "C" void shell_660_begin(Wire*w){wire=w;}
extern "C" void shell_660_run(Wire*w){wire=w;switch(w->operation){case 0:w->result=Win98ShellIconCache::Initialize(env,w->state);break;case 1:w->result=Win98ShellIconCache::Bootstrap(env,w->state,w->args[0],w->args[1],w->args[2],w->args[3],w->args[4],w->args[5]);break;case 2:w->result=Win98ShellIconCache::GetLists(env,w->state,w->args[0],w->args[1]);break;case 3:w->result=Win98ShellIconCache::Maximum(env,w->state);break;}}
extern "C" U shell_660_provider(U id,const U*a,const char*text,char*out){switch(id){
 case 0:return env.Open(a[0],text);case 1:return env.OpenExplorer(text);case 2:env.Close(a[0]);return 0;
 case 3:env.QueryText(a[0],text,out,a[1]);return wire->query_error;case 4:env.Event(5,a[0]);snprintf(out,12,"%u",a[0]);return 0;
 case 5:return env.Decimal(text,20);case 6:return env.Metric(a[0]);case 7:return env.Depth();case 8:env.Lock();return 0;case 9:env.Unlock();return 0;case 10:env.ClearIndex();return 0;case 11:env.ClearRelated();return 0;
 case 12:return env.LoadDiskCache(wire->state,a[0],a[1],a[2],a[3],a[4]);case 13:return env.Create(a[0],a[1],a[2],a[3],a[4]);case 14:return env.Remove(a[0],a[1]);case 15:return env.Resize(a[0],a[1],a[2]);case 16:return env.Destroy(a[0]);case 17:return env.Background(a[0],a[1]);case 18:return env.Color(a[0]);case 19:env.ModuleFilename(out,a[0]);return 0;case 20:return env.LoadResource(a[0],a[1],a[2],a[3]);case 21:return env.IconLocation(out);case 22:{U large=0,small=0;env.Extract(text,a[0],a[1],a[2],a[3],large,small);memcpy(out,&large,4);memcpy(out+4,&small,4);return 2;}
 case 23:return env.NextSlot();case 24:return env.Replace(a[0],a[1],a[2]);case 25:env.RecordMetadata(text,a[0],a[1],a[2]);return 0;case 26:env.DestroyIcon(a[0]);return 1;case 27:env.ListsChanged();return 0;case 28:env.Notify(a[0],a[1],a[2],a[3]);return 0;
 }assert(false);return 0;}
#ifdef SHELL660_REPLAY
int main(int argc,char**argv){assert(argc==2);FILE*f=fopen(argv[1],"rb");assert(f);Wire a,b;U count=0;while(fread(&a,sizeof a,1,f)){assert(fread(&b,sizeof b,1,f));shell_660_run(&a);assert(!memcmp(&a,&b,sizeof a));++count;}fclose(f);printf("SHELL660 sanitizer replays PASS %u\n",count);}
#endif
