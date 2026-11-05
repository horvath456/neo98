// SPDX-License-Identifier: GPL-2.0-or-later
#include "x86_app.h"
#include "win98_crt_output.h"
#include "win98_nls.h"
#ifdef NEO98_RUNTIME_CONTENT
#include "windows_runtime_data.h"
#else
#include "../build/generated/nls_tables.h"
#endif
#include <string.h>
namespace X86 {
unsigned Application::CrtOutputApi(ApiName name,const unsigned*a){
 const Win98CrtOutput::Export*entry=nullptr;
 for(const auto&x:Win98CrtOutput::Exports)if(!strcmp(x.name,name)){entry=&x;break;}
 if(!entry){Fail("Unknown MSVCRT output entry");return ~0u;}
 struct Memory{
  Application&app;unsigned page;
  bool Good(){return app.state!=Application::Failed&&app.state!=Application::Exited;}
  void Unsupported(const char*reason){app.Fail(reason);}
  unsigned Read8(unsigned p){unsigned char v=0;app.Read(p,&v,1);return v;}
  unsigned Read16(unsigned p){unsigned short v=0;app.Read(p,&v,2);return v;}
  unsigned Read32(unsigned p){return app.Read32(p);}
  void Write8(unsigned p,unsigned v){unsigned char b=v;if(Good())app.Store(p,&b,1);}
  void Write16(unsigned p,unsigned v){unsigned short w=v;if(Good())app.Store(p,&w,2);}
  void Write32(unsigned p,unsigned v){if(Good())app.Write32(p,v);}
  const Win98Nls::Table*Table(){
   static const Win98Nls::Table tables[]={{OriginalCodePage1252,sizeof OriginalCodePage1252},{OriginalCodePage437,sizeof OriginalCodePage437},{OriginalCodePage850,sizeof OriginalCodePage850}};
   unsigned i=page==1252?0:page==437?1:page==850?2:3;
   if(i==3||!tables[i].Valid()){Unsupported("MSVCRT output locale conversion provider not ported");return nullptr;}return &tables[i];
  }
  // Core wctomb and mbtowc conversions.
  // SBCS non-C conversion uses the existing original Win98 NLS tables.
  bool Narrow(unsigned character,unsigned&byte){
   if(!page){if(character<=255){byte=character;return true;}}
   else if(auto*t=Table()){
    byte=t->Narrow(character);unsigned info=t->Offset(1);
    if(byte!=(t->Half(info+2)&255)||character==t->Half(info+6))return true;
   }
   if(Good())app.SetCrtErrno(42);return false;
  }
  bool Widen(unsigned byte,unsigned&character){if(!page){character=byte;return true;}auto*t=Table();if(!t)return false;character=t->Wide(byte);return true;}
  void Floating(unsigned long long,unsigned,unsigned,unsigned){Unsupported("MSVCRT floating output conversion provider not ported");}
 }memory{*this,LocaleCodePage()};
 // The format classification table is LC_CTYPE-based, not the independent
 // _setmbcp string table. All currently supplied LC_CTYPE pages are SBCS.
 if(memory.page&&memory.page!=1252&&memory.page!=437&&memory.page!=850){memory.Unsupported("MSVCRT output DBCS locale provider not ported");return ~0u;}
 unsigned format=a[entry->bounded?2:1];
 unsigned arguments=entry->vector?a[entry->bounded?3:2]:Register(Reg::Esp)+4+entry->argc*4;
 return Win98CrtOutput::Format(memory,a[0],entry->bounded?a[1]:0,format,arguments,entry->wide,entry->bounded);
}
}
