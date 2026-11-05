// SPDX-License-Identifier: GPL-2.0-or-later
unsigned Application::FileExtendedApi(ApiName name,const unsigned *a){
    const auto bad=[&](unsigned error){last_error=error;return 0u;};
    switch (name.id)
    {
    API_CASE("LockFile") API_CASE("UnlockFile"){
        unsigned error=files.Lock(a[0],(unsigned long long)a[1]|((unsigned long long)a[2]<<32),(unsigned long long)a[3]|((unsigned long long)a[4]<<32),name[0]=='U');
        return error?bad(error):1;
    }
    API_CASE("MoveFileExA"){
        // Windows 98 accepts only its undocumented 10h/30h modes. Standard NT
        // MOVEFILE_* combinations answer ERROR_CALL_NOT_IMPLEMENTED on Win98.
        if(a[2]!=0x10&&a[2]!=0x30)return bad(120);
        char source[260],destination[260];unsigned error=files.FullPath(ReadString(a[0]),source);
        if(!error)error=files.FullPath(ReadString(a[1]),destination);
        if(!error)error=files.store.Rename(source,destination);
        return error?bad(error):1;
    }
    API_CASE("SetVolumeLabelA"){
        char letter='C';
        if(a[0]){const char *root=ReadString(a[0]);if(strlen(root)!=3||root[1]!=':'||(root[2]!='\\'&&root[2]!='/')||!files.store.Drive(root[0]))return bad(15);letter=root[0];}
        const char *label=a[1]?ReadString(a[1]):"";unsigned n=strlen(label);
        if(n>11||strpbrk(label,"*?/:\\|,;+=<>[]\""))return bad(123);
        for(unsigned i=0;i<n;++i)if((unsigned char)label[i]<32)return bad(123);
        // A read-only medium keeps its label; Win98 reports write protection.
        if(!files.store.SetDriveLabel(letter,label))return bad(19);
        return 1;
    }
    API_CASE("OpenFile"){
        const unsigned previous_error=last_error;
        if(!virtual_memory.Access(a[1],136,true)){last_error=87;return ~0u;}
        unsigned char of[136]{};Read(a[1],of,sizeof of);char path[260];
        unsigned error=(a[2]&0x8000)&&!memchr(of+8,0,128)?123:
            files.FullPath((a[2]&0x8000)?reinterpret_cast<char*>(of+8):ReadString(a[0]),path);
        unsigned handle=~0u,status=0;
        // For unqualified filenames, OpenFile searches the application
        // directory, current directory, system directory, Windows directory
        // and PATH. OF_PARSE, OF_DELETE and OF_CREATE bypass searching. This
        // lets SysTray find SNDVOL32.EXE outside its current directory.
        if(!error&&!(a[2]&0x1300)){
            char requested[260]={};
            const char*given=(a[2]&0x8000)?reinterpret_cast<char*>(of+8):ReadString(a[0]);
            if(given&&strlen(given)<sizeof requested)strcpy(requested,given);
            if(requested[0]&&!strpbrk(requested,"\\/:")){
                const unsigned length=(unsigned)strlen(requested);
                const unsigned block=Allocate(length+1+260);
                if(block){
                    Store(block,requested,length+1);
                    const unsigned old_error=last_error;
                    const unsigned search[]={0,block,0,260,block+length+1,0};
                    if(SearchPathApi(search)){
                        const char*found=ReadString(block+length+1);
                        if(found&&*found&&strlen(found)<sizeof path)strcpy(path,found);
                    }
                    else last_error=old_error;
                    Free(block);
                }
            }
        }
        if(!error&&strlen(path)>127)error=206;
        if(!error){
            if(a[2]&0x100)handle=0;
            else if(a[2]&0x200){error=files.store.Remove(path,false);if(!error)handle=1;}
            else{static const unsigned shares[]={3,0,1,2,3};unsigned mode=(a[2]>>4)&7,access=a[2]&3;
                if(mode>4||access==3)error=87;
                else error=files.Open(path,(a[2]&0x1000)?3:access==0?1:access==1?2:3,shares[mode],(a[2]&0x1000)?2:3,32,handle,status);
                if(!error&&(a[2]&0x4000)){files.Close(handle);handle=1;}
                else if(!error){const unsigned legacy=files.LegacyHandle(handle);if(legacy==Files::Invalid){files.Close(handle);error=4;}handle=legacy;}
            }
        }
        if(!error){memcpy(of+8,path,strlen(path)+1);of[0]=136;of[1]=1;last_error=previous_error;}
        of[2]=error;of[3]=error>>8;Store(a[1],of,sizeof of);if(error){last_error=error;return ~0u;}return handle;
    }
    default:
        break;
    }
    if(!files.Owns(a[0]))return bad(6);
    if(API_IS(name,"CancelIo")){
        // The RAM provider completes transfers before returning. Queued
        // completion callbacks are already completed IO, not cancelable work.
        return 1;
    }
    if(API_IS(name,"GetOverlappedResult")){
        if(!virtual_memory.Access(a[1],20,false)||!virtual_memory.Access(a[2],4,true))return bad(87);
        unsigned status=Read32(a[1]);if(status==997){
            if(!a[3])return bad(996);unsigned event=Read32(a[1]+16);if(!event)return bad(996);
            unsigned wait[]={event,~0u};if(ThreadApi("WaitForSingleObject",wait)==~0u)return 0;status=Read32(a[1]);
        }
        Write32(a[2],Read32(a[1]+4));return status?bad(status):1;
    }
    const bool read=API_IS(name,"ReadFileEx");
    if(!read&&!API_IS(name,"WriteFileEx")){Fail(name);return 0;}
    if(!a[4]||!virtual_memory.Access(a[3],20,true)||!virtual_memory.Access(a[1],a[2],read))return bad(87);
    if(threads[current_thread].apc_count==64)return bad(8);
    unsigned old=0,position=0,done=0;
    unsigned error=files.Seek(a[0],0,1,old);
    if(!error)error=files.Seek(a[0],(long long)((unsigned long long)Read32(a[3]+8)|((unsigned long long)Read32(a[3]+12)<<32)),0,position);
    if(error)return bad(error);
    auto *buffer=a[2]?new(std::nothrow) unsigned char[a[2]]:nullptr;if(a[2]&&!buffer){files.Seek(a[0],old,0,position);return bad(8);}
    if(!read&&a[2])Read(a[1],buffer,a[2]);
    error=read?files.Read(a[0],buffer,a[2],done):files.Write(a[0],buffer,a[2],done);
    if(read&&done)Store(a[1],buffer,done);delete[] buffer;files.Seek(a[0],old,0,position);
    if(error)return bad(error);
    if(read&&a[2]&&!done)error=38;
    Write32(a[3],error);Write32(a[3]+4,done);
    const unsigned callback[]={error,done,a[3]};return EnqueueApc(current_thread,a[4],callback,3,a[0]);
}
