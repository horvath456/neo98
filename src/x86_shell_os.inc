// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_shell_os.h"
namespace X86 {
struct Application::ShellOsEnvironment{
 Application&app;
 struct Slot{Application*owner=nullptr;Win98ShellOs::State state;};
 static Slot*Find(Application&a,bool create){static Slot slots[64];Slot*empty=nullptr;for(auto&s:slots){if(s.owner==&a)return &s;if(!s.owner&&!empty)empty=&s;}if(create&&empty){empty->owner=&a;return empty;}return nullptr;}
 bool Version(unsigned char*version){unsigned memory=app.Allocate(156);if(!memory)return false;app.Store(memory,version,156);const unsigned a[]={memory};unsigned result=app.ThreadApi("GetVersionExA",a);app.Read(memory,version,156);app.Free(memory);return result!=0;}
 unsigned Open(unsigned&key){bool created=false;return app.registry.Open(0x80000002,"System\\CurrentControlSet\\Control\\ProductOptions",false,key,created);}
 unsigned Product(unsigned key,char*buffer,unsigned capacity){unsigned node=0;const Registry::Value*value=nullptr;unsigned error=app.registry.Key(key,node);if(!error)error=app.registry.store.Query(node,"ProductType",value);if(error)return error;if(value->size>capacity)return 234;if(!value->size||!memchr(value->data,0,value->size)){app.last_error=13;return 13;}memcpy(buffer,value->data,value->size);return 0;}
 void Close(unsigned key){app.registry.Close(key);}
 bool Contains(const char*text,const char*needle){for(const auto*p=(const unsigned char*)text;*p;++p)if(*p>=128){app.last_error=50;app.Fail("Shell OS ProductType needs non-ASCII substring collation");return false;}for(unsigned at=0;text[at];++at){unsigned i=0;while(needle[i]&&text[at+i]&&Win98ShellOs::LowerAscii((unsigned char)text[at+i])==Win98ShellOs::LowerAscii((unsigned char)needle[i]))++i;if(!needle[i])return true;}return false;}
 unsigned Metric(unsigned n){return app.SystemMetric(n);}
};
unsigned Application::ShellOsApi(unsigned selector){auto*s=ShellOsEnvironment::Find(*this,true);if(!s){last_error=8;Fail("Shell OS state provider capacity exhausted");return 0;}ShellOsEnvironment environment{*this};return Win98ShellOs::Query(environment,s->state,selector);}
void Application::FreeShellOs(){auto*s=ShellOsEnvironment::Find(*this,false);if(s)*s=ShellOsEnvironment::Slot{};}
}
