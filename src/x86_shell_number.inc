// SPDX-License-Identifier: GPL-2.0-or-later
unsigned Application::ShellNumberApi(ApiName name,const unsigned*a){
 struct Environment{
  Application&app;unsigned reads=0,scratch=0;
  ~Environment(){if(scratch)app.Free(scratch);}
  unsigned Get(unsigned p,unsigned bytes){unsigned value=0;if(++reads>1048576){app.Fail("SHLWAPI integer string exceeds native read budget");return 0;}app.Read(p,&value,bytes);return value;}
  void Put(unsigned p,unsigned value){app.Write32(p,value);}
  unsigned Next(unsigned p){return app.CharacterApi(4,&p);}
  unsigned Stack(){if(!scratch)scratch=app.Allocate(260);if(!scratch)app.Fail("SHLWAPI integer conversion scratch unavailable");return scratch;}
  unsigned Narrow(unsigned s,unsigned d,unsigned capacity){unsigned a[]={0,0,s,~0u,d,capacity,0,0};return app.NlsApi(811,a);}
  unsigned Allocate(unsigned bytes){unsigned a[]={0x40,bytes};return app.Api("LocalAlloc",a);}
  void Free(unsigned p){app.Api("LocalFree",&p);}
 }e{*this};
 for(unsigned op=0;op<4;++op)if(!strcmp(name,Win98ShellNumber::Exports[op].name))return Win98ShellNumber::Call(e,op,a);
 Fail("Unknown SHLWAPI integer conversion");return 0;
}
