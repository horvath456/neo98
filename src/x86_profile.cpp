// SPDX-License-Identifier: GPL-2.0-or-later
// Native Win32 marshalling and Files provider around original KRNL profile bodies.
#include "x86_app.h"
#include "profile_files.h"
#include "win98_profile_struct.h"
#include <stdlib.h>
#include <string.h>
namespace X86 {
namespace {
struct CopyString {unsigned char*p=nullptr;explicit CopyString(const char*s){if(s){unsigned n=strlen(s)+1;p=(unsigned char*)malloc(n);if(p)memcpy(p,s,n);}}~CopyString(){free(p);}};
}
unsigned Application::ProfileString(const char*section,const char*key,const char*fallback,char*out,unsigned capacity,const char*filename){
 using namespace Win98Profile;if(capacity>65535)return 0;CopyString value(fallback);if(fallback&&!value.p)return 0;auto&service=ProfileFiles::System();Buffer*b=service.Acquire(filename,false,Now()/1000,files);if(!b&&service.LastError())last_error=service.LastError();unsigned char empty[3]={'\r','\n',0};
 if(!section){if(!b){if(capacity)out[0]=0;return 0;}return SectionNames(b->text,b->size,(unsigned char*)out,capacity);}
 return Win98Profile::String(b?b->text:empty,b?b->size:3,(const unsigned char*)section,(const unsigned char*)key,value.p,(unsigned char*)out,capacity);
}
unsigned Application::ProfileApi(ApiName name,const unsigned*a){
 using namespace Win98Profile;
 // The original public A gates reject counts with nonzero high WORD before
 // invoking their Win16 thunk. Guest pointer access remains this runtime's provider.
 bool private_api=strstr(name,"Private")!=nullptr,write=!strncmp(name,"Write",5),integer=strstr(name,"IntA")!=nullptr,structure=strstr(name,"StructA")!=nullptr,names=strstr(name,"SectionNames")!=nullptr,section_api=!names&&strstr(name,"SectionA")!=nullptr;
 U count_index=structure?3:names?1:section_api?2:4;if(!write||structure){if(!integer&&a[count_index]>65535){last_error=87;return 0;}}
 struct Input {unsigned char*p=nullptr;~Input(){free(p);}}inputs[4];
 auto input=[&](unsigned address,unsigned slot,bool multi=false)->unsigned char*{if(!address)return nullptr;auto&v=inputs[slot];v.p=(unsigned char*)malloc(65538);if(!v.p){last_error=8;return nullptr;}bool previous=false;for(U n=0;n<65536;++n){if(!Read(address+n,v.p+n,1))return nullptr;if(!v.p[n]){if(!multi||previous){v.p[n+1]=0;return v.p;}previous=true;}else previous=false;}last_error=87;return nullptr;};
 unsigned char *section=nullptr,*key=nullptr,*value=nullptr;const char*filename=nullptr;
 U filename_address=private_api?a[structure?4:names?2:section_api?(write?2:3):integer?3:write?3:5]:0;
 if(filename_address){filename=(const char*)input(filename_address,3);if(!filename)return 0;}
 if(!names){section=input(a[0],0);if(a[0]&&!section)return 0;}
 if(!section_api&&!names){key=input(a[1],1);if(a[1]&&!key)return 0;}
 auto&service=ProfileFiles::System();U now=Now()/1000;
 if(write&&!structure){value=input(a[section_api?1:2],2,section_api);if(a[section_api?1:2]&&!value)return 0;
  if(!section&&!key&&!value){if(!section_api){service.Flush();service.Invalidate(filename,now,files);}return 0;}
  Buffer*b=service.Acquire(filename,true,now,files);if(!b){if(service.LastError())last_error=service.LastError();return 0;}U result=section_api?WriteSection(service,*b,section,value):WriteString(service,*b,section,key,value);
  // Original writeback runs from the system idle/flush path. NEO98 has no such
  // global idle task yet, so successful mutations flush through the same service.
  if(result)service.Flush();return result;
 }
 if(integer){unsigned char out[256];U got=ProfileString((char*)section,(char*)key,"",(char*)out,sizeof out,filename);return got?Number(out):a[2];}
 if(structure){
  struct Env:StructEnvironment{Application&app;ProfileFiles::Service&service;unsigned char*section,*key;const char*filename;unsigned now;Env(Application&a,ProfileFiles::Service&s,unsigned char*c,unsigned char*k,const char*f,unsigned n):app(a),service(s),section(c),key(k),filename(f),now(n){}
   unsigned char*Allocate(U n)override{return (unsigned char*)malloc(n);}void Free(unsigned char*p)override{free(p);}U Read(unsigned char*p,W n)override{return app.ProfileString((char*)section,(char*)key,nullptr,(char*)p,n,filename);}
   U Write(const unsigned char*p)override{Buffer*b=service.Acquire(filename,true,now,app.files);if(!b)return 0;CopyString copy((const char*)p);if(p&&!copy.p)return 0;U result=WriteString(service,*b,section,key,copy.p);if(result)service.Flush();return result;}
  }env(*this,service,section,key,filename&&*filename?filename:nullptr,now);
  unsigned char*data=(unsigned char*)malloc(U(a[3])+1);if(!data){last_error=8;return 0;}if(a[2]&&!Read(a[2],data,a[3])){free(data);return 0;}U result=write?WriteStruct(env,a[2]?data:nullptr,a[3]):ReadStruct(env,data,a[3]);if(!write&&a[2])Store(a[2],data,a[3]);free(data);return result;
 }
 U capacity=a[count_index],output=a[names?0:section_api?1:3];unsigned char*out=(unsigned char*)malloc(65538);if(!out){last_error=8;return 0;}memset(out,0,65538);if(capacity&&!Read(output,out,capacity)){free(out);return 0;}U result=0;
 if(names||section_api){Buffer*b=service.Acquire(filename,false,now,files);unsigned char empty[3]={'\r','\n',0};if(names){if(b)result=SectionNames(b->text,b->size,out,capacity);else out[0]=0;}else result=SectionText(b?b->text:empty,b?b->size:3,section,out,capacity);}
 else{value=input(a[2],2);if(a[2]&&!value){free(out);return 0;}result=ProfileString((char*)section,(char*)key,(char*)value,(char*)out,capacity,filename);}
 if(capacity)Store(output,out,capacity);free(out);return result;
}
}
