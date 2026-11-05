// SPDX-License-Identifier: GPL-2.0-or-later
// Native metadata for the existing RAM store. No host-volume query or FAT claim.
unsigned Application::VolumeInformationApi(ApiName name,const unsigned*a){
 struct E{
  Application&app;
  unsigned Byte(unsigned p){unsigned char v=0;app.Read(p,&v,1);return v;}
  void Byte(unsigned p,unsigned v){unsigned char b=v;app.Store(p,&b,1);}
  void Write(unsigned p,unsigned v){app.Write32(p,v);}void Error(unsigned v){app.last_error=v;}
  unsigned Read(unsigned p){return app.Read32(p);}
  void Text(unsigned p,const char*s){app.Store(p,s,strlen(s)+1);}
  void Current(unsigned p){Text(p,app.files.current);}
  bool FullPath(unsigned p,unsigned out){unsigned args[]={p,260,out,0};return app.KernelPathApi("GetFullPathNameA",args)!=0;}
  // Every mounted drive answers, not only C:. A letter with a volume provider
  // behind it reports that medium; without one it is the RAM volume.
  //
  static char Letter(unsigned drive){return char('A'+drive-1);}
  bool Exists(unsigned index){return app.files.store.Drive(char('A'+index));}
  bool IoctlDrive(unsigned drive){return app.files.store.Drive(Letter(drive));}
  // Windows 98 passes the zero-based index here, unlike its other services.
  bool Cd(unsigned index){return app.files.store.DriveType(char('A'+index))==5;}
  bool Remote(unsigned drive,unsigned&flags){flags=0;return app.files.store.Drive(Letter(drive));}
  unsigned FixedMedia(unsigned drive){return app.files.store.DriveType(Letter(drive))==3;}
  // A volatile RAM volume has no persistent media serial and reports zero.
  bool Serial(unsigned drive,unsigned&serial){serial=app.files.store.DriveSerial(Letter(drive));return true;}
  unsigned Query(unsigned root,unsigned fs,unsigned&maximum,unsigned&flags){
   const char*s=app.ReadString(root);if(strlen(s)!=3||s[1]!=':'||s[2]!='\\')return 15;
   if(!app.files.store.Drive(s[0]))return 15;
   // An empty CD-ROM drive.
   if(!app.files.store.Ready(s[0]))return 21;
   // Node::name[256] retains filename bytes. The store is case-insensitive and
   // implements no ACL, compression, encryption, sparse file or Unicode layer.
   Text(fs,"NEO98");maximum=255;flags=2;return 0;
  }
  bool Label(unsigned root,unsigned label){const char*s=app.ReadString(root);if(strlen(s)!=3||s[1]!=':')return false;
   if(!app.files.store.Ready(s[0]))return false;
   const char*text=app.files.store.DriveLabel(s[0]);if(!text)return false;Text(label,text);return true;}
  unsigned Attributes(unsigned path){unsigned node,error=app.files.store.Lookup(app.ReadString(path),node);if(error){Error(error);return ~0u;}return app.files.store.Get(node)->attributes;}
  // The RAM volume allocates by the byte, so its unit stays one byte. A mounted
  // medium reports the FAT32 geometry Win98 would format it with, and the old
  // query caps its figures where FAT32 stops counting.
  bool Geometry(unsigned drive,Win98DiskSpace::Geometry&g,bool cap){
   const char letter=Letter(drive);
   if(app.files.store.Drive(letter)&&!app.files.store.Ready(letter)){Error(21);return false;}
   const char*where=app.files.store.DriveDescription(letter);
   if(!where)return false;
   if(!strcmp(where,"RAM")){unsigned total,available;app.files.store.DataSpace(total,available);g={1,1,available,total};return true;}
   unsigned long long total=0,available=0;app.files.store.DriveSpace(letter,total,available);
   const unsigned long long cluster=4096,limit=0x7ffff000ull;
   if(cap){if(total>limit)total=limit;if(available>limit)available=limit;}
   g={8,512,unsigned(available/cluster),unsigned(total/cluster)};return true;
  }
  bool Basic(unsigned drive,Win98DiskSpace::Geometry&g){
   if(!Geometry(drive,g,true))return false;
   // The DOS disk-free call behind the legacy API has WORD cluster counts.
   // Increase its reported allocation unit instead of overflowing that count.
   while(g.total>65535){g.sectors*=2;g.free/=2;g.total/=2;}
   return true;
  }
  bool Extended(unsigned path,Win98DiskSpace::Geometry&g,unsigned&size){size=0x2c;
   const char*s=app.ReadString(path);
   return Geometry(s&&s[0]&&s[1]==':'?unsigned((s[0]&0xdf)-'A'+1):3u,g,false);}
 }e{*this};
 switch (name.id)
 {
 API_CASE("GetDiskFreeSpaceA") API_CASE("GetDiskFreeSpaceExA"){
  const bool extended=API_IS(name,"GetDiskFreeSpaceExA");
  for(unsigned i=1;i<(extended?4u:5u);++i)if(a[i]&&!virtual_memory.Access(a[i],extended?8:4,true)){last_error=87;return 0;}
  unsigned scratch=Allocate(1024);if(!scratch){last_error=8;return 0;}
  unsigned result=extended?Win98DiskSpace::QueryEx(e,a,scratch):Win98DiskSpace::Legacy(e,a,scratch);Free(scratch);return result;
 }
 API_CASE("GetDriveTypeA"){
  auto*mode=ProcessFileState();if(!mode)return 0;
  if((mode->process_flags&0x40)&&!(threads[current_thread].file_api_flags&6)){Fail("GetDriveTypeA OEM conversion provider is not ported");return 0;}
  unsigned scratch=Allocate(1024);if(!scratch){last_error=8;return 0;}
  unsigned result=Win98DriveType::Query(e,a[0],scratch);Free(scratch);return result;
 }
 API_CASE("GetLogicalDrives")return Win98Volume::Drives(e);
 API_CASE("GetLogicalDriveStringsA"){
  if(a[0]&&!virtual_memory.Access(a[1],a[0],true)){last_error=87;return 0;}
  return Win98Volume::Strings(e,a[0],a[1]);
 }
 default:
     break;
 }
 for(unsigned index=0;index<2;++index){unsigned i=index?6:1;if(a[i]&&a[i+1]&&!virtual_memory.Access(a[i],a[i+1],true)){last_error=87;return 0;}}
 for(unsigned i=3;i<=5;++i)if(a[i]&&!virtual_memory.Access(a[i],4,true)){last_error=87;return 0;}
 unsigned scratch=Allocate(1024);if(!scratch){last_error=8;return 0;}
 unsigned result=Win98Volume::Information(e,a,scratch);Free(scratch);return result;
}
