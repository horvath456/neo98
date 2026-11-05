// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_character_case.h"
#include <stdlib.h>
namespace X86 {
unsigned Application::CharacterCaseApi(ApiName name,const unsigned*a){
 struct E{
  Application&app;
  unsigned UserLocale(){return app.UserDefaultLocale();}
  void Error(unsigned value){app.last_error=value;}
  unsigned Length(unsigned p){
   for(unsigned n=0;n<1048576;++n){if(p+n<p||!app.virtual_memory.Access(p+n,1,false))return n;unsigned char c=0;app.Read(p+n,&c,1);if(!c)return n;}
   app.Fail("Native character case string limit exceeded");return 0;
  }
  bool BadString(unsigned p,unsigned n){unsigned args[]={p,n};return app.PointerProbeApi("IsBadStringPtrA",args)!=0;}
  bool BadWrite(unsigned p,unsigned n){if(n>1048576){app.Fail("Native character case buffer limit exceeded");return true;}unsigned args[]={p,n};return app.PointerProbeApi("IsBadWritePtr",args)!=0;}
  unsigned char*Allocate(unsigned n){auto*p=static_cast<unsigned char*>(calloc(n,1));if(!p)Error(8);return p;}void Free(unsigned char*p){free(p);}
  void Write8(unsigned p,unsigned value){unsigned char c=value;app.Store(p,&c,1);}
  unsigned Map(unsigned locale,bool lower,unsigned source,unsigned n,unsigned char*out,bool scalar,unsigned byte){
   if(!app.InitializeNls())return 0;
   struct M:CharacterTypeEnvironment{
    unsigned char*out;
    M(Application&a,unsigned char*p):CharacterTypeEnvironment(a),out(p){}
    unsigned LocaleNumber(unsigned){return app.nls_system->locale.number;}
    unsigned CaseByte(unsigned descriptor,bool lower,unsigned c){auto*t=Table(descriptor);if(!t){app.Fail("Native case mapping locale table missing");return c;}return t->data[t->Offset(lower?7:6)+(c&255)];}
    unsigned CaseWord(unsigned descriptor,bool lower,unsigned index){auto*t=Table(descriptor);return t?t->Half(t->Offset(lower?7:6)+index*2):0;}
    bool Lead(unsigned descriptor,unsigned c){return LocaleLead(descriptor,c);}
    void Write8(unsigned address,unsigned value){out[address-1]=value;}
   }m(app,out);m.scalar=scalar;m.byte=byte;
   unsigned result=Win98CharacterCase::KernelCase(m,locale,lower,source,n,1,n);return app.state==Failed?0:result;
  }
  unsigned MapCharacter(unsigned locale,bool lower,unsigned byte,unsigned char&out){return Map(locale,lower,1,1,&out,true,byte);}
  unsigned MapBuffer(unsigned locale,bool lower,unsigned source,unsigned count,unsigned char*out){return Map(locale,lower,source,count,out,false,0);}
 }e{*this};
 return Win98CharacterCase::Public(e,a[0],strstr(name,"Buff")?a[1]:0,!strncmp(name,"CharLower",9),strstr(name,"Buff")!=nullptr);
}
}
