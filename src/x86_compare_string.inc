// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_compare_string.h"
namespace X86 {
unsigned Application::CompareStringApi(const unsigned*a) {
 // public byte probes. Native page access replaces the x86 SEH
 // boundary; failing memory access is not claimed as original SEH dispatch.
 if(!a[2]||!a[4]||!virtual_memory.Access(a[2],1,false)||!virtual_memory.Access(a[4],1,false)) {last_error=87;return 0;}
 if(!InitializeNls())return 0;
 struct E:CharacterTypeEnvironment {
  unsigned reads=0;bool backwards=false;
  explicit E(Application&a):CharacterTypeEnvironment(a){}
  unsigned LookupLocale(unsigned lcid) {
   if(lcid==0x409)return CharacterTypeEnvironment::LookupLocale(lcid);
   const char *id=Win98Locale::SystemInfo(lcid,1),*cp=Win98Locale::SystemInfo(lcid,0x1004);
   if(!id||!cp||Win98SystemLocale::ParseHex(id)!=lcid)return 0;
   if(Win98SystemLocale::ParseDecimal(cp)!=1252){Unsupported("CompareStringA non-CP1252 sort provider is not installed");return 0;}
   const auto *t=tables.CodePage(1252);if(!t)return 0;
   auto word=[&](unsigned p){return t->Half(p)|(t->Half(p+2)<<16);};
   const unsigned overrides=t->Offset(18),count=word(overrides);
   // Windows 98 selects the locale override in CP_1252.NLS. A descriptor
   // without remapping/compression uses the shared sort/expansion tables.
   for(unsigned i=0;i<count;++i)if(word(overrides+4+i*8)==lcid){
    const unsigned at=overrides+word(overrides+8+i*8)*2;
    if(word(at+4)||word(at+8)||word(at+12)||t->Half(at+18)||t->Half(at+22)||t->Half(at+24)){
     Unsupported("CompareStringA locale weight overrides/compression are not installed");return 0;
    }
    backwards=word(at)!=0;break;
   }
   return 0xfff00000u|lcid;
  }
  const Win98Nls::Table *Table(unsigned descriptor){
   return (descriptor&0xfff00000u)==0xfff00000u?tables.CodePage(1252):CharacterTypeEnvironment::Table(descriptor);
  }
  bool Backward(unsigned descriptor){return (descriptor&0xfff00000u)==0xfff00000u&&backwards;}
  bool FastTable(unsigned descriptor) {return Table(descriptor)!=nullptr;}
  bool Aborted() const {return app.state==Failed;}
  unsigned Read8(unsigned p) {
   if(app.state==Failed)return 0;
   if(++reads>8388608) {Unsupported("Native CompareStringA read budget exceeded");return 0;}
   unsigned char c=0;if(!app.Read(p,&c,1))return 0;return c;
  }
  unsigned Weight(unsigned descriptor,unsigned c) {
   const auto*t=Table(descriptor);unsigned offset=t->Offset(13)+(c&255)*4;
   return t->Half(offset)|(t->Half(offset+2)<<16);
  }
  unsigned SystemWeight(unsigned c) {return Weight(SystemTable(),c);}
  unsigned Expansion(unsigned descriptor,unsigned index,unsigned part) {
   const auto*t=Table(descriptor);return t->Half(t->Offset(14)+index*4+part*2)&255;
  }
  void Error(unsigned code) {if(app.state!=Failed)app.last_error=code;}
  void Unsupported(const char*reason) {app.last_error=120;app.Fail(reason);}
 }e(*this);
 unsigned result=Win98CompareString::Compare(e,a[0],a[1],a[2],a[3],a[4],a[5]);
 return state==Failed?0:result;
}
}
