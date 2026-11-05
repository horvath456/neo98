// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_shell_cabinet.h"
namespace X86 {
struct Application::CabinetState:Win98ShellCabinet::Settings{
 Application*owner=nullptr;unsigned explorer=0;
 static CabinetState*Find(Application*p,bool create){static CabinetState slots[64];CabinetState*empty=nullptr;for(auto&s:slots){if(s.owner==p)return &s;if(!s.owner&&!empty)empty=&s;}if(create&&empty){empty->owner=p;return empty;}return nullptr;}
};
struct Application::CabinetEnvironment:ShellRegistryEnvironment{
 CabinetState&state;CabinetEnvironment(Application&a,CabinetState&s):ShellRegistryEnvironment(a),state(s){}
 void Lock(){app.CrtLock(true);}void Unlock(){app.CrtLock(false);}
 U ExplorerKey(){if(!state.explorer){bool made=false;app.registry.Open(0x80000001,Win98ShellCabinet::Explorer,false,state.explorer,made);}return state.explorer;}
 U InitialFlags(){return 0;}U InitialSettingsBits(){return 0;}
 bool OtherPlatform(){return false;}bool LowMemory(){return false;} // Native Win98 machine configuration: normal memory class.
 U Restricted(U p){return app.ShellPolicyApi(100,&p);}
 U CleanBoot(){return app.SystemMetric(67);}
 U Open(U parent,const char*name,U,U&key){bool made=false;return app.registry.Open(parent,name,false,key,made);}
 U Create(const char*name,U&key){bool made=false;return app.registry.Open(0x80000001,name,true,key,made);}
 U Close(U key){return app.registry.Close(key);}
 U Query(U key,const char*name,U*type,void*data,U*bytes){
  U capacity=*bytes,storage=!strcmp(name,"ShellState")||!strcmp(name,"Hidden")?28:!strcmp(name,"Settings")?12:4;
  if(capacity>storage){app.last_error=87;return 87;}
  if(!strcmp(name,"Hidden")){U node=0;const Registry::Value*v=nullptr;if(!app.registry.Key(key,node)&&!app.registry.store.Query(node,name,v)&&v->size>4){app.last_error=87;return 87;}}
  Scratch local(*this,static_cast<U>(strlen(name))+1+12+capacity);U kind=local.address,size=kind+4,text=size+4,buffer=text+strlen(name)+1;
  Write32(kind,type?*type:0);Write32(size,capacity);for(U i=0;i<=strlen(name);++i)Write8(text+i,name[i]);for(U i=0;i<capacity;++i)Write8(buffer+i,static_cast<unsigned char*>(data)[i]);
  U result=Win98ShellRegistry::QueryExpanded(*this,key,text,0,type?kind:0,buffer,size);
  if(type)*type=Read32(kind);*bytes=Read32(size);for(U i=0;i<capacity;++i)static_cast<unsigned char*>(data)[i]=Read8(buffer+i);return result;
 }
 U Set(U key,const char*name,U type,const void*data,U bytes){U node=0,result=app.registry.Key(key,node);return result?result:app.registry.store.Set(node,name,type,data,bytes);}
 void CopyResult(void*out,const void*record,U bytes){if(bytes>12){app.last_error=87;return;}memcpy(out,record,bytes);}
};
unsigned Application::CabinetApi(unsigned ordinal,const unsigned*a){
 auto*s=CabinetState::Find(this,true);if(!s){last_error=8;return 0;}CabinetEnvironment e(*this,*s);
 if(ordinal==290){unsigned value=Win98ShellCabinet::GetSettings(e,*s,a[1]);if(!a[0]){last_error=998;return 0;}return Store(a[0],&value,4)?0:0;}
 if(ordinal==652){if(!a[0])return 0;unsigned short size=0;if(!Read(a[0],&size,2))return 0;Win98ShellCabinet::Record record{};unsigned bytes=size<12?size:12;if(!Read(a[0],&record,bytes))return 0;record.size=size;return Win98ShellCabinet::Write(e,*s,&record);}
 unsigned bytes=ordinal==651?12:a[1];if(bytes>12){last_error=87;return 0;}
 unsigned char result[12]={};unsigned okay=Win98ShellCabinet::Read(e,*s,a[0]?result:nullptr,bytes);if(a[0]&&bytes)Store(a[0],result,bytes);return okay;
}
void Application::FreeCabinet(){auto*s=CabinetState::Find(this,false);if(!s)return;if(s->explorer)registry.Close(s->explorer);*s=CabinetState{};}
}
