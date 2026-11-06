// SPDX-License-Identifier: GPL-2.0-or-later
#include <cassert>
#include <memory>
#include <cstdio>
#include <cstring>
#include <cstdlib>
// libstdc++ wraps <stdlib.h> with using std::malloc: take it before the
// allocator is renamed below.
#include <stdlib.h>
#include <vector>
#include <new>
#define private public
#include "../src/x86_app.h"
#undef private
#include "../src/win16_machine.h"
static unsigned fail_at=0,alloc_count=0,image_live=0;
static void*image_malloc(size_t n){if(fail_at&&++alloc_count==fail_at)return nullptr;void*p=malloc(n);if(p)++image_live;return p;}
static void*image_calloc(size_t n,size_t m){if(fail_at&&++alloc_count==fail_at)return nullptr;void*p=calloc(n,m);if(p)++image_live;return p;}
static void image_free(void*p){if(p){assert(image_live);--image_live;free(p);}}
#define malloc image_malloc
#define calloc image_calloc
#define free image_free
#include "../src/win_image.cpp"
#include "../src/x86_image.cpp"
#undef malloc
#undef calloc
#undef free
#include "../build/image-resource/fixture-images.h"
static std::vector<unsigned char> load(const char*p){FILE*f=fopen(p,"rb");assert(f);fseek(f,0,SEEK_END);auto n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
static unsigned image_count(const X86::Application&a){unsigned n=0;for(unsigned slot=0;slot<1023;++slot){const auto&i=X86::Application::SharedIcons()[slot];n+=i.data&&i.owner==&a;}return n;}
int main(int argc,char**argv){assert(argc==4);auto exe=load(argv[1]),font=load(argv[2]),dll=load(argv[3]);
 {
 auto app_storage=std::make_unique<X86::Application>(nullptr);
        auto &app=*app_storage;assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"IMAGES.EXE"));assert(!app.files.store.Seed("C:\\IMAGERES.DLL",dll.data(),dll.size()));assert(!app.files.store.Seed("C:\\IMAGE.ICO",image_ico,sizeof image_ico));assert(!app.files.store.Seed("C:\\IMAGE.CUR",image_cur,sizeof image_cur));assert(!app.files.store.Seed("C:\\IMAGE.BMP",image_bmp,sizeof image_bmp));
 for(unsigned i=0;i<10000&&app.Status()!=X86::Application::Exited&&app.Status()!=X86::Application::Failed;++i)app.Pump(32);
 if(app.Status()!=X86::Application::Exited||app.ExitCode())fprintf(stderr,"images: state=%u exit=%u error=%s calls=%u\n",app.Status(),app.ExitCode(),app.Error(),app.ApiCalls());assert(app.Status()==X86::Application::Exited&&!app.ExitCode());printf("IMAGE RESOURCE GUEST PASS calls=%u callbacks=%u\n",app.ApiCalls(),app.Callbacks());
 }assert(!image_live);
 // The first allocation creates the system-image context; following failures hit
 // icon objects and both pixel buffers. Start may already initialize images;
 // release those before injecting failures. Every failed startup can be retried.
 for(unsigned failure=1;failure<=8;++failure){
 auto app_storage=std::make_unique<X86::Application>(nullptr);
        auto &app=*app_storage;assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"IMAGES.EXE"));app.threads[0].suspended=1;app.FreeImageResources();assert(!app.image_resources&&!image_count(app)&&!image_live);fail_at=failure;alloc_count=0;const unsigned a[]={0,32512};assert(!app.ImageApi("LoadIconA",a));fail_at=0;assert(!app.image_resources&&!image_count(app)&&!image_live);assert(app.last_error==8);assert(app.ImageApi("LoadIconA",a)&&image_count(app)==22);app.FreeImageResources();assert(!image_count(app)&&!image_live);
 }
 {
 auto observer_storage=std::make_unique<X86::Application>(nullptr);
        auto &observer=*observer_storage;
 auto app_storage=std::make_unique<X86::Application>(nullptr);
        auto &app=*app_storage;assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"IMAGES.EXE"));app.threads[0].suspended=1;assert(app.InitializeImageResources());const unsigned baseline=image_live;
 app.win16.state=new X86::Application::Win16Machine;
 unsigned aliased=app.CreateResourceIcon(image_icon,sizeof image_icon,true,0x30000,2,2,0);assert(aliased);
 const unsigned word=app.Win16ImageAlias(aliased,0x1234,false,101,nullptr);
 assert(word&&word<=0xffff&&app.Win16ImageAlias(aliased,0x1234,false,101,nullptr)==word);
 assert(app.Win16ImageNative(word)==aliased&&app.Win16ImageWord(aliased)==word);
 assert(app.Win16DestroyImage(word)&&!app.Win16ImageNative(word)&&!app.FindIcon(aliased));
 unsigned successor=app.CreateResourceIcon(image_icon,sizeof image_icon,true,0x30000,2,2,0);assert(successor);
 const unsigned successor_word=app.Win16ImageAlias(successor,0x1234,false,102,nullptr);
 assert(successor_word&&successor_word!=word&&!app.Win16ImageNative(word));
 assert(app.Win16DestroyImage(successor_word)&&image_live==baseline);
 for(unsigned failure=1;failure<=3;++failure){fail_at=failure;alloc_count=0;assert(!app.CreateResourceIcon(image_icon,sizeof image_icon,true,0x30000,2,2,0));fail_at=0;assert(app.last_error==8&&image_live==baseline&&image_count(app)==22);}
 auto data=std::vector<unsigned char>(image_icon,image_icon+sizeof image_icon);const unsigned h=app.CreateResourceIcon(data.data(),data.size(),true,0x30000,2,2,0);assert(h);memset(data.data(),0,data.size());assert(app.FindIcon(h)->data->color[0]==0x00ff00);const unsigned destroy[]={h};assert(app.ImageApi("DestroyIcon",destroy));assert(!app.FindIcon(h)&&image_live==baseline);
 const unsigned raw=app.String("C:\\IMAGE.CUR");assert(!app.files.store.Seed("C:\\IMAGE.CUR",image_cur,sizeof image_cur));const unsigned file[]={raw};const unsigned cursor=app.ImageApi("LoadCursorFromFileA",file);assert(cursor);auto*c=app.FindIcon(cursor);assert(c&&c->cursor&&c->hotspot_x==16&&c->hotspot_y==16);const unsigned cd[]={cursor};assert(app.ImageApi("DestroyCursor",cd));
 // LR_CREATEDIBSECTION copies the calculated scan-line extent. Real BMPs may
 // have a file tail, while a truncated raster must fail without faulting the
 // application or leaving a bitmap behind.
 std::vector<unsigned char> tailed(image_bmp,image_bmp+sizeof image_bmp);tailed.push_back(0xa5);tailed.push_back(0x5a);
 assert(!app.files.store.Seed("C:\\TAILED.BMP",tailed.data(),tailed.size()));
 const unsigned tailed_name=app.String("C:\\TAILED.BMP"),load_tailed[]={0,tailed_name,0,0,0,0x2010};
 const unsigned tailed_bitmap=app.ImageApi("LoadImageA",load_tailed);assert(tailed_bitmap&&app.Status()!=X86::Application::Failed);assert(Gdi::DeleteObject(tailed_bitmap));
 std::vector<unsigned char> truncated(image_bmp,image_bmp+sizeof image_bmp-1);
 assert(!app.files.store.Seed("C:\\TRUNCATED.BMP",truncated.data(),truncated.size()));
 const unsigned truncated_name=app.String("C:\\TRUNCATED.BMP"),load_truncated[]={0,truncated_name,0,0,0,0x2010};
 const unsigned objects=Gdi::LiveObjects();
 assert(!app.ImageApi("LoadImageA",load_truncated)&&app.last_error==13&&app.Status()!=X86::Application::Failed&&Gdi::LiveObjects()==objects);
 // Native handle/table exhaustion is an explicit provider bound. It must leave
 // existing images and later allocation intact.
 unsigned owned[1001];for(auto&v:owned){v=app.CreateResourceIcon(image_icon,sizeof image_icon,true,0x30000,2,2,0);assert(v);}assert(!app.CreateResourceIcon(image_icon,sizeof image_icon,true,0x30000,2,2,0)&&app.last_error==8);for(unsigned v:owned){unsigned d[]={v};assert(app.ImageApi("DestroyIcon",d));}assert(image_count(app)==22&&image_live==baseline);
 // Other processes resolve the identical system-wide HICON, never a local slot.
 unsigned shared=app.CreateResourceIcon(image_icon,sizeof image_icon,true,0x30000,2,2,0);assert(shared);
 assert(observer.FindIcon(shared)==app.FindIcon(shared));
 const unsigned remove[]={shared};assert(app.ImageApi("DestroyIcon",remove));assert(!observer.FindIcon(shared));
 unsigned replacement=app.CreateResourceIcon(image_icon,sizeof image_icon,true,0x30000,2,2,0);assert(replacement&&replacement!=shared);
 assert(!observer.FindIcon(shared)&&observer.FindIcon(replacement)==app.FindIcon(replacement));
 const unsigned remove_replacement[]={replacement};assert(app.ImageApi("DestroyIcon",remove_replacement));
 app.FreeImageResources();assert(!image_live&&!image_count(app));
 }
 assert(!image_live);puts("IMAGE RESOURCE NATIVE PASS: real resources/modules/files, WORD ABI, shared ownership, fallible allocations and retry");
}
