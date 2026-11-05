/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Andreas Horvath
 */
// One family guest in two real Applications; the existing probe handles WordPad.
#define main window_text_single_probe
#include "probe_application.cpp"
#undef main
#include <cassert>
int main(int argc,char**argv){
 if(argc!=4||strcmp(argv[1],"--dual"))return window_text_single_probe(argc,argv);
 auto exe=load(argv[2]),font=load(argv[3]);auto*a=new X86::Application(logger);auto*b=new X86::Application(logger);
 assert(a->Start(exe.data(),exe.size(),font.data(),font.size(),"WINDOWTEXT.EXE","A"));
 for(unsigned i=0;i<80&&a->Status()!=X86::Application::Failed&&a->Status()!=X86::Application::Exited;++i)a->Pump(8);
 if(a->Status()==X86::Application::Failed||a->Status()==X86::Application::Exited){printf("WINDOWTEXT FAIL A state=%u code=%u error=%s\n",a->Status(),a->ExitCode(),a->Error());return 1;}
 assert(b->Start(exe.data(),exe.size(),font.data(),font.size(),"WINDOWTEXT.EXE","B"));
 for(unsigned i=0;i<160;++i){if(b->Status()!=X86::Application::Exited&&b->Status()!=X86::Application::Failed)b->Pump(8);if(a->Status()!=X86::Application::Exited&&a->Status()!=X86::Application::Failed)a->Pump(8);if(a->Status()==X86::Application::Exited&&b->Status()==X86::Application::Exited)break;}
 bool ok=a->Status()==X86::Application::Exited&&b->Status()==X86::Application::Exited&&!a->ExitCode()&&!b->ExitCode();
 printf("WINDOWTEXT %s A_state=%u A_code=%u A_calls=%u A_callbacks=%u B_state=%u B_code=%u B_calls=%u B_callbacks=%u error_A=%s error_B=%s\n",ok?"PASS":"FAIL",a->Status(),a->ExitCode(),a->ApiCalls(),a->Callbacks(),b->Status(),b->ExitCode(),b->ApiCalls(),b->Callbacks(),a->Error(),b->Error());delete b;delete a;assert(!User::LiveWindows());return ok?0:1;
}
