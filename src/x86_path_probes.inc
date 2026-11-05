// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_path_probes.h"
#include "win98_path_probe_exports.h"
namespace X86 {
unsigned Application::PathProbeApi(ApiName name,const unsigned*a){
 int op=PathProbeOperation(name);if(op<0)return 0;bool wide=op&&!(op&1);
 struct E {
  Application&app;bool okay=true;unsigned scratch=0,directory=0,position=0;
  explicit E(Application&a):app(a){}~E(){if(scratch)app.Free(scratch);}
  unsigned Space(){if(!scratch)scratch=app.Allocate(1040);return scratch;}
  void Unsupported(){okay=false;app.last_error=50;}
  const char*Next(const char*p){unsigned c=(unsigned char)*p;return p+((app.NlsApi(537,&c)&&p[1])?2:1);}
  const unsigned short*Next(const unsigned short*p){return p+1;}
  unsigned SetMode(unsigned value){return app.Api("SetErrorMode",&value);}
  const char*Ansi(const char*p){return p;}
  const char*Ansi(const unsigned short*p){if(!p)return nullptr;if(!Space())return nullptr;unsigned count=0;while(p[count])++count;app.Store(scratch,p,(count+1)*2);unsigned args[]={0,0,scratch,~0u,scratch+520,260,0,0};if(!app.NlsApi(811,args)){Unsupported();return nullptr;}return app.ReadString(scratch+520);}
  unsigned Attributes(const char*p){if(!p||!*p){app.last_error=p?123:87;return ~0u;}if(p[0]=='\\'&&p[1]=='\\'){Unsupported();return ~0u;}char full[260];unsigned node=0,error=app.files.FullPath(p,full);if(!error)error=app.files.store.Lookup(full,node);if(error){app.last_error=error;return ~0u;}auto*n=app.files.store.Get(node);return n?n->attributes:~0u;}
  unsigned Attributes(const unsigned short*p){return Attributes(Ansi(p));}
  unsigned Network(const char*,Win98PathProbes::Resource&,bool){Unsupported();return 50;}
  unsigned Network(const unsigned short*,Win98PathProbes::Resource&,bool){Unsupported();return 50;}
  bool Pattern(const char*p,char*out){unsigned n=strlen(p);if(n>255){Unsupported();return false;}memcpy(out,p,n+1);if(n&&out[n-1]!='\\')out[n++]='\\';memcpy(out+n,"*.*",4);return true;}
  bool Pattern(const unsigned short*p,unsigned short*out){unsigned n=0;while(p[n]){if(n==255){Unsupported();return false;}out[n]=p[n];++n;}if(n&&out[n-1]!='\\')out[n++]='\\';out[n++]='*';out[n++]='.';out[n++]='*';out[n]=0;return true;}
  unsigned First(const char*pattern,char*out){char path[260];unsigned n=strlen(pattern);if(n<4)return ~0u;memcpy(path,pattern,n-3);path[n-3]=0;char full[260];if(app.files.FullPath(path,full)||app.files.store.Lookup(full,directory)){app.last_error=3;return ~0u;}position=1;strcpy(out,".");return 1;}
  unsigned First(const unsigned short*pattern,unsigned short*out){char text[260];const char*p=Ansi(pattern);if(!p)return ~0u;strcpy(text,p);char found[260];unsigned h=First(text,found);out[0]='.';out[1]=0;return h;}
  unsigned More(unsigned,char*out){if(position++==1){strcpy(out,"..");return 1;}unsigned node=0;if(app.files.store.Enumerate(directory,position-3,node)){app.last_error=18;return 0;}auto*n=app.files.store.Get(node);if(!n)return 0;strcpy(out,n->name);return 1;}
  unsigned More(unsigned h,unsigned short*out){char text[260];if(!More(h,text))return 0;if(!Space())return 0;app.Store(scratch,text,strlen(text)+1);unsigned args[]={0,0,scratch,~0u,scratch+520,260};unsigned n=app.NlsApi(594,args);if(!n)return 0;return app.Read(scratch+520,out,n*2);}
  void Close(unsigned){} // Native enumeration owns only an index into Files::Store.
 }e(*this);
 char text[260]={};unsigned short wtext[260]={};if(a[0]){
  unsigned n=0;for(;n<260;++n){if(wide){if(!Read(a[0]+n*2,wtext+n,2))return 0;if(!wtext[n])break;}else{if(!Read(a[0]+n,text+n,1))return 0;if(!text[n])break;}}
  if(n==260){last_error=50;return 0;}
 }
 unsigned result=0,attributes=~0u;
 if(op==0)result=Win98PathProbes::ShellExists(e,a[0]?text:nullptr,attributes);
 else if(op<=2)result=wide?Win98PathProbes::Exists(e,a[0]?wtext:nullptr):Win98PathProbes::Exists(e,a[0]?text:nullptr);
 else if(op<=4)result=wide?Win98PathProbes::Directory(e,a[0]?wtext:nullptr):Win98PathProbes::Directory(e,a[0]?text:nullptr);
 else if(op<=6)result=wide?Win98PathProbes::Empty(e,a[0]?wtext:nullptr):Win98PathProbes::Empty(e,a[0]?text:nullptr);
 else result=wide?Win98PathProbes::Unc(e,a[0]?wtext:nullptr,op>=9?3:2):Win98PathProbes::Unc(e,a[0]?text:nullptr,op>=9?3:2);
 return e.okay?result:0;
}
}
