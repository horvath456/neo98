#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Inject allocator failures into the native path adapter with model guest/lock providers."""
from pathlib import Path
import hashlib,json,subprocess
root=Path(__file__).resolve().parents[1];out=root/'build/path-family/runtime-oom'
out.mkdir(parents=True,exist_ok=True)
source=(root/'src/x86_kernel_paths.cpp').read_text()
# Instrument only native allocator calls; the whole prepared adapter body is compiled.
instrumented=source.replace('malloc(', 'path_malloc(').replace('free(', 'path_free(')
(out/'adapter.cpp').write_text(instrumented)
(out/'x86_app.h').write_text(r'''
#pragma once
#include "api_name.h"
#include "files.h"
#include "win98_path_family.h"
#include <cstring>
namespace X86 {
class Application {
public:
 using U=unsigned;enum State {Running,Idle,Exited,Failed};
 // The adapter writes its call trace through the Application logger, exactly as
 // src/x86_app.h declares it; the fixture routes it into a counter.
 using Logger=void (*)(const char *);
 Logger log=nullptr;bool trace_calls=false;
 struct KernelPathEnvironment;KernelPathEnvironment *kernel_paths=nullptr;
 Files::Session files;U last_error=0;State status=Running;
 struct {U ansi=1252,oem=437;}system_locale;
 // The adapter saves and restores the calling thread's file-API mode.
 struct {U file_api_flags=0;}threads[1];U current_thread=0;
 struct Memory {bool Access(U p,U n,bool) const {
  return p>=0x10000&&p-0x10000<=4096&&n<=4096-(p-0x10000);
 }} virtual_memory;
 unsigned char guest[4096]{};unsigned depths[2]{},maximum_depth=0;
 // The adapter reads the running module's own path and writes the file part back.
 struct Module{char path[260]={};}modules[1];
 const char *ReadString(U p){if(p<0x10000||p-0x10000>=sizeof guest)return nullptr;
  const char *text=reinterpret_cast<const char*>(guest+(p-0x10000));
  return std::memchr(text,0,sizeof guest-(p-0x10000))?text:nullptr;}
 void Write32(U p,U value){Store(p,&value,4);}
 explicit Application(Files::Store&s):files(s){}
 ~Application(){FreeKernelPaths();}
 State Status()const{return status;}
 void Fail(const char*){status=Failed;}
 bool Read(U p,void*out,U n){if(p<0x10000||p-0x10000>sizeof guest||n>sizeof guest-(p-0x10000)){Fail("read");return false;}std::memcpy(out,guest+(p-0x10000),n);return true;}
 bool Store(U p,const void*in,U n){if(p<0x10000||p-0x10000>sizeof guest||n>sizeof guest-(p-0x10000)){Fail("write");return false;}std::memcpy(guest+(p-0x10000),in,n);return true;}
 U Api(ApiName,const U*){return 0;}
 U FindProcessEnvironment(const char*){return 0;} // This OOM fixture has an empty environment.
 bool InitializeNls(){return true;}
 static unsigned long long Now(){return 0;}
 void KernelPathLock(bool acquire,U id){if(id>1){Fail("lock");return;}if(acquire){++depths[id];if(depths[id]>maximum_depth)maximum_depth=depths[id];}else if(depths[id])--depths[id];else Fail("unlock");}
 U KernelPathApi(ApiName,const U*);
 U SearchPathApi(const U*);
 void FreeKernelPaths();
 Win98PathFamily::State *ProcessFileState();
};}
''')
(out/'test.cpp').write_text(r'''
#include <cstdlib>
#include <cassert>
#include <set>
#include <cstdio>
// KernelPathEnvironment itself plus the seven buffers Initialize() reserves:
// path, current_scratch, set_scratch, temp_scratch, task_directory,
// drive_directory and the text of the current directory.
static const unsigned ALLOCATIONS=8;
static unsigned attempt=0,fail_at=0,traced=0;
static void path_log(const char *line){assert(line&&*line);++traced;}
static std::set<void*> owned;
static void *path_malloc(std::size_t n){if(++attempt==fail_at)return nullptr;void*p=std::malloc(n);if(p)assert(owned.insert(p).second);return p;}
static void path_free(void*p){if(p){assert(owned.erase(p)==1);std::free(p);}}
#include "adapter.cpp"
int main()
{
 Files::Store store;
 for(unsigned failure=0;failure<=ALLOCATIONS;++failure)
 {
  X86::Application app(store);app.log=path_log;app.trace_calls=true;unsigned args[]={260,0x10100};
  attempt=0;fail_at=failure;
  unsigned result=app.KernelPathApi("GetCurrentDirectoryA",args);
  if(failure){assert(result==0&&app.last_error==8&&!app.kernel_paths&&owned.empty());assert(app.status==X86::Application::Running);}
  else{assert(result==3&&owned.size()==ALLOCATIONS&&app.kernel_paths);assert(!std::strcmp(reinterpret_cast<char*>(app.guest+0x100),"C:\\"));}
  assert(!app.depths[0]&&!app.depths[1]);
  fail_at=0;attempt=0;
  assert(app.KernelPathApi("GetCurrentDirectoryA",args)==3);
  assert(app.maximum_depth>=2&&!app.depths[0]&&!app.depths[1]);
  unsigned count[]={0,0xf2000000};
  assert(app.KernelPathApi("GetCurrentDirectoryA",count)==4&&app.Status()==X86::Application::Running);
  unsigned bad[]={0xf2000000,260,0x10100,0};
  assert(app.KernelPathApi("GetFullPathNameA",bad)==0&&app.Status()==X86::Application::Failed);
  assert(!app.depths[0]&&!app.depths[1]);
  app.FreeKernelPaths();assert(!app.kernel_paths&&owned.empty());
 }
 std::printf("Path runtime OOM: all %u allocation sites roll back and retry; recursive lock calls balance; %u trace lines\n",ALLOCATIONS,traced);
}
''')
command=['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(root/'src'),str(out/'test.cpp'),str(root/'src/files.cpp'),str(root/'src/volume.cpp'),str(root/'src/system_tree.cpp'),str(root/'src/win98_nls.cpp'),'-o',str(out/'test')]
subprocess.run(command,check=True)
subprocess.run([str(out/'test')],check=True,env=__import__('os').environ|{'UBSAN_OPTIONS':'halt_on_error=1'})
report={'adapter_sha256':hashlib.sha256(source.encode()).hexdigest(),'allocation_failures':8,'success_case':1,'checks':['entire native adapter with allocator calls instrumented','adapter trace logging through the Application logger','object and seven buffer allocations','zero retained allocations after every failed initialization','successful retry on same Application','balanced recursive lock provider calls','private tokens denied when used; ignored zero-capacity output remains legal','ASan and UBSan'],'boundary':'Guest memory/Application context and lock scheduling are model providers; native Files::Store and full path adapter/core are compiled. This does not replace integrated guest or contended-lock verification.'}
(out/'results.json').write_text(json.dumps(report,indent=2)+'\n')
