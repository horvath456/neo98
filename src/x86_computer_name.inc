// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_computer_name.h"
#ifdef NEO98_RUNTIME_CONTENT
#include "windows_runtime_data.h"
#else
#include "../build/generated/computer_name_tables.h"
#endif
namespace X86 {
unsigned Application::ComputerNameApi(ApiName name,const unsigned*a){
 if(!InitializeNls())return 0;
 struct E {
  Application&app;
  const char*Active(){return "";} // Native boot has no VNETSUP-supplied active-name cache.
  void Error(unsigned value){app.last_error=value;}
  unsigned Read8(unsigned p){unsigned char c=0;app.Read(p,&c,1);return c;}
  unsigned Read32(unsigned p){return app.Read32(p);}
  void Write8(unsigned p,unsigned v){unsigned char c=v;app.Store(p,&c,1);}
  void Write32(unsigned p,unsigned v){app.Write32(p,v);}
  unsigned Length(unsigned p){for(unsigned n=0;n<1048576;++n){if(!Read8(p+n)||app.state==Failed)return n;}app.Fail("Computer-name string provider limit exceeded");return 0;}
  unsigned Open(bool create,unsigned&key){bool created=false;return app.registry.Open(0x80000002,Win98ComputerName::Path(),create,key,created);}
  void Close(unsigned key){app.registry.Close(key);}
  unsigned Set(unsigned key,const char*text,unsigned count){unsigned node=0,error=app.registry.Key(key,node);return error?error:app.registry.store.Set(node,Win98ComputerName::Value(),1,text,count);}
  unsigned Query(unsigned key,unsigned output,unsigned size){unsigned node=0,error=app.registry.Key(key,node);if(error)return error;const Registry::Value*value=nullptr;error=app.registry.store.Query(node,Win98ComputerName::Value(),value);if(error)return error;
   const unsigned capacity=Read32(size);Write32(size,value->size);if(!output)return 0;if(capacity<value->size)return 234;return !value->size||app.Store(output,value->data,value->size)?0:87;
  }
  unsigned char*Allocate(unsigned n){return new unsigned char[n];}void Free(unsigned char*p){delete[]p;}
  unsigned AnsiHigh(unsigned i){return OriginalComputerAnsiHigh[i];}unsigned OemByte(unsigned i){return OriginalComputerOemBytes[i];}
  void Upper(const unsigned char*input,unsigned n,unsigned output){
   struct M:CharacterTypeEnvironment {
    const unsigned char*input;
    M(Application&a,const unsigned char*p):CharacterTypeEnvironment(a),input(p){}
    unsigned Read8(unsigned p){return input[p-1];}unsigned Length(unsigned){return 0;}
    unsigned LocaleNumber(unsigned){return app.nls_system->locale.number;}
    unsigned CaseByte(unsigned descriptor,bool lower,unsigned c){auto*t=Table(descriptor);if(!t){app.Fail("Computer-name case table missing");return c;}return t->data[t->Offset(lower?7:6)+(c&255)];}
    unsigned CaseWord(unsigned descriptor,bool lower,unsigned i){auto*t=Table(descriptor);return t?t->Half(t->Offset(lower?7:6)+i*2):0;}
    bool Lead(unsigned descriptor,unsigned c){return LocaleLead(descriptor,c);}
    void Write8(unsigned p,unsigned v){unsigned char c=v;app.Store(p,&c,1);}
   }m(app,input);
   Win98CharacterCase::KernelCase(m,0x800,false,1,n,output,n);
  }
 }e{*this};
 if(API_IS(name,"GetComputerNameA")){
  // The original public guard reads each destination page and touches the size
  // DWORD before entering the body; actual output writes retain page checks.
  if(!a[1]||!virtual_memory.Access(a[1],4,true)){last_error=87;return 0;}
  const unsigned size=Read32(a[1]);
  if(a[0]&&size&&!virtual_memory.Access(a[0],size,false)){last_error=87;return 0;}
  return Win98ComputerName::Get(e,a[0],a[1]);
 }
 return Win98ComputerName::Set(e,a[0]);
}
}
