// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_shell_modules.h"
namespace X86 {
struct Application::ShellModuleEnvironment {
 Application&app;
 bool IsNt(){return app.ShellOsApi(1)!=0;}
 unsigned NativeWide(unsigned,unsigned,unsigned,unsigned){app.last_error=120;return 0;}
 unsigned Alloc(unsigned n,unsigned){return app.Allocate(n);}
 void Free(unsigned p){app.Free(p);}
 unsigned Byte(unsigned p){unsigned char b=0;app.Read(p,&b,1);return b;}
 void Byte(unsigned p,unsigned b){unsigned char c=b;app.Store(p,&c,1);}
 unsigned Word(unsigned p){unsigned short w=0;app.Read(p,&w,2);return w;}
 void Word(unsigned p,unsigned w){unsigned short v=w;app.Store(p,&v,2);}
 unsigned Get(unsigned p,unsigned unit){return unit==2?Word(p):Byte(p);}
 void Put(unsigned p,unsigned value,unsigned unit){if(unit==2)Word(p,value);else Byte(p,value);}
 unsigned Length(unsigned p){const char*s=app.ReadString(p);return s?strlen(s):0;}
 unsigned Wide(unsigned p,unsigned n,unsigned out,unsigned cap){const unsigned a[]={0,0,p,n,out,cap,0,0};return app.NlsApi(811,a);}
 unsigned Multi(unsigned p,unsigned n,unsigned out,unsigned cap){const unsigned a[]={0,0,p,n,out,cap};return app.NlsApi(594,a);}
 unsigned Error(){return app.last_error;}
 void Error(unsigned error){app.last_error=error;}
 void ConversionFailure(){app.last_error=50;app.Fail("Shell module name second conversion failed");}
 unsigned Module(unsigned ordinal,unsigned a,unsigned b,unsigned c){const unsigned args[]={a,b,c};return app.ModuleApi(ordinal==83?"GetModuleHandleA":ordinal==105?"LoadLibraryExA":"GetModuleFileNameA",args);}
};
unsigned Application::ShellModuleApi(unsigned ordinal,const unsigned*a){
 ShellModuleEnvironment e{*this};unsigned local=Allocate(520);if(!local){last_error=8;return 0;}
 unsigned result=ordinal==80?Win98ShellModules::FileName(e,a[0],a[1],a[2],local):Win98ShellModules::InputCall(e,ordinal,a[0],ordinal==105?a[1]:0,ordinal==105?a[2]:0,local);
 Free(local);return result;
}
}
