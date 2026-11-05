// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_shell_file_info.h"
namespace X86 {
// One transient native file view backs the original namespace operations. It is
// not a public Windows PIDL or a second persistent shell icon cache.
struct Application::ShellFileInfoEnvironment:ShellIconCacheEnvironment {
 U attributes=0,node=Files::Invalid,type=0,absolute=0,refs=0;bool parsed=false,synthetic=false;
 char path[260]{},klass[260]{},icon_path[260]{};U icon_resource=0;bool location_ready=false;
 explicit ShellFileInfoEnvironment(Application&a):ShellIconCacheEnvironment(a){}
 ~ShellFileInfoEnvironment(){if(absolute)app.Free(absolute);}
 U Get(U p){return app.Read32(p);}void Put(U p,U v){app.Write32(p,v);}U GetByte(U p){unsigned char b=0;app.Read(p,&b,1);return b;}void Byte(U p,U v){unsigned char b=v;app.Store(p,&b,1);}
 void Text(U p,const char*s,U capacity){if(!capacity)return;U n=strlen(s);if(n>=capacity)n=capacity-1;app.Store(p,s,n);Byte(p+n,0);}
 bool ReadPath(U p,char(&out)[260]){for(U i=0;i<260;++i){if(!app.Read(p+i,out+i,1)){app.last_error=87;return false;}if(!out[i])return true;}app.last_error=206;return false;}
 U Desktop(){return 1;}void NullPath(){app.last_error=87;}U ExecutableType(U){app.Fail("SHGetFileInfo executable signature classification is not ported");return 0;}
 bool IsRoot(U p){const U a[]={p};return app.PathApi("PathIsRootA",a)!=0;}
 bool Relative(U p){const U a[]={p};return app.PathApi("PathIsRelativeA",a)!=0;}
 U Absolute(U p){char input[260]{},full[260]{};if(!ReadPath(p,input))return 0;U error=app.files.FullPath(input,full);if(error){app.last_error=error;return 0;}absolute=app.String(full);return absolute;}
 bool Query(const char*key,const char*name,char*out,U capacity,bool existence=false){
  U h=0,id=0;bool created=false;if(app.registry.Open(0x80000000,key,false,h,created))return false;
  const Registry::Value*v=nullptr;U error=app.registry.Key(h,id);if(!error)error=app.registry.store.Query(id,name,v);
  bool ok=!error;if(ok&&!existence){ok=v->type==1&&v->size&&v->size<=capacity&&v->data[v->size-1]==0;if(ok)memcpy(out,v->data,v->size);}
  app.registry.Close(h);return ok;
 }
 void Class(){klass[0]=0;if(attributes&16){strcpy(klass,"Folder");return;}const char*dot=strrchr(path,'.'),*slash=strrchr(path,'\\');if(dot&&(!slash||dot>slash))Query(dot,"",klass,sizeof klass);}
 U Parse(U p,bool make){char input[260]{};if(!p||!ReadPath(p,input))return 0;
  U error=app.files.FullPath(input,path);if(error){app.last_error=error;return 0;}
  // Files has no UNC provider; never manufacture a network namespace item.
  if(path[0]=='\\'&&path[1]=='\\'){app.Fail("SHGetFileInfo network namespace has no native file provider");return 0;}
  synthetic=make;error=app.files.store.Lookup(path,node);
  if(!make&&error){app.last_error=error;return 0;}
  attributes=make?0:app.files.store.Get(node)->attributes;type=(attributes&16)?0x31:0x32;
  parsed=true;Class();return 2;
 }
 U Last(U p){if(!parsed||p!=2){app.Fail("SHGetFileInfo external namespace PIDLs are not ported");return 0;}return 3;}
 U Type(U){return type;}void Type(U,U v){type=v;attributes=(v&1)?16:0;Class();}
 void TypeName(U,U out,U size){char name[260]{};
  if(!klass[0]||!Query(klass,"",name,sizeof name)||!name[0]){
   if(attributes&16)strcpy(name,"File Folder");else {const char*dot=strrchr(path,'.'),*slash=strrchr(path,'\\');if(dot&&(!slash||dot>slash)&&strlen(dot+1)<70){snprintf(name,sizeof name,"%s File",dot+1);for(char*p=name;*p&&*p!=' ';++p)if(*p>='a'&&*p<='z')*p-=32;}else strcpy(name,"File");}
  }Text(out,name,size);
 }
 void AddRef(U){++refs;}int Bind(U,U,U,U&folder){if(!parsed)return -1;folder=4;++refs;return 0;}void Release(U){if(refs)--refs;}
 int Attributes(U,U,U out){
  if(!parsed)return -1;
  U value=0x40000000;bool root=strlen(path)==3&&path[1]==':'&&path[2]=='\\';
  if(!root)value|=1; // Native Files can copy a file or directory.
  if(!root&&!(attributes&1))value|=0x32; // Move, rename and delete.
  if(attributes&1)value|=0x40000;if(attributes&2)value|=0x80000;
  if(attributes&16){value|=0x30000000;U child=0;for(U i=0;!synthetic&&!app.files.store.Enumerate(node,i,child);++i)if(app.files.store.Get(child)->attributes&16){value|=0x80000000;break;}}
  if(klass[0]&&Query(klass,"IsShortcut",nullptr,0,true))value|=0x10000;
  Put(out,Get(out)&value);return 0;
 }
 void DefaultLocation(){if(location_ready)return;location_ready=true;
  char key[280],value[260]{};if(klass[0]){snprintf(key,sizeof key,"%s\\DefaultIcon",klass);if(Query(key,"",value,sizeof value)&&value[0]){
   char expanded[260]{};U n=app.ExpandProcessEnvironment(value,expanded,sizeof expanded);if(n&&n<=sizeof expanded){strcpy(icon_path,expanded);icon_resource=IconLocation(icon_path);if(!strcmp(icon_path,"%1")){strcpy(icon_path,path);icon_resource=0;}return;}
  }}
  strcpy(icon_path,"C:\\WINDOWS\\SYSTEM\\SHELL32.DLL");icon_resource=0u-(attributes&16?4u:1u);
 }
 int Extractor(U,U,U&out){if(!parsed)return -1;DefaultLocation();out=5;++refs;return 0;}
 void Location(U,U out,U capacity,U index,U&flags){DefaultLocation();Text(out,icon_path,capacity);Put(index,icon_resource);flags=0;}
 U LargeList(){return shared.state.large;}U SmallList(){return shared.state.small;}
 void InitializeLists(){Win98ShellIconCache::Initialize(*this,shared.state);}
 U IconIndex(U,U,bool open,U out){DefaultLocation();U resource=icon_resource;if(open&&(attributes&16)&&!strcmp(icon_path,"C:\\WINDOWS\\SYSTEM\\SHELL32.DLL"))resource=0u-5u;
  // Bootstrap metadata records original zero-based resource-table indices;
  // extracted filename entries record the actual ExtractIcon index.
  for(U i=0;i<shared.count;++i){const auto&m=shared.items[i];if(!strcmp(m.file,icon_path)&&((m.flags==0x80000000&&m.resource==resource)||(m.flags==0&&int(resource)<0&&m.resource<45&&Win98ShellIconCache::ResourceIds[m.resource]==0u-resource))){if(open)Put(out,m.slot);return m.slot;}}
  U large=0,small=0;Extract(icon_path,resource,(shared.state.large_x&65535)|(shared.state.small_x<<16),(shared.state.large_y&65535)|(shared.state.small_y<<16),shared.state.load_flags,large,small);
  if(!large){app.Fail("SHGetFileInfo icon location has no decodable image provider");return ~0u;}
  U slot=Win98ShellIconCache::AddPair(*this,shared.state,large,small,icon_path,resource,0x80000000);if(open)Put(out,slot);return slot;
 }
 U Icon(U handle,U index,U flags){
  if(flags){app.Fail("SHGetFileInfo selected/shortcut image overlay composition is not ported");return 0;}
  auto*list=app.FindImageList(handle);if(!list||index>=list->count||!list->items[index]){app.last_error=6;return 0;}
  auto*image=new WinImage::Image;if(!image||!image->Copy(*list->items[index])){delete image;app.last_error=8;return 0;}return app.NewIcon(image);
 }
 U ResizeIcon(U h,U cx,U cy){auto*icon=app.FindIcon(h);if(!icon)return 0;if(icon->data->width==cx&&icon->data->height==cy)return h;
  auto*image=new WinImage::Image;if(!image||!image->Copy(*icon->data,cx,cy)){delete image;return 0;}U copy=app.NewIcon(image);if(copy)DestroyIcon(h);return copy;
 }
 void DisplayName(U,U,U out,U capacity){const char*name=strrchr(path,'\\');Text(out,name&&name[1]?name+1:path,capacity);}
 void FreePidl(U){parsed=false;}
};
unsigned Application::ShellFileInfoApi(ApiName name,const unsigned*a){
 if(API_IS(name,"SHGetFileInfoW"))return Win98ShellFileInfo::Wide();
 ShellFileInfoEnvironment e(*this);
 return Win98ShellFileInfo::Query(e,a[0],a[1],a[2],a[3],a[4]);
}
}
