// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98Multiwait {
using U=unsigned;
// KERNEL32. Objects are identities retained by Resolve.
template<class E> U Kernel(E&e,U count,const U*handles,U all,U timeout,U alertable){
 if(!count||count>64){e.Error(151);return ~0u;}
 U result;
 do{
  U objects[64],used=0;result=~0u;e.Lock();
  for(;used<count;++used){
   U object=e.Resolve(handles[used]);if(!object)break;
   U prior=0;for(;prior<used&&objects[prior]!=object;++prior){}
   if(prior!=used){e.Release(object);e.Error(87);break;}
   objects[used]=object;
  }
  if(used==count)result=e.Wait(count,objects,timeout,all,alertable);
  while(used)e.Release(objects[--used]);
  e.Unlock();
 }while(result==0x40000001);
 if(result==0xc0)e.DeliverAPC();
 return result;
}
// USER, Win32-task branch. Segmented allocation, queue event,
// input synchronization and process wake bookkeeping are provider operations.
template<class E> U Message(E&e,U count,const U*handles,U timeout,U mask,U flags){
 if(flags&~7u)return ~0u;
 U status=e.Status();
 if((status|((flags&4)?status>>16:0))&mask&65535)return count;
 if(!e.Allocate(count+1))return ~0u;
 e.SetMask((mask|0x2000)&65535);e.ResetQueue();
 U result=count;
 if(!(e.Status()&mask&65535)){
  e.PrepareQueue();
  result=e.KernelWait(count,handles,timeout,flags&1,flags&2);
  e.ProcessWake();
 }
 e.Free();e.SetMask(0);return result;
}
// USER. WaitMessage wakes for new queue bits, no removal.
template<class E> U MessageOnly(E&e){return e.WaitQueue(0x203f,~0u,1)|1u;}
}
