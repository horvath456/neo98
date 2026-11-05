// SPDX-License-Identifier: GPL-2.0-or-later
// Runs the serial fixture (tools/kernel_comm_fixture.c) in the native x86 host
// and checks that every listed export was reached through its import.
#define private public
#include "../src/x86_app.h"
#include "../src/x86_comm_state.h"
#include "../src/registry.h"
#undef private
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>
using App=X86::Application;
static std::vector<unsigned char> load(const char *path){FILE*f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
// The ISO installation in QEMU has one serial port, COM1 under Enum\Root\*PNP0500.
// The fixture drives the COM1/COM2 pair of a machine with two ports, as the
// reference system installation had them, so the run adds COM2 the way Setup records a
// second port: a devnode with PortName COM2 and a Ports class key with the DCB
// and dialog of COM1.
static void SecondPort(){
 auto &r=Registry::System();bool created=false;unsigned hklm=r.roots[2],ports=0,com1=0,com2=0,node=0;
 assert(!r.Open(hklm,"System\\CurrentControlSet\\Services\\Class\\Ports",false,ports,created));
 assert(!r.Open(ports,"0000",false,com1,created));assert(!r.Open(ports,"0002",true,com2,created));
 for(unsigned i=0;;++i){const Registry::Value *v=nullptr;if(r.EnumValue(com1,i,v))break;r.Set(com2,v->name,v->type,v->data,v->size);}
 assert(!r.Open(hklm,"Enum\\Root\\*PNP0501\\0000",true,node,created));
 auto text=[&](const char*name,const char*value){unsigned n=0;while(value[n])++n;r.Set(node,name,1,value,n+1);};
 text("Class","Ports");text("PortName","COM2");text("Driver","Ports\\0002");
 text("FRIENDLYNAME","Communications Port (COM2)");text("DeviceDesc","Communications Port (COM2)");
}
int main(int argc,char **argv){
 SecondPort();
 assert(argc==4);auto exe=load(argv[1]),font=load(argv[2]);App app(nullptr);
 assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"KERNELCOMM.EXE"));
 const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
 while(app.Status()!=App::Exited&&app.Status()!=App::Failed&&std::chrono::steady_clock::now()<deadline)app.Pump(64);
 printf("KERNELCOMM state=%u exit=%u calls=%u error=%s\n",app.Status(),app.ExitCode(),app.ApiCalls(),app.Error());
 FILE *names=fopen(argv[3],"r");assert(names);char name[128];unsigned covered=0,total=0;
 while(fgets(name,sizeof name,names)){name[strcspn(name,"\r\n")]=0;++total;unsigned calls=0;
  for(unsigned i=0;i<app.import_count;++i)if(!strcmp(app.imports[i].dll,"KERNEL32.DLL")&&!strcmp(app.imports[i].name,name))calls+=app.imports[i].calls;
  if(calls)++covered;else printf("NOT REACHED %s\n",name);
 }
 fclose(names);printf("KERNELCOMM coverage=%u/%u\n",covered,total);fflush(stdout);
 if(app.Status()!=App::Exited)for(unsigned i=0;i<16;++i)if(app.threads[i].fiber&&!app.threads[i].done)
  printf("thread %u block=%u wait_handle=%08x deadline=%llu eip=%08x\n",i,app.threads[i].block,app.threads[i].wait_handle,app.threads[i].deadline,app.ThreadEip(i));
 if(app.Status()!=App::Exited&&app.comm.state)for(unsigned i=0;i<2;++i){const auto &p=app.comm.state->ports[i];
  printf("port %u open=%d overlapped=%d mask=%x events=%x abort_wait=%d abort_read=%d rx=%u wait_ptr=%x pending=%x/%x rts=%d dtr=%d modem=%x\n",i,p.open,p.overlapped,p.mask,p.events,p.abort_wait,p.abort_read,p.rx_count,p.wait_pointer,p.pending_pointer,p.pending_event,p.rts,p.dtr,p.modem);}
 assert(app.Status()==App::Exited&&app.ExitCode()==0&&total==21&&covered==total);
 puts("KERNELCOMM PASS");
}
