// SPDX-License-Identifier: GPL-2.0-or-later
unsigned Application::ShellCompareApi(ApiName name,const unsigned*a){
 struct Environment{Application&app;unsigned scratch=0,reads=0;~Environment(){if(scratch)app.Free(scratch);}
  unsigned Get(unsigned p,unsigned n){unsigned value=0;if(++reads>1048576){app.Fail("SHLWAPI comparison exceeds native read budget");return 0;}app.Read(p,&value,n);return value;}
  void Put(unsigned p,unsigned value,unsigned n){app.Store(p,&value,n);}
  unsigned Scratch(unsigned slot){if(!scratch)scratch=app.Allocate(1080);if(!scratch)app.Fail("SHLWAPI comparison scratch unavailable");return scratch+slot*528;}
  unsigned Narrow(unsigned p,unsigned n,unsigned d,unsigned capacity){unsigned a[]={0,0,p,n,d,capacity,0,0};return app.NlsApi(811,a);}
  unsigned Allocate(unsigned n){return app.Allocate(n);}void Free(unsigned p){app.Free(p);}
  unsigned Locale(){return app.DefaultLocaleApi("GetThreadLocale");}
  unsigned Compare(unsigned locale,unsigned flags,unsigned a,unsigned na,unsigned b,unsigned nb){unsigned args[]={locale,flags,a,na,b,nb};return app.CompareStringApi(args);}
  unsigned LocalCompare(unsigned a,unsigned b,bool insensitive){return Compare(0x400,insensitive?1:0,a,~0u,b,~0u)-2;}
  bool Lead(unsigned c){return app.NlsApi(537,&c)!=0;}
  bool Dbcs(){unsigned p=Scratch(2),a[]={0,p};return app.NlsApi(318,a)&&Get(p+6,1);}
  unsigned Next(unsigned p){return app.CharacterApi(4,&p);}
 }e{*this};for(unsigned op=0;op<8;++op)if(!strcmp(name,Win98ShellCompare::Exports[op].name))return Win98ShellCompare::Call(e,op,a);Fail("Unknown SHLWAPI comparison");return 0;
}
