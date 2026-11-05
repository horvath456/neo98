// SPDX-License-Identifier: GPL-2.0-or-later
// VMM cache-pool behavior with native event and physical-page providers.
#pragma once
namespace Win98CachePool {
struct State {unsigned pending=0,resident=0,capacity=128,scan=0,pressure=0,reclaimable=0;};
template<class E> bool Request(State&s,E&e){
 if(s.pending||s.resident>=128)return false;
 unsigned target=s.capacity<128?s.capacity:128;
 s.pending=target-s.resident;s.scan=0;s.pressure=0;e.Schedule();return true;
}
template<class E> void Fill(State&s,E&e){
 if(!s.pending)return;
 do{
  unsigned slot=e.Claim(s.capacity);if(!slot)return;
  if(!e.Commit(slot)){e.Release(slot);return;}
  if(s.reclaimable)--s.reclaimable;
  e.Publish(slot);++s.resident;
  // A provider may cancel the request while a page is being committed.
  if(!s.pending)return;
 }while(--s.pending);
}
}
