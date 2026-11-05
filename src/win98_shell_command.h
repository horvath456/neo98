// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string.h>
namespace Win98ShellCommand {
using U=unsigned;
inline constexpr const char*Extensions[]={".pif",".com",".exe",".bat",".lnk",".cmd",""};
// SHELL32 #40. ACP lead-byte decision remains an explicit provider.
template<class E> bool Relative(E&e,const char*p){return !p[0]||(p[0]!='\\'&&(e.Lead((unsigned char)p[0])||p[1]!=':'));}
// SHLWAPI scheme scan70BD4A3C, sufficient for PathIsURL's success decision.
inline bool Url(const char*p){if(!p)return false;const char*start=p;U n=0;while(*p){unsigned c=(unsigned char)*p;if(c==':'){if((start[0]=='u'||start[0]=='U')&&(start[1]=='r'||start[1]=='R')&&(start[2]=='l'||start[2]=='L')){start=p+1;n=~0u;}else return n>1;}else if(!((c>='0'&&c<='9')||(c>='A'&&c<='Z')||(c>='a'&&c<='z')||c=='+'||c=='-'||c=='.'))return false;++p;++n;}return false;}
// The extension mask is consumed low bit first.
template<class E> U Extension(E&e,char*path,U mask){
 if(!path||e.UncRoot(path))return 0;if(!mask)return e.Exists(path);
 if(*e.Extension(path)&&(mask&0x40))return e.Exists(path);
 U n=strlen(path);if(n>255)return 0;
 for(U i=0;i<7;++i,mask>>=1)if(mask&1){strcpy(path+n,Extensions[i]);if(e.Exists(path))return 1;}
 path[n]=0;return 0;
}
// An oversized PATH component would overrun the Windows 98 local buffer.
template<class E> const char* NextPath(E&e,const char*p,char*out){
 if(!p)return nullptr;while(*p==';')++p;if(!*p)return nullptr;const char*end=p;while(*end&&*end!=';')++end;U n=end-p;if(n>=260){e.Boundary("PATH component exceeds original260-byte frame");return nullptr;}
 memcpy(out,p,n);out[n]=0;e.Trim(out);if(!*out)return nullptr;return *end?end+1:end;
}
// PathFindOnPathA calls this helper with mask 0.
template<class E> U Search(E&e,char*path,const char*const*directories,U mask){
 if(!path||!e.FileSpec(path))return 0;char candidate[260]={};
 if(directories)for(U i=0;directories[i]&&directories[i][0];++i){e.Combine(candidate,directories[i],path);if(Extension(e,candidate,mask)){strcpy(path,candidate);return 1;}}
 e.SystemDirectory(candidate);if(!e.Append(candidate,path))return 0;if(Extension(e,candidate,mask)){strcpy(path,candidate);return 1;}
 e.WindowsDirectory(candidate);if(!e.Append(candidate,path))return 0;if(Extension(e,candidate,mask)){strcpy(path,candidate);return 1;}
 const char*environment=e.EnvironmentPath();if(!environment)return 0;const char*next=environment;
 while((next=NextPath(e,next,candidate))){bool duplicate=false;if(directories)for(U i=0;directories[i];++i)if(!e.Compare(candidate,directories[i])){duplicate=true;break;}
  if(!duplicate){e.Append(candidate,path);if(Extension(e,candidate,mask)){strcpy(path,candidate);e.FreeEnvironment(environment);return 1;}}
 }
 e.FreeEnvironment(environment);return 0;
}
// SHELL32, called only when an extension is absent.
template<class E> U FindExtension(E&e,char*path,const char*const*dirs,bool search,U mask){if(*e.Extension(path))return 0;return search?Search(e,path,dirs,mask):Extension(e,path,mask);}
// SHELL32 unnamed51, and cold blocks7FCD88E5..89AD.
template<class E> U Resolve(E&e,char*path,const char*const*dirs,U flags){
 const U extensions=(flags&8)?15:63;e.Unquote(path);
 if(e.Root(path)){
  if(!e.UncRoot(path)&&path[0]=='\\'&&!path[1])e.Qualify(path,(flags&4)&&dirs?dirs[0]:nullptr,false);
  if(!(flags&1)||e.ShellExists(path))return 1;
  if(path[0]=='\\'&&path[1]=='\\')return e.NetworkResolve(path);return 0;
 }
 if(e.FileSpec(path)){if((flags&3)&&FindExtension(e,path,dirs,true,extensions))return 1;return Search(e,path,dirs,0);}
 if(Url(path))return 0;e.Qualify(path,(flags&4)&&dirs?dirs[0]:nullptr,true);
 if(!(flags&1))return 1;if((flags&3)&&FindExtension(e,path,dirs,false,extensions))return 1;return e.ShellExists(path);
}
// SHELL32 unnamed653, and7FCD8F5D..9058.
template<class E> U Process(E&e,const char*command,char*out,U capacity,U flags){
 if(!command)return ~0u;char path[261]={};const char*tail=nullptr;bool found=false;
 if(command[0]=='"'){
  const char*p=command+1;U n=0;while(*p&&*p!='"'&&n<260)path[n++]=*p++;
  if(n==260){e.Boundary("Quoted command exceeds original260-byte frame");return ~0u;}path[n]=0;if(*p)tail=p+1;
  if(((flags&0x40)||Relative(e,path))&&!(flags&0x20))found=Resolve(e,path,nullptr,3)!=0;else found=true;
 }else{
  bool resolve=Relative(e,command)||(flags&0x40);
  for(U n=0;n<260;++n){path[n]=command[n];if(!path[n]||path[n]==' '){
    path[n]=0;bool okay=!resolve||Resolve(e,path,nullptr,3);U attributes=okay?e.Attributes(path):~0u;
    if(okay&&attributes!=~0u&&(!(attributes&0x10)||!(flags&0x10))){
      if(resolve&&(flags&0x20)){if(n){memcpy(path,command,n-1);path[n-1]=0;}path[0]=' ';}
      tail=command+n;found=true;break;
    }
    if(resolve)memcpy(path,command,n+1);else path[n]=command[n];if(!path[n])break;
  }}
 }
 if(!found)return ~0u;bool quote=(flags&1)&&e.Space(path);U bytes=strlen(path)+1+(quote?2:0);bool arguments=(flags&3)&&tail;if(arguments)bytes+=strlen(tail);
 if(!out)return bytes;if(int(bytes)>int(capacity))return ~0u;
 char*temporary=e.Allocate(bytes);if(!temporary)return ~0u;char*p=temporary;if(quote)*p++='"';strcpy(p,path);if(quote)strcat(p,"\"");if(arguments)strcat(p,tail);strcpy(out,temporary);e.Free(temporary);return bytes;
}
}
