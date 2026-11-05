// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/profile_files.h"
#include <cassert>
#include <cstring>
#include <cstdio>
using namespace Win98Profile;
int main(){Files::Store disk;Files::Session caller(disk);ProfileFiles::Service service(disk);unsigned char section[]="App",key[]="Key",value[]="'first'";unsigned char out[256]{};auto*b=service.Acquire("private.ini",false,0,caller);assert(b&&b->flags==0x29&&b->size==3);b=service.Acquire("private.ini",true,1,caller);assert(b&&!(b->flags&8));assert(WriteString(service,*b,section,key,value));service.Flush();unsigned node;assert(!disk.Lookup("C:\\WINDOWS\\private.ini",node));auto*p=disk.Get(node);assert(p&&p->size==b->size-3);assert(String(b->text,b->size,section,key,nullptr,out,sizeof out)==5&&!strcmp((char*)out,"first"));
 const char changed[]="[App]\r\nKey=second\r\n";assert(!disk.Seed("C:\\WINDOWS\\private.ini",changed,sizeof changed-1));b=service.Acquire("private.ini",false,4000,caller);assert(String(b->text,b->size,section,key,nullptr,out,sizeof out)==5&&!strcmp((char*)out,"first"));b=service.Acquire("private.ini",false,5001,caller);assert(String(b->text,b->size,section,key,nullptr,out,sizeof out)==6&&!strcmp((char*)out,"second"));
 auto*other=service.Acquire("other.ini",true,5002,caller);assert(other);assert(WriteString(service,*other,section,key,value));b=service.Acquire("private.ini",false,5003,caller);assert(b&&String(b->text,b->size,section,key,nullptr,out,sizeof out)==6);assert(!disk.Lookup("C:\\WINDOWS\\other.ini",node));p=disk.Get(node);assert(p->size>0);
 assert(!caller.SetDirectory("C:\\WINDOWS\\TEMP"));assert(service.Acquire("relative.ini",true,5004,caller));assert(!disk.Lookup("C:\\WINDOWS\\relative.ini",node));assert(service.Acquire(".\\relative.ini",true,5005,caller));assert(!disk.Lookup("C:\\WINDOWS\\TEMP\\relative.ini",node));
 b=service.Acquire(nullptr,true,5006,caller);assert(b);assert(WriteString(service,*b,section,key,value));service.Flush();assert(service.Acquire("WIN.INI",false,5007,caller)==b);assert(service.Acquire("c:\\windows\\win.ini",false,5008,caller)==b);
 assert(!disk.Attributes("C:\\WINDOWS\\private.ini",1));service.Invalidate();b=service.Acquire("private.ini",false,6000,caller);assert(b&&(b->flags&8));assert(!WriteString(service,*b,section,key,value));
 assert(!service.Acquire("C:\\nonexistent\\missing.ini",true,6001,caller));
 // SYSTEM.INI names the display adapter of the port under [boot.description]
 // as it is read (src/profile_files.cpp); other lines stay.
 {const char ini[]="[boot]\r\ndisplay.drv=pnpdrvr.drv\r\n[boot.description]\r\nmouse.drv=Standard mouse\r\ndisplay.drv=Cirrus Logic\r\nkeyboard.typ=Standard\r\n";
  assert(!disk.Seed("C:\\WINDOWS\\SYSTEM.INI",ini,sizeof ini-1));service.Invalidate();auto*system=service.Acquire("SYSTEM.INI",false,7000,caller);assert(system);
  unsigned char description[]="boot.description",boot[]="boot",display[]="display.drv",keyboard[]="keyboard.typ",none[]="";
  assert(String(system->text,system->size,description,display,none,out,sizeof out)==21&&!strcmp((char*)out,"NEO98 Display Adapter"));
  assert(String(system->text,system->size,description,keyboard,none,out,sizeof out)==8&&!strcmp((char*)out,"Standard"));
  assert(String(system->text,system->size,boot,display,none,out,sizeof out)==11&&!strcmp((char*)out,"pnpdrvr.drv"));}
 // The OEM information is a real INI file, read by the original SYSDM.CPL.
 assert(service.SyncOemInfo());assert(!service.SyncOemInfo());
 {auto*oem=service.Acquire("C:\\WINDOWS\\SYSTEM\\OEMINFO.INI",false,8000,caller);assert(oem);
  unsigned char general[]="General",manufacturer[]="Manufacturer",support[]="Support Information",line[]="Line4";
  assert(String(oem->text,oem->size,general,manufacturer,nullptr,out,sizeof out)==5&&!strcmp((char*)out,"NEO98"));
  assert(String(oem->text,oem->size,support,line,nullptr,out,sizeof out)==strlen("See README.md for project information.")&&!strcmp((char*)out,"See README.md for project information."));}
 puts("PROFILE Files/cache PASS: private/default routing, two slots, 5s reload, persistence, shared-volume writes, readonly, missing parents, OEM information");}
