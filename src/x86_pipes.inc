// SPDX-License-Identifier: GPL-2.0-or-later
// Native synchronous byte pipes. Guest fibers block without blocking macOS.
struct Application::PipeState {
    struct Buffer {
        unsigned char *bytes=nullptr;
        unsigned capacity=0,first=0,count=0,readers=0,writers=0;
        ~Buffer(){delete[] bytes;}
    };
    struct Handle {Buffer *buffer=nullptr;unsigned generation=1;bool write=false,inherit=false;};
    Handle handles[128];
    Handle *Find(unsigned value){unsigned i=(value&255)-1;return (value&0xff000000)==0x63000000&&i<128&&handles[i].buffer&&handles[i].generation==((value>>8)&65535)?&handles[i]:nullptr;}
    static void Reference(Handle &h){if(h.write)++h.buffer->writers;else ++h.buffer->readers;}
    static void Close(Handle &h){auto*b=h.buffer;if(!b)return;if(h.write)--b->writers;else --b->readers;h.buffer=nullptr;h.generation=h.generation%65535+1;if(!b->readers&&!b->writers)delete b;}
    unsigned Add(Buffer *b,bool write,bool inherit){for(unsigned i=0;i<128;++i)if(!handles[i].buffer){auto &h=handles[i];h.buffer=b;h.write=write;h.inherit=inherit;Reference(h);return 0x63000000|(h.generation<<8)|(i+1);}return 0;}
    ~PipeState(){for(auto &h:handles)Close(h);}
};
void Application::FreePipes(){delete pipes;pipes=nullptr;}
bool Application::OwnsPipe(unsigned handle) const{return pipes&&pipes->Find(handle);}
bool Application::PipeReady(unsigned handle,bool writing) const{
    auto*h=pipes?pipes->Find(handle):nullptr;if(!h)return true;
    auto*b=h->buffer;return writing?(!b->readers||b->count<b->capacity):(!b->writers||b->count);
}
bool Application::InheritPipes(const Application &source){
    if(!source.pipes)return true;
    if(!pipes)pipes=new PipeState;if(!pipes)return false;
    for(unsigned i=0;i<128;++i){auto &h=source.pipes->handles[i];if(h.buffer&&h.inherit){if(pipes->handles[i].buffer)return false;pipes->handles[i]=h;PipeState::Reference(pipes->handles[i]);}}
    return true;
}
unsigned Application::DuplicatePipe(const unsigned*a){
    auto*source=ProcessFromHandle(a[0]);auto*target=ProcessFromHandle(a[2]);
    if(!source||!target||!source->OwnsPipe(a[1])){last_error=6;return 0;}
    if(a[6]&~3u||!virtual_memory.Access(a[3],4,true)){last_error=87;return 0;}
    auto *h=source->pipes->Find(a[1]);
    // A unidirectional pipe cannot gain the other endpoint's access.
    const unsigned desired=h->write?0x40000000u:0x80000000u;
    if(!(a[6]&2)&&a[4]!=desired){last_error=5;return 0;}
    if(!target->pipes)target->pipes=new PipeState;
    unsigned result=target->pipes?target->pipes->Add(h->buffer,h->write,a[5]!=0):0;
    if(!result){last_error=8;return 0;}
    Write32(a[3],result);
    if(a[6]&1)PipeState::Close(*h);
    return 1;
}
unsigned Application::PipeApi(ApiName name,const unsigned*a){
    const auto bad=[&](unsigned error){last_error=error;return 0u;};
    const auto output=[&](unsigned p,unsigned n){return virtual_memory.Access(p,n,true);};
    if(API_IS(name,"CreatePipe")){
        if(!output(a[0],4)||!output(a[1],4))return bad(87);
        bool inherit=false;
        if(a[2]){if(!virtual_memory.Access(a[2],12,false)||Read32(a[2])!=12)return bad(87);inherit=Read32(a[2]+8)!=0;}
        unsigned capacity=a[3]?a[3]:4096;if(capacity>Files::DataCapacity)return bad(8);
        if(!pipes)pipes=new PipeState;if(!pipes)return bad(8);
        auto*b=new PipeState::Buffer;if(!b)return bad(8);
        b->capacity=capacity;b->bytes=new unsigned char[capacity];if(!b->bytes){delete b;return bad(8);}
        unsigned read=pipes->Add(b,false,inherit);if(!read){delete b;return bad(8);}
        unsigned write=pipes->Add(b,true,inherit);if(!write){PipeState::Close(*pipes->Find(read));return bad(8);}
        Write32(a[0],read);Write32(a[1],write);return 1;
    }
    auto*h=pipes?pipes->Find(a[0]):nullptr;if(!h)return bad(6);
    if(API_IS(name,"CloseHandle")){PipeState::Close(*h);return 1;}
    if(API_IS(name,"GetFileType"))return 3;
    if(API_IS(name,"PeekNamedPipe")){
        if(h->write)return bad(5);
        if((a[1]&&a[2]&&!output(a[1],a[2]))||(a[3]&&!output(a[3],4))||(a[4]&&!output(a[4],4))||(a[5]&&!output(a[5],4)))return bad(87);
        auto*b=h->buffer;if(!b->count&&!b->writers)return bad(109);
        unsigned count=a[1]?(a[2]<b->count?a[2]:b->count):0;
        for(unsigned i=0;i<count;++i)Store(a[1]+i,b->bytes+(b->first+i)%b->capacity,1);
        if(a[3])Write32(a[3],count);if(a[4])Write32(a[4],b->count);if(a[5])Write32(a[5],0);return 1;
    }
    const bool writing=API_IS(name,"WriteFile");
    if(!writing&&!API_IS(name,"ReadFile"))return bad(87);
    if(h->write!=writing)return bad(5);
    if(!output(a[3],4))return bad(87);Write32(a[3],0);
    if(a[4])return bad(87);
    if(a[2]&&!virtual_memory.Access(a[1],a[2],!writing))return bad(998);
    if(!a[2])return 1;
    unsigned done=0;
    for(;;){
        h=pipes?pipes->Find(a[0]):nullptr;if(!h)return bad(6);
        auto*b=h->buffer;
        if(writing&&!b->readers)return bad(109);
        if(!writing&&!b->count&&!b->writers)return bad(109);
        unsigned count=writing?b->capacity-b->count:b->count;
        if(count>a[2]-done)count=a[2]-done;
        while(count){
            unsigned at=writing?(b->first+b->count)%b->capacity:b->first;
            unsigned chunk=b->capacity-at;if(chunk>count)chunk=count;
            if(writing){if(!Read(a[1]+done,b->bytes+at,chunk))return bad(998);b->count+=chunk;}
            else{if(!Store(a[1]+done,b->bytes+at,chunk))return bad(998);b->first=(b->first+chunk)%b->capacity;b->count-=chunk;}
            done+=chunk;count-=chunk;
        }
        Write32(a[3],done);
        if(done==a[2]||(!writing&&done))return 1;
        auto&t=threads[current_thread];t.block=writing?11:10;t.wait_handle=a[0];Suspend(Idle);
        if(state==Exited||state==Failed)return bad(995);
    }
}
