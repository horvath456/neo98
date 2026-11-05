// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/x86_app.h"
#include "fixture_native_modules.h"
#include "host_volume.h"
#include "input_contract_driver.h"
#include "crt_exit_contract.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
static std::vector<unsigned char> load(const char *path){FILE *file=fopen(path,"rb");assert(file);fseek(file,0,SEEK_END);long n=ftell(file);rewind(file);std::vector<unsigned char> data(n);assert(fread(data.data(),1,n,file)==(unsigned)n);fclose(file);return data;}
static bool gdi_called=false;
// ExitWindowsEx hands the finished session to a host provider. The runner is
// not a host, so its provider only records the request; that keeps the whole
// USER side of the export testable without leaving the process.
static unsigned session_end_calls=0, session_end_flags=0, session_end_code=0;
static void on_session_end(void *, X86::Application &, unsigned flags, unsigned code)
{ ++session_end_calls; session_end_flags = flags; session_end_code = code; }
static void log(const char *text){if(!strcmp(text,"ExtTextOutA"))gdi_called=true;puts(text);fflush(stdout);}
static Gdi::POINT clientPoint(unsigned window, int x, int y)
{
    Gdi::POINT point = {x, y};
    const bool mapped = ContractClientToScreen(window, point);
    assert(mapped);
    return point;
}
static unsigned pixel(const unsigned char *pixels, int x, int y)
{
    assert(x >= 0 && x < 800 && y >= 0 && y < 600);
    const auto *p = pixels + (y * 800 + x) * 4;
    return unsigned(p[2]) | (unsigned(p[1]) << 8) | (unsigned(p[0]) << 16);
}
static unsigned clientPixel(const unsigned char *pixels, unsigned window, int x, int y)
{
    const auto point = clientPoint(window, x, y);
    return pixel(pixels, point.x, point.y);
}
// A screenshot may only be taken from a settled frame: the guest is parked in
// the handshake of tools/capture_handshake.h with nothing left to answer, and
// everything it drew has reached the composed image. The capture used to fire
// as soon as the window existed and to rely on the composition having happened
// by then, which under load it had not.
static bool settled(X86::Application &app)
{
    if (!app.WaitingForInput())
        return false;
    for (unsigned n = 0; n < 64 && User::Render(); ++n)
        ;
    return !User::Render();
}
// The window and its children have nothing left to paint.
static bool drawn(unsigned window)
{
    Gdi::RECT update;
    if (!window || User::GetUpdateRect(window, &update))
        return false;
    for (unsigned child = User::GetWindow(window, 5); child; child = User::GetWindow(child, 2))
        if (User::GetUpdateRect(child, &update))
            return false;
    return true;
}
int main(int argc,char **argv){
    assert(argc >= 5);
    // The same contract runs on either volume provider: without --drive-c on
    // the RAM volume, with it on a writable host directory.
    //
    {
        auto drives = HostVolume::Scan(argc, argv);
        drives.ram = drives.drive_c == nullptr;
        assert(HostVolume::Mount(drives));
        if (!drives.ram) printf("COMMON DRIVE C %s\n", drives.drive_c);
    }
    auto exe = load(argv[1]), font = load(argv[2]);
    auto *app = new X86::Application(log);
    // COMMON.EXE is the fixture of our own implementations, so this run keeps
    // the five DLLs of PLAN.md step 7 on their native providers; every other
    // run takes the original x86 code.
    assert(FixtureModules::KeepNative(*app));
    X86::Application::SetSessionEndHook(nullptr, on_session_end);
    assert(app->Start(exe.data(), exe.size(), font.data(), font.size(), "COMMON.EXE"));
    bool captured_windows = false, captured = false, captured_list = false, captured_gdi = false, captured_paint = false, captured_listbox = false;
    for(unsigned i=0;i<100000&&app->Status()!=X86::Application::Exited&&app->Status()!=X86::Application::Failed;++i){
        auto state=app->Pump();
        if(state==X86::Application::Idle){
            InputContractDriver(*app);
            MouseFocusContractDriver(*app);
            if (!captured_windows && User::FindWindow("NEO98WindowContract","Window ordering and desktop") && settled(*app))
            {
                const auto *pixels=User::Pixels();
                Gdi::POINT button, background;
                const bool mapped = WindowContractPoints(button, background);
                assert(mapped && pixel(pixels, background.x, background.y) == 0xff0000);
                std::string file=argv[3];file.erase(file.find_last_of('/')+1);file+="windows.ppm";
                FILE *out=fopen(file.c_str(),"wb");assert(out);fprintf(out,"P6\n800 600\n255\n");
                for(unsigned n=0;n<800*600;++n){unsigned char rgb[]={pixels[n*4+2],pixels[n*4+1],pixels[n*4]};fwrite(rgb,1,3,out);}fclose(out);
                app->Mouse(button.x,button.y,true,false);app->Mouse(button.x,button.y,false,true);
                app->Mouse(background.x,background.y,true,false);app->Mouse(background.x,background.y,false,true);
                captured_windows=true;
            }
            const auto listbox_parent = User::FindWindow("NEO98ListBoxContract","Original USER listbox geometry");
            if (!captured_listbox && listbox_parent && drawn(User::GetDlgItem(listbox_parent, 102)) &&
                drawn(User::GetDlgItem(listbox_parent, 103)) && settled(*app))
            {
                const auto *pixels=User::Pixels();
                const auto parent = listbox_parent;
                const auto multi = User::GetDlgItem(parent, 102), variable = User::GetDlgItem(parent, 103);
                // Owner-drawn rows 0 and 2 are selected; the variable-height row is not.
                assert(clientPixel(pixels,multi,86,5)==0x800000 &&
                       clientPixel(pixels,multi,86,45)==0x800000 &&
                       clientPixel(pixels,variable,156,5)==0xc0c0c0);
                std::string file=argv[3];file.erase(file.find_last_of('/')+1);file+="listbox.ppm";
                FILE *out=fopen(file.c_str(),"wb");assert(out);fprintf(out,"P6\n800 600\n255\n");
                for(unsigned n=0;n<800*600;++n){unsigned char rgb[]={pixels[n*4+2],pixels[n*4+1],pixels[n*4]};fwrite(rgb,1,3,out);}fclose(out);
                unsigned frame=User::FrameSerial();assert(!User::Render()&&frame==User::FrameSerial());
                assert(User::PostMessage(parent,0x8f00,0,0));
                captured_listbox=true;
            }
            if (!captured_paint && drawn(User::FindWindow("NEO98PaintContract","Win98 paint protocol")) && settled(*app))
            {
                const auto *pixels=User::Pixels();
                const auto window = User::FindWindow("NEO98PaintContract", "Win98 paint protocol");
                // Match the fixture's damaged region, excluded hole, and untouched background.
                assert(clientPixel(pixels,window,11,11)==0xff &&
                       clientPixel(pixels,window,20,30)==0x800000 &&
                       clientPixel(pixels,window,80,80)==0x800000);
                std::string file=argv[3];file.erase(file.find_last_of('/')+1);file+="paint-protocol.ppm";
                FILE *out=fopen(file.c_str(),"wb");assert(out);fprintf(out,"P6\n800 600\n255\n");
                for(unsigned n=0;n<800*600;++n){unsigned char rgb[]={pixels[n*4+2],pixels[n*4+1],pixels[n*4]};fwrite(rgb,1,3,out);}fclose(out);
                unsigned frame=User::FrameSerial();assert(!User::Render()&&frame==User::FrameSerial());
                assert(User::PostMessage(window,0x8f00,0,0));
                captured_paint=true;
            }
            // Not gated on a settled frame: gdi_fixture.c draws straight into
            // GetDC(0), and the next composition of the desktop paints over it.
            if (gdi_called && !captured_gdi)
            {
                const auto *pixels=User::Pixels();
                // gdi_fixture.c draws into GetDC(0), with viewport origin (40,50).
                assert(pixel(pixels,46,52)==0xff&&pixel(pixels,47,52)==0x112233&&pixel(pixels,66,52)==0xffffff&&pixel(pixels,52,62)==0x112233&&pixel(pixels,46,56)==0xffffff);
                std::string file=argv[3];file.erase(file.find_last_of('/')+1);file+="gdi-abi.ppm";
                FILE *out=fopen(file.c_str(),"wb");assert(out);fprintf(out,"P6\n800 600\n255\n");
                for(unsigned n=0;n<800*600;++n){unsigned char rgb[]={pixels[n*4+2],pixels[n*4+1],pixels[n*4]};fwrite(rgb,1,3,out);}fclose(out);
                captured_gdi=true;
            }
            unsigned parent=User::FindWindow("NEO98TreeContract","Native TreeView");
            if(parent&&!captured&&drawn(User::GetDlgItem(parent,98))&&settled(*app)){
                const auto tree = User::GetDlgItem(parent, 98);
                assert(User::CommonControl(tree)==1);
                const auto *pixels=User::Pixels();unsigned blue=0,ink=0;
                const auto origin = clientPoint(tree, 0, 0);
                for(int y=2;y<58;++y)for(int x=21;x<241;++x){const auto color=pixel(pixels,origin.x+x,origin.y+y);blue+=color==0x800000;ink+=color==0;}
                assert(blue>100&&ink>100);
                FILE *out=fopen(argv[3],"wb");assert(out);fprintf(out,"P6\n800 600\n255\n");
                for(unsigned n=0;n<800*600;++n){unsigned char rgb[]={pixels[n*4+2],pixels[n*4+1],pixels[n*4]};fwrite(rgb,1,3,out);}fclose(out);captured=true;
                unsigned frame=User::FrameSerial();assert(!User::Render()&&frame==User::FrameSerial());
                assert(User::PostMessage(parent,0x8f00,0,0));
            }
            unsigned list_parent = User::FindWindow("NEO98ListContract", "Native ListView");
            if (list_parent && !captured_list && drawn(User::GetDlgItem(list_parent, 99)) && settled(*app))
            {
                const auto list = User::GetDlgItem(list_parent, 99);
                assert(User::CommonControl(list) == 2);
                assert(User::CommonControl(User::GetDlgItem(list_parent, 100)) == 3);
                const auto *pixels = User::Pixels();
                const auto origin = clientPoint(list, 0, 0);
                unsigned blue = 0, ink = 0;
                for (int y = 21; y < 60; ++y)
                    for (int x = 6; x < 411; ++x)
                    {
                        const auto color = pixel(pixels, origin.x + x, origin.y + y);
                        blue += color == 0x800000;
                        ink += color == 0;
                    }
                assert(blue > 100 && ink > 100);
                FILE *out = fopen(argv[4], "wb");
                assert(out);
                fprintf(out, "P6\n800 600\n255\n");
                for (unsigned n = 0; n < 800 * 600; ++n)
                {
                    unsigned char rgb[] = {pixels[n * 4 + 2], pixels[n * 4 + 1], pixels[n * 4]};
                    fwrite(rgb, 1, 3, out);
                }
                fclose(out);
                assert(User::PostMessage(list_parent,0x8f00,0,0));
                captured_list = true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    printf("COMMON APP state=%u code=%u calls=%u callbacks=%u tree_rendered=%u list_rendered=%u gdi_rendered=%u paint_rendered=%u listbox_rendered=%u windows_rendered=%u error=%s\n",
           app->Status(), app->ExitCode(), app->ApiCalls(), app->Callbacks(), captured, captured_list, captured_gdi, captured_paint, captured_listbox, captured_windows,
           app->Error());
    assert(app->Status() == X86::Application::Exited && app->ExitCode() == 0 && captured && captured_list && captured_gdi && captured_paint && captured_listbox && captured_windows);
    assert(CrtExitContractResult());
    // The fixture denies one end of session and agrees to the next; only the
    // second one reaches the provider, with EWX_SHUTDOWN and ExitKernel code 0.
    printf("COMMON EXITWINDOWS calls=%u flags=%08x code=%u\n", session_end_calls, session_end_flags, session_end_code);
    assert(session_end_calls == 1 && session_end_flags == 1 && session_end_code == 0);
    X86::Application::SetSessionEndHook(nullptr, nullptr);
    delete app;
    assert(!User::LiveWindows() && !Gdi::LiveObjects());
    std::string lifetime_path = argv[1];
    lifetime_path.erase(lifetime_path.find_last_of('/') + 1);
    lifetime_path += "ATOMS-LIFETIME.EXE";
    const auto lifetime = load(lifetime_path.c_str());
    for (unsigned phase = 0; phase < 2; ++phase)
    {
        auto *next = new X86::Application(log);
        assert(next->Start(lifetime.data(), lifetime.size(), font.data(), font.size(), "ATOMS-LIFETIME.EXE"));
        for (unsigned i = 0; i < 1000 && next->Status() != X86::Application::Exited &&
                                 next->Status() != X86::Application::Failed; ++i)
            next->Pump();
        assert(next->Status() == X86::Application::Exited && next->ExitCode() == 98 + phase);
        delete next;
        assert(!User::LiveWindows() && !Gdi::LiveObjects());
    }
    puts("GLOBAL ATOM PROCESS LIFETIME PASS desktop-properties=clean local-atoms=private");
}
