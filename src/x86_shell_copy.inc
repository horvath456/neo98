// SPDX-License-Identifier: GPL-2.0-or-later
unsigned Application::ShellCopyApi(ApiName name,const unsigned*a){
 struct Environment{
  Application&app;unsigned accesses=0;
  unsigned Get(unsigned p,unsigned bytes){unsigned value=0;if(++accesses>1048576){app.Fail("SHLWAPI string exceeds native access budget");return 0;}app.Read(p,&value,bytes);return value;}
  void Put(unsigned p,unsigned value,unsigned bytes){app.Store(p,&value,bytes);}
  unsigned Wide(unsigned s,unsigned d,unsigned count){unsigned a[]={0,0,s,~0u,d,count};return app.NlsApi(594,a);}
  unsigned Allocate(unsigned bytes,bool task){
   if(!task){unsigned a[]={0x40,bytes};return app.Api("LocalAlloc",a);}
   // Preserve the original COM allocator's identity; a local heap allocation
   // cannot be freed by the unchanged OLE32 CoTaskMemFree implementation.
   unsigned scratch=app.Allocate(64);if(!scratch)return 0;
   app.Store(scratch,"OLE32.DLL",10);unsigned module=app.ModuleApi("GetModuleHandleA",&scratch);
   if(!module)module=app.ModuleApi("LoadLibraryA",&scratch);
   app.Free(scratch);if(!module)return 0;
   unsigned proc=app.ModuleProcedure(app.ModuleIndex(module),"CoTaskMemAlloc",0);
   if(!proc){app.Fail("SHStrDup requires the guest OLE32 COM allocator");return 0;}
   return app.Call(proc,&bytes,1);
  }
 }e{*this};
 for(unsigned op=0;op<sizeof(Win98ShellCopy::Exports)/sizeof(*Win98ShellCopy::Exports);++op)if(!strcmp(name,Win98ShellCopy::Exports[op].name))return Win98ShellCopy::Call(e,op,a);
 Fail("Unknown SHLWAPI copy export");return 0;
}
