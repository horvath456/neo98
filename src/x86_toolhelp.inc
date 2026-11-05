// SPDX-License-Identifier: GPL-2.0-or-later
// Toolhelp snapshots own copied rows: enumeration survives process/module exit.
struct Application::ToolhelpState {
    struct Row {unsigned char bytes[548]{};unsigned size=0;} ;
    struct Snapshot {unsigned index=0,refs=0,flags=0,counts[4]{},positions[4]{};Row *rows[4]{};Snapshot *next=nullptr;
        ~Snapshot(){for(auto *p:rows)delete[] p;}}
        *snapshots=nullptr;
    Snapshot *Find(unsigned index){for(auto *s=snapshots;s;s=s->next)if(s->index==index)return s;return nullptr;}
    ~ToolhelpState(){while(snapshots){auto *s=snapshots;snapshots=s->next;delete s;}}
};
void Application::FreeToolhelp(){delete toolhelp;toolhelp=nullptr;}
void Application::ReferenceSnapshot(unsigned index){if(toolhelp)if(auto *s=toolhelp->Find(index))++s->refs;}
void Application::ReleaseSnapshot(unsigned index){
    if(!toolhelp)return;
    for(auto **link=&toolhelp->snapshots;*link;link=&(*link)->next)if((*link)->index==index){
        auto *s=*link;if(!--s->refs){*link=s->next;delete s;}return;
    }
}
unsigned Application::ToolhelpApi(ApiName name,const unsigned *a){
    const auto bad=[&](unsigned error){last_error=error;return 0u;};
    if(API_IS(name,"Toolhelp32ReadProcessMemory")){
        unsigned open[]={0x10,0,a[0]?a[0]:process_id};unsigned handle=OpenProcessApi(open);if(!handle)return 0;
        unsigned args[]={handle,a[1],a[2],a[3],a[4]};unsigned result=ProcessMemoryApi("ReadProcessMemory",args),error=last_error;
        if(handle!=0x7fffffff)ThreadApi("CloseHandle",&handle);last_error=error;return result;
    }
    if(API_IS(name,"Heap32First")||API_IS(name,"Heap32Next")){
        if(!virtual_memory.Access(a[0],36,true)||Read32(a[0])<36)return bad(24);
        const bool first=API_IS(name,"Heap32First");
        unsigned pid=first?a[1]:Read32(a[0]+28),heap=first?a[2]:Read32(a[0]+32),previous=first?0:Read32(a[0]+8);
        auto *target=pid==process_id?this:static_cast<Application*>(User::ProcessContextOf(pid));if(!target)return bad(87);
        unsigned address=0,size=0,flags=0;
        if(heap&&heap==target->heap_base){
            for(const auto &block:target->AllocationBlocks())if(block.used&&block.address>previous&&(!address||block.address<address)){address=block.address;size=block.size;flags=1;}
        }else{
            bool known=false;if(target->heap_system)for(unsigned h:target->heap_system->heaps)if(h==heap)known=true;
            if(!known)return bad(87);
            for(unsigned region=heap,guard=0;region&&guard++<256;region=target->Read32(region+4)){
                const unsigned end=region+target->Read32(region)-4;
                for(unsigned block=region+(region==heap?0x74:8),walk=0;block<end&&walk++<1048576;){
                    if(!target->virtual_memory.Access(block,4,false))break;
                    unsigned header=target->Read32(block),bytes=header&0x0ffffffc;
                    if(bytes<4||bytes>end-block)break;
                    if(bytes>4&&block+4>previous&&(!address||block+4<address)){address=block+4;size=bytes-4;flags=(header&1)?4:1;}
                    block+=bytes;
                }
            }
        }
        if(!address)return bad(18);
        unsigned data[]={36,flags==4?0:address,address,size,flags,0,0,pid,heap};return Store(a[0],data,sizeof data);
    }
    if(API_IS(name,"CreateToolhelp32Snapshot")){
        // Win98 KERNEL32 requires at least one selection bit.
        if(!(a[0]&15)||(a[0]&~0x8000000fu)){last_error=87;return ~0u;}
        const unsigned pid=a[1]?a[1]:process_id;
        auto *target=pid==process_id?this:static_cast<Application*>(User::ProcessContextOf(pid));
        if((a[0]&9)&&!target){last_error=87;return ~0u;}
        if(!toolhelp)toolhelp=new(std::nothrow) ToolhelpState;if(!toolhelp){last_error=8;return ~0u;}
        unsigned slot=0;while(toolhelp->Find(slot))++slot;
        auto *s=new(std::nothrow) ToolhelpState::Snapshot;if(!s){last_error=8;return ~0u;}s->flags=a[0];
        const unsigned processes=User::ProcessContextCount();
        unsigned threads=0;
        for(unsigned i=0;i<processes;++i)if(auto *app=static_cast<Application*>(User::ProcessContextAt(i)))threads+=app->LiveThreads();
        const unsigned capacities[]={processes,threads,32,33};
        for(unsigned i=0;i<4;++i){s->rows[i]=new(std::nothrow) ToolhelpState::Row[capacities[i]];if(!s->rows[i]){delete s;last_error=8;return ~0u;}}
        const auto add=[&](unsigned group,const unsigned *fields,unsigned words,const char *text=nullptr){
            auto &row=s->rows[group][s->counts[group]++];row.size=fields[0];memcpy(row.bytes,fields,words*4);
            if(text){unsigned n=strlen(text);if(n>=row.size-words*4)n=row.size-words*4-1;memcpy(row.bytes+words*4,text,n);}return &row;
        };
        for(unsigned i=0;i<processes;++i){auto *app=static_cast<Application*>(User::ProcessContextAt(i));if(!app||app->state==Exited||app->state==Failed)continue;
            if(a[0]&2){unsigned row[]={296,0,app->process_id,app->HeapApi("GetProcessHeap",nullptr),0,app->LiveThreads(),app->parent_process?app->parent_process->process_id:0,app->process_priority_base,app->process_flags};add(0,row,9,app->program_name);}
            if(a[0]&4)for(const auto &t:app->threads)if(t.fiber&&!t.done){unsigned row[]={28,0,t.id,app->process_id,t.control_priority.effective,0,0};add(1,row,7);}
        }
        if(a[0]&8)for(const auto &m:target->modules)if(m.kind&&!m.unloading){
            unsigned image_size=0;if(target->virtual_memory.Access(m.handle,64,false)){
                unsigned pe=target->Read32(m.handle+60);if(pe<0x100000&&target->virtual_memory.Access(m.handle+pe+80,4,false))image_size=target->Read32(m.handle+pe+80);}
            unsigned row[]={548,m.handle,pid,m.references,m.references,m.handle,image_size,m.handle};auto *r=add(2,row,8);
            const char *leaf=m.path;for(const char *p=m.path;*p;++p)if(*p=='\\'||*p=='/')leaf=p+1;
            strncpy(reinterpret_cast<char*>(r->bytes+32),leaf,255);strncpy(reinterpret_cast<char*>(r->bytes+288),m.path,259);
        }
        if(a[0]&1){
            const unsigned primary=target->HeapApi("GetProcessHeap",nullptr);
            if(target->heap_system)for(unsigned heap:target->heap_system->heaps)if(heap){unsigned row[]={16,pid,heap,heap==primary?1u:0u};add(3,row,4);}
            unsigned row[]={16,pid,target->heap_base,0};add(3,row,4);
        }
        s->index=slot;s->next=toolhelp->snapshots;toolhelp->snapshots=s;
        unsigned handle=NewKernelHandle(10,slot);
        if(!handle){toolhelp->snapshots=s->next;delete s;return ~0u;}
        FindKernelHandle(handle)->inherit=(a[0]&0x80000000)!=0;return handle;
    }
    auto *handle=FindKernelHandle(a[0]);if(!handle||handle->kind!=10||!toolhelp)return bad(6);
    auto *s=toolhelp->Find(handle->object);if(!s)return bad(6);
    unsigned group=!strncmp(name,"Process",7)?0:!strncmp(name,"Thread",6)?1:!strncmp(name,"Module",6)?2:3;
    const unsigned sizes[]={296,28,548,16};
    if(!virtual_memory.Access(a[1],sizes[group],true)||Read32(a[1])<sizes[group])return bad(24);
    if(strstr(name,"First"))s->positions[group]=0;
    if(s->positions[group]>=s->counts[group])return bad(18);
    const auto &row=s->rows[group][s->positions[group]++];return Store(a[1],row.bytes,row.size);
}
