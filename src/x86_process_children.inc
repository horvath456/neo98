// SPDX-License-Identifier: GPL-2.0-or-later
// Project-owned child execution provider. Original entry/retry code is separate.
#include "win98_process_entry.h"
#include "win98_process_command.h"
#include "win98_open_process.h"
#include <new>
namespace X86 {
namespace {
struct ChildProcess {
    Application *app=nullptr;
    unsigned references=0,pid=0,tid=0,result=259,thread_result=259;
    bool used=false,done=false,thread_done=false;
    unsigned debug_owner=0,debug_phase=0,debug_module=1;
    unsigned debug_process_handle=0,debug_thread_handle=0;
    unsigned debug_image_name=0;
    bool debug_pending=false;
} children[Application::ChildCapacity];
bool pumping_children=false;
char child_errors[16][192]={};
unsigned child_error_count=0;
bool Contains(const char *text,const char *part){
    if(!part||!*part)return true;
    for(unsigned i=0;text[i];++i){
        unsigned j=0;
        while(part[j]&&text[i+j]==part[j])++j;
        if(!part[j])return true;
    }
    return false;
}
}
unsigned Application::FailedChildren(const char *contains,const char **first){
    unsigned n=0;
    for(unsigned i=0;i<child_error_count&&i<16;++i)
        if(Contains(child_errors[i],contains)){
            if(!n&&first)*first=child_errors[i];
            ++n;
        }
    return n;
}
const char *Application::FailedChild(unsigned index){
    return index<child_error_count&&index<16?child_errors[index]:nullptr;
}
// The child table is one flat, process-wide list, so no recursion is needed.
bool Application::SystemRunnable(){if(AnyRunnable())return true;for(auto &c:children)if(c.used&&!c.done&&c.app&&c.app!=this&&c.app->AnyRunnable())return true;return false;}
// The children that still do the work of this session. A process that called
// RegisterServiceProcess(0, 1) is none of them: Win98 marks it with bit 8 of
// its process flags, keeps it out of the task list and lets it outlive the
// program that started it. DDHELP.EXE, which DDRAW starts for every DirectDraw
// client, sets that bit in its own startup and stays resident afterwards, so a runner that waits for the last process would wait forever
// while Win98 has long returned to the shell.
unsigned Application::LiveChildren(){unsigned n=0;for(auto &c:children)if(c.used&&!c.done&&!(c.app&&(c.app->process_flags&0x100u)))++n;return n;}
bool Application::SessionWaitingForInput(){
    bool any=false;
    for(auto &c:children)
        if(c.used&&!c.done&&c.app){
            if(!c.app->WaitingForInput())return false;
            any=true;
        }
    return any;
}
unsigned Application::ChildRecords(){unsigned n=0;for(auto &c:children)if(c.used)++n;return n;}
// Every child the session ever started. A record disappears when the last
// handle of a finished child closes, so the live count alone cannot say
// whether the autostart ran at all.
unsigned Application::children_started=0;
unsigned Application::ChildrenStarted(){return children_started;}
// The body KERNEL32!TerminateProcess runs on its target: exit code and state,
// the pipes, every thread and the mutexes it abandons.
void Application::TerminateSelf(unsigned code){
    if(user_initialized&&state!=Exited&&state!=Failed)HandForegroundOn();
    exit_code=code;state=Exited;FreePipes();
    for(auto &t:threads)if(t.fiber){t.done=true;t.result=code;t.apc_count=0;User::ReleaseWindowUpdateLock(process_id,t.id);User::DropScreenThread(process_id,t.id);}
    // Cancel outstanding sends when their queues exit.
    CancelSendLifetimes();
    if(user_initialized)for(auto &t:threads)if(t.fiber)User::ForgetThreadQueue(t.id);
    // The mutex table is session-wide; abandon only what this process holds.
    for(auto &m:kernel_mutexes)if(m.owner)for(const auto &t:threads)if(t.id==m.owner){m.owner=0;m.recursion=0;m.abandoned=true;break;}
}
// Win98 ends a session by ending the processes that are still running; the
// autostart programs of the Run keys are among them, and one of this
// installation's runs for the whole session. A host runner calls this instead
// of waiting for children that are not meant to end. Each one goes through the
// same path as TerminateProcess; PumpChildren then reaps the records.
unsigned Application::TerminateChildren(unsigned code){
    unsigned n=0;
    for(auto &c:children)if(c.used&&!c.done&&c.app&&c.app->state!=Exited&&c.app->state!=Failed){c.app->TerminateSelf(code);++n;}
    PumpChildren(1);
    return n;
}
void Application::ReferenceChild(unsigned i){++children[i].references;}
void Application::ReleaseChild(unsigned i){auto &c=children[i];if(c.references)--c.references;if(c.done&&!c.references)c={};}
bool Application::ChildSignaled(unsigned i,bool thread){return thread?children[i].thread_done:children[i].done;}
// A process handle of a foreign process. Win98 keeps the process object alive
// while a handle names it and signals it when the process ends, so the holder
// of the handle learns of the death and nothing else. NEO98 finds the process
// through USER's process table: a record that is gone belongs to a process
// that ended, and a record whose Application has left Running says the same.
bool Application::ForeignProcessSignaled(unsigned identity){
    // A process that has no record in USER yet, or never gets one, is in the
    // session's own list.
    for(auto*process:session_processes)
        if(process&&process->ProcessId()==identity)return process->state==Exited||process->state==Failed;
    auto*app=static_cast<Application*>(User::ProcessContextOf(identity));
    return !app||app->state==Exited||app->state==Failed;
}
unsigned Application::ChildExitCode(unsigned i,bool thread){return thread?children[i].thread_result:children[i].result;}
unsigned Application::ChildProcessIdentity(unsigned i){return i<ChildCapacity?children[i].pid:0;}
// KERNEL32 OpenProcess looks the process object up by its identity
// and rejects anything that is not a process. Only live children and this
// process itself exist here.
unsigned Application::FindProcessIdentity(unsigned identity,bool &live){
    live=false;
    if(!identity)return 0;
    if(identity==(ProcessId()&0xffffffffu)){live=true;return 0x10000;}
    for(unsigned i=0;i<ChildCapacity;++i)if(children[i].used&&children[i].pid==identity){live=!children[i].done;return i+1;}
    // The parent and sibling processes are known to USER's process table; a
    // second Explorer instance opens the first one this way (SHAllocShared).
    if(User::ProcessContextOf(identity)){live=true;return 0x20000;}
    return 0;}
// The Application behind a process handle: the pseudo-handle, a child slot,
// or a foreign process (kind 9) looked up by its identifier.
Application *Application::ProcessFromHandle(unsigned handle){
    if(handle==0x7fffffffu)return this;
    auto*h=FindKernelHandle(handle);if(!h)return nullptr;
    if(h->kind==6){bool done=false;return ChildApplication(h->object,done);}
    if(h->kind==9)return static_cast<Application*>(User::ProcessContextOf(h->object));
    return nullptr;
}
// A child outlives its parent: PumpChildren keeps running it after the parent
// Application is gone, as Win98 keeps a process whose parent ended. Nothing may
// still point at the freed parent, so the root walk in Suspend, in the
// composition owner and in Toolhelp stops at the orphan itself.
void Application::OrphanChildren(){for(auto &c:children){
    if(c.app&&c.app->parent_process==this)c.app->parent_process=nullptr;
    if(c.used&&c.debug_owner==process_id){
        c.debug_owner=0;
        if(c.app){c.app->debugger_process=0;c.app->debug_stopped=false;c.app->TerminateSelf(0xc0000001u);}
        ReleaseChild(unsigned(&c-children));
    }
}}
void Application::FreeChildHandles(){for(auto &h:kernel_handles)if(h.kind==6||h.kind==7){unsigned i=h.object;h.kind=0;ReleaseChild(i);}}
void Application::PumpChildren(unsigned slices){
    if(pumping_children)return;
    // This entry is a host scheduler boundary. Never recursively run a child
    // on a guest API/callback fiber; the normal Pump caller is outside it.
    pumping_children=true;
    const unsigned saved_pid=User::CurrentProcess(),saved_tid=User::CurrentThread();
    for(auto &c:children){
        auto *app=c.app;if(!app)continue;
        if(app->debug_stopped&&app->state!=Exited&&app->state!=Failed)continue;
        app->Pump(slices);
        if(app->threads[0].done){c.thread_done=true;c.thread_result=app->threads[0].result;}
        if(app->state!=Exited&&app->state!=Failed)continue;
        c.done=true;c.result=app->state==Exited?app->exit_code:0xc0000142u;
        if(app->state==Failed&&child_error_count<16){
            // The reason alone does not say which autostart program died, and
            // that is the whole point of this list, so the name goes first.
            char *out=child_errors[child_error_count++];unsigned i=0;
            const char *name=app->modules[0].path;
            for(const char *q=name;*q;++q)if(*q=='\\'||*q=='/')name=q+1;
            for(;name&&name[i]&&i+2<192;++i)out[i]=name[i];
            if(i+2<192){out[i++]=':';out[i++]=' ';}
            const char *why=app->Error();
            for(unsigned j=0;why&&why[j]&&i+1<192;++j)out[i++]=why[j];
            out[i]=0;
        }
        if(!c.thread_done){c.thread_done=true;c.thread_result=c.result;}
        c.app=nullptr;
        // Keep the record while the destructor releases this child's own
        // process handles. No handle is an owner of a live Application.
        ++c.references;
        app->~Application();free(app);
        if(!--c.references)c={};
    }
    User::BindTask(saved_pid,saved_tid);pumping_children=false;
}
// KERNEL32 ordinal 603. The identity lookup, the handle table and the
// allocator are native; the decisions come from win98_open_process.h.
unsigned Application::OpenProcessApi(const unsigned *a){
    bool live=false;
    const unsigned found=FindProcessIdentity(a[2],live);
    // Every record NEO98 keeps is a process, so the original type byte is 6
    // whenever the lookup succeeds and the NotAProcess path is unreachable.
    const auto decision=Win98OpenProcess::Classify(found!=0,Win98OpenProcess::ProcessTag);
    if(decision==Win98OpenProcess::NotFound)return 0;   // LastError untouched
    if(decision==Win98OpenProcess::NotAProcess){last_error=Win98OpenProcess::InvalidParameter;return 0;}
    const unsigned flags=Win98OpenProcess::Flags(a[0],a[1]);
    // NEO98 has no record for its own process, so opening it yields the same
    // pseudo-handle GetCurrentProcess returns. Child processes get a real
    // handle with the original access mask and inheritance bit.
    if(found==0x10000)return 0x7fffffffu;
    const unsigned handle=Win98OpenProcess::Handle(found==0x20000?NewKernelHandle(9,a[2]):NewKernelHandle(6,found-1));
    if(auto *entry=FindKernelHandle(handle)){
        entry->access=Win98OpenProcess::Access(flags);
        entry->inherit=Win98OpenProcess::Inherit(flags);
    }
    return handle;
}
struct Application::ProcessLaunchEnvironment {
    Application &app;
    unsigned reads=0;
    bool Read8(unsigned p,unsigned &v){
        unsigned char b=0;
        if(++reads>131072||!app.virtual_memory.Access(p,1,false)||app.cpu->Read(p,&b,1)!=CpuError::Ok)return false;
        v=b;return true;
    }
    bool Write8(unsigned p,unsigned v){unsigned char b=v;return app.virtual_memory.Access(p,1,true)&&app.cpu->Write(p,&b,1)==CpuError::Ok;}
    bool Span(unsigned p,void*out,unsigned n){return p<=~0u-n&&app.virtual_memory.Access(p,n,false)&&app.cpu->Read(p,out,n)==CpuError::Ok;}
    bool Writable(unsigned p,unsigned n){return p&&p<=~0u-n&&app.virtual_memory.Access(p,n,true);}
    bool Text(unsigned p,char*out,unsigned cap){
        for(unsigned i=0;i<cap;++i){unsigned v;if(!Read8(p+i,v)){app.last_error=998;return false;}out[i]=v;if(!v)return true;}
        app.last_error=206;return false;
    }
    unsigned ConvertInput(unsigned,unsigned){app.last_error=50;return 0;}
    void FreeInput(unsigned,unsigned){}
    unsigned Create(const unsigned *a){return app.LaunchChild(a,nullptr,nullptr);}
    unsigned Retry(unsigned application,unsigned command,const unsigned *a,unsigned result){
        if(application||!command)return result;
        char *copy=static_cast<char*>(malloc(32768));if(!copy){app.last_error=8;return 0;}
        if(!Text(command,copy,32768)){free(copy);return 0;}
        struct RetryProvider {
            Application &app;const unsigned *args;
            unsigned LastError(){return app.last_error;}
            void Error(unsigned v){app.last_error=v;}
            char *Allocate(unsigned n){return static_cast<char*>(malloc(n));}
            void Free(char*p){free(p);}
            unsigned Create(const char*name,const char*line){return app.LaunchChild(args,name,line);}
        } retry{app,a};
        result=Win98ProcessCommand::Retry(retry,nullptr,copy,result);free(copy);return result;
    }
};
unsigned Application::ProcessApi(ApiName name,const unsigned *a){
    ProcessLaunchEnvironment e{*this};
    if(!API_IS(name,"CreateProcessA")){
        const bool thread=API_IS(name,"GetExitCodeThread");auto*h=FindKernelHandle(a[0]);
        if(!thread&&a[0]==0x7fffffffu){if(!e.Writable(a[1],4)){last_error=998;return 0;}Write32(a[1],state==Exited?exit_code:259);return 1;}
        // A handle to a foreign process answers the same way: STILL_ACTIVE
        // while it runs, its exit code afterwards. The process object outlives
        // the process, so a record that is gone answers zero.
        if(!thread&&h&&h->kind==9){
            if(!(h->access&0x400)){last_error=5;return 0;}
            if(!e.Writable(a[1],4)){last_error=998;return 0;}
            auto*other=static_cast<Application*>(User::ProcessContextOf(h->object));
            Write32(a[1],!other?0u:other->state==Exited?other->exit_code:other->state==Failed?0xc0000142u:259u);
            return 1;
        }
        if(!h||h->kind!=(thread?7u:6u)){last_error=6;return 0;}
        if(!(h->access&0x400)){last_error=5;return 0;}
        if(!e.Writable(a[1],4)){last_error=998;return 0;}
        Write32(a[1],ChildExitCode(h->object,thread));return 1;
    }
    if(!Win98ProcessEntry::Probe(e,a)){last_error=998;return 0;}
    // Full native span checks supplement the original endpoint probes. SEH
    // dispatch and its error selection have not been implemented.
    if(!e.Writable(a[9],16)){last_error=998;return 0;}
    if((!a[0]&&!a[1])||!a[8]){last_error=87;return 0;}
    // Unsupported facilities are rejected before child allocation/publication.
    // Security descriptors, kernel-object handle inheritance,
    // console attachment, debugging and Unicode environment need services.
    // Creation flags whose documented effect is already what NEO98 does, so
    // accepting them changes nothing observable:
    // CREATE_NEW_CONSOLE and CREATE_NEW_PROCESS_GROUP address console
    // attachment and Ctrl+C groups. Console children currently receive their
    // own native buffer; shared parent-console attachment and process groups
    // remain unmodeled. GUI children such as RUNDLL32 receive no console.
    // CREATE_DEFAULT_ERROR_MODE asks for the default error mode instead of
    // the parent's. A child Application always starts with its own default
    // process_error_mode, so the flag matches; it is the cleared case, the
    // inheritance, that remains unimplemented.
    // The priority classes 0x1E0, that is NORMAL 0x20, IDLE 0x40, HIGH 0x80
    // and REALTIME 0x100. Windows masks the creation flags with 0x1E0 and,
    // if anything is left, hands it to SetPriorityClass, which
    // turns exactly one of the four into the base 8, 4, 13 or 24 at
    // process object+A0h. Its return value is not examined, so
    // two classes at once leave ERROR_INVALID_PRIORITY (1800) standing and
    // CreateProcessA still succeeds with the inherited class. ApplyPriority
    // below does the same. GetPriorityClass is the inverse map.
    // CREATE_SUSPENDED (4) leaves the primary thread with the suspend count
    // one; it runs once the creator calls ResumeThread on the thread handle
    // (ThreadControlApi). MSI starts every EXE custom action like that and
    // failed with error 2743 while the flag was refused.
    // CREATE_SEPARATE_WOW_VDM (0x800) has no effect in Win98. KERNEL32's
    // path reads console, debug, suspend and priority flags, then
    // forwards the flags to a worker that checks only debug bits.
    // Neither uses 0x800. NFS III passes it when starting its PE setup helpers.
    // Unimplemented flag bits, including CREATE_UNICODE_ENVIRONMENT (0x400),
    // still return ERROR_NOT_SUPPORTED.
    constexpr unsigned accepted_creation_flags=0x00000003u|0x00000004u|0x00000008u|0x00000010u|0x000001e0u|0x00000200u|0x00000800u|0x04000000u;
    if(a[2]||a[3]||(a[5]&~accepted_creation_flags)){if(log){char text[160];snprintf(text,sizeof text,"CreateProcess rejected: security=%08x,%08x inherit=%u flags=%08x",a[2],a[3],a[4],a[5]);log(text);}last_error=50;return 0;}
    // Windows uses bInheritHandles to decide whether to copy the parent's
    // handle table into the child and keeps the entries whose access word
    // carries 80000000h, at the same index and therefore with the same handle
    // value. Named kernel objects and file mappings live in the session-wide
    // namespace now, so their entries can be copied. The other kinds still
    // point into per-process tables and keep ERROR_NOT_SUPPORTED.
    if(a[4]) {
        for(const auto &h:kernel_handles)
            if(h.kind&&h.inherit&&h.kind!=1&&h.kind!=3&&h.kind!=4&&h.kind!=5){last_error=50;return 0;}
    }
    KernelPathLock(true,0);
    struct ModeLock{Application &app;~ModeLock(){app.KernelPathLock(false,0);}} lock{*this};
    if(state==Failed||state==Exited)return 0;
    auto *mode=ProcessFileState();if(!mode)return 0;
    if(mode->process_flags&0x40){if(log)log("CreateProcess rejected: process flags 0x40");last_error=50;return 0;}
    mode->thread_flags=threads[current_thread].file_api_flags;
    const unsigned result=Win98ProcessEntry::Launch(e,*mode,a);
    threads[current_thread].file_api_flags=mode->thread_flags;
    // Win98's CreateProcess waits until the child has loaded its modules
    // (that is how a missing DLL comes back as the parent's error), and the
    // parent, woken from that wait with the boost VMM gives a satisfied wait,
    // goes on before the new thread. The launch here runs on the parent's
    // turn and can use all of it under load, so the next few calls ended the
    // turn and the child ran first: its main thread had already left when
    // the parent asked for STILL_ACTIVE. The parent starts a new turn.
    if(result){turn_start=Now();slice_stop=false;}
    return result;
}
unsigned Application::LaunchChild(const unsigned *a,const char *override_application,const char *override_command){
    // Original outer adapter's internal thirteen-argument order.
    ProcessLaunchEnvironment e{*this};
    struct Scratch {
        char application[32768]{},command[32768]{},directory[260]{},title[32768]{},desktop[32768]{};
        char environment[32768]{};unsigned environment_size=0,startup[17]{};
        unsigned char reserved[65536]{};unsigned reserved_size=0;
    };
    auto*s=static_cast<Scratch*>(calloc(1,sizeof(Scratch)));if(!s){last_error=8;return 0;}
    struct FreeScratch{Scratch*p;~FreeScratch(){free(p);}} scratch{s};
    if(override_application){if(strlen(override_application)>=sizeof s->application){last_error=206;return 0;}strcpy(s->application,override_application);}
    else if(a[1]&&!e.Text(a[1],s->application,sizeof s->application))return 0;
    if(override_command){if(strlen(override_command)>=sizeof s->command){last_error=206;return 0;}strcpy(s->command,override_command);}
    else if(a[2]&&!e.Text(a[2],s->command,sizeof s->command))return 0;
    if(log){char text[400];snprintf(text,sizeof text,"CreateProcess application=\"%.150s\" command=\"%.150s\"",s->application,s->command);log(text);}
    if(!e.Span(a[10],s->startup,68)){last_error=998;return 0;}
    if(s->startup[0]!=68){last_error=87;return 0;}
    // STARTF_USEHOTKEY (0x200) carries the hot key in hStdInput,
    // STARTF_HASSHELLDATA (0x400) the shell's monitor data in hStdOutput and
    // STARTF_TITLEISLINKNAME (0x800) marks the title; the original CreateProcess
    // copies them into the new PDB for the shell. The hot key itself is not
    // registered yet.
    // in the creation service loads STARTUPINFOA.dwFlags and tests
    // ah against 1, that is STARTF_USESTDHANDLES (0x100). Is it clear, the
    // branch duplicates the parent's three handles out of the
    // parent PDB at +14h, +18h and +1Ch, and hStdInput, hStdOutput and
    // hStdError are never read. then tests ah against 6, so
    // STARTF_USEHOTKEY and STARTF_HASSHELLDATA take the same branch even with
    // 0x100 set. Only (dwFlags & 0x700) == 0x100 reads the three fields, and
    // only that case has to carry handles this process owns. A caller that
    // leaves the rest of its STARTUPINFOA uninitialized, as the original
    // DDRAW.DLL and DSOUND.DLL do in their DLL_PROCESS_ATTACH, passes.
    const unsigned startup_flags=s->startup[11];
    const bool use_std_handles=(startup_flags&0x700u)==0x100u;
    // STARTF_FORCEONFEEDBACK (0x40) and STARTF_FORCEOFFFEEDBACK (0x80) select
    // the launch cursor; KERNEL32 reads them out of the new process's
    // own STARTUPINFO and passes the answer to USER's signal proc.
    // lpDesktop names a window station and desktop, which Windows 98 does
    // not have; the creation service never reads it. MSI passes
    // "WinSta0\Default" for the EXE of a custom action. The child's
    // copy gets no pointer into the creator's memory.
    if(s->startup[1]||(startup_flags&~0xfc1u)){if(log){char text[200];snprintf(text,sizeof text,"CreateProcess rejected: STARTUPINFO reserved=%08x desktop=%08x flags=%08x show=%08x cbReserved2=%08x std=%08x,%08x,%08x",s->startup[1],s->startup[2],s->startup[11],s->startup[12],s->startup[13],s->startup[14],s->startup[15],s->startup[16]);log(text);}last_error=50;return 0;}
    s->reserved_size=s->startup[12]>>16;
    if(s->reserved_size&&!e.Span(s->startup[13],s->reserved,s->reserved_size)){last_error=998;return 0;}
    if(use_std_handles){
        if(!a[6]){last_error=50;return 0;}
        for(unsigned i=14;i<17;++i)if(s->startup[i]&&s->startup[i]!=~0u&&!OwnsPipe(s->startup[i])&&!files.Owns(s->startup[i])&&!OwnsConsole(s->startup[i])){last_error=50;return 0;}
    }
    if(s->startup[2]&&!e.Text(s->startup[2],s->desktop,sizeof s->desktop))return 0;
    if(s->startup[3]&&!e.Text(s->startup[3],s->title,sizeof s->title))return 0;
    if(a[9]){if(!e.Text(a[9],s->directory,sizeof s->directory))return 0;}else strcpy(s->directory,files.current);
    Files::Session child_files(files.store);unsigned error=child_files.SetDirectory(s->directory);if(error){last_error=error==2||error==3?267:error;return 0;}
    const unsigned env=Win98ProcessCommand::Environment(a[8],process_environment);
    for(unsigned i=0;i<sizeof s->environment;++i){
        unsigned v;if(!e.Read8(env+i,v)){last_error=998;return 0;}s->environment[i]=v;
        if(i&&!v&&!s->environment[i-1]){s->environment_size=i+1;break;}
    }
    if(!s->environment_size){last_error=206;return 0;}
    // Native Files resolution, not a claim to have ported the internal MZ/NE/
    // DOS/COM loader. Retry supplies the original successive quoted prefixes.
    char token[260]{};
    if(s->application[0]){if(strlen(s->application)>=sizeof token){last_error=206;return 0;}strcpy(token,s->application);}
    else {
        const char*p=s->command;bool quote=*p=='"';if(quote)++p;unsigned n=0;
        while(*p&&(quote?*p!='"':(*p!=' '&&*p!='\t'))){if(n+1==sizeof token){last_error=206;return 0;}token[n++]=*p++;}
        if(quote&&*p!='"'){last_error=2;return 0;}
    }
    if(!token[0]){last_error=2;return 0;}
    const char*part=strrchr(token,'\\');part=part?part+1:token;
    if(!strchr(part,'.')){if(strlen(token)+4>=sizeof token){last_error=206;return 0;}strcat(token,".exe");}
    char path[260]{};unsigned node=0;bool found=false;
    auto candidate=[&](const char*prefix){char text[520];if(prefix&&*prefix)snprintf(text,sizeof text,"%s\\%s",prefix,token);else snprintf(text,sizeof text,"%s",token);if(files.FullPath(text,path))return false;unsigned err=files.store.Lookup(path,node);return !err&&!(files.store.Get(node)->attributes&0x10);};
    if(strchr(token,'\\')||strchr(token,'/')||strchr(token,':'))found=candidate(nullptr);
    else {
        char executable_dir[260];strcpy(executable_dir,modules[0].path);char*slash=strrchr(executable_dir,'\\');if(slash)*slash=0;
        found=candidate(executable_dir)||candidate(nullptr)||candidate("C:\\WINDOWS\\SYSTEM")||candidate("C:\\WINDOWS");
        if(!found){const unsigned address=FindProcessEnvironment("PATH");char entries[32768];if(address&&e.Text(address,entries,sizeof entries)){
            char*p=entries;while(*p&&!found){char*end=strchr(p,';');if(end)*end=0;found=candidate(p);if(!end)break;p=end+1;}
        }}
    }
    if(!found){last_error=2;return 0;}
    if(strlen(path)>=sizeof program_name){last_error=206;return 0;}
    const auto*file=files.store.Get(node);
    // An empty batch file is still a batch file: Win98 opens a DOS window
    // that runs no line and ends. Any other empty file is no image.
    const size_t path_length=strlen(path);
    const bool empty_batch=!file->size&&path_length>4&&!strcasecmp(path+path_length-4,".BAT");
    if(!file->size&&!empty_batch){last_error=193;return 0;}
    // The program is read straight into the buffer the child owns, like a
    // library (Application::Load), not through the node table's cache: that
    // cache is for small files and would refuse a self-extractor of 55 MB.
    const unsigned exe_size=file->size;
    unsigned char*image=static_cast<unsigned char*>(malloc(exe_size?exe_size:1));if(!image){last_error=8;return 0;}
    if(exe_size){
        unsigned handle=0,status=0,done=0;
        // CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0).
        unsigned error=files.Open(path,1,1,3,0,handle,status);
        if(!error){error=files.Read(handle,image,exe_size,done);files.Close(handle);}
        if(error||done!=exe_size){free(image);last_error=error==3?2u:error?error:1117;return 0;}
    }
    struct OwnedImage{unsigned char*&bytes;~OwnedImage(){free(bytes);}} owned_image{image};
    // Win98 CreateProcess accepts an NE DLL as a library load. It runs the
    // library initializer and leaves PROCESS_INFORMATION untouched;
    // OLE2 and DISPDIB were measured in a private original Win98 VM.
    // A normal NE program still takes the child/task path below.
    if (Win98Ne::IsImage(image, exe_size))
    {
        Win98Ne::Image ne;
        unsigned error = 0;
        if (ne.Parse(image, exe_size, error) && ne.IsLibrary())
        {
            const unsigned handle = Win16Process() ? Win16LoadNamedLibrary(path) : Win16HostedLibrary(path, error);
            if (handle >= 32) return 1;
            last_error = error ? error : handle ? handle : 2u;
            return 0;
        }
    }
    unsigned slot=ChildCapacity;for(unsigned i=0;i<ChildCapacity;++i)if(!children[i].used){slot=i;break;}
    if(slot==ChildCapacity){
        // Which records fill the table.
        if(log){char text[2048];int at=snprintf(text,sizeof text,"CreateProcess failed: all child process records are taken:");
            for(unsigned i=0;i<ChildCapacity&&at>0&&at<(int)sizeof text;++i)at+=snprintf(text+at,sizeof text-at," %u:%s/r%u/%.12s",i,children[i].done?"done":"live",children[i].references,children[i].app?children[i].app->program_name:"-");
            log(text);}
        last_error=8;return 0;}
    unsigned free_handles=0;for(auto &h:kernel_handles)if(!h.kind)++free_handles;if(free_handles<2){last_error=8;return 0;}
    auto *storage=malloc(sizeof(Application));if(!storage){last_error=8;return 0;}
    auto *child=new(storage)Application(log);
    child->parent_process=this;
    child->trace_calls=trace_calls;child->trace_from=trace_from?1:0; // a traced parent traces its children from their start
    // A route override stays in the child: a test that runs one of our
    // implementations keeps it for the programs the run starts.
    for(unsigned i=0;i<NativeModuleCount();++i)child->AddNativeModule(NativeModuleAt(i));
    const unsigned saved_pid=User::CurrentProcess(),saved_tid=User::CurrentThread();
    struct RestoreBinding{unsigned p,t;~RestoreBinding(){User::BindTask(p,t);}} restore{saved_pid,saved_tid};
    auto discard=[&](unsigned code){child->~Application();free(child);last_error=code;return 0u;};
    child->owned_executable=image;image=nullptr;
    child->owned_font=static_cast<unsigned char*>(malloc(font_size));
    child->initial_environment=static_cast<char*>(malloc(s->environment_size));
    if(!child->owned_executable||!child->owned_font||!child->initial_environment)return discard(8);
    memcpy(child->owned_font,font_bytes,font_size);memcpy(child->initial_environment,s->environment,s->environment_size);child->initial_environment_size=s->environment_size;
    memcpy(child->startup_info,s->startup,68);child->startup_info[2]=0;child->startup_info[3]=0;child->initial_title=s->startup[3]?s->title:nullptr;
    strcpy(child->files.current,child_files.current);
    // Keyboard configuration is process-owned and copied
    // without borrowing any pointer to the creator's Unicorn or native state.
    child->keyboard_layout_state=keyboard_layout_state;
    // CREATE_SUSPENDED; a[7] is dwCreationFlags. Start gives the primary
    // thread its pending suspension (InitializeThreadControl).
    child->threads[0].suspended=(a[7]&4u)?1:0;
    if(!child->Start(child->owned_executable,exe_size,child->owned_font,font_size,path,s->command[0]?s->command:path)){
        // Windows 98 tells the user which library is missing instead of
        // calling the program a bad image. KERNEL32 carries the sentence as
        // string four, "A required.DLL file, %s, was not found.", the
        // caption as string seven, "Error Starting Program", and it imports
        // MessageBoxA for exactly this. The error behind it is the child's
        // exit code : ERROR_DLL_NOT_FOUND for a PE program, which SHELL32 takes as already shown;
        // 193 would make the shell say the file is no valid Win32 application

        if(child->missing_module[0]){
            memcpy(missing_module,child->missing_module,sizeof missing_module);
            missing_module[sizeof missing_module-1]=0;
            memcpy(missing_text,child->missing_text,sizeof missing_text);
            missing_text[sizeof missing_text-1]=0;
            missing_module_error=child->missing_module_error?child->missing_module_error:126;
            return discard(missing_module_error);
        }
        // A DOS program whose PIF names a program that is not there is a
        // missing file, not a bad image.
        if(child->DosVmProcess()&&child->last_error==2)return discard(2);
        // A 16-bit program whose library is missing fails with the error of
        // KRNL386 LoadModule, and nobody has shown anything yet: the shell
        // asks PK16FNF for the name and puts up its own box.
        if(child->load_failure&&child->last_error)return discard(child->last_error);
        return discard(193);
    }
    if(!child->InheritConsole(*this,a[6]!=0,a[7]))return discard(8);
    // A host runner that watches code (probe_application --trace-eip) puts
    // the same watch into every process of the session.
    if(child_started)child_started(*child);
    if(a[6]){
        if(!child->InheritPipes(*this))return discard(8);
        unsigned error=child->files.Inherit(files);if(error)return discard(error);
        // the inheritable entries, at their original index.
        for(unsigned i=0;i<KernelHandleCapacity;++i){
            const auto &h=kernel_handles[i];
            if(!h.kind||!h.inherit)continue;
            if(h.kind==5){
                const unsigned made=child->DuplicateMappingFrom(*this,h.object);
                if(!made)return discard(8);
                const unsigned index=KernelHandleNumber(made)-1;
                if(index!=i){child->kernel_handles[i]=child->kernel_handles[index];child->kernel_handles[index]={};}
                child->kernel_handles[i].generation=h.generation;
                child->kernel_handles[i].access=h.access;
                child->kernel_handles[i].inherit=true;
                continue;
            }
            child->kernel_handles[i]=h;
            if(h.kind==1)++events[h.object].references;
            else if(h.kind==3)++kernel_mutexes[h.object].references;
            else if(h.kind==4)++semaphores[h.object].references;
        }
    }
    if(use_std_handles){
        for(unsigned i=0;i<3;++i){
            const unsigned handle=s->startup[14+i];
            if(handle&&handle!=~0u&&!child->OwnsPipe(handle)&&!child->files.Owns(handle)&&!child->OwnsConsole(handle))return discard(6);
            child->standard_handles[i]=handle;
        }
    }
    if(s->startup[2]){
        const unsigned bytes=strlen(s->desktop)+1,data=child->Allocate(bytes);
        if(!data||!child->Store(data,s->desktop,bytes))return discard(8);
        child->startup_info[2]=data;
    }
    child->startup_info[13]=0;
    if(s->reserved_size){
        unsigned data=child->Allocate(s->reserved_size);if(!data)return discard(8);
        if(!child->Store(data,s->reserved,s->reserved_size))return discard(998);
        child->startup_info[13]=data;
    }
    child->initial_title=nullptr;
    // the creation flags carry the child's priority class.
    // a is the internal thirteen-argument order, so a[7] is dwCreationFlags.
    if(const unsigned klass=a[7]&0x1e0u){
        unsigned base=0;
        switch(klass){case 0x20:base=8;break;case 0x40:base=4;break;case 0x80:base=13;break;case 0x100:base=24;break;}
        if(base)child->process_priority_base=base;
        else last_error=1800; // ignores the failure.
    }
    auto &record=children[slot];record={};record.used=true;record.app=child;record.pid=child->ProcessId();record.tid=child->threads[0].id;
    if((a[7]&3)||debug_children){
        child->debugger_process=(a[7]&3)?process_id:debugger_process;
        child->debug_children=!(a[7]&2);child->debug_stopped=true;
        record.debug_owner=child->debugger_process;record.debug_phase=1;
        ++record.references; // the debugger retains the exit event after handles close
    }
    ++children_started;
    const unsigned process=NewKernelHandle(6,slot),thread=NewKernelHandle(7,slot);
    // Slots and output were reserved/probed before Start; no provider yields.
    if(!process||!thread){if(process){unsigned close[]={process};ThreadApi("CloseHandle",close);}record.app=nullptr;record.done=true;if(!record.references)record={};return discard(8);}
    const unsigned result[]={process,thread,record.pid,record.tid};
    if(cpu->Write(a[11],result,sizeof result)!=CpuError::Ok){unsigned close[]={process};ThreadApi("CloseHandle",close);close[0]=thread;ThreadApi("CloseHandle",close);record.app=nullptr;record.done=true;if(!record.references)record={};return discard(998);}
    BeginLaunchFeedback(slot,startup_flags);
    return 1;
}
Application *Application::ChildApplication(unsigned slot,bool &done){
    done=slot<ChildCapacity&&children[slot].used&&children[slot].done;
    return slot<ChildCapacity&&children[slot].used?children[slot].app:nullptr;
}
// USER32 WaitForInputIdle: waits until the process has entered its message
// loop, until it ends, or until the timeout (258 = WAIT_TIMEOUT). The wait is
// a series of short sleeps so the host keeps pumping the child.
unsigned Application::WaitForInputIdleApi(const unsigned *a){
    auto *h=FindKernelHandle(a[0]);
    if(!h||(h->kind!=6&&h->kind!=9)){last_error=6;return 0xffffffff;}
    const unsigned slot=h->object;const bool foreign=h->kind==9;
    const unsigned long long deadline=a[1]==0xffffffff?0:Now()+(unsigned long long)a[1]*1000;
    for(;;){
        bool done=false;auto*child=foreign?static_cast<Application*>(User::ProcessContextOf(slot)):ChildApplication(slot,done);
        if(!child||done||child->input_idle)return 0;
        if(deadline&&Now()>=deadline)return 258;
        auto &t=threads[current_thread];t.block=16;t.deadline=Now()+10000;Suspend(Idle); // 16: WaitForInputIdle, a USER wait
        if(state==Failed||state==Exited)return 0xffffffff;
    }
}
// Lifecycle debugger events are tied to actual child records. A delivered
// event keeps the debuggee stopped until that debugger continues that event.
unsigned Application::DebugApi(ApiName name,const unsigned *a){
    if(API_IS(name,"ContinueDebugEvent")){
        if(a[2]!=0x10002u&&a[2]!=0x80010001u){last_error=87;return 0;}
        for(auto &c:children)if(c.used&&c.debug_owner==process_id&&c.pid==a[0]&&c.tid==a[1]&&c.debug_pending){
            c.debug_pending=false;
            if(c.debug_phase==1)c.debug_phase=2;
            else if(c.debug_phase==3){c.debug_phase=4;if(c.app)c.app->debug_stopped=false;}
            else if(c.debug_phase==5){
                const unsigned ph=c.debug_process_handle,th=c.debug_thread_handle;
                c.debug_owner=0;
                if(ph){const unsigned close[]={ph};ThreadApi("CloseHandle",close);}
                if(th){const unsigned close[]={th};ThreadApi("CloseHandle",close);}
                ReleaseChild(unsigned(&c-children));
            }
            return 1;
        }
        last_error=87;return 0;
    }
    if(!virtual_memory.Access(a[0],96,true)){last_error=87;return 0;}
    const auto deadline=a[1]==~0u?0:Now()+(unsigned long long)a[1]*1000;
    for(;;){
        bool attached=false;
        for(auto &c:children)if(c.used&&c.debug_owner==process_id){
            attached=true;if(c.debug_pending)continue;
            unsigned event[24]{};event[1]=c.pid;event[2]=c.tid;
            if(c.debug_phase==1&&c.app){
                event[0]=3; // CREATE_PROCESS_DEBUG_EVENT
                if(!c.debug_process_handle)c.debug_process_handle=NewKernelHandle(6,unsigned(&c-children));
                if(!c.debug_thread_handle)c.debug_thread_handle=NewKernelHandle(7,unsigned(&c-children));
                if(!c.debug_process_handle||!c.debug_thread_handle){last_error=8;return 0;}
                event[4]=c.debug_process_handle;event[5]=c.debug_thread_handle;
                event[6]=c.app->image.Base();event[10]=c.app->image.Entry();
            }else if(c.debug_phase==2&&c.app){
                while(c.debug_module<c.app->module_watermark&&!c.app->modules[c.debug_module].handle)++c.debug_module;
                if(c.debug_module<c.app->module_watermark){
                    event[0]=6; // LOAD_DLL_DEBUG_EVENT for an actually loaded image.
                    const auto &module=c.app->modules[c.debug_module];
                    if(!c.debug_image_name)c.debug_image_name=c.app->Allocate(264);
                    if(!c.debug_image_name){last_error=8;return 0;}
                    // lpImageName points to a pointer in the debuggee, not
                    // to a string in the debugger's address space.
                    const unsigned string=c.debug_image_name+4;
                    if(!c.app->Store(c.debug_image_name,&string,4)||
                       !c.app->Store(c.debug_image_name+4,module.path,unsigned(strlen(module.path))+1)){
                        last_error=8;return 0;
                    }
                    event[4]=module.handle;event[7]=c.debug_image_name;
                    ++c.debug_module;
                }else{
                    c.debug_phase=3;event[0]=1;event[3]=0x80000003u;
                    event[6]=c.app->image.Entry();event[23]=1; // first-chance entry breakpoint
                }
            }else if(c.debug_phase==4&&c.done){
                c.debug_phase=5;event[0]=5;event[3]=c.result; // EXIT_PROCESS_DEBUG_EVENT
            }else continue;
            if(!Store(a[0],event,sizeof event))return 0;
            c.debug_pending=true;return 1;
        }
        if(!attached){last_error=87;return 0;}
        if(!a[1]||(deadline&&Now()>=deadline)){last_error=121;return 0;}
        auto &t=threads[current_thread];t.block=3;t.deadline=Now()+1000;Suspend(Idle);
        if(state==Failed||state==Exited)return 0;
    }
}
}
