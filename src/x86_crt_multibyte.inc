// SPDX-License-Identifier: GPL-2.0-or-later
// Included inside Application::CrtApi. This family uses the CRT multibyte
// codepage, independently of the existing LC_CTYPE conversion provider.
API_CASE("_setmbcp"){
 struct Configuration{
  Application&app;
  void Lock(unsigned id){app.CrtLock(true,id);}void Unlock(unsigned id){app.CrtLock(false,id);}
  unsigned Ansi(){return 1252;}unsigned Oem(){return 437;}unsigned LocalePage(){return app.LocaleCodePage();}
  bool CpInfo(unsigned page,unsigned&maximum,unsigned char*){if(page!=1252&&page!=437&&page!=850)return false;maximum=1;return true;}
  void CaseTable(Win98CrtMultibyte::State&){/* NLS case-map construction is outside this case-sensitive family. */}
 }e{*this};auto result=Win98CrtMultibyte::SetPage(e,crt_multibyte,a[0]);crt_code_page=crt_multibyte.page;return result;
}
API_CASE("_getmbcp")return Win98CrtMultibyte::GetPage(crt_multibyte);
API_CASE("_ismbblead")return Win98CrtMultibyte::Lead(crt_multibyte,a[0]);
API_CASE("_ismbbtrail")return Win98CrtMultibyte::Trail(crt_multibyte,a[0]);
API_CASE("_mbscmp") API_CASE("_mbsncmp") API_CASE("_mbsnbcmp") API_CASE("_mbslen") API_CASE("_mbsinc") API_CASE("_mbsdec") API_CASE("_mbsninc") API_CASE("_mbsnextc") API_CASE("_mbclen") API_CASE("_mbsnbcnt") API_CASE("_mbsnccnt") API_CASE("_ismbslead") API_CASE("_ismbstrail"){
 struct Reader{
  Application&app;unsigned Read8(unsigned p){unsigned char v=0;app.Read(p,&v,1);return v;}
  bool Multibyte(){return app.crt_multibyte.multibyte!=0;}bool LeadByte(unsigned c){return Win98CrtMultibyte::Lead(app.crt_multibyte,c);}
  void Lock(unsigned id){app.CrtLock(true,id);}void Unlock(unsigned id){app.CrtLock(false,id);}
 }e{*this};using namespace Win98CrtMultibyte;
 if(API_IS(name,"_mbscmp"))return Compare(e,a[0],a[1],0,0);
 if(API_IS(name,"_mbsncmp"))return Compare(e,a[0],a[1],a[2],1);
 if(API_IS(name,"_mbsnbcmp"))return Compare(e,a[0],a[1],a[2],2);
 if(API_IS(name,"_mbslen"))return Length(e,a[0]);
 if(API_IS(name,"_mbsinc"))return Increment(e,a[0]);
 if(API_IS(name,"_mbsdec"))return Decrement(e,a[0],a[1]);
 if(API_IS(name,"_mbsninc"))return Advance(e,a[0],a[1]);
 if(API_IS(name,"_mbsnextc"))return Character(e,a[0]);
 if(API_IS(name,"_mbclen"))return CharacterBytes(e,a[0]);
 if(API_IS(name,"_mbsnbcnt"))return ByteCount(e,a[0],a[1]);
 if(API_IS(name,"_mbsnccnt"))return CharacterCount(e,a[0],a[1]);
 return Boundary(e,a[0],a[1],API_IS(name,"_ismbstrail"));
}
