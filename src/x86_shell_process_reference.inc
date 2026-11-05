// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_shell_process_reference.h"
namespace X86 {
struct Application::ShellProcessReferenceEnvironment {
 Application&app;
 struct Slot{Application*owner=nullptr;Win98ShellProcessReference::State state;};
 static Slot*Find(Application&a,bool create){static Slot slots[64];Slot*empty=nullptr;for(auto&s:slots){if(s.owner==&a)return &s;if(!s.owner&&!empty)empty=&s;}if(create&&empty){empty->owner=&a;return empty;}return nullptr;}
 void Publish(unsigned output,unsigned object){app.Write32(output,object);}
 void AddRef(unsigned object){if(app.state==Failed||app.state==Exited)return;unsigned table=app.Read32(object),procedure=app.Read32(table+4);if(app.state!=Failed)app.Call(procedure,&object,1);}
};
unsigned Application::ShellProcessReferenceApi(ApiName name,const unsigned*a){
 ShellProcessReferenceEnvironment e{*this};
 const bool set=API_IS(name,"ordinal_176");auto*s=ShellProcessReferenceEnvironment::Find(*this,set&&a[0]);
 if(set){if(!s){if(a[0]){last_error=8;Fail("Shell process reference provider capacity exhausted");}return 0;}return Win98ShellProcessReference::Set(s->state,a[0]);}
 const Win98ShellProcessReference::State empty{};return Win98ShellProcessReference::Get(e,s?s->state:empty,a[0]);
}
void Application::FreeShellProcessReference(){auto*s=ShellProcessReferenceEnvironment::Find(*this,false);if(s)*s=ShellProcessReferenceEnvironment::Slot{};}
}
