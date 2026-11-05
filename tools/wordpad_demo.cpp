// SPDX-License-Identifier: GPL-2.0-or-later
// Original WordPad keyboard and mouse demo. Only the native RAM volume is modified.
#define private public
#include "x86_app.h"
#undef private
#include "host_volume.h"
#include "system_tree_host.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>
static std::vector<unsigned char> load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return {};
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    if (n <= 0 || n > 8 * 1024 * 1024) {
        fclose(f);
        return {};
    }
    std::vector<unsigned char> b(n);
    if (fread(b.data(), 1, b.size(), f) != b.size())
        b.clear();
    fclose(f);
    return b;
}
static X86::Application*diagnostic_app=nullptr;
static void snapshot_cpp_exception(X86::Application&);
static void logger(const char *s) {
    if(diagnostic_app&&strstr(s,"Unimplemented API MSVCRT.dll!__CxxFrameHandler"))snapshot_cpp_exception(*diagnostic_app);
    puts(s);
    fflush(stdout);
}
static const char *leaf(const char *s) {
    const auto *p = strrchr(s, '/');
    return p ? p + 1 : s;
}
static void hex_text(const char *text) {
    for (const auto *p = reinterpret_cast<const unsigned char *>(text); *p; ++p)
        printf("%02x", *p);
}
static void snapshot_windows() {
    unsigned handles[192], count = 0;
    if (!User::WindowSnapshot(User::GetDesktopWindow(), handles, 192, count))
        return;
    for (unsigned i = 0; i < count; ++i) {
        const unsigned hwnd = handles[i];
        char klass[64];
        std::vector<char> text(User::TextLength(hwnd) + 1);
        User::GetClassName(hwnd, klass, sizeof klass);
        User::GetWindowText(hwnd, text.data(), text.size());
        printf("PROBE_WINDOW hwnd=%u parent=%u owner=%u visible=%u class=", hwnd, User::Parent(hwnd),
               User::Owner(hwnd), User::IsVisible(hwnd));
        hex_text(klass);
        printf(" text=");
        hex_text(text.data());
        putchar('\n');
    }
}
static std::vector<unsigned char> framebuffer(const char*path){
 User::Render();const auto*p=User::Pixels();std::vector<unsigned char>rgb(800*600*3);for(unsigned i=0;i<800*600;++i){rgb[i*3]=p[i*4+2];rgb[i*3+1]=p[i*4+1];rgb[i*3+2]=p[i*4];}FILE*f=fopen(path,"wb");if(!f)return {};fprintf(f,"P6\n800 600\n255\n");bool ok=fwrite(rgb.data(),1,rgb.size(),f)==rgb.size();ok=fclose(f)==0&&ok;return ok?rgb:std::vector<unsigned char>{};
}

static void snapshot_cpp_exception(X86::Application &app)
{
    if (!app.cpu) return;
    auto read = [&](unsigned p, void *out, unsigned bytes) {
        return app.cpu->Read(p, out, bytes) == X86::CpuError::Ok;
    };
    auto dump = [&](const char *name, unsigned p, unsigned words) {
        unsigned data[20] = {};
        if (words > 20 || !read(p, data, words * 4)) return;
        printf("PROBE_CXX name=%s address=%08x data=", name, p);
        for (unsigned i = 0; i < words; ++i) printf("%08x,", data[i]);
        putchar('\n');
    };
    unsigned sp = 0, metadata = 0, args[5] = {}, record[20] = {};
    if (app.cpu->Get(X86::Reg::Esp, sp) != X86::CpuError::Ok ||
        app.cpu->Get(X86::Reg::Eax, metadata) != X86::CpuError::Ok ||
        !read(sp, args, sizeof args)) return;
    dump("arguments", sp, 5);
    dump("function", metadata, 10);
    dump("record", args[1], 20);
    if (args[2] >= 4) dump("frame", args[2] - 4, 8);
    dump("context-ebp-eip", args[3] + 0xb4, 2);
    dump("context-esp", args[3] + 0xc4, 1);
    if (!read(args[1], record, sizeof record)) return;
    if ((record[0] & 0x0ffffffe) == 0x006d007e && record[4] == 1) {
        unsigned delay[9] = {};
        dump("delay-load", record[5], 9);
        if (read(record[5], delay, sizeof delay) && delay[0] == sizeof delay) {
            for (unsigned i = 0; i < (delay[4] == 1 ? 2u : 1u); ++i) {
                char text[260] = {}; unsigned address = delay[i ? 5 : 3];
                for (unsigned n = 0; n < sizeof text; ++n) {
                    if (!read(address + n, text + n, 1)) break;
                    if (!text[n]) {
                        printf("PROBE_CXX_DELAY %s=", i ? "name" : "dll");
                        hex_text(text); putchar('\n'); break;
                    }
                }
            }
        }
        return;
    }
    if (record[0] != 0xe06d7363 || record[4] != 3) return;
    unsigned info[4] = {}, types[2] = {};
    dump("throw-info", record[7], 4);
    if (!read(record[7], info, sizeof info)) return;
    dump("catchable-array", info[3], 2);
    if (read(info[3], types, sizeof types) && types[0]) dump("catchable-type", types[1], 7);
}

