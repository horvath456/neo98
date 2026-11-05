// SPDX-License-Identifier: GPL-2.0-or-later
bool Application::EnqueueApc(unsigned index,unsigned procedure,const unsigned *args,unsigned count,unsigned io){
    if(index>=ThreadCapacity||threads[index].done||!threads[index].fiber){last_error=6;return false;}
    auto &t=threads[index];if(t.apc_count==64){last_error=8;return false;}
    auto &entry=t.apcs[t.apc_count++];entry.procedure=procedure;entry.count=count;entry.io=io;
    for(unsigned i=0;i<count;++i)entry.args[i]=args[i];return true;
}
bool Application::DeliverApcs(){
    auto &t=threads[current_thread];bool delivered=false;
    while(t.apc_count&&state!=Failed&&state!=Exited){
        auto item=t.apcs[0];memmove(t.apcs,t.apcs+1,(--t.apc_count)*sizeof item);
        const bool saved=t.alertable;t.alertable=false;
        Call(item.procedure,item.args,item.count);t.alertable=saved;delivered=true;
    }return delivered;
}
unsigned Application::KernelAsyncApi(ApiName name,const unsigned *a){
    TickWaitableTimers();
    switch (name.id)
    {
    API_CASE("QueueUserAPC"){
        unsigned index=current_thread;
        if(a[1]!=0xfffffffe){auto *h=FindKernelHandle(a[1]);if(!h||h->kind!=2){last_error=6;return 0;}index=h->object;}
        return EnqueueApc(index,a[0],a+2,1,0);
    }
    API_CASE("SleepEx"){
        auto &t=threads[current_thread];const bool saved=t.alertable;t.alertable=a[1]!=0;
        const auto deadline=a[0]==~0u?0:Now()+(unsigned long long)a[0]*1000;
        unsigned result=0;
        for(;;){
            if(t.alertable&&t.apc_count){DeliverApcs();result=0xc0;break;}
            if(!a[0]||(deadline&&Now()>=deadline)){if(!a[0])Suspend(Running);break;}
            t.block=3;ArmTimeout(a[0]);t.deadline=deadline;Suspend(Idle);if(state==Exited||state==Failed)break;
        }t.alertable=saved;t.block=0;t.deadline=0;return result;
    }
    API_CASE("WaitForSingleObjectEx"){
        const unsigned scratch=Allocate(4);if(!scratch){last_error=8;return ~0u;}Write32(scratch,a[0]);
        const unsigned args[]={1,scratch,0,a[1],a[2]};unsigned result=MultiwaitApi("WaitForMultipleObjectsEx",args);Free(scratch);return result;
    }
    API_CASE("FreeLibraryAndExitThread"){ModuleApi("FreeLibrary",a);return ThreadApi("ExitThread",a+1);}
    API_CASE("UninitializeCriticalSection")return CriticalApi("DeleteCriticalSection",a);
    API_CASE("PulseEvent"){
        auto *h=FindKernelHandle(a[0]);if(!h||h->kind!=1||events[h->object].timer){last_error=6;return 0;}
        if(!(h->access&2)){last_error=5;return 0;}
        auto &event=events[h->object];event.signaled=true;
        // Capture only threads already waiting, before resetting the event.
        for(unsigned i=0;i<ThreadCapacity;++i){auto &t=threads[i];if(t.done||!t.fiber)continue;
            if(t.block==2&&FindKernelHandle(t.wait_handle)&&FindKernelHandle(t.wait_handle)->kind==1&&FindKernelHandle(t.wait_handle)->object==h->object){t.pulse_handle=t.wait_handle;if(!event.manual)break;}
            else if(t.block==7){auto &w=t.multiwait;for(unsigned j=0;j<w.count;++j)if(w.objects[j]==((1u<<16)|(h->object+1))&&MultiwaitReady(i)){t.pulse_handle=a[0];break;}if(t.pulse_handle&&!event.manual)break;}
        }event.signaled=false;return 1;
    }
    API_CASE("TerminateProcess"){
        auto *target=ProcessFromHandle(a[0]);if(!target){last_error=6;return 0;}
        if(target->state==Exited){last_error=5;return 0;}
        target->TerminateSelf(a[1]);
        return 1;
    }
    default:
        break;
    }
    unsigned index=current_thread;
    if(a[0]!=0xfffffffe){auto *h=FindKernelHandle(a[0]);if(!h||h->kind!=2){last_error=6;return 0;}index=h->object;}
    auto &t=threads[index];
    if(API_IS(name,"TerminateThread")){
        if(user_initialized){DestroyThreadWindows(t.id);User::ForgetThreadQueue(t.id);}
        t.done=true;t.result=a[1];t.apc_count=0;User::ReleaseWindowUpdateLock(process_id,t.id);User::DropScreenThread(process_id,t.id);
        for(auto &m:kernel_mutexes)if(m.owner==t.id){m.owner=0;m.recursion=0;m.abandoned=true;}return 1;
    }
    const bool set=API_IS(name,"SetThreadContext");
    if(!API_IS(name,"GetThreadContext")&&!set){Fail(name);return 0;}
    if(t.done||!virtual_memory.Access(a[1],204,!set)){last_error=87;return 0;}
    const unsigned flags=Read32(a[1]);if(flags&~0x1001fu){last_error=87;return 0;}
    // A register write to a detached context cannot load segment descriptors
    // (Unicorn's context has no MMU). Switch the live engine temporarily.
    struct ContextScope {
        Cpu *cpu;CpuContext *saved=nullptr;
        ~ContextScope(){if(saved){cpu->Restore(saved);cpu->FreeContext(saved);}}
    } scope{cpu};
    if(index!=current_thread){
        if(cpu->NewContext(scope.saved)!=CpuError::Ok){last_error=8;return 0;}
        if(cpu->Save(scope.saved)!=CpuError::Ok||cpu->Restore(t.context)!=CpuError::Ok){last_error=87;return 0;}
    }
    const auto reg=[&](unsigned offset,int id,unsigned bytes=4){
        unsigned char value[16]{};unsigned result,word=0;const bool wide=id>=Reg::Fp0;
        if(set){if(!Read(a[1]+offset,value,bytes))return false;memcpy(&word,value,4);result=wide?cpu->SetWide(id,value):cpu->Set(id,word);}
        else{if(wide)result=cpu->GetWide(id,value);else{result=cpu->Get(id,word);memcpy(value,&word,4);}if(result==CpuError::Ok)Store(a[1]+offset,value,bytes);}
        return result==CpuError::Ok;
    };
    bool ok=true;
    if(flags&1){ok=reg(180,Reg::Ebp)&&reg(184,Reg::Eip)&&reg(188,Reg::Cs)&&reg(192,Reg::Eflags)&&reg(196,Reg::Esp)&&reg(200,Reg::Ss);}
    if(ok&&(flags&2))ok=reg(156,Reg::Edi)&&reg(160,Reg::Esi)&&reg(164,Reg::Ebx)&&reg(168,Reg::Edx)&&reg(172,Reg::Ecx)&&reg(176,Reg::Eax);
    if(ok&&(flags&4))ok=reg(140,Reg::Gs)&&reg(144,Reg::Fs)&&reg(148,Reg::Es)&&reg(152,Reg::Ds);
    if(ok&&(flags&8)){
        ok=reg(28,Reg::Fpcw)&&reg(32,Reg::Fpsw)&&reg(36,Reg::Fptag)&&reg(40,Reg::Fip)&&reg(44,Reg::Fcs)&&reg(48,Reg::Fdp)&&reg(52,Reg::Fds);
        for(unsigned i=0;ok&&i<8;++i)ok=reg(56+i*10,Reg::Fp0+i,10);
        if(!set)Write32(a[1]+136,0);
    }
    if(ok&&(flags&16)){const int ids[]={Reg::Dr0,Reg::Dr1,Reg::Dr2,Reg::Dr3,Reg::Dr6,Reg::Dr7};for(unsigned i=0;ok&&i<6;++i)ok=reg(4+i*4,ids[i]);}
    // A native API wait has no executable x86 body. Expose its caller's
    // continuation, then preserve a SetThreadContext redirection across the
    // eventual native API return instead of overwriting EIP/ESP in Dispatch.
    const bool native_wait=index!=current_thread && t.pending>=GateBase &&
        t.pending<GateBase+import_count*16 && !((t.pending-GateBase)&15);
    if(ok&&!set&&(flags&1)&&native_wait&&!t.context_redirect){
        unsigned sp=0;cpu->Get(Reg::Esp,sp);
        const auto &call=imports[(t.pending-GateBase)/16];
        if(virtual_memory.Access(sp,4,false)){
            Write32(a[1]+184,Read32(sp));
            Write32(a[1]+196,sp+4+((call.argc&0x80000000u)?0:(call.argc&0x7fffffffu)*4));
        }
    }
    if(ok&&set&&(flags&1)&&native_wait){
        t.context_redirect=flags;
        t.redirect_eip=Read32(a[1]+184);t.redirect_esp=Read32(a[1]+196);
        if(flags&2)t.redirect_eax=Read32(a[1]+176);
    }
    // Win98's public context is the 204-byte i386 structure; extended SSE state
    // requires its separate VWIN32 contract and is not silently claimed here.
    if(ok&&set&&index!=current_thread)ok=cpu->Save(t.context)==CpuError::Ok;
    if(!ok)last_error=87;return ok;
}
