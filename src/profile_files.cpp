// SPDX-License-Identifier: GPL-2.0-or-later
#include "neo98_oem_logo.h"
#include "profile_files.h"
#include "neo98_hardware.h"
#include <string.h>
namespace ProfileFiles {
using namespace Win98Profile;
namespace {bool Equal(const char*a,const char*b){while(*a&&Lower(*a)==Lower(*b))++a,++b;return *a==*b;}}
Service&System(){static Service s(Files::System());return s;}
unsigned Service::Stamp(const char*path){U n;if(files.store.Lookup(path,n))return 0;const auto*p=files.store.Get(n);const auto*bytes=files.store.Bytes(n);if(!p)return 0;U hash=2166136261u;for(U i=0;bytes&&i<p->size;++i)hash=(hash^bytes[i])*16777619u;return (hash^p->size^p->attributes)|1;}
bool Service::Flush(Record&r){
 // KRNL386 writes size-3 then performs a zero-length truncation.
 if(!(r.buffer.flags&2))return true;U h,status,error=files.Open(r.path,3,3,4,0,h,status);if(error)return false;U done=0;error=files.Write(h,r.bytes,r.buffer.size>=3?r.buffer.size-3:0,done);
 bool okay=!error&&done==r.buffer.size-3;if(okay){files.Truncate(h);r.buffer.flags&=~0x12u;}files.Close(h);r.stamp=Stamp(r.path);return okay;
}
void Service::Flush(){for(auto&r:records)Flush(r);}
void Service::Drop(Record&r){if(r.buffer.flags&2)Flush();r.loaded=false;r.buffer.flags=0;}
void Service::Invalidate(){for(auto&r:records)Drop(r);}
void Service::Invalidate(const char*filename,U now,const Files::Session&caller){Buffer*b=Acquire(filename,false,now,caller);if(b)for(auto&r:records)if(&r.buffer==b){Drop(r);break;}}
// SYSTEM.INI names the display adapter of the machine Win98 was installed on
// under [boot.description] ("display.drv=Cirrus Logic"), the line the display
// class installer writes when the adapter changes and Windows Setup shows.
// The adapter here is the port's (src/neo98_hardware.h, README.md):
// the buffer says so as it is read, and SyncHardware writes the line into
// the file itself when the machine starts, for a program that reads the file
// as text (Notepad, SYSEDIT).
static U DisplayDescription(unsigned char*bytes,U count,U capacity){
 static const char section[]="[boot.description]",key[]="display.drv=";const char*name=Neo98Hardware::DisplayName;
 const U name_length=(U)strlen(name);bool inside=false;
 for(U line=0;line<count;){U end=line;while(end<count&&bytes[end]!='\r'&&bytes[end]!='\n')++end;
  const auto starts=[&](const char*text){U n=(U)strlen(text);if(end-line<n)return false;for(U i=0;i<n;++i)if(Lower(bytes[line+i])!=Lower((unsigned char)text[i]))return false;return true;};
  if(end>line&&bytes[line]=='[')inside=starts(section);
  else if(inside&&starts(key)){U value=line+(U)strlen(key),old=end-value;
   if(old==name_length&&!memcmp(bytes+value,name,old))return count;
   if(count-old+name_length>capacity)return count;
   memmove(bytes+value+name_length,bytes+end,count-end);memcpy(bytes+value,name,name_length);return count-old+name_length;}
  line=end;while(line<count&&(bytes[line]=='\r'||bytes[line]=='\n'))++line;}
 return count;
}
// The file through the machine's own file session: in a game machine the
// write goes to its layer and leaves the folder of the normal machine alone.
// Nothing is written when the line already says so.
bool Service::SyncHardware(){
 static const char path[]="C:\\WINDOWS\\SYSTEM.INI";static unsigned char bytes[0xffe0],before[0xffe0];
 U h=0,status=0;if(files.Open(path,3,3,3,0,h,status))return false;
 U length=0,done=0,position=0;bool changed=false;
 if(!files.Size(h,length)&&length&&length<=0xffdd&&!files.Read(h,bytes,length,done)&&done==length){
  memcpy(before,bytes,done);const U count=DisplayDescription(bytes,done,0xffdd);
  if(count!=done||memcmp(before,bytes,done)){
   U written=0;changed=!files.Seek(h,0,0,position)&&!position&&!files.Write(h,bytes,count,written)&&written==count&&!files.Truncate(h);}}
 files.Close(h);if(changed)Invalidate();return changed;
}
bool Service::SyncOemInfo(){
 static const char path[]="C:\\WINDOWS\\SYSTEM\\OEMINFO.INI";
 const U wanted=sizeof Neo98Hardware::OemInfo-1;
 U h=0,status=0,length=0,done=0,position=0;
 if(files.Open(path,3,3,4,0,h,status))return false;
 unsigned char before[sizeof Neo98Hardware::OemInfo]{};
 const bool same=!files.Size(h,length)&&length==wanted&&!files.Read(h,before,wanted,done)&&done==wanted&&
                 !memcmp(before,Neo98Hardware::OemInfo,wanted);
 bool changed=false;
 if(!same){U written=0;changed=!files.Seek(h,0,0,position)&&!position&&
   !files.Write(h,Neo98Hardware::OemInfo,wanted,written)&&written==wanted&&!files.Truncate(h);}
 files.Close(h);if(changed)Invalidate(path,0,files);return changed;
}
bool Service::SyncOemLogo(){
 static const char path[]="C:\\WINDOWS\\SYSTEM\\OEMLOGO.BMP";
 static unsigned char logo[Neo98OemLogo::Bytes],before[Neo98OemLogo::Bytes];
 Neo98OemLogo::Draw(logo);
 U h=0,status=0,length=0,done=0,position=0;
 if(files.Open(path,3,3,4,0,h,status))return false;
 const bool same=!files.Size(h,length)&&length==Neo98OemLogo::Bytes&&!files.Read(h,before,length,done)&&done==length&&
                 !memcmp(before,logo,length);
 bool changed=false;
 if(!same){U written=0;changed=!files.Seek(h,0,0,position)&&!position&&
   !files.Write(h,logo,Neo98OemLogo::Bytes,written)&&written==Neo98OemLogo::Bytes&&!files.Truncate(h);}
 files.Close(h);return changed;
}
bool Service::Load(Record&r,bool write,U now){
 // KRNL386: RW first, readonly fallback for reads; private missing
 // files get the original two-space seed and flags28, then Normalize adds bit1.
 r.buffer.flags=0;U h,status,error=files.Open(r.path,3,3,write?4:3,0,h,status);
 if(error&&!write){r.buffer.flags|=8;error=files.Open(r.path,1,3,3,0,h,status);}
 U count=2;r.bytes[0]=r.bytes[1]=' ';
 if(error){if(write||&r==&records[0])return false;r.buffer.flags|=0x28;}
 else{U length=0;error=files.Size(h,length);if(!length)r.buffer.flags|=0x10;U limit=length>0xffdd?0xffdd:length,done=0;if(!error)error=files.Read(h,r.bytes,limit,done);files.Close(h);if(error)return false;count=done<2?2:done;
  if(Equal(r.path,"C:\\WINDOWS\\SYSTEM.INI"))count=DisplayDescription(r.bytes,count,0xffdd);}
 r.buffer.size=Normalize(r.bytes,count,sizeof r.bytes);r.buffer.flags|=1;r.loaded=true;r.stamp=Stamp(r.path);r.deadline=now+5000;return true;
}
Win98Profile::Buffer*Service::Acquire(const char*filename,bool write,U now,const Files::Session&caller){
 error=0;
 char path[260];if(!filename||Equal(filename,"WIN.INI")||Equal(filename,"C:\\WINDOWS\\WIN.INI"))strcpy(path,"C:\\WINDOWS\\WIN.INI");
 else{char input[260];bool explicit_path=(filename[0]&&filename[1]==':')||strchr(filename,'/')||strchr(filename,'\\');U n=strlen(filename);if(n+(explicit_path?0:11)>=sizeof input)return nullptr;input[0]=0;if(!explicit_path)strcpy(input,"C:\\WINDOWS\\");strcat(input,filename);if(caller.FullPath(input,path))return nullptr;}
 // A cached profile does not bypass the caller's current sharing restrictions.
 // Probe before mutation so a failed write cannot leave dirty cached content.
 U probe=0,status=0;error=files.Open(path,write?3:1,3,write?4:3,0,probe,status);
 if(error&&error!=2)return nullptr;
 if(!error)files.Close(probe);
 Record&r=records[Equal(path,"C:\\WINDOWS\\WIN.INI")?0:1];if(!Equal(path,r.path)){Drop(r);strcpy(r.path,path);}
 //Windows 98 compares DOS file stamps after its5s deadline. RAM files have no
 // DOS timestamp; the provider uses content/attribute fingerprints instead.
 if(r.loaded&&now>=r.deadline){if(Stamp(path)!=r.stamp)Drop(r);else r.deadline=now+5000;}
 if(write&&(r.buffer.flags&0x20))Drop(r);if(!r.loaded&&!Load(r,write,now))return nullptr;current=&r;return &r.buffer;
}
bool Service::Reserve(Buffer&b,int change){
 // KRNL386. RAM buffers reserve the maximum selector-sized
 // storage up front; Files exposes512-byte allocation units. The original
 // cluster crossing test and seek/newsize/write-one-byte decisions remain.
 if(change>0&&U(change)>0xffe0-b.size)return false;if(change<0&&U(-change)>b.size)return false;
 U wanted=b.size+change;if(!(b.flags&0x10)&&!((wanted^b.size)&~511u))return true;
 b.flags&=~0x10u;if(!current)return false;U h,status;if(files.Open(current->path,3,3,4,0,h,status))return false;
 U position=0,done=0;unsigned char zero=0;U error=files.Seek(h,wanted,0,position);if(!error&&position==wanted)error=files.Write(h,&zero,1,done);files.Close(h);return !error&&position==wanted&&done==1;
}
}
