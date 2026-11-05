// SPDX-License-Identifier: GPL-2.0-or-later
// Exploratory runner. Read-only host files are copied into the native RAM volume.
#define private public
#include "../src/x86_app.h"
#undef private
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>
static std::vector<unsigned char> load(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return {};
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    if (n <= 0 || n > 8 * 1024 * 1024)
    {
        fclose(f);
        return {};
    }
    std::vector<unsigned char> b(n);
    if (fread(b.data(), 1, b.size(), f) != b.size())
        b.clear();
    fclose(f);
    return b;
}
static X86::Application *observed_app;
static void snapshot_property_sheet(X86::Application &app);
static void snapshot_cpp_exception(X86::Application &app);
static void logger(const char *s)
{
    puts(s);
    if(observed_app && !strcmp(s,"SetWindowPos")) {unsigned sp=0,v[8]{};observed_app->cpu->Get(X86::Reg::Esp, sp);observed_app->cpu->Read(sp,v,sizeof v);printf("CONTROL_FIRST_POSITION");for(auto x:v)printf(" %08x",x);putchar('\n');}
    // Capture nested calls before Application::Call restores its caller's
    // register context. Only inspect the app explicitly owned by this probe.
    if (observed_app && strstr(s, "Unimplemented API") && strstr(s, "PropertySheet"))
        snapshot_property_sheet(*observed_app);
    if (observed_app && strstr(s, "Unimplemented API") && strstr(s, "__CxxFrameHandler"))
        snapshot_cpp_exception(*observed_app);
    fflush(stdout);
}
static const char *leaf(const char *s)
{
    const auto *p = strrchr(s, '/');
    return p ? p + 1 : s;
}
static void hex_text(const char *text)
{
    for (const auto *p = reinterpret_cast<const unsigned char *>(text); *p; ++p)
        printf("%02x", *p);
}
static void snapshot_windows()
{
    unsigned handles[192], count = 0;
    if (!User::WindowSnapshot(User::GetDesktopWindow(), handles, 192, count))
        return;
    for (unsigned i = 0; i < count; ++i)
    {
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
        Gdi::RECT wr{},cr{};User::GetWindowRect(hwnd,&wr);User::GetClientRect(hwnd,&cr);
        printf("PAINT_GEOMETRY hwnd=%u proc=%08x style=%08x exstyle=%08x previous=%u next=%u window=%d,%d,%d,%d client=%d,%d,%d,%d\n",hwnd,User::Procedure(hwnd),User::Style(hwnd),User::ExtendedStyle(hwnd),User::GetWindow(hwnd,3),User::GetWindow(hwnd,2),wr.left,wr.top,wr.right,wr.bottom,cr.left,cr.top,cr.right,cr.bottom);
        if(!strcmp(klass,"CalWndMain")||!strcmp(klass,"ClockWndMain")){
            auto dc=User::GetDC(hwnd);unsigned samples=0,black=0,white=0,grey=0,other=0;
            for(int y=0;y<cr.bottom;y+=3)for(int x=0;x<cr.right;x+=3){auto c=Gdi::GetPixel(dc,x,y);++samples;if(!c)++black;else if(c==0xffffff)++white;else if(c==0xc0c0c0)++grey;else ++other;}
            printf("PAINT_CANVAS hwnd=%u dc=%u samples=%u black=%u white=%u grey=%u other=%u\n",hwnd,dc,samples,black,white,grey,other);User::ReleaseDC(hwnd,dc);
        }
    }
}
static void trace_control(X86::Cpu&engine,unsigned address,unsigned,void*opaque){X86::Cpu*uc=&engine;
 auto &a=*static_cast<X86::Application*>(opaque);unsigned index=(address-0x70000000)/16;
 if(index>=a.import_count)return;const char*n=a.imports[index].name;
 bool pos=!strcmp(n,"SetWindowPos"),msg=!strcmp(n,"SendMessageA"),style=!strcmp(n,"SetWindowLongA");
 if(!pos&&!msg&&!style)return;unsigned sp=0,v[9]{};uc->Get(X86::Reg::Esp, sp);if(uc->Read(sp,v,sizeof v))return;
 if(msg&&v[2]!=0xb3&&v[2]!=0xb4&&v[2]!=0xd3)return;
 printf("CONTROL_CALL %s",n);for(auto x:v)printf(" %08x",x);putchar('\n');
}
static unsigned visible_dialog(const char *caption)
{
    if (!caption) return 0;
    unsigned nodes[192], count = 0;
    if (!User::WindowSnapshot(0, nodes, 192, count)) return 0;
    for (unsigned i = 0; i < count; ++i) {
        char name[64], text[512];
        User::GetClassName(nodes[i], name, sizeof name);
        User::GetWindowText(nodes[i], text, sizeof text);
        if (User::IsVisible(nodes[i]) && !strcmp(name, "#32770") && !strcmp(text, caption))
            return nodes[i];
    }
    return 0;
}
static bool screenshot(const char *path)
{
    if (!path) return true;
    FILE *file = fopen(path, "wb");
    if (!file) return false;
    fprintf(file, "P6\n800 600\n255\n");
    const auto *pixels = User::Pixels();
    for (unsigned i = 0; i < 800 * 600; ++i) {
        unsigned char rgb[] = {pixels[i*4+2], pixels[i*4+1], pixels[i*4]};
        if (fwrite(rgb, 1, 3, file) != 3) { fclose(file); return false; }
    }
    return fclose(file) == 0;
}
static void snapshot_cpp_exception(X86::Application &app)
{
    if (!app.cpu || app.Status() != X86::Application::Failed) return;
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
static void snapshot_property_sheet(X86::Application &app)
{
    if (!app.cpu || app.Status() != X86::Application::Failed ||
        !strstr(app.Error(), "PropertySheet")) return;
    unsigned sp = 0, address = 0, header[16] = {};
    auto read = [&](unsigned p, void *out, unsigned bytes) {
        return app.cpu->Read(p, out, bytes) == X86::CpuError::Ok;
    };
    if (app.cpu->Get(X86::Reg::Esp, sp) != X86::CpuError::Ok ||
        !read(sp + 4, &address, 4) || !read(address, header, 4) ||
        header[0] < 40 || header[0] > sizeof header || (header[0] & 3) ||
        !read(address, header, header[0])) return;
    printf("PROBE_PROPSHEET address=%08x header=", address);
    for (unsigned i = 0; i < header[0] / 4; ++i) printf("%08x,", header[i]);
    putchar('\n');
    if (header[6] > 16) return;
    unsigned page_address = header[8];
    for (unsigned i = 0; i < header[6]; ++i) {
        unsigned page[16] = {};
        if (!(header[1] & 8)) {
            if (!read(page_address + i * 4, page, 4)) return;
            printf("PROBE_PROPPAGE index=%u handle=%08x\n", i, page[0]);
            continue;
        }
        if (!read(page_address, page, 4) || page[0] < 40 ||
            page[0] > sizeof page || (page[0] & 3) ||
            !read(page_address, page, page[0])) return;
        printf("PROBE_PROPPAGE index=%u address=%08x data=", i, page_address);
        for (unsigned n = 0; n < page[0] / 4; ++n) printf("%08x,", page[n]);
        putchar('\n');
        page_address += page[0];
    }
}
static void snapshot_delay_failure(X86::Application &app)
{
    if (!app.cpu || app.Status() != X86::Application::Failed ||
        (!strstr(app.Error(), "RaiseException") && !strstr(app.Error(), "Unhandled exception")))
        return;
    // Inspect the stopped guest without calling Application::Read, whose
    // failure path changes runtime state. DelayLoadInfo is nine x86 DWORDs.
    unsigned sp = 0, stack[5] = {}, address = 0, info[9] = {}, code = 0, at = 0, count = 0;
    auto read = [&](unsigned p, void *out, unsigned bytes) {
        return app.cpu->Read(p, out, bytes) == X86::CpuError::Ok;
    };
    // Two stops carry the same structure: a RaiseException call that is still
    // on the guest stack, and an exception the guest never handled, whose first
    // parameter the runtime names in its failure message.
    if (sscanf(app.Error(), "Unhandled exception %x at %x parameters %u first %x",
               &code, &at, &count, &address) != 4 || count != 1 || !address) {
        address = 0;
        if (app.cpu->Get(X86::Reg::Esp, sp) != X86::CpuError::Ok ||
            !read(sp, stack, sizeof stack) || stack[3] != 1 ||
            !read(stack[4], &address, 4))
            return;
        code = stack[1];
    }
    if (!read(address, info, sizeof info) ||
        info[0] != sizeof info || info[4] > 1 || (info[8] != 126 && info[8] != 127))
        return;
    char dll[260] = {};
    unsigned i = 0;
    for (; i < sizeof dll; ++i)
        if (!read(info[3] + i, dll + i, 1)) return;
        else if (!dll[i]) break;
    if (i == sizeof dll) return;
    printf("PROBE_DELAY exception=%08x thunk=%08x by_name=%u import=%08x last_error=%u dll=",
           code, info[2], info[4], info[5], info[8]);
    hex_text(dll);
    putchar('\n');
    if (info[4]) {
        char name[260] = {};
        for (unsigned n = 0; n < sizeof name; ++n) {
            if (!read(info[5] + n, name + n, 1)) return;
            if (!name[n]) {
                printf("PROBE_DELAY_NAME name=");
                hex_text(name);
                putchar('\n');
                break;
            }
        }
    }
}
int main(int argc, char **argv)
{
    if (argc < 3)
        return 2;
    const char *command = nullptr, *accept_caption = nullptr, *dialog_screenshot = nullptr;
    const char *final_screenshot = nullptr, *input_screenshot = nullptr;
    for (int i = 3; i < argc; i++)
    {
        if (!strcmp(argv[i], "--input-screenshot")) {
            if (++i == argc) return 2;
            input_screenshot = argv[i];
            continue;
        }
        if (!strcmp(argv[i], "--screenshot")) {
            if (++i == argc) return 2;
            final_screenshot = argv[i];
            continue;
        }
        if (!strcmp(argv[i], "--accept-dialog-caption") || !strcmp(argv[i], "--dialog-screenshot")) {
            const bool accept = !strcmp(argv[i], "--accept-dialog-caption");
            if (++i == argc) return 2;
            (accept ? accept_caption : dialog_screenshot) = argv[i];
            continue;
        }
        if (!strcmp(argv[i], "--command"))
        {
            if (++i == argc)
                return 2;
            command = argv[i];
            continue;
        }
        auto bytes = load(argv[i]);
        char guest[260];
        snprintf(guest, sizeof guest, "C:\\WINDOWS\\SYSTEM\\%s", leaf(argv[i]));
        if (bytes.empty() || Files::System().Seed(guest, bytes.data(), bytes.size()))
            return 3;
    }
    auto exe = load(argv[1]), font = load(argv[2]);
    auto *a = new X86::Application(logger);
    observed_app = a;
    const bool started = a->Start(exe.data(), exe.size(), font.data(), font.size(), leaf(argv[1]), command);
    if(started)a->cpu->AddTrace(X86::TraceKind::Instruction,0x70000000,0x7000ffff,trace_control,a);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    if (started)
        while (a->Status() != X86::Application::Exited && a->Status() != X86::Application::Failed &&
               std::chrono::steady_clock::now() < deadline)
        {
            a->Pump();
            if (const auto dialog = visible_dialog(accept_caption)) {
                if (!screenshot(dialog_screenshot)) return 4;
                printf("PROBE_ACCEPT hwnd=%u caption=", dialog); hex_text(accept_caption); putchar('\n');
                a->RawKey(13, 0, true);
                a->RawKey(13, 0, false);
                accept_caption = nullptr; // One explicitly selected dialog per observation.
                continue;
            }
            if (a->WaitingForInput())
                break;
            if (a->Status() == X86::Application::Idle)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    printf("PROBE started=%u state=%u code=%u calls=%u callbacks=%u windows=%u error=%s\n", started,
           a->Status(), a->ExitCode(), a->ApiCalls(), a->Callbacks(), User::LiveWindows(), a->Error());
    snapshot_delay_failure(*a);
    if (started)
        snapshot_windows(); // Native read-only inspection; no window callbacks are entered.
    unsigned live_gdi=0;for(unsigned i=1;i<=128;++i)for(unsigned g=0;g<4096;++g)if(Gdi::GetObjectType((g<<8)|i)){++live_gdi;break;}printf("CONTROL_GDI live=%u capacity=128\n",live_gdi);
    if (!screenshot(final_screenshot)) return 4;
    if(started&&getenv("COMPOSITION_INPUT")){
        unsigned handles[192],count=0,lower=0;User::WindowSnapshot(User::GetDesktopWindow(),handles,192,count);
        for(unsigned i=0;i<count;++i)if(User::ControlID(handles[i])==11)lower=handles[i];
        if(!lower)return 7;
        Gdi::RECT wr{};User::GetWindowRect(lower,&wr);
        auto pixel=[&](){User::Render();const auto*p=User::Pixels()+((wr.top+10)*800+wr.left+10)*4;return unsigned(p[2])|(unsigned(p[1])<<8)|(unsigned(p[0])<<16);};
        auto initial=User::Style(lower);unsigned clipped=pixel();User::SetStyle(lower,initial&~0x4000000u);unsigned plain=pixel();User::SetStyle(lower,initial);unsigned restored=pixel();
        printf("COMPOSITION_INPUT clipped=%06x plain=%06x restored=%06x\n",clipped,plain,restored);
        if(clipped!=0xff||plain!=0xff0000||restored!=0xff)return 8;
    }
    if(started&&getenv("CONTROL_INPUT")){
        unsigned handles[192],count=0,combo=0;User::WindowSnapshot(User::GetDesktopWindow(),handles,192,count);
        for(unsigned i=0;i<count;++i)if(User::CommonControl(handles[i])==6&&User::IsVisible(handles[i])){combo=handles[i];break;}
        unsigned before=a->Send(combo,0x147,0,0);Gdi::RECT rect{};User::GetWindowRect(combo,&rect);
        a->Mouse(rect.right-9,(rect.top+rect.bottom)/2,true,false);a->Mouse(rect.right-9,(rect.top+rect.bottom)/2,false,true);
        for(unsigned i=0;i<12;++i)a->Pump();
        unsigned list=0;User::WindowSnapshot(User::GetDesktopWindow(),handles,192,count);
        for(unsigned i=0;i<count;++i)if(User::CommonControl(handles[i])==4&&User::Owner(handles[i])==combo&&User::IsVisible(handles[i])){list=handles[i];break;}
        if(!list){printf("CONTROL_INPUT FAIL no popup combo=%u\n",combo);return 5;}
        unsigned target=before+1,storage=a->Allocate(sizeof rect);a->Send(list,0x198,target,storage);a->Read(storage,&rect,sizeof rect);a->Free(storage);
        Gdi::POINT point{(rect.left+rect.right)/2,(rect.top+rect.bottom)/2};Gdi::POINT origin{};User::ScreenToClient(list,&origin);point.x-=origin.x;point.y-=origin.y;
        a->Mouse(point.x,point.y,true,false);a->Mouse(point.x,point.y,false,true);for(unsigned i=0;i<12;++i)a->Pump();
        unsigned after=a->Send(combo,0x147,0,0);printf("CONTROL_INPUT combo=%u popup=%u point=%d,%d before=%u target=%u after=%u popup_visible=%u state=%u\n",combo,list,point.x,point.y,before,target,after,User::IsVisible(list),a->Status());
        char text[256]{};User::GetWindowText(combo,text,sizeof text);
        printf("CONTROL_TEXT text=");hex_text(text);printf(" calls=%u callbacks=%u\n",a->ApiCalls(),a->Callbacks());
        if (!screenshot(input_screenshot)) return 4;
        if(after!=target||User::IsVisible(list)||a->Status()==X86::Application::Failed)return 6;
    }
    observed_app = nullptr;
    delete a;
    return 0;
}
