// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/x86_app.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <chrono>
#include <thread>
static void log(const char*p){puts(p);}
static std::vector<unsigned char>load(const char*path){FILE*f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
int main(int argc,char**argv){assert(argc==3);auto exe=load(argv[1]),font=load(argv[2]);assert(!Files::System().Seed("C:\\MULTIWAIT.EXE",exe.data(),exe.size()));auto*a=new X86::Application(log);assert(a->Start(exe.data(),exe.size(),font.data(),font.size(),"MULTIWAIT.EXE","PARENT"));auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);unsigned idle=0,stalled=0;bool key_down=false,key_up=false;
 while(a->Status()!=X86::Application::Failed&&a->Status()!=X86::Application::Exited&&std::chrono::steady_clock::now()<deadline){unsigned calls=a->ApiCalls();a->Pump(8);if(!key_down&&User::FindWindow("WaitProbe","queue-input-ready")){a->RawKey(0x41,0,true,0x1e);User::SetWindowText(User::FindWindow("WaitProbe","queue-input-ready"),"!ready");key_down=true;}if(!key_up&&User::FindWindow("WaitProbe","queue-input-again")){a->RawKey(0x41,0,false,0x1e);User::SetWindowText(User::FindWindow("WaitProbe","queue-input-again"),"!ready");key_up=true;}if(a->Status()==X86::Application::Idle){++idle;if(calls==a->ApiCalls())++stalled;std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
 bool okay=a->Status()==X86::Application::Exited&&!a->ExitCode()&&stalled&&key_down&&key_up;printf("MULTIWAIT %s state=%u code=%u calls=%u callbacks=%u idle=%u stalled=%u error=%s\n",okay?"PASS":"FAIL",a->Status(),a->ExitCode(),a->ApiCalls(),a->Callbacks(),idle,stalled,a->Error());delete a;assert(!X86::Application::LiveChildren()&&!X86::Application::ChildRecords()&&!User::LiveWindows());return okay?0:1;
}