int main(int argc,char**argv){
 if(argc<3)return 2;bool save=false,roundtrip=false;const char*save_path=nullptr;
 // The profile has no DLL list any more; the originals come out of the
 // imported Win98 tree. Mount it before the RAM volume is first used.
 if(!SystemTreeHost::Mount()){fprintf(stderr,"no Win98 folder; run python3 neo98.py setup --iso ...\n");return 11;}
 // --drive-c runs the same roundtrip on a writable host directory instead of
 // the RAM volume.
 {auto drives=HostVolume::Scan(argc,argv);drives.ram=drives.drive_c==nullptr;
  if(!HostVolume::Mount(drives)){fprintf(stderr,"cannot mount drive C\n");return 13;}
  if(!drives.ram)printf("DRIVE_C %s\n",drives.drive_c);}
 for(int i=3;i<argc;++i){if(!strcmp(argv[i],"--drive-c")){if(++i==argc)return 2;continue;}if(!strcmp(argv[i],"--ram-drive"))continue;if(!strcmp(argv[i],"--roundtrip")){roundtrip=true;continue;}if(!strcmp(argv[i],"--save")){save=true;continue;}if(!strcmp(argv[i],"--save-as")){if(++i==argc)return 2;save=true;save_path=argv[i];continue;}auto data=load(argv[i]);char path[260];snprintf(path,sizeof path,"C:\\WINDOWS\\SYSTEM\\%s",leaf(argv[i]));if(data.empty()||Files::System().Seed(path,data.data(),data.size()))return 3;}
 auto exe=load(argv[1]),font=load(argv[2]);auto*a=new X86::Application(logger);diagnostic_app=a;bool good=a->Start(exe.data(),exe.size(),font.data(),font.size(),"WORDPAD.EXE");unsigned packets=0;
 auto pump=[&](const char*phase){auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(20);while(a->Status()!=X86::Application::Failed&&a->Status()!=X86::Application::Exited&&std::chrono::steady_clock::now()<limit){a->Pump(32);if(a->WaitingForInput()){printf("PHASE name=%s state=%u calls=%u callbacks=%u\n",phase,a->Status(),a->ApiCalls(),a->Callbacks());return true;}if(a->Status()==X86::Application::Idle)std::this_thread::sleep_for(std::chrono::milliseconds(1));}printf("PHASE_FAILED name=%s state=%u calls=%u callbacks=%u error=%s\n",phase,a->Status(),a->ApiCalls(),a->Callbacks(),a->Error());return false;};
 good=good&&pump("startup");unsigned handles[192],count=0,editor=0;User::WindowSnapshot(User::GetDesktopWindow(),handles,192,count);for(unsigned i=0;i<count;++i){char klass[64];User::GetClassName(handles[i],klass,sizeof klass);if(!strcmp(klass,"RichEdit20A")&&User::IsVisible(handles[i]))editor=handles[i];}
 Gdi::RECT client{};Gdi::POINT origin{};if(!editor||!User::GetClientRect(editor,&client)||!User::ScreenToClient(editor,&origin))good=false;
 printf("EDITOR hwnd=%u x=%d y=%d width=%d height=%d procedure=%08x\n",editor,-origin.x,-origin.y,client.right,client.bottom,User::Procedure(editor));
 auto before=framebuffer("before.ppm");
 auto mouse=[&](unsigned buttons){int x=-origin.x+28,y=-origin.y+22;printf("INPUT n=%u kind=mouse x=%d y=%d buttons=%u\n",packets++,x,y,buttons);a->MouseButtons(x,y,buttons);return pump("mouse");};
 auto key=[&](unsigned vk,unsigned ch,bool down){printf("INPUT n=%u kind=key vk=%u character=%u down=%u\n",packets++,vk,ch,down);a->RawKey(vk,ch,down);};
 auto type=[&](const char*text){for(const unsigned char*p=(const unsigned char*)text;*p;++p){unsigned c=*p,vk=c>='a'&&c<='z'?c-32:c;bool shift=c>='A'&&c<='Z'||c=='!'||c==':';if(shift)key(16,0,true);if(c=='!')vk='1';if(c==':')vk=0xba;if(c=='\\')vk=0xdc;if(c=='.')vk=0xbe;if(c=='/')vk=0xbf;key(vk,c,true);key(vk,0,false);if(shift)key(16,0,false);if(!pump("typing"))return false;}return true;};
 unsigned query=a->Allocate(4096);
 auto text=[&](const char*stage,const char*expected){if(!query)return false;unsigned char empty[4096]={};a->Store(query,empty,sizeof empty);printf("QUERY stage=%s hwnd=%u message=13 buffer=%08x\n",stage,editor,query);User::PostMessage(editor,13,4096,query);if(!pump("query"))return false;char result[4096]={};if(!a->Read(query,result,sizeof result))return false;result[4095]=0;printf("EDITOR_TEXT stage=%s text=",stage);hex_text(result);putchar('\n');char name[100];snprintf(name,sizeof name,"text-%s.bin",stage);FILE*f=fopen(name,"wb");if(!f)return false;fwrite(result,1,strlen(result),f);fclose(f);return !strcmp(result,expected);};
 if(good)good=mouse(0)&&mouse(1)&&mouse(0)&&text("empty","");
 if(good)good=type("Windows 98 on ARM")&&text("typed","Windows 98 on ARM");
 auto typed=framebuffer("typed.ppm");
 if(good)good=type("!")&&text("appended","Windows 98 on ARM!");
 if(good){key(8,8,true);key(8,0,false);good=pump("backspace")&&text("edited","Windows 98 on ARM");}
 auto after=framebuffer("after.ppm");unsigned changed=0;if(before.size()==after.size())for(int y=-origin.y;y<-origin.y+client.bottom&&y<600;++y)for(int x=-origin.x;x<-origin.x+client.right&&x<800;++x)if(x>=0&&y>=0){unsigned at=(y*800+x)*3;if(memcmp(before.data()+at,after.data()+at,3))++changed;}
 good=good&&changed>0;unsigned quiet=0,frame=User::FrameSerial();if(good)for(;quiet<24;++quiet){a->Pump(1);if(a->Status()==X86::Application::Failed||User::FrameSerial()!=frame)break;}
 printf("EDIT_RESULT ok=%u state=%u editor=%u packets=%u changed_pixels=%u quiet=%u calls=%u callbacks=%u\n",good,a->Status(),editor,packets,changed,quiet,a->ApiCalls(),a->Callbacks());
 if(good&&save){
  key(17,0,true);key('S',0,true);key('S',0,false);key(17,0,false);bool opened=pump("save");framebuffer("save.ppm");printf("SAVE_RESULT opened=%u state=%u error=%s\n",opened,a->Status(),a->Error());snapshot_windows();
  if(save_path){
   unsigned windows[192],n=0,dialog=0;User::WindowSnapshot(0,windows,192,n);
   for(unsigned i=0;i<n;++i){char title[128]{};User::GetWindowText(windows[i],title,sizeof title);if(!strcmp(title,"Save As")&&User::IsVisible(windows[i]))dialog=windows[i];}
   unsigned field=User::GetDlgItem(dialog,1152);Gdi::RECT bounds{};
   bool completed=opened&&field&&User::GetWindowRect(field,&bounds);
   if(completed){
    int x=(bounds.left+bounds.right)/2,y=(bounds.top+bounds.bottom)/2;
    for(unsigned buttons:{0u,1u,0u}){printf("INPUT n=%u kind=mouse x=%d y=%d buttons=%u\n",packets++,x,y,buttons);a->MouseButtons(x,y,buttons);if(!pump("filename-focus")){completed=false;break;}}
   }
   // Win98's ordinary EDIT has Home/Shift+End selection; Ctrl+A is not
   // a universal select-all command for these controls.
   if(completed){key(36,0,true);key(36,0,false);key(16,0,true);key(35,0,true);key(35,0,false);key(16,0,false);completed=pump("filename-select")&&type(save_path);}
   // Select RTF through the control's type-ahead, then verify its name.
   unsigned formats=User::GetDlgItem(dialog,1136);Gdi::RECT format_bounds{};
   if(completed&&formats&&User::GetWindowRect(formats,&format_bounds)){
    int x=format_bounds.left+20,y=format_bounds.top+8;
    for(unsigned buttons:{0u,1u,0u}){printf("INPUT n=%u kind=mouse x=%d y=%d buttons=%u\n",packets++,x,y,buttons);a->MouseButtons(x,y,buttons);if(!pump("format-focus")){completed=false;break;}}
    if(completed){key('R','r',true);key('R',0,false);completed=pump("format-select");}
    // An open ComboBox consumes Enter through its original WM_GETDLGCODE path.
    if(completed){key(13,0,true);key(13,0,false);completed=pump("format-accept")&&User::IsWindow(dialog);}
    char selected[128]{};User::GetWindowText(formats,selected,sizeof selected);printf("SAVE_FORMAT text=");hex_text(selected);putchar('\n');completed=completed&&strstr(selected,"Rich Text Format");
   }else completed=false;
   unsigned button=User::GetDlgItem(dialog,1);Gdi::RECT button_bounds{};
   if(completed&&button&&User::GetWindowRect(button,&button_bounds)){
    int x=(button_bounds.left+button_bounds.right)/2,y=(button_bounds.top+button_bounds.bottom)/2;
    for(unsigned buttons:{0u,1u,0u}){printf("INPUT n=%u kind=mouse x=%d y=%d buttons=%u\n",packets++,x,y,buttons);a->MouseButtons(x,y,buttons);if(!pump("save-file")){completed=false;break;}}
   }else completed=false;
   unsigned node=0;const Files::Node*file=nullptr;
   if(completed&&!Files::System().Lookup(save_path,node))file=Files::System().Get(node);
   bool rtf=false,contains=false;
   if(file&&file->size){
    rtf=file->size>=5&&!memcmp(file->data,"{\\rtf",5);
    const char expected[]="Windows 98 on ARM";
    for(unsigned i=0;i+sizeof(expected)-1<=file->size;++i)if(!memcmp(file->data+i,expected,sizeof(expected)-1)){contains=true;break;}
    FILE*out=fopen("saved-document.bin","wb");if(!out)completed=false;else{completed=fwrite(file->data,1,file->size,out)==file->size&&completed;completed=fclose(out)==0&&completed;}
   }
   completed=completed&&file&&file->size&&rtf&&contains&&!User::IsWindow(dialog);
   framebuffer("saved.ppm");printf("SAVE_FILE_RESULT ok=%u bytes=%u rtf=%u contains_text=%u dialog_closed=%u state=%u calls=%u callbacks=%u\n",completed,file?file->size:0,rtf,contains,!User::IsWindow(dialog),a->Status(),a->ApiCalls(),a->Callbacks());
   good=good&&completed;
  }
 }

 if(good&&roundtrip){
  bool restored=save_path!=nullptr;
  auto control=[&](unsigned vk){key(17,0,true);key(vk,0,true);key(vk,0,false);key(17,0,false);};
  auto refresh_editor=[&](){unsigned windows[192],n=0;editor=0;origin={};User::WindowSnapshot(User::GetDesktopWindow(),windows,192,n);for(unsigned i=0;i<n;++i){char klass[64]{};User::GetClassName(windows[i],klass,sizeof klass);if(!strcmp(klass,"RichEdit20A")&&User::IsVisible(windows[i]))editor=windows[i];}return editor&&User::GetClientRect(editor,&client)&&User::ScreenToClient(editor,&origin);};
  auto find_dialog=[&](const char*caption){unsigned windows[192],n=0;User::WindowSnapshot(0,windows,192,n);for(unsigned i=0;i<n;++i){char title[128]{};User::GetWindowText(windows[i],title,sizeof title);if(!strcmp(title,caption)&&User::IsVisible(windows[i]))return windows[i];}return 0u;};
  auto click=[&](unsigned hwnd,const char*phase){Gdi::RECT r{};if(!hwnd||!User::GetWindowRect(hwnd,&r))return false;int x=(r.left+r.right)/2,y=(r.top+r.bottom)/2;for(unsigned buttons:{0u,1u,0u}){printf("INPUT n=%u kind=mouse x=%d y=%d buttons=%u\n",packets++,x,y,buttons);a->MouseButtons(x,y,buttons);if(!pump(phase))return false;}return true;};
  if(restored){
   control('N');restored=pump("new-document");
   // Win98 WordPad asks for a document type in its original dialog resource160.
   unsigned choice=find_dialog("New");
   if(restored&&choice){auto*d=a->FindDialog(choice);printf("NEW_ROUTING hwnd=%u window_proc=%08x dialog_proc=%08x\n",choice,User::Procedure(choice),d?d->procedure:0);framebuffer("new-type-dialog.ppm");bool clicked=click(User::GetDlgItem(choice,1),"new-type-accept");printf("NEW_DIALOG hwnd=%u clicked=%u exists=%u visible=%u\n",choice,clicked,User::IsWindow(choice),User::IsVisible(choice));restored=clicked&&!User::IsWindow(choice);}
   if(restored){bool found=refresh_editor();printf("NEW_EDITOR found=%u hwnd=%u\n",found,editor);restored=found&&text("new","");}
  }
  auto blank=framebuffer("new-document.ppm");
  if(restored){control('O');restored=pump("open-dialog");}
  unsigned dialog=find_dialog("Open"),field=User::GetDlgItem(dialog,1152);
  framebuffer("open-dialog.ppm");
  restored=restored&&dialog&&click(field,"open-filename-focus");
  if(restored){key(36,0,true);key(36,0,false);key(16,0,true);key(35,0,true);key(35,0,false);key(16,0,false);restored=pump("open-filename-select")&&type(save_path);}
  if(restored)restored=click(User::GetDlgItem(dialog,1),"open-file")&&!User::IsWindow(dialog)&&refresh_editor()&&text("reopened","Windows 98 on ARM");
  auto reopened=framebuffer("reopened.ppm");unsigned pixels=0;
  if(blank.size()==reopened.size())for(int y=-origin.y;y<-origin.y+client.bottom&&y<600;++y)for(int x=-origin.x;x<-origin.x+client.right&&x<800;++x)if(x>=0&&y>=0){unsigned at=(y*800+x)*3;if(memcmp(blank.data()+at,reopened.data()+at,3))++pixels;}
  restored=restored&&pixels>0;
  printf("ROUNDTRIP_RESULT ok=%u dialog_closed=%u changed_pixels=%u state=%u calls=%u callbacks=%u\n",restored,dialog&&!User::IsWindow(dialog),pixels,a->Status(),a->ApiCalls(),a->Callbacks());good=good&&restored;
 }
 if(!good){snapshot_windows();}if(query)a->Free(query);delete a;return good?0:4;
}
