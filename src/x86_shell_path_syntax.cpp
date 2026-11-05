// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_shell_path_syntax.h"
#include "win98_shell_path_syntax_exports.h"
#include "x86_app.h"
#include <stdlib.h>
#include <string.h>
#include "win98_shell_command.h"
#include "win98_path_probes.h"
#include "win98_shell_execute.h"
namespace X86 {
unsigned Application::ShellPathSyntaxApi(ApiName name,const unsigned*a){
 struct Env:Win98ShellPathSyntax::Environment{Application&app;unsigned scratch=0;Env(Application&a):app(a){}~Env(){if(scratch)app.Free(scratch);}unsigned Byte(unsigned p)override{unsigned char c=0;app.Read(p,&c,1);return c;}unsigned Word(unsigned p)override{unsigned short c=0;app.Read(p,&c,2);return c;}
  void Put(unsigned p,unsigned c,bool wide)override{unsigned short word=c;app.Store(p,&word,wide?2:1);}
  void Move(unsigned dst,unsigned src,unsigned bytes)override{if(!bytes)return;auto*p=(unsigned char*)malloc(bytes);if(!p){app.Fail("SHLWAPI path mutation copy allocation failed");return;}if(app.Read(src,p,bytes))app.Store(dst,p,bytes);free(p);}
  bool Lead(unsigned c)override{return app.NlsApi(537,&c)!=0;}
  unsigned Next(unsigned p)override{unsigned c=Byte(p);return !c?p:p+(Lead(c)&&Byte(p+1)?2:1);}
  unsigned Prev(unsigned first,unsigned at,bool wide)override{if(wide)return at==first?at:at-2;unsigned prev=first;while(first<at){prev=first;unsigned next=Next(first);if(next==first)break;first=next;}return prev;}
  bool Dbcs()override{if(!scratch)scratch=app.Allocate(20);if(!scratch)return false;unsigned args[]={0,scratch};return app.NlsApi(318,args)&&Byte(scratch+6);}
  void EndBeyondTerminator()override{app.Fail("SHLWAPI path provider reached invalid reverse endpoint");}
 }e(*this);int operation=ShellPathSyntaxOperation(name);if(operation<0){Fail("Unknown SHLWAPI path-syntax export");return 0;}return Win98ShellPathSyntax::Call(e,operation,a);
}
struct Application::ShellCommandEnvironment {
 Application &app;unsigned scratch=0;char extension[260]={};
 explicit ShellCommandEnvironment(Application&a):app(a){scratch=app.Allocate(1040);if(!scratch)app.Fail("Shell command scratch unavailable");}
 ~ShellCommandEnvironment(){if(scratch)app.Free(scratch);}
 using U=unsigned;
 void Boundary(const char*why){app.Fail(why);}
 bool Lead(U c){return app.NlsApi(537,&c)!=0;}
 bool UncRoot(const char*p){if(p[0]!='\\'||p[1]!='\\')return false;U count=0;while(*p){if(*p=='\\')++count;p+=Lead((unsigned char)*p)&&p[1]?2:1;}return count==2||count==3;}
 U QueryPath(const char*api,const char*path){app.Store(scratch,path,strlen(path)+1);const U args[]={scratch};return !strcmp(api,"PathIsRootA")?app.PathApi(api,args):app.ShellPathSyntaxApi(api,args);}
 bool Root(const char*p){return QueryPath("PathIsRootA",p)!=0;}
 bool FileSpec(const char*p){return QueryPath("PathIsFileSpecA",p)!=0;}
 void Unquote(char*p){QueryPath("PathUnquoteSpacesA",p);strcpy(p,app.ReadString(scratch));}
 void Trim(char*p){QueryPath("PathRemoveBlanksA",p);strcpy(p,app.ReadString(scratch));}
 const char*Extension(const char*p){U result=QueryPath("PathFindExtensionA",p);strcpy(extension,app.ReadString(result));return extension;}
 bool Space(const char*p){app.Store(scratch,p,strlen(p)+1);const U args[]={scratch,32};return app.ShellSearchApi("StrChrA",args)!=0;}
 void Combine(char*out,const char*base,const char*file){if(strlen(base)+strlen(file)+2>260){out[0]=0;return;}app.Store(scratch+260,base,strlen(base)+1);app.Store(scratch+520,file,strlen(file)+1);const U args[]={scratch,scratch+260,scratch+520};app.PathApi("PathCombineA",args);strcpy(out,app.ReadString(scratch));}
 bool Append(char*out,const char*file){char base[260];strcpy(base,out);if(strlen(base)+strlen(file)+2>260)return false;Combine(out,base,file);return true;}
 int Compare(const char*a,const char*b){app.Store(scratch,a,strlen(a)+1);app.Store(scratch+260,b,strlen(b)+1);const U args[]={scratch,scratch+260};return int(app.Api("lstrcmpiA",args));}
 void Directory(char*out,const char*name){const U args[]={scratch,260};app.Api(name,args);strcpy(out,app.ReadString(scratch));}
 void SystemDirectory(char*out){Directory(out,"GetSystemDirectoryA");}
 void WindowsDirectory(char*out){Directory(out,"GetWindowsDirectoryA");}
 const char*EnvironmentPath(){U address=app.FindProcessEnvironment("PATH");if(!address)return nullptr;const char*p=app.ReadString(address);U n=strlen(p)+1;char*out=(char*)malloc(n);if(out)memcpy(out,p,n);return out;}
 void FreeEnvironment(const char*p){free((void*)p);}
 U Attributes(const char*path){if(!path||!*path){app.last_error=path?123:87;return ~0u;}char full[260];U error=app.files.FullPath(path,full),node=0;if(!error)error=app.files.store.Lookup(full,node);if(error){app.last_error=error;return ~0u;}const auto*n=app.files.store.Get(node);return n?n->attributes:~0u;}
 const char*Next(const char*p){return p+((Lead((unsigned char)*p)&&p[1])?2:1);}
 U SetMode(U value){return app.Api("SetErrorMode",&value);}
 U Network(const char*,Win98PathProbes::Resource&,bool){Boundary("Shell UNC existence requires deferred network path provider");return 50;}
 U Exists(const char*path){return Win98PathProbes::Exists(*this,path);}
 U ShellExists(const char*path){U attributes;return Win98PathProbes::ShellExists(*this,path,attributes);}
 U NetworkResolve(const char*){Boundary("Shell root resolution requires deferred network PIDL provider");return 0;}
 void Qualify(char*path,const char*directory,bool){if(path[0]=='\\'&&path[1]=='\\'){Boundary("Shell UNC qualification requires deferred network provider");return;}char combined[260]={},full[260]={};const char*input=path;if(directory){Combine(combined,directory,path);input=combined;}U error=app.files.FullPath(input,full);if(error){app.last_error=error;return;}strcpy(path,full);}
 char*Allocate(U bytes){if(bytes>8192){Boundary("Resolved command exceeds native8192-byte output provider");return nullptr;}return (char*)malloc(bytes);}void Free(char*p){free(p);}
};
unsigned Application::ShellCommandApi(unsigned operation,const unsigned*a){
 ShellCommandEnvironment e(*this);char path[8192]={};const char*input=a[0]?ReadString(a[0]):nullptr;if(input)strcpy(path,input);
 if(operation!=653&&strlen(path)>=260){Fail("Shell path exceeds original260-byte frame");return 0;}
 if(operation==653){char output[8192]={};unsigned result=Win98ShellCommand::Process(e,input?path:nullptr,a[1]?output:nullptr,a[2],a[3]);if(a[1]&&result!=~0u)Store(a[1],output,result);return result;}
 if(a[0]&&operation!=40&&operation!=471){if(!virtual_memory.Access(a[0],260,true)){last_error=87;return 0;}Read(a[0],path,260);}
 const char*directories[33]={};char storage[32][260]={};
 if((operation==51||operation==5||operation==479)&&a[1])for(unsigned i=0;i<33;++i){unsigned ptr=Read32(a[1]+i*4);if(!ptr)break;if(i==32){Fail("Shell search directory list exceeds32entries");return 0;}const char*text=ReadString(ptr);if(strlen(text)>=260){Fail("Shell search directory exceeds260-byte frame");return 0;}strcpy(storage[i],text);directories[i]=storage[i];}
 unsigned result=0;auto dirs=a[1]?directories:nullptr;
 if(operation==40)return input?Win98ShellCommand::Relative(e,path):0;
 if(operation==3)result=Win98ShellCommand::Extension(e,input?path:nullptr,a[1]);
 else if(operation==5||operation==479)result=Win98ShellCommand::Search(e,input?path:nullptr,dirs,operation==479?0:a[2]);
 else if(operation==51){if((a[2]&4)&&(!a[1]||!directories[0])){Fail("Shell resolver flag4 requires a directory array");return 0;}if(!input){last_error=87;return 0;}result=Win98ShellCommand::Resolve(e,path,dirs,a[2]);}
 else if(operation==471)return e.Exists(input?path:nullptr);
 if(a[0])Store(a[0],path,260);return result;
}

// Shell execution uses the same Files, Registry, NLS and path providers.
struct Application::ShellExecuteEnvironment:ShellCommandEnvironment {
 using U=unsigned;explicit ShellExecuteEnvironment(Application&a):ShellCommandEnvironment(a){}
 const char*Find(const char*p,char c){for(;*p;p+=Lead((unsigned char)*p)&&p[1]?2:1)if(*p==c)return p;return nullptr;}
 const char*FileName(const char*p){const char*last=p;for(const char*q=p;*q;q+=Lead((unsigned char)*q)&&q[1]?2:1)if(*q=='\\'||*q=='/'||*q==':')last=q+1;return last;}
 U Query(U root,const char*path,const char*name,char*out,U capacity){
  // Reuse implemented #508/#509 registry querying, including value coercion.
  if(out&&capacity)out[0]=0;U frame=app.Allocate(2048);if(!frame)return 8;app.Store(frame,path,strlen(path)+1);if(name)app.Store(frame+300,name,strlen(name)+1);app.Write32(frame+700,capacity);
  U result;if(name){U key=0;bool created=false;result=app.registry.Open(root,path,false,key,created);if(!result){const U a[]={key,frame+300,0,0,frame+800,frame+700};result=app.ShellRegistryOrdinalApi(509,a);app.registry.Close(key);}}else {const U a[]={root,frame,out?frame+800:0,frame+700};result=app.ShellRegistryOrdinalApi(508,a);}
  if(!result&&out&&capacity){app.Read(frame+800,out,capacity);out[capacity-1]=0;}app.Free(frame);return result;
 }
 bool LongExecutable(const char*command){char path[260]={};strncpy(path,command,259);U at=QueryPath("PathGetArgsA",path)-scratch;if(at&&path[at])path[at-1]=0;Unquote(path);char full[260]={};U node=0;if(app.files.FullPath(path,full)||app.files.store.Lookup(full,node))return false;const auto*f=app.files.store.Get(node);if(!f||f->size<64||!app.files.store.Bytes(node))return false;if(f->data[0]!='M'||f->data[1]!='Z')return false;U offset=0;memcpy(&offset,f->data+60,4);if(offset>f->size-4)return false;return !memcmp(f->data+offset,"PE\0\0",4);}
 U Short(const char*file,char*out,U capacity){app.Store(scratch,file,strlen(file)+1);const U a[]={scratch,scratch+260,capacity};U result=app.Api("GetShortPathNameA",a);if(result&&result<capacity)strcpy(out,app.ReadString(scratch+260));return result&&result<capacity;}
 bool Pidl(U,U*,const char*,char*){Unsupported("Shell template requires shared PIDL provider");return false;}
 bool Display(U,char*){Unsupported("Shell template requires PIDL display-name provider");return false;}
 U Unsupported(const char*reason){if(app.log)app.log(reason);app.last_error=120;return 5;}
 void Error(U error){app.last_error=error;}U LastError(){return app.last_error;}
 bool Initialize(){return scratch!=0;} // Native frame availability replaces Win16 thunk initialization.
 U Item(U*info){info[8]=Unsupported("ShellExecuteExA requires PIDL invocation provider");return 0;}
 void ErrorUI(U*info){
  if((info[1]&0x400)||app.last_error==1223)return;
  U saved=app.last_error;char message[512]={};snprintf(message,sizeof message,"Cannot open %s.\nSystem error %u.",info[4]?app.ReadString(info[4]):"",saved);
  app.MessageBox(info[2],message,"Windows",0x10);app.last_error=saved;
 }
 U Finish(U*info,U error){info[8]=error;if(app.last_error!=120)app.last_error=Win98ShellExecute::WinError(error);return 0;}
 U Body(U*info){
  info[14]=0;app.last_error=0;
  // These flags request still-unported providers, before any command is launched.
  if(info[1]&~(0x800u|0x400u|0x1000u|0x40u|3u|0x8000u))return Finish(info,Unsupported("ShellExecuteExA mask requires deferred execution provider"));
  char file[260]={},params[2048]={},directory[260]={},verb[260]={},klass[260]={},command[520]={};
  auto text=[&](U ptr,char*out,U size){if(!ptr){out[0]=0;return true;}const char*p=app.ReadString(ptr);if(strlen(p)>=size){Unsupported("ShellExecuteExA input exceeds native execution frame");return false;}strcpy(out,p);return true;};
  if(!text(info[4],file,sizeof file)||!text(info[5],params,sizeof params)||!text(info[6],directory,sizeof directory)||!text(info[3],verb,sizeof verb))return Finish(info,5);
  if(!file[0])return Finish(info,Unsupported("ShellExecuteExA empty target requires shell folder provider"));
  if(Win98ShellCommand::Url(file)||(file[0]=='\\'&&file[1]=='\\'))return Finish(info,Unsupported("ShellExecuteExA URL or network target requires shell provider"));
  if(!info[6])strcpy(directory,app.files.current);const char*dirs[]={directory,nullptr};U key=0;bool own=false;
  if((info[1]&3)==3)key=info[11];
  else if((info[1]&3)==1){if(!text(info[10],klass,sizeof klass))return Finish(info,5);bool created=false;app.registry.Open(0x80000000,klass,false,key,created);own=key!=0;}
  else{
   if(!Win98ShellCommand::Resolve(*this,file,dirs,7)){
    if(!Win98ShellExecute::AppPath(*this,file)||!Win98ShellCommand::Resolve(*this,file,dirs,7))return Finish(info,2);
   }
   if(Attributes(file)&0x10)return Finish(info,Unsupported("ShellExecuteExA folder target requires PIDL execution provider"));
   // Native local file-class provider; original binds a Shell PIDL.
   char ext[260]={};strcpy(ext,Extension(file));if(!Query(0x80000000,ext,nullptr,klass,260)&&klass[0]){bool created=false;app.registry.Open(0x80000000,klass,false,key,created);own=key!=0;}
  }
  U error=key?Win98ShellExecute::Command(*this,key,info[3]?verb:nullptr,file,info[5]?params:nullptr,info[7],command):31;
  if(own)app.registry.Close(key);
  if(error==31){const char*ext=Extension(file);bool executable=!Compare(ext,".exe")||!Compare(ext,".com")||!Compare(ext,".bat")||!Compare(ext,".pif")||!Compare(ext,".cmd");if(executable&&(!info[3]||!Compare(verb,"open"))){strcpy(command,file);if(params[0]){Win98ShellExecute::Append(command," ",520);Win98ShellExecute::Append(command,params,520);}error=0;}}
  if(error)return Finish(info,error);
  if(app.log){app.log("Shell execution prepared command:");app.log(command);app.log("Shell execution directory:");app.log(directory);}
  // Native binding to the real process manager. The full original ShellExecute
  // body is still separate from its implemented entry/template decisions.
  U frame=app.Allocate(1024);if(!frame){app.last_error=8;info[8]=8;return 0;}
  U startup[17]={68};startup[11]=1;startup[12]=info[7]&0xffff;
  app.Store(frame,command,strlen(command)+1);app.Store(frame+520,directory,strlen(directory)+1);app.Store(frame+800,startup,sizeof startup);
  const U launch[]={0,frame,0,0,0,0,0,frame+520,frame+800,frame+880};
  U result=app.ProcessApi("CreateProcessA",launch),launch_error=app.last_error;
  if(result){
   U process=app.Read32(frame+880),thread=app.Read32(frame+884);const U close_thread[]={thread};app.ThreadApi("CloseHandle",close_thread);
   if(info[1]&0x40)info[14]=process;else {const U close_process[]={process};app.ThreadApi("CloseHandle",close_process);}
   info[8]=33; // Successful ShellExecute status; not an HMODULE in this process.
  }else info[8]=Win98ShellExecute::ShellError(launch_error);
  app.Free(frame);app.last_error=result?0:launch_error;return result;
 }
 U Execute(U*info){return Win98ShellExecute::Extended(*this,info);}
};
unsigned Application::ShellExecuteApi(ApiName name,const unsigned*a){ShellExecuteEnvironment e(*this);if(API_IS(name,"ShellExecuteA"))return Win98ShellExecute::Simple(e,a);if(!virtual_memory.Access(a[0],60,true)){last_error=5;return 0;}unsigned info[15]={};Read(a[0],info,60);unsigned result=e.Execute(info);Store(a[0],info,60);return result;}

}

#include "x86_path_probes.inc"
