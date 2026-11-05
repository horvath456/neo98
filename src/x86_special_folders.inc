// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_special_folders.h"
namespace X86 {
struct Application::SpecialFolderState:Win98SpecialFolders::State {
 Application*owner=nullptr;char*paths[30]={};
 static SpecialFolderState*Find(Application*p,bool create){static SpecialFolderState slots[64];SpecialFolderState*empty=nullptr;for(auto&s:slots){if(s.owner==p)return &s;if(!s.owner&&!empty)empty=&s;}if(create&&empty){empty->owner=p;return empty;}return nullptr;}
};
struct Application::SpecialFolderEnvironment:ShellRegistryEnvironment {
 SpecialFolderState&state;unsigned index;bool okay=true;
 SpecialFolderEnvironment(Application&a,SpecialFolderState&s,U i):ShellRegistryEnvironment(a),state(s),index(i){}
 U Epoch(){return 0;} // Native shell notifications do not yet advance an association/path generation.
 void Lock(){app.CrtLock(true);}void Unlock(){app.CrtLock(false);}
 void Limit(){okay=false;app.last_error=50;}
 U Open(U root,const char*sub){if(root!=0x80000001&&root!=0x80000002)return 0;char path[260]="Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\";strcat(path,sub);U key=0;bool made=false;app.registry.Open(root,path,true,key,made);return key;}
 void Close(U key){app.registry.Close(key);}
 U Query(U key,const char*name,U*type,char*out){
  Scratch local(*this,U(strlen(name))+1+12+260);U kind=local.address,size=kind+4,text=size+4,buffer=text+strlen(name)+1;
  Write32(kind,0);Write32(size,260);for(U i=0;i<=strlen(name);++i)Write8(text+i,name[i]);
  U result=Win98ShellRegistry::QueryExpanded(*this,key,text,0,kind,buffer,size);*type=Read32(kind);
  for(U i=0;i<260;++i)out[i]=Read8(buffer+i);if(!memchr(out,0,260)){out[259]=0;return 87;}return result;
 }
 void Set(U key,const char*name,const char*path){U node=0;if(!app.registry.Key(key,node))app.registry.store.Set(node,name,1,path,U(strlen(path))+1);}
 void PersonalChanged(){} // Original private Personal text cache has no separate native counterpart.
 void Windows(char*out){strcpy(out,"C:\\WINDOWS");} // Same configured directory as native GetWindowsDirectoryA.
 bool LongNames(const char*){return true;} // Native RAM volume accepts long component names.
 void DefaultName(U i,bool lfn,char*out){strcpy(out,lfn?Win98SpecialFolders::Table[i].long_name:Win98SpecialFolders::Table[i].short_name);}
 void CombineDefault(char*out,const char*leaf){while(*leaf=='\\')++leaf;U n=strlen(out);if(n+strlen(leaf)+2>260){Limit();return;}if(n&&out[n-1]!='\\')strcat(out,"\\");strcat(out,leaf);}
 bool Canonicalize(char*out){if(!okay)return false;char full[260];if(!out[0]||out[0]=='\\'||app.files.FullPath(out,full)){Limit();return false;}strcpy(out,full);return true;} // Native local LFN provider; no network/8.3 drive probing.
 U Attributes(const char*p){U node=0;if(!okay||app.files.store.Lookup(p,node))return ~0u;auto*n=app.files.store.Get(node);return n?n->attributes:~0u;}
 bool MoveCollision(const char*p,U suffix){char target[280];snprintf(target,sizeof target,"%s.%u",p,suffix);if(strlen(target)>=260){Limit();return false;}return !app.files.store.Rename(p,target);}
 U OwnerMedia(U,const char*,U a){Limit();return a;} // Original interactive network/removable-media path needs its UI provider.
 void CreateDirectories(const char*p){if(!okay)return;char part[260];strcpy(part,p);for(U i=3;part[i];++i)if(part[i]=='\\'){part[i]=0;U e=app.files.store.MakeDirectory(part);part[i]='\\';if(e&&e!=183)return;}app.files.store.MakeDirectory(part);}
 void SetHidden(const char*p){app.files.store.Attributes(p,2);}
 U Virtual(U){Limit();return 0;}
 U PathItem(const char*p){if(!okay)return 0;auto*copy=static_cast<char*>(malloc(strlen(p)+1));if(!copy){app.last_error=8;return 0;}strcpy(copy,p);free(state.paths[index]);state.paths[index]=copy;return index+1;}
 U Share(U p){return p;}void FreeTemporary(U){} // Native internal path token has one cache-owned string; it is not a PIDL.
 void ReleaseShared(U p){if(p&&p<=30){free(state.paths[p-1]);state.paths[p-1]=nullptr;}}
 U PathFromItem(U p,char*out){if(!okay||!p||p>30||!state.paths[p-1])return 0;strcpy(out,state.paths[p-1]);return 1;}
};
unsigned Application::SpecialFolderApi(ApiName name,const unsigned*a){
 const bool wide=API_IS(name,"SHGetSpecialFolderPathW");char path[260]={};unsigned result=0;
 int index=Win98SpecialFolders::Index(a[2]);auto*s=SpecialFolderState::Find(this,true);
 if(!s){last_error=8;}else if(index>=0&&index<7){last_error=50;} // No fabricated namespace PIDLs.
 else {SpecialFolderEnvironment e(*this,*s,index<0?0:unsigned(index));result=Win98SpecialFolders::Path(e,*s,a[0],path,a[2],a[3]);}
 if(wide){
  // SHELL32 always converts, preserving the ANSI BOOL result.
  unsigned input=Allocate(unsigned(strlen(path))+1);if(!input)return 0;Store(input,path,unsigned(strlen(path))+1);
  unsigned args[]={0,0,input,~0u,a[1],260};NlsApi(594,args);Free(input);
 }else PutString(a[1],path);
 return result;
}
void Application::FreeSpecialFolders(){auto*s=SpecialFolderState::Find(this,false);if(!s)return;for(auto*p:s->paths)free(p);*s=SpecialFolderState{};}
}
