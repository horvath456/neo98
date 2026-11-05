// SPDX-License-Identifier: GPL-2.0-or-later
#include "x86_app.h"
#include "win98_shell_registry.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
namespace X86
{
struct Application::ShellRegistryEnvironment
{
    using U=unsigned;
    static constexpr U Prefix=0xf7000000, Capacity=4096;
    Application &app;
    unsigned char scratch[Capacity]{};
    U used=0;
    explicit ShellRegistryEnvironment(Application &a):app(a){}
    static bool Private(U p){return p>=Prefix && p-Prefix<Capacity;}
    U Read8(U p)
    {
        if(Private(p))return scratch[p-Prefix];
        unsigned char b=0;app.Read(p,&b,1);return b;
    }
    void Write8(U p,U v)
    {
        if(Private(p))scratch[p-Prefix]=v;
        else {const unsigned char b=v;app.Store(p,&b,1);}
    }
    U Read32(U p){U value=0;for(U i=0;i<4;++i)value|=Read8(p+i)<<(i*8);return value;}
    void Write32(U p,U v){for(U i=0;i<4;++i)Write8(p+i,v>>(i*8));}
    struct Scratch
    {
        ShellRegistryEnvironment &e;U address,bytes;
        Scratch(ShellRegistryEnvironment &env,U n):e(env),address(Prefix+env.used),bytes((n+3)&~3u)
        {
            if(bytes>Capacity-4 || e.used>Capacity-4-bytes){e.app.Fail("Shell registry scratch exceeds its native frame");bytes=0;return;}
            memset(e.scratch+e.used,0,bytes);e.used+=bytes;
        }
        ~Scratch(){e.used-=bytes;}
    };
    U EmptyString(){return Prefix+Capacity-4;}
    U CharPrev(U start,U current){return current>start?current-1:start;} // configured ACP1252 is SBCS
    U BadWrite(U p,U n){const U a[]={p,n};return app.PointerProbeApi("IsBadWritePtr",a);}
    U LastError(){return app.last_error;}
    U LocalAllocate(U flags,U n){const U a[]={flags,n};return app.Api("LocalAlloc",a);}
    U LocalFree(U p){const U a[]={p};return app.Api("LocalFree",a);}
    struct Text
    {
        char *bytes=nullptr;
        Text(ShellRegistryEnvironment &e,U p)
        {
            U n=p?Win98ShellRegistry::Length(e,p):0;
            if(e.app.Status()==Application::Failed || n==~0u)return;
            bytes=static_cast<char *>(malloc(static_cast<size_t>(n)+1));if(!bytes)return;
            for(U i=0;i<n;++i)bytes[i]=e.Read8(p+i);bytes[n]=0;
        }
        ~Text(){free(bytes);}
    };
    U RegOpen(U key,U path,U access,U out)
    {
        (void)access; // RAM registry has no ACL/access-mask enforcement yet.
        Text name(*this,path);if(!name.bytes)return 8;
        U handle=0;bool created=false;
        const U result=app.registry.Open(key,name.bytes,false,handle,created);
        if(!result)Write32(out,handle);return result;
    }
    U RegCreate(U key,U path,U klass,U access,U out,U disposition)
    {
        (void)access;
        Text name(*this,path),class_name(*this,klass);if(!name.bytes||!class_name.bytes)return 8;
        U handle=0;bool created=false;
        const U result=app.registry.Open(key,name.bytes,true,handle,created,class_name.bytes);
        if(!result){Write32(out,handle);if(disposition)Write32(disposition,created?1:2);}return result;
    }
    U RegClose(U key){return app.registry.Close(key);}
    U RegQuery(U handle,U name,U reserved,U type,U data,U size)
    {
        if(reserved || (data&&!size))return 87;
        U key=0,result=app.registry.Key(handle,key);if(result)return result;
        Text text(*this,name);if(!text.bytes)return 8;
        const Registry::Value *value=nullptr;result=app.registry.store.Query(key,text.bytes,value);if(result)return result;
        const U capacity=size?Read32(size):0;
        if(type)Write32(type,value->type);if(size)Write32(size,value->size);
        if(!data)return 0;if(capacity<value->size)return 234;
        for(U i=0;i<value->size;++i)Write8(data+i,value->data[i]);return 0;
    }
    U RegSet(U handle,U name,U reserved,U type,U data,U size)
    {
        if(reserved || size>65536 || (size&&!data))return 87;
        U key=0,result=app.registry.Key(handle,key);if(result)return result;
        Text text(*this,name);if(!text.bytes)return 8;
        auto *bytes=size?static_cast<unsigned char *>(malloc(size)):nullptr;
        if(size&&!bytes)return 8;
        for(U i=0;i<size;++i)bytes[i]=Read8(data+i);
        result=app.Status()==Application::Failed?87:app.registry.store.Set(key,text.bytes,type,bytes,size);
        free(bytes);return result;
    }
    U RegDeleteValue(U handle,U name)
    {
        U key=0,result=app.registry.Key(handle,key);if(result)return result;
        Text text(*this,name);return text.bytes?app.registry.store.DeleteValue(key,text.bytes):8;
    }
    U RegDeleteKey(U handle,U name)
    {
        U key=0,result=app.registry.Key(handle,key);if(result)return result;
        Text text(*this,name);return text.bytes?app.registry.store.DeleteKey(key,text.bytes):8;
    }
    U RegEnumKey(U handle,U index,U name,U length)
    {
        if(!name||!length)return 87;
        U key=0,result=app.registry.Key(handle,key),child=0;if(result)return result;
        result=app.registry.store.EnumKey(key,index,child);if(result)return result;
        const char *text=app.registry.store.nodes[child].name;const U n=strlen(text);
        if(Read32(length)<=n)return 234;
        for(U i=0;i<=n;++i)Write8(name+i,static_cast<unsigned char>(text[i]));Write32(length,n);return 0;
    }
    U RegEnumValue(U handle,U index,U name,U length,U type,U data,U size)
    {
        if(!name||!length||(data&&!size))return 87;
        U key=0,result=app.registry.Key(handle,key);if(result)return result;
        const Registry::Value *value=nullptr;result=app.registry.store.EnumValue(key,index,value);if(result)return result;
        const U n=strlen(value->name),capacity=Read32(length),data_capacity=size?Read32(size):0;
        if(type)Write32(type,value->type);if(size)Write32(size,value->size);if(capacity<=n)return 234;
        for(U i=0;i<=n;++i)Write8(name+i,static_cast<unsigned char>(value->name[i]));Write32(length,n);
        if(data){if(data_capacity<value->size)return 234;for(U i=0;i<value->size;++i)Write8(data+i,value->data[i]);}return 0;
    }
    U RegInfo(U handle,U children,U max_child,U values,U max_value)
    {
        U key=0,result=app.registry.Key(handle,key);if(result)return result;
        U child_count=0,child_max=0,value_count=0,value_max=0;
        auto &store=app.registry.store;
        for(U i=store.nodes[key].child;i!=Registry::Missing;i=store.nodes[i].sibling)
        {const auto &n=store.nodes[i];if(!n.used||n.deleted)continue;
         ++child_count;const U length=strlen(n.name);if(length>child_max)child_max=length;}
        for(U i=store.nodes[key].values;i!=Registry::Missing;i=store.values[i].next)
        {const auto &v=store.values[i];if(!v.used)continue;
         ++value_count;const U length=strlen(v.name);if(length>value_max)value_max=length;}
        if(children)Write32(children,child_count);if(max_child)Write32(max_child,child_max);
        if(values)Write32(values,value_count);if(max_value)Write32(max_value,value_max);return 0;
    }
    U Expand(U source,U output,U capacity)
    {
        if(capacity>16u*1024*1024){app.last_error=87;return 0;}
        Text text(*this,source);if(!text.bytes){app.last_error=8;return 0;}
        auto *buffer=capacity?static_cast<char *>(malloc(capacity)):nullptr;
        if(capacity&&!buffer){app.last_error=8;return 0;}
        for(U i=0;i<capacity;++i)buffer[i]=Read8(output+i);
        const U result=app.ExpandProcessEnvironment(text.bytes,buffer,capacity);
        for(U i=0;i<capacity;++i)Write8(output+i,static_cast<unsigned char>(buffer[i]));free(buffer);return result;
    }
};
unsigned Application::ShellRegistryApi(ApiName name,const unsigned *a)
{
    namespace W=Win98ShellRegistry;
    ShellRegistryEnvironment e(*this);
    // Tokens for automatic native scratch are not valid guest pointers. Other
    // faults use the runtime's existing memory policy; no general SEH is added.
    unsigned pointers=0;
    if(API_IS(name,"SHRegOpenUSKeyA")||API_IS(name,"SHRegCreateUSKeyA"))pointers=0x0d;
    else if(API_IS(name,"SHRegQueryUSValueA")||API_IS(name,"SHRegGetUSValueA"))pointers=0x5f;
    else if(API_IS(name,"SHRegSetUSValueA")||API_IS(name,"SHRegWriteUSValueA"))pointers=0x0b;
    else if(API_IS(name,"SHRegDeleteUSValueA")||API_IS(name,"SHRegDeleteEmptyUSKeyA"))pointers=3;
    else if(API_IS(name,"SHRegCloseUSKey"))pointers=1;
    else if(API_IS(name,"SHRegEnumUSKeyA"))pointers=0x0d;
    else if(API_IS(name,"SHRegEnumUSValueA"))pointers=0x7d;
    else if(API_IS(name,"SHRegQueryInfoUSKeyA"))pointers=0x1f;
    else if(API_IS(name,"SHGetValueA"))pointers=0x3e;
    else if(API_IS(name,"SHQueryValueExA"))pointers=0x3a;
    else if(API_IS(name,"SHSetValueA"))pointers=0x16;
    else if(API_IS(name,"SHDeleteValueA"))pointers=6;
    else if(API_IS(name,"SHDeleteEmptyKeyA")||API_IS(name,"SHDeleteOrphanKeyA"))pointers=2;
    else if(API_IS(name,"SHEnumKeyExA"))pointers=0x0c;
    else if(API_IS(name,"SHEnumValueA"))pointers=0x7c;
    else if(API_IS(name,"SHQueryInfoKeyA"))pointers=0x1e;
    for(unsigned i=0;i<8;++i)if((pointers&(1u<<i))&&ShellRegistryEnvironment::Private(a[i]))
    {Fail("Guest pointer aliases private shell registry scratch");return 87;}
    switch (name.id)
    {
    API_CASE("SHRegOpenUSKeyA")return W::Open(e,a[0],a[1],a[2],a[3],a[4]);
    API_CASE("SHRegCreateUSKeyA")return W::Create(e,a[0],a[1],a[2],a[3],a[4]);
    API_CASE("SHRegQueryUSValueA")return W::Query(e,a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7]);
    API_CASE("SHRegGetUSValueA")return W::Get(e,a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7]);
    API_CASE("SHRegSetUSValueA")return W::Set(e,a[0],a[1],a[2],a[3],a[4],a[5]);
    API_CASE("SHRegWriteUSValueA")return W::Write(e,a[0],a[1],a[2],a[3],a[4],a[5]);
    API_CASE("SHRegDeleteUSValueA")return W::Delete(e,a[0],a[1],a[2],false);
    API_CASE("SHRegDeleteEmptyUSKeyA")return W::Delete(e,a[0],a[1],a[2],true);
    API_CASE("SHRegCloseUSKey")return W::Close(e,a[0]);
    API_CASE("SHRegEnumUSKeyA")return W::Enumerate(e,0,a[0],a+1,a[4]);
    API_CASE("SHRegEnumUSValueA")return W::Enumerate(e,1,a[0],a+1,a[7]);
    API_CASE("SHRegQueryInfoUSKeyA")return W::Enumerate(e,2,a[0],a+1,a[5]);
    API_CASE("SHGetValueA")return W::GetValue(e,a[0],a[1],a[2],a[3],a[4],a[5]);
    API_CASE("SHSetValueA")return W::SetValue(e,a[0],a[1],a[2],a[3],a[4],a[5]);
    API_CASE("SHDeleteValueA")return W::DeleteValue(e,a[0],a[1],a[2]);
    API_CASE("SHDeleteEmptyKeyA") API_CASE("SHDeleteOrphanKeyA")return W::DeleteEmpty(e,a[0],a[1]);
    API_CASE("SHRegDuplicateHKey")return W::Duplicate(e,a[0]);
    API_CASE("SHQueryValueExA")return W::QueryExpanded(e,a[0],a[1],a[2],a[3],a[4],a[5]);
    API_CASE("SHEnumKeyExA")return e.RegEnumKey(a[0],a[1],a[2],a[3]);
    API_CASE("SHEnumValueA")return e.RegEnumValue(a[0],a[1],a[2],a[3],a[4],a[5],a[6]);
    API_CASE("SHQueryInfoKeyA")return e.RegInfo(a[0],a[1],a[2],a[3],a[4]);
    default:
        break;
    }
    Fail("Unknown shell registry API");return 120;
}
} // namespace X86

