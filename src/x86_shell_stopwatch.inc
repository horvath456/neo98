// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_shell_stopwatch.h"
namespace X86 {
struct Application::ShellStopwatchEnvironment {
 Application&app;
 struct Slot{Application*owner=nullptr;Win98ShellStopwatch::State state;};
 static Slot*Find(Application&a,bool create){static Slot slots[64];Slot*empty=nullptr;for(auto&s:slots){if(s.owner==&a)return &s;if(!s.owner&&!empty)empty=&s;}if(create&&empty){empty->owner=&a;return empty;}return nullptr;}
 unsigned Open(unsigned&key){bool made=false;return app.registry.Open(0x80000002,"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Performance",false,key,made);}
 unsigned Mode(unsigned key,unsigned&mode){unsigned node=0;const Registry::Value*v=nullptr;unsigned error=app.registry.Key(key,node);if(!error)error=app.registry.store.Query(node,"Mode",v);if(error)return error;if(v->size>4)return 234;if(v->size)memcpy(&mode,v->data,v->size);return 0;}
 void Close(unsigned key){app.registry.Close(key);}
 unsigned Allocate(unsigned n,unsigned){return app.Allocate(n);}
 void Activate(Win98ShellStopwatch::State&){app.last_error=50;app.Fail("Configured SHLWAPI StopWatch profiling provider is not ported");}
 bool Query(const char*name,unsigned long long&v){unsigned p=app.Allocate(8);if(!p)return false;const unsigned a[]={p};unsigned ok=app.TimeApi(name,a);if(ok)app.Read(p,&v,8);app.Free(p);return ok!=0;}
 void Frequency(unsigned long long&v){Query("QueryPerformanceFrequency",v);}
 bool Counter(unsigned long long&v){return Query("QueryPerformanceCounter",v);}
 void ClockFailure(){app.last_error=50;app.Fail("SHLWAPI StopWatch clock provider unavailable");}
};
bool Application::AttachShellStopwatch(){auto*s=ShellStopwatchEnvironment::Find(*this,true);if(!s){last_error=8;Fail("SHLWAPI StopWatch state capacity exhausted");return false;}ShellStopwatchEnvironment e{*this};Win98ShellStopwatch::Initialize(e,s->state);return state!=Failed;}
void Application::FreeShellStopwatch(){auto*s=ShellStopwatchEnvironment::Find(*this,false);if(s){if(s->state.storage)Free(s->state.storage);*s={};}}
unsigned Application::ShellStopwatchApi(unsigned ordinal){auto*s=ShellStopwatchEnvironment::Find(*this,false);if(!s){last_error=50;Fail("SHLWAPI StopWatch module is not attached");return 0;}if(ordinal==241)return Win98ShellStopwatch::Mode(s->state);ShellStopwatchEnvironment e{*this};auto q=Win98ShellStopwatch::Clock(e,s->state);Register(Reg::Edx,unsigned(q>>32));return unsigned(q);}
}
