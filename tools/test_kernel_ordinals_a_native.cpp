// SPDX-License-Identifier: GPL-2.0-or-later
// ASan/UBSan runner for the ordinal-only KERNEL32 exports 1..60. Runs the x86
// fixture, checks that every listed ordinal import was reached, and dumps the
// two OEM translation tables the native side built for the original
// comparison. With a fifth argument the run must stop with that message.
#define private public
#include "../src/x86_app.h"
#undef private
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdlib>
static void logger(const char *text){puts(text);}
#include <vector>
using App=X86::Application;
static std::vector<unsigned char> load(const char *path){FILE*f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
int main(int argc,char **argv){
 assert(argc==5||argc==6);auto exe=load(argv[1]),font=load(argv[2]);App app(getenv("ORDINALSA_LOG")?logger:nullptr);
 assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"ORDINALSA.EXE"));
 const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
 while(app.Status()!=App::Exited&&app.Status()!=App::Failed&&std::chrono::steady_clock::now()<deadline)app.Pump(64);
 if(getenv("ORDINALSA_LOG")){for(unsigned i=0;i<128;++i)printf("%02x%s",app.ansi_high[i],i%32==31?"\n":"");for(unsigned i=0;i<128;++i)printf("%02x%s",app.oem_high[i],i%32==31?"\n":"");}
 printf("ORDINALSA state=%u exit=%u calls=%u error=%s\n",app.Status(),app.ExitCode(),app.ApiCalls(),app.Error());
 FILE *names=fopen(argv[3],"r");assert(names);char name[128];unsigned covered=0,total=0;
 while(fgets(name,sizeof name,names)){name[strcspn(name,"\r\n")]=0;++total;unsigned calls=0;
  for(unsigned i=0;i<app.import_count;++i)if(!strcmp(app.imports[i].dll,"KERNEL32.DLL")&&!strcmp(app.imports[i].name,name))calls+=app.imports[i].calls;
  if(calls)++covered;else printf("NOT REACHED %s\n",name);
 }
 fclose(names);printf("ORDINALSA coverage=%u/%u\n",covered,total);fflush(stdout);
 if(argc==6){
  assert(app.Status()==App::Failed&&strstr(app.Error(),argv[5])&&covered==total);
  puts("ORDINALSA STOP PASS");return 0;
 }
 assert(app.Status()==App::Exited&&app.ExitCode()==0&&covered==total&&app.oem_tables_ready);
 FILE *tables=fopen(argv[4],"wb");assert(tables);
 assert(fwrite(app.ansi_high,1,128,tables)==128&&fwrite(app.oem_high,1,128,tables)==128);fclose(tables);
 puts("ORDINALSA PASS");
}
