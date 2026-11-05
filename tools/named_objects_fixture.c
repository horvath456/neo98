// SPDX-License-Identifier: GPL-2.0-or-later
// Two processes over one namespace of named kernel objects. The fixture
// starts itself as its own child; the first command-line letter selects the
// role.
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API U CreateEventA(U,U,U,const char*);API U OpenEventA(U,U,const char*);API U SetEvent(U);API U ResetEvent(U);
API U CreateMutexA(U,U,const char*);API U OpenMutexA(U,U,const char*);API U ReleaseMutex(U);
API U CreateSemaphoreA(U,U,U,const char*);API U OpenSemaphoreA(U,U,const char*);API U ReleaseSemaphore(U,U,U*);
API U CreateFileMappingA(U,U,U,U,U,const char*);API U OpenFileMappingA(U,U,const char*);
API U MapViewOfFile(U,U,U,U,U);API U UnmapViewOfFile(U);API U FlushViewOfFile(U,U);
API U WaitForSingleObject(U,U);API U WaitForMultipleObjects(U,const U*,U,U);
API U CloseHandle(U);API U GetLastError(void);API void SetLastError(U);
API U CreateProcessA(const char*,char*,U,U,U,U,const char*,const char*,U*,U*);
API U GetExitCodeProcess(U,U*);API void ExitProcess(U);API char*GetCommandLineA(void);
API void Sleep(U);API U GetCurrentProcessId(void);
static void check(U v,U code){if(!v)ExitProcess(code);}
static U hex(const char*p){U v=0;for(;*p;++p){U d=*p>='a'?*p-'a'+10:*p-'0';v=v*16+d;}return v;}
static void print_hex(char*out,U v){for(U i=0;i<8;++i){U d=(v>>(28-4*i))&15;out[i]=d<10?'0'+d:'a'+d;}out[8]=0;}
// The bytes both processes share. Offsets in DWORDs.
#define PARENT_WORD 0
#define CHILD_WORD 1
#define VIEW_WORD 2      // the address the parent's view has
#define PARENT_PTR 3     // a pointer the parent stores inside the block
#define CHILD_PTR 4      // a pointer the child stores inside the block
#define PARENT_PAYLOAD 8
#define CHILD_PAYLOAD 9
static const char event_name[]="NEO98-NAMED-EVENT";
static const char go_name[]="NEO98-NAMED-GO";
static const char mutex_name[]="NEO98-NAMED-MUTEX";
static const char semaphore_name[]="NEO98-NAMED-SEMAPHORE";
static const char mapping_name[]="NEO98-NAMED-MAPPING";
void mainCRTStartup(void)
{
    char *line=GetCommandLineA();
    U sa[3]={12,0,1},si[17]={68},pi[4]={0},v=0;
    if(line[0]=='C')
    {
        // The child. Its argument is the value of an inherited handle, which
        // puts at the same table index as in the parent.
        U inherited=hex(line+2);
        check(WaitForSingleObject(inherited,0)==258,17400);
        // Every name of the parent is visible here.
        U e=OpenEventA(0x1f0003,0,event_name);check(e,17401);
        U go=OpenEventA(0x1f0003,0,go_name);check(go,17402);
        U m=OpenMutexA(0x1f0001,0,mutex_name);check(m,17403);
        U s=OpenSemaphoreA(0x1f0003,0,semaphore_name);check(s,17404);
        // A named file mapping is visible here, its view has the same address
        // as in the parent, and a pointer the parent stored inside the block
        // still points at the parent's payload. That is the contract of the
        // session arena.
        U mp=OpenFileMappingA(0xf001f,0,mapping_name);check(mp,17405);
        U view=MapViewOfFile(mp,0xf001f,0,0,0);check(view,17430);
        {
            U *words=(U*)view;
            check(words[PARENT_WORD]==0xa1a2a3a4u,17431);
            check(words[VIEW_WORD]==view,17432);
            check(words[PARENT_PTR]==(U)(words+PARENT_PAYLOAD),17433);
            check(*(U*)words[PARENT_PTR]==0xdeadbeefu,17434);
            words[CHILD_PAYLOAD]=0x5a5a5a5au;
            words[CHILD_PTR]=(U)(words+CHILD_PAYLOAD);
            words[CHILD_WORD]=GetCurrentProcessId();
        }
        check(UnmapViewOfFile(view),17435);check(CloseHandle(mp),17436);
        // An unnamed mapping stays private: it has no name to find.
        SetLastError(0);check(!OpenFileMappingA(0xf001f,0,"NEO98-NAMED-ABSENT")&&GetLastError()==123,17437);
        // One namespace: the mutex name is not an event name.
        SetLastError(0);check(!OpenEventA(0x1f0003,0,mutex_name)&&GetLastError()==6,17406);
        // A second create of a name the parent owns is the parent's object.
        SetLastError(0);U again=CreateEventA(0,1,0,event_name);
        check(again&&again!=e&&GetLastError()==183,17407);check(CloseHandle(again),17408);
        // The parent holds the mutex, so it cannot be taken.
        check(WaitForSingleObject(m,0)==258,17411);
        // Wake the parent across the process boundary.
        check(SetEvent(e),17413);
        // It answers by releasing the mutex and setting the second event.
        check(WaitForSingleObject(go,~0u)==0,17414);
        check(WaitForSingleObject(m,0)==0,17415);
        check(ReleaseMutex(m),17416);
        check(ReleaseSemaphore(s,1,0),17417);
        check(SetEvent(inherited),17418);
        check(CloseHandle(e)&&CloseHandle(go)&&CloseHandle(m)&&CloseHandle(s),17419);
        check(CloseHandle(inherited),17420);
        ExitProcess(55);
    }
    // The parent.
    SetLastError(0);
    U e=CreateEventA((U)sa,1,0,event_name);check(e&&!GetLastError(),17300);
    SetLastError(0);U twin=CreateEventA(0,1,0,event_name);
    check(twin&&twin!=e&&GetLastError()==183,17301);check(CloseHandle(twin),17302);
    // the name is taken by another type.
    SetLastError(0);check(!CreateMutexA(0,0,event_name)&&GetLastError()==6,17303);
    SetLastError(0);check(!CreateSemaphoreA(0,0,1,event_name)&&GetLastError()==6,17304);
    SetLastError(0);check(!CreateFileMappingA(~0u,0,4,0,4096,event_name)&&GetLastError()==6,17305);
    // an absent name is ERROR_INVALID_NAME, not ERROR_FILE_NOT_FOUND.
    SetLastError(0);check(!OpenEventA(0x1f0003,0,"NEO98-NOT-THERE")&&GetLastError()==123,17306);
    SetLastError(0);check(!OpenMutexA(0x1f0001,0,"NEO98-NOT-THERE")&&GetLastError()==123,17307);
    SetLastError(0);check(!OpenSemaphoreA(0x1f0003,0,"NEO98-NOT-THERE")&&GetLastError()==123,17308);
    SetLastError(0);check(!OpenFileMappingA(0xf001f,0,"NEO98-NOT-THERE")&&GetLastError()==123,17309);
    // 260 characters are a name, 261 are ERROR_INVALID_NAME.
    {
        char long_name[263];U i;
        for(i=0;i<260;++i)long_name[i]='a';
        long_name[260]=0;
        SetLastError(0);U ok=CreateEventA(0,1,0,long_name);check(ok&&!GetLastError(),17310);
        check(CloseHandle(ok),17311);
        long_name[260]='a';long_name[261]=0;
        SetLastError(0);check(!CreateEventA(0,1,0,long_name)&&GetLastError()==123,17312);
    }
    // Windows 98 compares case-sensitively, so this is a second object.
    {
        SetLastError(0);U other=CreateEventA(0,1,0,"neo98-named-event");
        check(other&&other!=e&&!GetLastError(),17313);check(CloseHandle(other),17314);
    }
    U go=CreateEventA(0,1,0,go_name);check(go,17315);
    U m=CreateMutexA(0,1,mutex_name);check(m,17316);
    U s=CreateSemaphoreA(0,0,2,semaphore_name);check(s,17317);
    U mp=CreateFileMappingA(~0u,0,4,0,4096,mapping_name);check(mp,17318);
    U view=MapViewOfFile(mp,0xf001f,0,0,0);check(view,17319);
    U *words=(U*)view;words[PARENT_WORD]=0xa1a2a3a4u;words[CHILD_WORD]=0;
    // The view address and a pointer into the block. Under Win98 both mean the
    // same thing in every process, because the block lies in the session arena.
    words[VIEW_WORD]=view;words[PARENT_PAYLOAD]=0xdeadbeefu;
    words[PARENT_PTR]=(U)(words+PARENT_PAYLOAD);words[CHILD_PTR]=0;words[CHILD_PAYLOAD]=0;
    check(FlushViewOfFile(view,4096),17320);
    // A second view of the same object in this process has one address, as the
    // original hands out (OLE32 reopens its own section this way).
    {
        U again=MapViewOfFile(mp,0xf001f,0,0,0);check(again==view,17342);
        check(UnmapViewOfFile(again),17343);
    }
    // An unnamed but inheritable event. Its handle value travels in the
    // command line and has to mean the same object in the child.
    U inheritable=CreateEventA((U)sa,1,0,0);check(inheritable,17321);
    char command[64];
    {
        const char *prefix="C:\\NAMED.EXE C ";U i=0;
        for(;prefix[i];++i)command[i]=prefix[i];
        print_hex(command+i,inheritable);
    }
    // Windows 98 accepts NORMAL_PRIORITY_CLASS. The file lookup comes first, so
    // a rejected flag would be error 50 rather than the absent program.
    SetLastError(0);check(!CreateProcessA("C:\\ABSENT.EXE",command,0,0,0,0x20,0,0,si,pi)&&GetLastError()==2,17322);
    SetLastError(0);check(!CreateProcessA("C:\\ABSENT.EXE",command,0,0,0,0x60,0,0,si,pi)&&GetLastError()==2,17323);
    // An unsupported flag is still ERROR_NOT_SUPPORTED, before the lookup.
    SetLastError(0);check(!CreateProcessA("C:\\ABSENT.EXE",command,0,0,0,0x4,0,0,si,pi)&&GetLastError()==50,17324);
    check(CreateProcessA("C:\\NAMED.EXE",command,0,0,1,0x20,0,0,si,pi),17325);
    // The child signals the parent's event across the process boundary.
    check(WaitForSingleObject(e,~0u)==0,17326);
    check(ReleaseMutex(m)&&SetEvent(go),17328);
    // The semaphore the child releases, waited for as one of two objects.
    {
        U objects[2];objects[0]=s;objects[1]=inheritable;
        check(WaitForMultipleObjects(2,objects,1,~0u)==0,17329);
    }
    check(WaitForSingleObject(inheritable,0)==0,17330);
    check(WaitForSingleObject(pi[0],~0u)==0,17331);
    check(GetExitCodeProcess(pi[0],&v)&&v==55,17332);
    // The mutex the child took and released is free and not abandoned.
    check(WaitForSingleObject(m,0)==0&&ReleaseMutex(m),17333);
    // What the child wrote into the block is here, and the pointer it stored
    // points at its payload in this process too.
    check(words[PARENT_WORD]==0xa1a2a3a4u,17334);
    check(words[CHILD_WORD]&&words[CHILD_WORD]!=GetCurrentProcessId(),17344);
    check(words[CHILD_PTR]==(U)(words+CHILD_PAYLOAD),17345);
    check(*(U*)words[CHILD_PTR]==0x5a5a5a5au,17346);
    // Lifetime: the child's handles are gone, this process still holds one,
    // so the name is still there. After the last handle it is not.
    {
        U still=OpenEventA(0x1f0003,0,event_name);check(still,17335);
        check(CloseHandle(still)&&CloseHandle(e),17336);
        SetLastError(0);check(!OpenEventA(0x1f0003,0,event_name)&&GetLastError()==123,17337);
    }
    check(CloseHandle(go)&&CloseHandle(m)&&CloseHandle(s),17338);
    check(UnmapViewOfFile(view)&&CloseHandle(mp)&&CloseHandle(inheritable),17339);
    SetLastError(0);check(!OpenFileMappingA(0xf001f,0,mapping_name)&&GetLastError()==123,17340);
    check(CloseHandle(pi[0])&&CloseHandle(pi[1]),17341);
    ExitProcess(0);
}
