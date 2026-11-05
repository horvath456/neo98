// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_shell_policy.h"
#include <cstdio>
#include <cstring>
#include <cassert>
using U=unsigned;
struct Wire{U op,id,nonnull,comparison,cache[57],opens[2],queries[2],types[2],values[2],writes[2],sizes[2],result,count,events[128];char section[128];};
static U hash(const char*s){U h=2166136261;for(;*s;++s)h=(h^static_cast<unsigned char>(*s))*16777619;return h;}
extern "C" void shell_policy(Wire*w){struct Env{Wire&w;void ev(U a){assert(w.count<128);w.events[w.count++]=a;}void Copy(char*d,const char*s){ev(1);ev(hash(s));strcpy(d,s);}void Append(char*d,const char*s){ev(2);ev(hash(s));strcat(d,s);}U Compare(const char*a,const char*b){ev(3);ev(hash(a));ev(hash(b));return w.comparison;}U Open(U root,const char*path,U options,U access,U&key){U n=root==0x80000002?0:1;ev(4);ev(root);ev(hash(path));ev(options);ev(access);key=w.opens[n]?0:0x500+n;return w.opens[n];}U Query(U key,const char*name,U reserved,U&type,U&value,U&size){U n=key-0x500;ev(5);ev(key);ev(hash(name));ev(reserved);ev(value);ev(size);type=w.types[n];size=w.sizes[n];memcpy(&value,&w.values[n],w.writes[n]);return w.queries[n];}U Close(U key){ev(6);ev(key);return 0;}}e{*w};Win98ShellPolicy::Cache c;memcpy(c.values,w->cache,sizeof c.values);w->result=w->op?Win98ShellPolicy::Reset(e,c,w->nonnull?w->section:nullptr):Win98ShellPolicy::Query(e,c,w->id);memcpy(w->cache,c.values,sizeof c.values);}
#ifdef POLICY_REPLAY
int main(int argc,char**argv){assert(argc==2);FILE*f=fopen(argv[1],"rb");assert(f);Wire a,b;U n=0;while(fread(&a,sizeof a,1,f)){assert(fread(&b,sizeof b,1,f));shell_policy(&a);assert(!memcmp(&a,&b,sizeof a));++n;}fclose(f);printf("POLICY sanitizer replay PASS %u\n",n);}
#endif
