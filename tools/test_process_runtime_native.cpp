/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
#include "../src/x86_app.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <cstring>
static void log(const char*p){puts(p);}
static std::vector<unsigned char>load(const char*path){FILE*f=fopen(path,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
static void pump(X86::Application&a){for(unsigned i=0;i<80&&a.Status()!=X86::Application::Failed&&a.Status()!=X86::Application::Exited;++i)a.Pump(8);if(a.Status()==X86::Application::Failed||(a.Status()==X86::Application::Exited&&a.ExitCode())){fprintf(stderr,"FAIL pid=%u code=%u %s\n",a.ProcessId(),a.ExitCode(),a.Error());assert(false);}}
static bool live(X86::Application&a){return a.Status()!=X86::Application::Failed&&a.Status()!=X86::Application::Exited;}
static void pump2(X86::Application&a,X86::Application&b){for(unsigned i=0;i<160&&live(a)&&live(b);++i){a.Pump(8);b.Pump(8);}if(a.Status()==X86::Application::Failed||b.Status()==X86::Application::Failed||(a.Status()==X86::Application::Exited&&a.ExitCode())||(b.Status()==X86::Application::Exited&&b.ExitCode())){fprintf(stderr,"FAIL a=%u/%s b=%u/%s\n",a.ExitCode(),a.Error(),b.ExitCode(),b.Error());assert(false);}}
static unsigned window(unsigned pid,const char*text){unsigned windows[192],n=0;assert(User::WindowSnapshot(User::GetDesktopWindow(),windows,192,n));for(unsigned i=0;i<n;++i){char t[256];User::GetWindowText(windows[i],t,256);if(User::WindowProcess(windows[i])==pid&&!strcmp(t,text))return windows[i];}return 0;}
static bool title(unsigned h,const char*text){char t[256];User::GetWindowText(h,t,256);return !strcmp(t,text);}
int main(int argc,char**argv){assert(argc==3);auto exe=load(argv[1]),font=load(argv[2]);auto*a=new X86::Application(log);assert(a->Start(exe.data(),exe.size(),font.data(),font.size(),"DUAL.EXE","A"));pump(*a);unsigned ah=window(a->ProcessId(),"A0");assert(ah);auto*b=new X86::Application(log);assert(b->Start(exe.data(),exe.size(),font.data(),font.size(),"DUAL.EXE","B"));pump(*b);unsigned bh=window(b->ProcessId(),"B0");assert(bh&&User::IsWindow(ah)&&a->ProcessId()!=b->ProcessId()&&User::WindowThread(ah)!=User::WindowThread(bh));
 // A blocks inside the synchronous cross-process send until B answers it, so
 // both processes have to run while that message is outstanding.
 assert(User::PostMessage(ah,0x407,bh,0));pump2(*a,*b);assert(title(ah,"Aboundary")&&title(bh,"B0")&&!User::Parent(ah));
 assert(User::PostMessage(ah,0x401,1,0)&&User::PostMessage(bh,0x401,2,0));pump(*a);assert(title(ah,"A1")&&title(bh,"B0"));pump(*b);assert(title(bh,"B1"));
 User::SetFocus(bh);a->RawKey('K',0,true,0x25,false);a->RawKey('K',0,false,0x25,false);pump(*a);assert(title(bh,"B1"));pump(*b);assert(title(bh,"Bkey"));
 a->MouseButtons(350,100,0);pump(*b);assert(User::PostMessage(bh,0x409,0,0));pump(*b);assert(title(bh,"Bcursor"));assert(User::PostMessage(ah,0x40a,0,0));pump(*a);assert(title(ah,"Acursor"));
 assert(User::PostMessage(bh,0x403,0,0));pump(*b);assert(title(bh,"Bclipboard"));assert(User::PostMessage(ah,0x406,0,0));pump(*a);assert(title(ah,"Acopied"));assert(User::PostMessage(ah,0x402,0,0));pump(*a);assert(a->Status()==X86::Application::Exited&&a->ExitCode()==0);unsigned acalls=a->ApiCalls();delete a;assert(!User::IsWindow(ah)&&User::IsWindow(bh));
 assert(User::PostMessage(bh,0x404,0,0));pump(*b);assert(title(bh,"Bsurvived")&&window(b->ProcessId(),"Bsecond"));assert(User::PostMessage(bh,0x401,2,0));pump(*b);assert(title(bh,"B2"));assert(User::PostMessage(bh,0x405,0,0));pump(*b);assert(b->Status()==X86::Application::Exited&&b->ExitCode()==0);unsigned bcalls=b->ApiCalls();delete b;assert(!User::LiveWindows()&&!User::ClipboardLength());printf("PROCESS RUNTIME PASS parent_calls=%u sibling_calls=%u\n",acalls,bcalls);}
