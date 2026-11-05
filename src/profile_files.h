// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "files.h"
#include "win98_profile_mutation.h"
namespace ProfileFiles {
// ARM provider for the two original KRNL386 cache records. Native file handles,
// storage and clocks replace DOS/GlobalHeap; normalized bytes use the original core.
class Service:public Win98Profile::MutationEnvironment {
 struct Record {unsigned char bytes[65536]{};Win98Profile::Buffer buffer{bytes,0,sizeof bytes,0};char path[260]{};unsigned stamp=0,deadline=0;bool loaded=false;};
 unsigned error=0;
 Record records[2];Files::Session files;Record*current=nullptr;
 unsigned Stamp(const char*);bool Load(Record&,bool,unsigned);bool Flush(Record&);void Drop(Record&);
 public:
 explicit Service(Files::Store&s):files(s){}
 unsigned LastError() const {return error;}
 Win98Profile::Buffer*Acquire(const char*filename,bool write,unsigned now,const Files::Session&caller);
 bool Reserve(Win98Profile::Buffer&,int)override;
 void Flush();void Invalidate();void Invalidate(const char*,unsigned,const Files::Session&);
 // Once at the start of the machine: the lines of SYSTEM.INI that name the
 // machine Win98 was installed on are written as the port's. True
 // when the file was changed.
 bool SyncHardware();
 bool SyncOemInfo();
 // OEMLOGO.BMP beside it, the NEO98 icon (src/neo98_oem_logo.h).
 bool SyncOemLogo();
};
Service&System();
}