// SHELL32 policy cache uses the already implemented SHQueryValueExA provider.
#include "win98_shell_policy.h"
namespace X86 {
struct Application::ShellPolicyEnvironment : ShellRegistryEnvironment {
 explicit ShellPolicyEnvironment(Application &a):ShellRegistryEnvironment(a){}
 void Copy(char *d,const char *s){strcpy(d,s);}
 void Append(char *d,const char *s){strcat(d,s);}
 U Compare(const char *a,const char *b){
  // Same ASCII case provider as native lstrcmpiA; full locale collation is separate.
  for(;;++a,++b){unsigned x=static_cast<unsigned char>(*a),y=static_cast<unsigned char>(*b);
   if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;
   if(x!=y)return x<y?~0u:1u;if(!x)return 0;}
 }
 U Open(U root,const char *path,U options,U access,U &key){
  (void)options;(void)access;bool created=false;
  U result=app.registry.Open(root,path,false,key,created);if(result)key=0;return result;
 }
 U Query(U key,const char *name,U reserved,U &type,U &data,U &bytes){
  U node=0;const Registry::Value *value=nullptr;
  if(!app.registry.Key(key,node) && !app.registry.store.Query(node,name,value) &&
     value->type==2 && value->size<=4 &&
     (!value->size || !memchr(value->data,0,value->size))){
   app.Fail("Unterminated expanded policy string would read past its cache DWORD");return 87;
  }
  Scratch frame(*this,static_cast<U>(strlen(name)+1)+12);
  U kind=frame.address,slot=kind+4,size=kind+8,text=kind+12;
  Write32(kind,type);Write32(slot,data);Write32(size,bytes);
  for(U i=0;i<=strlen(name);++i)Write8(text+i,name[i]);
  U result=Win98ShellRegistry::QueryExpanded(*this,key,text,reserved,kind,slot,size);
  type=Read32(kind);data=Read32(slot);bytes=Read32(size);return result;
 }
 U Close(U key){return app.registry.Close(key);}
};
unsigned Application::ShellPolicyApi(unsigned ordinal,const unsigned *args){
 // Original cache is in SHELL32's IMAGE_SCN_MEM_SHARED.data section.
 static Win98ShellPolicy::Cache cache;
 ShellPolicyEnvironment e(*this);
 if(ordinal==100)return Win98ShellPolicy::Query(e,cache,args[0]);
 if(!args[1])return Win98ShellPolicy::Reset(e,cache,nullptr);
 if(ShellRegistryEnvironment::Private(args[1])){Fail("Policy section aliases private registry scratch");return 0;}
 ShellRegistryEnvironment::Text section(e,args[1]);
 if(!section.bytes){last_error=8;return 0;}
 return Win98ShellPolicy::Reset(e,cache,section.bytes);
}
}

