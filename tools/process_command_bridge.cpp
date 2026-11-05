/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/win98_process_command.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
using U=unsigned;
struct Wire{U initial,application,fail_alloc,succeed_at,result,error,calls,allocated,freed;char command[512],attempts[16][520];};
struct E{Wire&w;U LastError(){return w.error;}void Error(U e){w.error=e;}char*Allocate(U n){w.allocated=n;return w.fail_alloc?nullptr:(char*)malloc(n);}void Free(char*p){++w.freed;free(p);}U Create(const char*,const char*s){assert(w.calls<16);strcpy(w.attempts[w.calls],s);++w.calls;w.error=100+w.calls;return w.calls==w.succeed_at?1:0;}};
extern "C" void run(Wire*w){E e{*w};w->result=Win98ProcessCommand::Retry(e,w->application?"explicit.exe":nullptr,w->command,w->initial);}
#ifdef REPLAY
int main(int argc,char**argv){assert(argc==2);FILE*f=fopen(argv[1],"rb");assert(f);Wire a,b;U n=0;while(fread(&a,sizeof a,1,f)){assert(fread(&b,sizeof b,1,f));run(&a);assert(!memcmp(&a,&b,sizeof a));++n;}fclose(f);printf("PROCESS COMMAND REPLAY PASS %u\n",n);}
#endif
