// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string.h>
#include <stdio.h>
namespace Win98ShellExecute {
using U=unsigned;
// SHELL32 uses deliberately asymmetric mappings.
inline U ShellError(U e){switch(e){case 14:return 8;case 32:return 26;case 53:case 148:case 161:case 1203:return 3;case 1150:return 10;case 1151:return 12;case 1152:return 16;case 1153:return 15;case 1154:return 20;case 1155:return 31;case 1156:return 29;case 1157:return 32;default:return e<26?e:5;}}
inline U WinError(U e){switch(e){case 10:return 1150;case 12:return 1151;case 15:return 1153;case 16:return 1152;case 20:return 1154;case 26:return 32;case 29:return 1156;case 31:return 1155;case 32:return 1157;default:return e;}}
// truncation includes the terminator, existing full output is untouched.
inline void Append(char*out,const char*s,U capacity){U n=strlen(out);if(n>=capacity)return;U bytes=strlen(s);if(bytes>=capacity-n)bytes=capacity-n-1;memcpy(out+n,s,bytes);out[n+bytes]=0;}
// Returns the next scan position, not necessarily a token.
// Doubled quotes preserve the second quote. Only ASCII space separates arguments.
template<class E> const char* Argument(E&e,const char*p,char*out,U capacity){
 char*write=out;U left=capacity-1;while(*p==' ')++p;if(!*p)return nullptr;bool quoted=*p=='"';if(quoted)++p;
 for(;;){const char*quote=e.Find(p,'"');const char*space=quoted?nullptr:e.Find(p,' ');
  if(quoted&&!quote)return nullptr;
  if(!quoted&&space&&(!quote||space<quote)){U n=space-p;if(write){if(left<=n)return nullptr;memcpy(write,p,n);write[n]=0;}return space;}
  if(!quote){if(write){U n=strlen(p);if(left<=n)return nullptr;memcpy(write,p,n+1);}return nullptr;}
  bool doubled=quote[1]=='"';if(doubled)++quote;U n=quote-p;if(write){if(left<=n)return nullptr;memcpy(write,p,n);write[n]=0;write+=n;left-=n;}
  p=quote+1;if(!doubled)return p;
 }
}
//  +. Files/PE recognition,
// short names, DBCS and PIDL formatting remain providers.
template<class E> U Expand(E&e,char*out,U capacity,const char*file,const char*format,const char*params,U show,U*hotkey,bool force_long,U pidl,U*shared){
 if(!capacity)return 5;char*write=out;U left=capacity-1;bool first=true;const char*p=format;
 while(*p){char temp[260]={};const char*insert=nullptr;
  if(*p=='%'){
   ++p;if(!*p){e.Boundary("Trailing percent exceeds original command template terminator");return 5;}
   switch(*p){
    case '0':case '1':insert=file;if(!first&&!force_long&&!e.LongExecutable(out)){if(e.Short(file,temp,260))insert=temp;}break;
    case '*':insert=params?params:"";break;
    case 'L':case 'l':insert=file;break;
    case 'S':case 's':snprintf(temp,sizeof temp,"%d",int(show));insert=temp;break;
    case 'H':case 'h':snprintf(temp,sizeof temp,"%X",hotkey?*hotkey:0);insert=temp;break;
    case 'I':case 'i':if(!shared)strcpy(temp,":0");else if(!e.Pidl(pidl,shared,format,temp))return 8;insert=temp;break;
    case 'D':case 'd':if(!pidl||!e.Display(pidl,temp))return 5;insert=temp;break;
    case '2':case '3':case '4':case '5':case '6':case '7':case '8':case '9':{const char*at=params;for(U n=0;at&&n<U(*p-'2');++n)at=Argument(e,at,nullptr,0);if(at)Argument(e,at,temp,260);insert=temp;break;}
    case '~':if(p[1]>='2'&&p[1]<='9'){++p;const char*at=params;for(U n=2;at&&n<U(*p-'0');++n)at=Argument(e,at,nullptr,0);insert=at?at:"";}else --p;break;
   }
  }
  if(insert){U n=strlen(insert);if(left<=n)return 5;memcpy(write,insert,n+1);write+=n;left-=n;if((*p=='H'||*p=='h')&&hotkey)*hotkey=0;first=false;}
  else{if(left<2)return 5;*write++=*p;--left;if(*p!='"')first=false;else if(e.Lead('"')){++p;*write++=*p;--left;}}
  ++p;
 }
 *write=0;return 0;
}
// App Paths fallback.
template<class E> bool AppPath(E&e,char*file){if(e.FileName(file)!=file)return false;char key[260]="Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\";Append(key,file,260);if(!*e.Extension(key))Append(key,".exe",260);char value[260]={};if(e.Query(0x80000002,key,nullptr,value,260))return false;strcpy(file,value);return true;}
// The local association lookup.
// Class lookup itself is a provider replacing original PIDL class bind.
template<class E> U Command(E&e,U key,const char*verb,const char*file,const char*params,U show,char*out){
 char selected[32]={},path[260]="shell\\",value[260]={};
 if(!verb){if(e.Query(key,"shell",nullptr,selected,32)||!selected[0])strcpy(selected,"open");verb=selected;}
 Append(path,verb,260);if(e.Query(key,path,nullptr,nullptr,0))return 31;
 if(!e.Query(key,path,"CLSID",value,40)&&value[0])return e.Unsupported("Shell execution requires original COM verb provider");
 U n=strlen(path);if(n<=239){Append(path,"\\ddeexec",260);e.Query(key,path,nullptr,value,260);if(value[0])return e.Unsupported("Shell execution requires original DDE conversation provider");path[n]=0;}
 Append(path,"\\command",260);value[0]=0;e.Query(key,path,nullptr,value,260);
 if(value[0])return Expand(e,out,520,file,value,params,show,nullptr,false,0,nullptr);
 return 31;
}
// complete six-argument public frame construction.
template<class E> U Simple(E&e,const U*a){U info[15]={60,0x1400,a[0],a[1],a[2],a[3],a[4],a[5]};e.Execute(info);return info[8];}
// wrapper mask preservation, init and error UI decisions.
template<class E> U Extended(E&e,U*info){if(info[0]!=60){info[8]=5;e.Error(5);return 0;}U mask=info[1];info[1]|=0x800;U result=0;if(!e.Initialize())e.Error(8);else if((info[1]&12)==12&&info[9])result=e.Item(info);else result=e.Body(info);if(!result&&e.LastError()!=0x485)e.ErrorUI(info);info[1]=mask;return result;}
}
