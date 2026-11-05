// SPDX-License-Identifier: GPL-2.0-or-later
// Native services below the original LZ32 wrappers and their Win16 HFILE bridge.
struct Application::LzState {
 struct File {unsigned handle=0,size=0,position=0;unsigned char *decoded=nullptr;bool initialized=false,exposed=false,no_file=false;int decode_error=0;} entries[128];
 ~LzState(){for(auto &f:entries)delete[] f.decoded;}
};
void Application::FreeLzState(){delete lz_state;lz_state=nullptr;}
unsigned Application::LegacyFileHandleApi(ApiName name,const unsigned *a){
 if(!lz_state)lz_state=new LzState;
 if(!lz_state)return 0;
 if(API_IS(name,"ordinal_20")){
  const unsigned i=(a[0]&65535)-256;if(i>=128)return 0;
  auto &f=lz_state->entries[i];if(!f.no_file&&!files.Owns(f.handle))return 0;f.exposed=true;return f.handle;
 }
 for(unsigned i=0;i<128;++i){auto &f=lz_state->entries[i];
  if(f.handle==a[0]&&(f.no_file||files.Owns(f.handle))){
   if(API_IS(name,"ordinal_22")){f.exposed=false;return 1;}
   return f.exposed?i+256:0;
  }
 }
 if(API_IS(name,"ordinal_21")&&files.Owns(a[0]))for(unsigned i=0;i<128;++i){auto &f=lz_state->entries[i];if(!f.no_file&&!files.Owns(f.handle)){delete[] f.decoded;f={};f.handle=a[0];f.exposed=true;return i+256;}}
 return 0;
}
unsigned Application::LzService(unsigned index,const unsigned *a){
 if(index==3)return 1;if(index==1)return 0;
 if(!lz_state)lz_state=new LzState;if(!lz_state)return unsigned(-5);
 if(index==8){
  // The DOS branch of LZOpenFile publishes an 8.3 path. Its Win32 fallback
  // (used for a new file) calls OpenFile directly and keeps the long path.
  char short_path[128]={},full[260];
  const unsigned old_error=last_error;
  if(!(a[2]&0x8000)&&!files.FullPath(ReadString(a[0]),full)){
   const unsigned block=Allocate(520);
   if(block){Store(block,full,strlen(full)+1);const unsigned query[]={block,block+260,260};
    const unsigned length=KernelPathApi("GetShortPathNameA",query);
    if(length&&length<sizeof short_path)Read(block+260,short_path,length+1);
    Free(block);
   }
  }
  last_error=old_error;
  unsigned handle=FileExtendedApi("OpenFile",a);
  if(handle==~0u)return ~0u;
  if(short_path[0])Store(a[1]+8,short_path,strlen(short_path)+1);
  // OF_PARSE/OF_DELETE/OF_EXIST succeed without leaving an open DOS file.
  // Their Win32 wrapper still maps the returned 16-bit result through ordinal 20.
  if(a[2]&0x4300)for(unsigned i=0;i<128;++i){auto &f=lz_state->entries[i];
   if(!f.no_file&&!files.Owns(f.handle)){
    delete[] f.decoded;f={};f.no_file=true;f.handle=0x4c000100u+i;return 256+i;
   }
  }
  unsigned args[]={handle};unsigned token=LegacyFileHandleApi("ordinal_21",args);
  if(!token){files.Close(handle);return ~0u;}
  if(!(a[2]&0xf303)){unsigned init[]={token};unsigned result=LzService(7,init);if(int(result)<0){unsigned close[]={token};LzService(4,close);return result;}}
  return token;
 }
 if(index==0){
  char path[260];const char *input=ReadString(a[0]);if(strlen(input)>=sizeof path)return unsigned(-1);strcpy(path,input);
  unsigned handle,status;unsigned error=files.Open(path,1,3,3,0,handle,status);if(error)return unsigned(-1);
  unsigned char header[14];unsigned done=0;error=files.Read(handle,header,14,done);files.Close(handle);if(error)return unsigned(-3);
  if(Win98Szdd::Header(header,done)){
   char *leaf=path;for(char *p=path;*p;++p)if(*p=='/'||*p=='\\'||*p==':')leaf=p+1;
   char *dot=strchr(leaf,'.');unsigned n=strlen(path);
   if(dot&&dot[1]==0)*dot=0;
   else if(dot&&n&&path[n-1]=='_'){
    unsigned char last=header[9];bool upper=false;
    for(unsigned i=n;i;--i){char c=path[i-1];if(c>='a'&&c<='z')break;if(c>='A'&&c<='Z'){upper=true;break;}}
    if(upper&&last>='a'&&last<='z')last-=32;if(!upper&&last>='A'&&last<='Z')last+=32;path[n-1]=last;
   }
  }
  unsigned bytes=strlen(path)+1;if(!virtual_memory.Access(a[1],bytes,true))return unsigned(-3);Store(a[1],path,bytes);return 1;
 }
 unsigned slot=(a[0]&65535)-256;if(slot>=128)return unsigned(-1);auto &f=lz_state->entries[slot];
 if(f.no_file){if(index==4){f={};return 0;}return unsigned(-1);}
 if(!files.Owns(f.handle))return unsigned(-1);
 if(index==4){files.Close(f.handle);delete[] f.decoded;f={};return 0;}
 if(index==7){
  if(f.initialized)return a[0];unsigned size=0,position=0,done=0;
  if(files.Size(f.handle,size)||files.Seek(f.handle,0,0,position))return unsigned(-1);
  unsigned char header[14];if(files.Read(f.handle,header,14,done))return unsigned(-3);
  files.Seek(f.handle,0,0,position);
  if(!Win98Szdd::Header(header,done)){f.initialized=true;return a[0];}
  if(header[8]!='A')return unsigned(-8);unsigned length=Win98Szdd::Size(header);
  if(size>Files::DataCapacity||length>Files::DataCapacity)return unsigned(-5);
  auto *input=new unsigned char[size];auto *decoded=new unsigned char[length?length:1];
  if(!input||!decoded){delete[] input;delete[] decoded;return unsigned(-5);}
  unsigned error=files.Read(f.handle,input,size,done),produced=0;
  int result=error||done!=size?-3:Win98Szdd::DecodeAvailable(input,size,decoded,length,produced);delete[] input;
  if(result&&!produced){delete[] decoded;return unsigned(result);}
  // LZ expands on demand. A truncated tail must not discard bytes that can
  // already be read; report its decoding error when a read reaches that tail.
  f.decoded=decoded;f.size=produced;f.decode_error=result;f.position=0;f.initialized=true;return a[0];
 }
 if(index==6){
  if(a[2]>2)return unsigned(-1);if(!f.decoded){unsigned pos=0;return files.Seek(f.handle,int(a[1]),a[2],pos)?unsigned(-1):pos;}
  long long pos=(a[2]==0?0:a[2]==1?f.position:f.size)+(long long)(int)a[1];
  if(pos<0||pos>0x7fffffff)return unsigned(-1);f.position=unsigned(pos);return f.position;
 }
 if(index==5){
  if(a[2]>0x7fff||!virtual_memory.Access(a[1],a[2],true))return unsigned(-3);
  if(f.decoded){unsigned n=f.position>=f.size?0:f.size-f.position;if(n>a[2])n=a[2];if(n)Store(a[1],f.decoded+f.position,n);f.position+=n;return n<a[2]&&f.decode_error?unsigned(f.decode_error):n;}
  unsigned char buffer[32768];unsigned n=0;if(files.Read(f.handle,buffer,a[2],n))return unsigned(-3);if(n)Store(a[1],buffer,n);return n;
 }
 if(index==2||index==9){
  unsigned other=(a[1]&65535)-256;if(other>=128||!files.Owns(lz_state->entries[other].handle))return unsigned(-2);
  unsigned init[]={a[0]};unsigned initialized=LzService(7,init);if(int(initialized)<0)return initialized;
  unsigned total=0;unsigned char buffer[4096];
  for(;;){unsigned count=0;
   if(f.decoded){count=f.position>=f.size?0:f.size-f.position;if(count>sizeof buffer)count=sizeof buffer;if(count)memcpy(buffer,f.decoded+f.position,count);f.position+=count;}
   else if(files.Read(f.handle,buffer,sizeof buffer,count))return unsigned(-3);
   if(!count)return total;unsigned written=0;if(files.Write(lz_state->entries[other].handle,buffer,count,written)||written!=count)return unsigned(-4);total+=count;
  }
 }
 Fail("Unmapped LZ service");return unsigned(-1);
}
