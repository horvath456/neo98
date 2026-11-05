// SPDX-License-Identifier: GPL-2.0-or-later
// Timers share the event wait machinery, but retain a distinct object type.
void Application::CancelTimerApcs(unsigned object){
    const unsigned token=0x65000000|object;
    for(auto &t:threads)for(unsigned i=0;i<t.apc_count;){
        if(t.apcs[i].io!=token){++i;continue;}
        memmove(t.apcs+i,t.apcs+i+1,(--t.apc_count-i)*sizeof t.apcs[0]);
    }
}
void Application::TickWaitableTimers(){
    if(!timers_armed)return;
    const auto now=Now();unsigned long long utc=0;
    if(timers_next_due&&now<timers_next_due)return;
    // What the scan leaves armed decides when the next one is needed: never,
    // at the earliest relative due time, or at once for an absolute timer or
    // one that signaled and still has a callback thread to check.
    bool armed=false,absolute=false;unsigned long long next=~0ull;
    for(unsigned i=0;i<KernelObjectSlots;++i){
        auto &e=events[i];if(!e.references||!e.timer||!e.active)continue;
        // The table is session-wide; only the process that armed the timer
        // owns its callback thread.
        if(e.timer_owner!=this)continue;
        if(e.callback&&(threads[e.thread].done||threads[e.thread].id!=e.thread_id)){
            e.active=false;CancelTimerApcs(i);continue;
        }
        if(e.absolute&&!utc)utc=CurrentFileTime();
        auto clock=e.absolute?utc:now;
        if(clock<e.due){armed=true;absolute|=e.absolute;if(!e.absolute&&e.due<next)next=e.due;continue;}
        e.signaled=true;
        if(e.period){const auto period=(unsigned long long)e.period*(e.absolute?10000:1000);e.due+=((clock-e.due)/period+1)*period;
            armed=true;absolute|=e.absolute;if(!e.absolute&&e.due<next)next=e.due;}
        else e.active=false;
        if(e.callback){
            auto &t=threads[e.thread];bool queued=false;
            for(unsigned n=0;n<t.apc_count;++n)if(t.apcs[n].io==(0x65000000|i))queued=true;
            if(!queued&&t.apc_count<64){
                if(!utc)utc=CurrentFileTime();
                const unsigned args[]={e.argument,unsigned(utc),unsigned(utc>>32)};
                const unsigned saved=last_error;
                EnqueueApc(e.thread,e.callback,args,3,0x65000000|i);last_error=saved;
            }
        }
    }
    timers_armed=armed;timers_next_due=armed&&!absolute?next:0;
}
unsigned Application::WaitableTimerApi(ApiName name,const unsigned *a){
    TickWaitableTimers();
    auto *h=FindKernelHandle(a[0]);
    if(!h||h->kind!=1||!events[h->object].timer){last_error=6;return 0;}
    if(!(h->access&2)){last_error=5;return 0;}
    auto &e=events[h->object];
    if(API_IS(name,"CancelWaitableTimer")){e.active=false;CancelTimerApcs(h->object);return 1;}
    long long due=0;
    if((int)a[2]<0||!virtual_memory.Access(a[1],8,false)||!Read(a[1],&due,8)){last_error=87;return 0;}
    CancelTimerApcs(h->object);e.signaled=false;e.active=true;e.absolute=due>=0;e.timer_owner=this;
    // Unsigned negation also handles INT64_MIN without signed overflow.
    e.due=e.absolute?(unsigned long long)due:Now()+((0ull-(unsigned long long)due)+9)/10;
    e.period=a[2];e.callback=a[3];e.argument=a[4];e.thread=current_thread;e.thread_id=ThreadId();
    timers_armed=true;timers_next_due=0;
    if(a[5])last_error=50; // No host wake-from-suspend provider.
    return 1;
}
