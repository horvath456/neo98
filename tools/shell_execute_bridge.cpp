/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/win98_shell_execute.h"
#include <assert.h>
#include <stdlib.h>
using U=unsigned;
struct Wire{U op,capacity,value,result,next,longexe,shortok,hotkey,force,shared,pidl,boundary;char input[1024],file[260],params[512],output[1024];};
struct E{Wire&w;bool Lead(U){return false;}const char*Find(const char*p,char c){return strchr(p,c);}bool LongExecutable(const char*){return w.longexe;}U Short(const char*,char*out,U){if(w.shortok)strcpy(out,"C:\\SHORT~1.TXT");return w.shortok;}bool Pidl(U,U*,const char*,char*){assert(false);return false;}bool Display(U,char*){assert(false);return false;}void Boundary(const char*){++w.boundary;}};
extern "C" void run(Wire*w){E e{*w};using namespace Win98ShellExecute;switch(w->op){case 0:w->result=ShellError(w->value);break;case 1:w->result=WinError(w->value);break;case 2:Append(w->output,w->input,w->capacity);break;case 3:{auto*p=Argument(e,w->input,w->value?nullptr:w->output,w->capacity);w->next=p?U(p-w->input):~0u;break;}case 4:w->result=Expand(e,w->output,w->capacity,w->file,w->input,w->value?nullptr:w->params,w->next,&w->hotkey,w->force,w->pidl,w->shared?&w->shared:nullptr);break;}}
#ifdef REPLAY
int main(int argc,char**argv){assert(argc==2);FILE*f=fopen(argv[1],"rb");assert(f);Wire a,b;U n=0;while(fread(&a,sizeof a,1,f)){assert(fread(&b,sizeof b,1,f));run(&a);assert(!memcmp(&a,&b,sizeof a));++n;}fclose(f);printf("SHELLEXEC replay PASS %u\n",n);}
#endif
struct PublicWire{U simple,initialized,body_result,body_error,body_instance,result,last_error,ui,seen_mask,info[15],args[6];};
struct PublicEnv{PublicWire&w;void Error(U n){w.last_error=n;}U LastError(){return w.last_error;}bool Initialize(){return w.initialized;}U Body(U*i){w.seen_mask=i[1];i[8]=w.body_instance;w.last_error=w.body_error;return w.body_result;}U Item(U*i){w.seen_mask=i[1]|0x80000000;return Body(i);}void ErrorUI(U*){++w.ui;}U Execute(U*i){U r=Win98ShellExecute::Extended(*this,i);memcpy(w.info,i,60);return r;}};
extern "C" void public_run(PublicWire*w){PublicEnv e{*w};w->result=w->simple?Win98ShellExecute::Simple(e,w->args):e.Execute(w->info);}
