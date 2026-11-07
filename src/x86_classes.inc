// SPDX-License-Identifier: GPL-2.0-or-later
// Guest memory/resource boundary around the original native USER class records.
#include <stdio.h>
#include <stdlib.h>
namespace X86 {
unsigned Application::RegisterNativeClass(const unsigned f[12],const char*name,const char*menu) {
 const unsigned result=User::RegisterNativeClass(f,name,menu);if(User::NativeClassError())last_error=User::NativeClassError();return result;
}
unsigned Application::GetNativeClassInfo(unsigned instance,const char*name,unsigned f[12]) {return User::GetNativeClassInfo(instance,name,f);}
unsigned Application::LoadClassMenu(unsigned token,unsigned module) {
 // A Win16 WNDCLASS names resources in its NE instance. The common window
 // creation path must not look for those menus in the PE module table.
 if(win16.state&&module<65536)
  return Win16LoadMenu(module,token<65536?(0x8000|(token&0x7fff)):0,token<65536?nullptr:User::ClassMenuName(token));
 if(token<65536)return LoadMenu(token,module,false);
 const char*name=User::ClassMenuName(token);if(!name)return 0;
 if(!module)module=image.Base();const Pe32::Image*source=&image;
 if(module!=image.Base()){unsigned i=ModuleIndex(module);if(i>=ModuleCapacity||!modules[i].pe)return 0;source=modules[i].pe;}
 unsigned short wide[260];Pe32::Image::ResourceKey type{4,nullptr,0},key;
 const unsigned guest=PublishClassMenu(token);if(!guest||!ResourceName(guest,key,wide))return 0;
 if(!key.name)return LoadMenu(key.id,module,false);
 Pe32::Image::ResourceEntry resource;
 if(!source->FindResource(type,key,~0u,resource)||resource.size<4||resource.size>1024*1024)return 0;
 // Resource bytes are guest-mapped by the same module loader used by FindResource.
 unsigned char *data=(unsigned char*)malloc(resource.size);if(!data){last_error=8;return 0;}
 if(!Read(source->Base()+resource.data,data,resource.size)){free(data);return 0;}
 unsigned kind=data[0]|(unsigned(data[1])<<8),position=4+data[2]+(unsigned(data[3])<<8),result=0;
 if(kind<=1&&position<=resource.size&&(kind==0||(resource.size>=8&&position>=8)))result=ParseMenu(data,resource.size,position,0,kind==1);
 free(data);return result;
}
unsigned Application::PublishClassMenu(unsigned token) {
 if(token<65536)return token;const char*name=User::ClassMenuName(token);if(!name)return 0;
 for(auto&lease:class_menu_leases)if(lease.token==token){if(!strcmp(ReadString(lease.guest),name))return lease.guest;Free(lease.guest);lease={};break;}
 for(auto&lease:class_menu_leases)if(!lease.token){unsigned guest=Allocate(strlen(name)+1);if(!guest)return 0;Store(guest,name,strlen(name)+1);lease={token,guest};return guest;}last_error=8;return 0;
}
unsigned Application::ClassApi(ApiName name,const unsigned*a) {
 char klass[256]={};unsigned namearg=0,instance=0;
 if(API_IS(name,"GetClassNameA")){unsigned capacity=a[2]&65535;if(!capacity||(capacity&32768))return 0;if(capacity>256)capacity=256;unsigned length=User::GetClassName(a[0],klass,capacity);if(!User::IsWindow(a[0]))return 0;return Store(a[1],klass,length+1)?length:0;}
 const bool reg=API_IS(name,"RegisterClassA")||API_IS(name,"RegisterClassExA");
 const bool info=API_IS(name,"GetClassInfoA")||API_IS(name,"GetClassInfoExA");
 unsigned f[12]={48};
 if(reg){if(!a[0])return 0;if(API_IS(name,"RegisterClassA")){if(!Read(a[0],f+1,40))return 0;}else if(!Read(a[0],f,48))return 0;namearg=f[10];}
 else if(info){instance=a[0];namearg=a[1];if(!namearg||!a[2])return 0;}
 else if(API_IS(name,"UnregisterClassA")){namearg=a[0];instance=a[1]?a[1]:image.Base();}
 else {
  if(!User::IsWindow(a[0])){last_error=1400;return 0;}
  const int index=(short)a[1];unsigned flags=(!strncmp(name,"Set",3)?1u:0u)|(strstr(name,"Long")?2u:0u);
  if(index<0&&(index< -34||index> -8||(index&1)||index== -22||index== -28||index== -30))return 0;
  unsigned result=User::ClassAccess(a[0],index,(flags&1)?a[2]:0,flags);
  if(index==-8)result=PublishClassMenu(result);
  return flags&2?result:result&65535;
 }
 if(namearg<65536){if(!User::ClassAtomName(namearg,klass,sizeof klass)){if(!namearg)return 0;snprintf(klass,sizeof klass,"#%u",namearg);}}
 else {const char*text=ReadString(namearg);if(strlen(text)>=sizeof klass){last_error=87;return 0;}strcpy(klass,text);}
 if(reg){
  // RegisterClassA/ExA call KERNEL32!MapHInstLS before USER validation.
  // Its original zero path substitutes the current task's instance.
  if(!f[5])f[5]=image.Base();
  auto rejected=[&](const char*reason){if(log){char trace[384];snprintf(trace,sizeof trace,"Class provider rejected class=%s reason=%s procedure=%08x instance=%08x icon=%08x cursor=%08x small=%08x",klass,reason,f[2],f[5],f[6],f[7],f[11]);log(trace);}return 0u;};
  const bool native_proc=(f[2]>=0x71000001&&f[2]<=0x71000004)||f[2]==0x71000020||f[2]==0x71000021||f[2]==0x71000030||f[2]==0x71000031||f[2]==0x71000005||f[2]==0x71000006||f[2]==0x7100000a||(f[2]>=0x71000100&&f[2]<=0x71000112);
  if(f[2]&&!native_proc&&!virtual_memory.Executable(f[2]))return rejected("procedure");
  if(f[5]&&ModuleIndex(f[5])>=ModuleCapacity)return rejected("instance");
  for(unsigned index=6;index<=7;++index)if(f[index]&&!FindIcon(f[index]))return rejected(index==6?"icon":"cursor");
  if(f[11]&&!FindIcon(f[11]))return rejected("small icon");
  const char*menu=f[9]>65535?ReadString(f[9]):nullptr;
  const unsigned result=RegisterNativeClass(f,klass,menu);
  if(!result&&log){char trace[384];snprintf(trace,sizeof trace,"Class registration rejected class=%s style=%08x extra=%u/%u instance=%08x brush=%08x error=%u",klass,f[1],f[3],f[4],f[5],f[8],last_error);log(trace);}
  return result;
 }
 if(info){unsigned result=GetNativeClassInfo(instance,klass,f);if(!result){if(log){char trace[320];snprintf(trace,sizeof trace,"Class lookup missed class=%s instance=%08x",klass,instance);log(trace);}return 0;}f[9]=PublishClassMenu(f[9]);f[10]=namearg;
  if(API_IS(name,"GetClassInfoExA")){if(!Store(a[2]+4,f+1,44))return 0;return result;}
  if(!Store(a[2],f+1,40))return 0;return 1;
 }
 unsigned before[12]={48};GetNativeClassInfo(instance,klass,before);
 const unsigned result=User::UnregisterNativeClass(instance,klass);
 if(result&&before[9]>65535)for(auto&lease:class_menu_leases)if(lease.token==before[9]){Free(lease.guest);lease={};}
 return result;
}
}
