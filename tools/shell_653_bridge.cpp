/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/win98_shell_command.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cassert>
using U=unsigned;
struct File{char path[260];U attributes;};
struct Wire{U operation,flags,capacity,null_input,null_output,fail_alloc,lead,result,boundary,event_count;char input[1024],output[1024],dirs[3][260],environment[1024];File files[8];U events[2048];};
U hash(const char*p){U v=2166136261u;if(p)while(*p){v^=(unsigned char)*p++;v*=16777619;}return v;}
struct Env{
 Wire&w;void event(U a,U b=0,U c=0,U d=0){assert(w.event_count+4<=2048);U*p=w.events+w.event_count;*p++=a;*p++=b;*p++=c;*p=d;w.event_count+=4;}
 bool Lead(U c){return w.lead&&c==0x81;}
 bool UncRoot(const char*p){if(p[0]!='\\'||p[1]!='\\')return false;U n=0;while(*p){if(*p=='\\')++n;p+=Lead((unsigned char)*p)&&p[1]?2:1;}return n==2||n==3;}
 bool FileSpec(const char*p){return !strchr(p,'\\')&&!strchr(p,':');}
 bool Root(const char*p){return !strcmp(p,"\\")||(strlen(p)==3&&p[1]==':'&&p[2]=='\\')||UncRoot(p);}
 void Unquote(char*p){U n=strlen(p);if(n>1&&p[0]=='"'&&p[n-1]=='"'){memmove(p,p+1,n-2);p[n-2]=0;}}
 const char*Extension(const char*p){const char*out=p+strlen(p);for(;*p;++p){if(*p=='\\'||*p==' ')out=p+strlen(p);else if(*p=='.')out=p;}return out;}
 U Attributes(const char*p){event(1,hash(p));for(auto&f:w.files)if(!strcmp(f.path,p)&&f.path[0])return f.attributes;return ~0u;}
 U Exists(const char*p){return p&&Attributes(p)!=~0u;}U ShellExists(const char*p){return Exists(p);}
 U NetworkResolve(const char*p){event(8,hash(p));return 0;}
 void Qualify(char*p,const char*base,bool check){event(2,hash(p),hash(base),check);char b[260];if(p[0]=='\\'&&p[1]!='\\'){snprintf(b,sizeof b,"C:%s",p);strcpy(p,b);}else if(Win98ShellCommand::Relative(*this,p)){snprintf(b,sizeof b,"%s\\%s",base?base:"C:\\WORK",p);strcpy(p,b);}}
 void Combine(char*out,const char*base,const char*file){if(strlen(base)+strlen(file)+2>260){out[0]=0;return;}snprintf(out,260,"%s%s%s",base,base[strlen(base)-1]=='\\'?"":"\\",file);}
 bool Append(char*out,const char*file){char base[260];strcpy(base,out);if(strlen(base)+strlen(file)+2>260)return false;Combine(out,base,file);return true;}
 void SystemDirectory(char*p){event(3,0);strcpy(p,"C:\\WINDOWS\\SYSTEM");}void WindowsDirectory(char*p){event(3,1);strcpy(p,"C:\\WINDOWS");}
 const char*EnvironmentPath(){event(4);return w.environment[0]?w.environment:nullptr;}void FreeEnvironment(const char*){}
 int Compare(const char*a,const char*b){return strcasecmp(a,b);}void Trim(char*p){U n=strlen(p);while(n&&p[n-1]==' ')p[--n]=0;U a=0;while(p[a]==' ')++a;if(a)memmove(p,p+a,n-a+1);}
 bool Space(const char*p){return strchr(p,' ')!=nullptr;}
 char*Allocate(U n){event(5,n);return w.fail_alloc?nullptr:(char*)malloc(n);}void Free(char*p){event(6);free(p);}void Boundary(const char*){++w.boundary;}
};
extern "C" void run(Wire*w){Env e{*w};const char*dirs[4]={};for(U i=0;i<3&&w->dirs[i][0];++i)dirs[i]=w->dirs[i];const char*const*d=dirs[0]?dirs:nullptr;char*p=w->null_input?nullptr:w->input;using namespace Win98ShellCommand;
 switch(w->operation){case 0:w->result=Process(e,p,w->null_output?nullptr:w->output,w->capacity,w->flags);break;case 1:w->result=Resolve(e,p,d,w->flags);break;case 2:w->result=Extension(e,p,w->flags);break;case 3:w->result=Search(e,p,d,w->flags);break;case 4:w->result=Relative(e,p);break;case 5:w->result=e.Exists(p);break;}}
#ifdef REPLAY
int main(int argc,char**argv){assert(argc==2);FILE*f=fopen(argv[1],"rb");assert(f);Wire a,b;U n=0;while(fread(&a,sizeof a,1,f)){assert(fread(&b,sizeof b,1,f));run(&a);assert(!memcmp(&a,&b,sizeof a));++n;}fclose(f);printf("SHELL653 replay PASS %u\n",n);}
#endif
