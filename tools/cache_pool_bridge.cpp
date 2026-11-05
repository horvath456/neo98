// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_cache_pool.h"
#include "../src/win98_vmm_bitmap.h"
struct Wire {Win98CachePool::State state;unsigned mode,result,queued,commits,fail_at,cancel_at,published;unsigned bitmap[8],pages[128];};
struct E {
 Wire&w;
 void Schedule(){w.queued=1;w.result=1;}
 unsigned Claim(unsigned capacity){return Win98VmmBitmap::Claim(w.bitmap,capacity);}
 void Release(unsigned slot){Win98VmmBitmap::Release(w.bitmap,slot);}
 bool Commit(unsigned){return ++w.commits!=w.fail_at;}
 void Publish(unsigned slot){w.pages[w.published++]=slot;if(w.published==w.cancel_at)w.state.pending=0;}
};
extern "C" void run(Wire*w){E e{*w};if(w->mode)Win98CachePool::Fill(w->state,e);else Win98CachePool::Request(w->state,e);}
