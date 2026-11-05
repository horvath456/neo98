// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_critical.h"
#include <cstring>
struct E final:Win98Critical::Environment{
 Win98Critical::Client client;Win98Critical::Mutex mutex;unsigned*output;unsigned count=0;
 void event(unsigned id,unsigned value=0){output[12+count++]=id;output[12+count++]=value;output[12+count++]=mutex.process_list;}
 Win98Critical::Client ReadClient(unsigned)override{return client;}bool ProbeClient(unsigned)override{return true;}void TouchClient(unsigned)override{}void WriteTag(unsigned,unsigned char)override{}void WriteObject(unsigned,unsigned)override{}
 Win98Critical::Mutex*FindMutex(unsigned)override{return &mutex;}Win98Critical::Mutex&Data(unsigned)override{return mutex;}bool HeapContains(unsigned)override{return true;}unsigned CurrentThread()override{return 0;}
 void GlobalLock()override{event(1,0x55);}void GlobalUnlock()override{event(2,0x55);}unsigned AllocateMutex()override{return 0;}void InitializeMutex(unsigned)override{}bool ProcessPresent()override{return true;}unsigned CreateProcessList()override{return 0;}bool BindProcess(unsigned)override{return true;}
 void DestroyProcessList(unsigned v)override{event(3,v);}void DestroyMutex(unsigned)override{}void FreeMutex(unsigned)override{}void Wait(unsigned)override{}void Wake(unsigned)override{}bool Fault(unsigned)override{return false;}
};
extern "C" void global_critical(const unsigned*input,unsigned*output){E e;e.output=output;memset(output,0,48*4);memcpy(&e.client,input,8);memcpy(&e.mutex,input+2,32);Win98Critical::MakeGlobal(e,0x201100);if(input[10])Win98Critical::MakeGlobal(e,0x201100);memcpy(output,&e.client,8);memcpy(output+2,&e.mutex,32);output[10]=e.count;}
