// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_shell_search.h"
#include "win98_shell_search_exports.h"
#include "x86_app.h"
#include <string.h>
namespace X86 {
unsigned Application::ShellSearchApi(ApiName name,const unsigned*a){
 struct Env:Win98ShellSearch::Environment {Application&app;unsigned scratch=0;Env(Application&a):app(a){}~Env(){if(scratch)app.Free(scratch);}
  unsigned Byte(unsigned p)override{unsigned char c=0;app.Read(p,&c,1);return c;}
  unsigned Word(unsigned p)override{unsigned short c=0;app.Read(p,&c,2);return c;}
  bool Lead(unsigned c)override{return app.NlsApi(537,&c)!=0;}
  unsigned Next(unsigned p)override{unsigned c=Byte(p);return !c?p:p+(Lead(c)&&Byte(p+1)?2:1);}
  bool Dbcs()override{if(!scratch)scratch=app.Allocate(20);if(!scratch)return false;unsigned args[]={0,scratch};return app.NlsApi(318,args)&&Byte(scratch+6);}
  void EndBeyondTerminator()override{app.Fail("SHLWAPI reverse-search end crosses the string terminator");}
 }e(*this);
 for(unsigned operation=0;operation<10;++operation)if(!strcmp(name,OriginalShellSearchExports[operation].name))return Win98ShellSearch::Call(e,operation,a);
 Fail("Unknown SHLWAPI character-search export");return 0;
}
}