#include "win98_shell_registry_ordinal.h"
namespace X86 {
struct Application::ShellRegistryOrdinalEnvironment:ShellRegistryEnvironment {
 explicit ShellRegistryOrdinalEnvironment(Application&a):ShellRegistryEnvironment(a){}
 U Read16(U p){return Read8(p)|(Read8(p+1)<<8);}
 U Open(bool wide,U key,U path,U options,U access,U out){
  (void)options;
  // ADVAPI32 RegOpenKeyExW is the original KERNEL32 #17 encoding05 gate.
  if(wide){app.last_error=120;return 0;}
  return RegOpen(key,path,access,out);
 }
 U Close(U key){return RegClose(key);}
 U QueryAnsi(U key,U name,U reserved,U type,U data,U size){return Win98ShellRegistry::QueryExpanded(*this,key,name,reserved,type,data,size);}
 U Query(bool wide,U key,U name,U reserved,U type,U data,U size){return wide?Win98ShellRegistryOrdinal::QueryWide(*this,key,name,reserved,type,data,size):QueryAnsi(key,name,reserved,type,data,size);}
 U Allocate(U flags,U bytes){return LocalAllocate(flags,bytes);}
 U Free(U pointer){return LocalFree(pointer);}
 void WideCopy(U to,U from){for(;;to+=2,from+=2){U ch=Read16(from);Write8(to,ch);Write8(to+1,ch>>8);if(!ch)return;}}
 U MultiToWide(U source,U count,U output,U capacity){const U a[]={0,0,source,count,output,capacity};return app.NlsApi(594,a);}
 U WideToAnsi(U source,U output,U capacity){
  // Native automatic scratch has no guest address. Use one short-lived gate buffer.
  U temporary=LocalAllocate(0x40,capacity);if(!temporary){app.Fail("No scratch for Unicode registry name conversion");return 0;}
  const U a[]={0,0,source,~0u,temporary,capacity,0,0};U result=app.NlsApi(811,a);
  if(result){for(U i=0;i<result;++i)Write8(output+i,Read8(temporary+i));}
  else app.Fail("Unicode registry name does not fit the original 260-byte ANSI frame");
  LocalFree(temporary);return result;
 }
};
unsigned Application::ShellRegistryOrdinalApi(unsigned ordinal,const unsigned *args){
 ShellRegistryOrdinalEnvironment e(*this);
 unsigned mask=ordinal==505?0:ordinal==506||ordinal==507?6:ordinal==508||ordinal==510?14:58;
 for(unsigned i=0;i<6;++i)if((mask&(1u<<i))&&ShellRegistryEnvironment::Private(args[i])){
  Fail("Shell ordinal pointer aliases private registry scratch");return 87;
 }
 return Win98ShellRegistryOrdinal::Call(e,ordinal,args);
}
}

