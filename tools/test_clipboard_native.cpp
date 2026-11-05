// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
#define private public
#include "../src/x86_app.h"
#undef private
#include "../src/x86_clipboard.cpp"
static std::vector<unsigned char> load(const char*p){FILE*f=fopen(p,"rb");assert(f);fseek(f,0,SEEK_END);auto n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
static unsigned api(X86::Application&a,const char*name,unsigned x=0,unsigned y=0,unsigned z=0){unsigned v[]={x,y,z};return a.Api(name,v);}
int main(int argc,char**argv){
    assert(argc==3);auto exe=load(argv[1]),font=load(argv[2]);unsigned before=0;
    {
        X86::Application app(nullptr);assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"CLIPBOARD.EXE"));
        // Task identity is a private slot token. Public TIDs can cross WORD
        // boundaries while the same native worker slot is reused after cleanup.
        app.next_thread_id=0xffff;
        for(unsigned n=0;n<10000 && app.Status()!=X86::Application::Failed && app.Status()!=X86::Application::Exited;++n)app.Pump(32);
        if(app.Status()==X86::Application::Failed || app.ExitCode())fprintf(stderr,"clipboard guest: status=%u code=%u error=%s\n",app.Status(),app.ExitCode(),app.Error());
        assert(app.Status()==X86::Application::Exited && !app.ExitCode());
        printf("CLIPBOARD GUEST PASS calls=%u callbacks=%u\n",app.ApiCalls(),app.Callbacks());
    }
    // The next profiles use real Application startup/heap/teardown while leaving
    // their guest entry suspended. Data storage must not retain guest pointers.
    for(unsigned profile=0;profile<3;++profile){
        auto*app=new X86::Application(nullptr);assert(app->Start(exe.data(),exe.size(),font.data(),font.size(),"CLIPBOARD.EXE"));app->threads[0].suspended=1;
        const unsigned name=app->String("Neo98.ProcessClipboard"),format=api(*app,"RegisterClipboardFormatA",name);
        if(profile==0){
            assert(api(*app,"OpenClipboard") && api(*app,"EmptyClipboard"));
            // GMEM_FIXED: the handle is the pointer. A GMEM_MOVEABLE handle
            // would need GlobalLock, as on Win98.
            const unsigned data=api(*app,"GlobalAlloc",0x40,64);const char contents[]="survives complete source-process teardown";
            assert(app->Store(data,contents,sizeof contents));assert(api(*app,"SetClipboardData",format,data)==data);
            assert(api(*app,"CloseClipboard"));before=api(*app,"GetClipboardSequenceNumber");
        }else{
            assert(api(*app,"IsClipboardFormatAvailable",format)==format && api(*app,"GetClipboardSequenceNumber")==before);
            assert(api(*app,"OpenClipboard"));unsigned view=api(*app,"GetClipboardData",format);assert(view);
            char text[64];assert(app->Read(view,text,sizeof text));assert(!strcmp(text,"survives complete source-process teardown"));
            // Free/reuse of an app alias must not return unrelated reused bytes.
            assert(!api(*app,"GlobalFree",view));unsigned reused=app->Allocate(64);assert(reused==view);
            assert(app->Store(reused,"unrelated",10));unsigned refreshed=api(*app,"GetClipboardData",format);assert(refreshed && refreshed!=reused);
            assert(app->Read(refreshed,text,sizeof text) && !strcmp(text,"survives complete source-process teardown"));
            assert(api(*app,"CloseClipboard"));
            if(profile==2){assert(api(*app,"OpenClipboard") && api(*app,"EmptyClipboard") && api(*app,"CloseClipboard"));assert(!app->clipboard->storage.bytes);
                // Original atom references saturate at WORD_MAX. Clipboard
                // ownership must not wrap a pinned atom through zero and free it.
                const char pinned_name[]="Neo98.PinnedClipboardAtom";
                const unsigned pinned=app->RegisterMessage(pinned_name);
                for(unsigned n=0;n<65534;++n)assert(app->RegisterMessage(pinned_name)==pinned);
                assert(api(*app,"OpenClipboard"));
                assert(!api(*app,"SetClipboardData",pinned,0) && api(*app,"CountClipboardFormats")==1);
                assert(app->RegisterMessage(pinned_name)==pinned);
                assert(api(*app,"EmptyClipboard") && api(*app,"CloseClipboard"));
                unsigned char pinned_text[64]={};
                assert(app->ClipboardAtom(pinned,0,pinned_text,sizeof pinned_text)==sizeof pinned_name-1);
                assert(!strcmp(reinterpret_cast<char*>(pinned_text),pinned_name));
                auto&e=*app->clipboard;const unsigned prior=api(*app,"GetClipboardSequenceNumber");
                const unsigned h1=e.GlobalAlloc(0,8*1024*1024),h2=e.GlobalAlloc(0,8*1024*1024);assert(h1 && h2);
                assert(!e.GlobalAlloc(0,1) && app->last_error==8 && api(*app,"GetClipboardSequenceNumber")==prior);
                e.GlobalFree(h1);e.GlobalFree(h2);assert(!e.storage.bytes);
                assert(api(*app,"OpenClipboard"));
                for(unsigned f=0x9000;f<0x9200;++f){api(*app,"SetClipboardData",f,0);assert(api(*app,"CountClipboardFormats")==f-0x9000+1);}
                app->last_error=123;assert(!api(*app,"SetClipboardData",0x9200,0) && app->last_error==8 && api(*app,"CountClipboardFormats")==512);
                assert(api(*app,"EmptyClipboard") && api(*app,"CloseClipboard") && !e.storage.bytes);
}
        }
        delete app;
    }
    puts("CLIPBOARD NATIVE PASS: guest family + three consecutive app heaps, alias reuse and teardown");
}