#include "win98_shell_icon_cache.h"
#include <stdio.h>
namespace X86 {
struct Application::ShellIconCacheEnvironment:ShellRegistryEnvironment {
 struct Metadata {char file[260]{};unsigned resource=0,flags=0,slot=0;};
 struct Shared {Win98ShellIconCache::State state;Metadata items[128];unsigned count=0,locks=0;} &shared;
 static Shared &System(){static Shared value;return value;}
 explicit ShellIconCacheEnvironment(Application&a):ShellRegistryEnvironment(a),shared(System()){}
 U Open(U root,const char*path){U key=0;bool created=false;U result=app.registry.Open(root,path,false,key,created);if(result)app.last_error=result;return result?0:key;}
 U OpenExplorer(const char*name){char path[260];snprintf(path,sizeof path,"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\%s",name);return Open(0x80000002,path);}
 void Close(U key){RegClose(key);}
 void QueryText(U key,const char*name,char*out,U capacity){
  Scratch frame(*this,static_cast<U>(strlen(name)+1)+capacity+8);U length=frame.address,data=length+8,text=data+capacity;
  Write32(length,capacity);for(U i=0;i<capacity;++i)Write8(data+i,static_cast<unsigned char>(out[i]));
  for(U i=0;i<=strlen(name);++i)Write8(text+i,name[i]);
  Win98ShellRegistry::QueryExpanded(*this,key,text,0,0,data,length);
  for(U i=0;i<capacity;++i)out[i]=Read8(data+i);
 }
 void QueryIndex(U key,U index,char*out,U capacity){char name[12];snprintf(name,sizeof name,"%u",index);QueryText(key,name,out,capacity);}
 U Decimal(const char*text,U capacity){U n=0,result=0;for(;n<capacity&&text[n]>='0'&&text[n]<='9';++n)result=result*10+unsigned(text[n]-'0');if(n==capacity)app.Fail("Unterminated decimal shell setting exceeds its original20-byte frame");return result;}
 U Metric(U index){return app.SystemMetric(index);}
 U Depth(){const U zero[]={0};U dc=app.Api("GetDC",zero);const U p[]={dc,14},b[]={dc,12};U planes=app.GdiApi("GetDeviceCaps",p),bits=app.GdiApi("GetDeviceCaps",b);const U release[]={0,dc};app.Api("ReleaseDC",release);return planes*bits;}
 void Lock(){++shared.locks;}void Unlock(){if(shared.locks)--shared.locks;} // Cooperative provider calls do not yield.
 void ClearIndex(){shared.count=0;}
 void ClearRelated(){} // No native filename/classification cache is attached in this bootstrap package.
 bool LoadDiskCache(Win98ShellIconCache::State&,U,U,U,U,U){U id=0;U error=app.files.store.Lookup("C:\\WINDOWS\\ShellIconCache",id);if(error){app.last_error=error;return false;}app.Fail("Existing ShellIconCache stream requires the deferred persistent-cache reader");return false;}
 U Create(U x,U y,U flags,U count,U grow){const U a[]={x,y,flags,count,grow};return app.ImageApi("ImageList_Create",a);}
 U Remove(U list,U at){const U a[]={list,at};return app.ImageApi("ImageList_Remove",a);}
 U Resize(U list,U x,U y){const U a[]={list,x,y};return app.ImageApi("ImageList_SetIconSize",a);}
 U Destroy(U list){const U a[]={list};return app.ImageApi("ImageList_Destroy",a);}
 U Background(U list,U color){const U a[]={list,color};return app.ImageApi("ImageList_SetBkColor",a);}
 U Color(U index){const U a[]={index};return app.Api("GetSysColor",a);}
 void ModuleFilename(char*out,U n){snprintf(out,n,"C:\\WINDOWS\\SYSTEM\\SHELL32.DLL");}
 U LoadResource(U id,U x,U y,U flags){return app.ShellBuiltinIcon(id,x,y,flags);}
 U IconLocation(char*text){U address=app.String(text);if(!address)return 0;const U a[]={address};U result=app.ShellPathSyntaxApi("PathParseIconLocationA",a);const char*after=app.ReadString(address);memcpy(text,after,strlen(after)+1);app.Free(address);return result;}
 void Extract(const char*path,U resource,U cx,U cy,U flags,U &large,U &small){app.ShellExtractIconPair(path,resource,cx,cy,flags,large,small);}
 U NextSlot(){return ~0u;} // Bootstrap has just cleared the lists and its filename index.
 U Replace(U list,U slot,U icon){const U a[]={list,slot,icon};return app.ImageApi("ImageList_ReplaceIcon",a);}
 void RecordMetadata(const char*file,U resource,U flags,U slot){
  if(shared.count==128){app.Fail("Native shell bootstrap metadata capacity exceeded");return;}
  auto &item=shared.items[shared.count++];snprintf(item.file,sizeof item.file,"%s",file);item.resource=resource;item.flags=flags;item.slot=slot;
 }
 void DestroyIcon(U icon){const U a[]={icon};app.ImageApi("DestroyIcon",a);}
 void AddPair(Win98ShellIconCache::State&s,U large,U small,const char*file,U resource,U flags){Win98ShellIconCache::AddPair(*this,s,large,small,file,resource,flags);}
 void Dirty(){unsigned windows[User::WindowCapacity],count=0;if(User::WindowSnapshot(User::GetDesktopWindow(),windows,User::WindowCapacity,count))for(unsigned i=0;i<count;++i)User::InvalidateRect(windows[i],nullptr,true);}
 void ListsChanged(){const U large[]={shared.state.large},small[]={shared.state.small};if(!app.ImageApi("ImageList_GetImageCount",large)||!app.ImageApi("ImageList_GetImageCount",small))app.Fail("Shell bootstrap produced no usable paired icon lists");Dirty();} // Native drawing invalidation; general shell change subscriptions are separate.
 void Notify(U,U,U,U){Dirty();}
};
unsigned Application::ShellIconCacheApi(unsigned ordinal,const unsigned *a){
 ShellIconCacheEnvironment e(*this);
 unsigned result=ordinal==71?Win98ShellIconCache::GetLists(e,e.shared.state,a[0],a[1]):Win98ShellIconCache::Initialize(e,e.shared.state);
 if(state==Failed){
  // A native provider boundary must not leave an unusable shared cache for the next Application.
  if(e.shared.state.large)e.Destroy(e.shared.state.large);
  if(e.shared.state.small)e.Destroy(e.shared.state.small);
  e.shared.state.large=e.shared.state.small=0;e.shared.count=0;
 }
 return result;
}
}

#include "x86_shell_cabinet.inc"

#include "x86_special_folders.inc"

#include "x86_shell_file_info.inc"
