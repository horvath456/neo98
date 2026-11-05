// SPDX-License-Identifier: GPL-2.0-or-later
// Guest address-space and callback adapter for implemented COMCTL32 code.
#include "win98_arrays.h"
#include "win98_printf.h"
#include "win98_strings.h"
#include "win98_paths.h"
#include "win98_shell_compare.h"
#include "win98_shell_number.h"
#include "win98_shell_copy.h"
#include <string.h>
#include <stdio.h>
#include "x86_app.h"
#include "win98_menu_help.h"
#include "win98_print_dialog.h"
namespace X86 {
#include "x86_print_dialog.inc"
#include "x86_shell_compare.inc"
#include "x86_shell_number.inc"
#include "x86_shell_copy.inc"
struct Application::CommonMemory final : Win98Strings::Environment {
  Application &app;
  explicit CommonMemory(Application &a) : app(a) {}
  bool Read(unsigned p, void *out, unsigned n) override {
    return app.Read(p, out, n);
  }
  bool Write(unsigned p, const void *data, unsigned n) override {
    return app.Store(p, data, n);
  }
  unsigned Resize(unsigned heap, unsigned p, unsigned n) override {
    if (heap != 1) {
      app.Fail("Unsupported COMCTL32 heap handle");
      return 0;
    }
    if (p) {
      unsigned args[] = {p, n, 2 | 64};
      return app.Api("LocalReAlloc", args);
    }
    unsigned args[] = {64, n};
    return app.Api("LocalAlloc", args);
  }
  bool Free(unsigned heap, unsigned p) override {
    if (heap != 1) {
      app.Fail("Unsupported COMCTL32 heap handle");
      return false;
    }
    return app.Free(p);
  }
  int Compare(unsigned proc, unsigned a, unsigned b,
              unsigned parameter) override {
    unsigned args[] = {a, b, parameter};
    return (int)app.Call(proc, args, 3);
  }
  bool LeadByte(unsigned) override { return false; } // Current native ACP is 1252.
  unsigned Next(unsigned p) override { return app.Api("CharNextA", &p); }
  bool Readable(unsigned p) override {
    unsigned char byte = 0;
    return app.cpu->Read(p, &byte, 1) == CpuError::Ok;
  }
  unsigned ToWide(unsigned flags, unsigned source, unsigned output, unsigned capacity) override {
    unsigned args[] = {0, flags, source, 0xffffffff, output, capacity};
    return app.NlsApi(594, args);
  }
  unsigned Local(unsigned bytes) override {
    unsigned args[] = {0, bytes};
    return app.Api("LocalAlloc", args);
  }
  void ReleaseLocal(unsigned p) override { app.Api("LocalFree", &p); }
};
unsigned Application::PathApi(ApiName name, const unsigned *args)
{
    for(const auto &entry:Win98ShellCompare::Exports)if(!strcmp(name,entry.name))return ShellCompareApi(name,args);
    for(const auto &entry:Win98ShellNumber::Exports)if(!strcmp(name,entry.name))return ShellNumberApi(name,args);
    for(const auto &entry:Win98ShellCopy::Exports)if(!strcmp(name,entry.name))return ShellCopyApi(name,args);
    CommonMemory memory(*this);
    switch (name.id)
    {
    API_CASE("GetFileTitleA")
    {
        unsigned error=~0u;unsigned result=Win98Paths::FileTitle(memory,args,error);
        if(error!=~0u)CommonDialogError(error);
        return result;
    }
    API_CASE("PathAddBackslashA") API_CASE("ordinal_451")
        return Win98Paths::Call(memory, 451, args, last_error);
    API_CASE("PathCanonicalizeA") API_CASE("ordinal_459")
        return Win98Paths::Call(memory, 459, args, last_error);
    API_CASE("PathCombineA") API_CASE("ordinal_461")
        return Win98Paths::Call(memory, 461, args, last_error);
    API_CASE("PathFindExtensionA") API_CASE("ordinal_473")
        return Win98Paths::Call(memory, 473, args, last_error);
    API_CASE("PathFindFileNameA") API_CASE("ordinal_475")
        return Win98Paths::Call(memory, 475, args, last_error);
    API_CASE("PathIsRelativeA") API_CASE("ordinal_503")
        return Win98Paths::Call(memory, 503, args, last_error);
    API_CASE("PathIsRootA") API_CASE("ordinal_505")
        return Win98Paths::Call(memory, 505, args, last_error);
    API_CASE("PathIsUNCA") API_CASE("ordinal_511")
        return Win98Paths::Call(memory, 511, args, last_error);
    API_CASE("PathRemoveFileSpecA") API_CASE("ordinal_539")
        return Win98Paths::Call(memory, 539, args, last_error);
    API_CASE("PathSkipRootA") API_CASE("ordinal_547")
        return Win98Paths::Call(memory, 547, args, last_error);
    API_CASE("PathStripToRootA") API_CASE("ordinal_551")
        return Win98Paths::Call(memory, 551, args, last_error);
    default:
        break;
    }
    Fail("Unsupported SHLWAPI path export");
    return 0;
}
unsigned Application::Printf(unsigned output, unsigned format, unsigned arguments)
{
    CommonMemory memory(*this);
    // The table the original walks is not in KERNEL32 itself: the
    // loader hands its address to the process, and it holds the ranges of the
    // ANSI code page without best-fit entries. The implementation asks the NLS
    // service for the same code page and treats a character that does not
    // survive the round trip as one of no range, which is the underscore case.
    struct CodePage final : Win98Printf::Ansi
    {
        Application &app;
        explicit CodePage(Application &a) : app(a) {}
        unsigned Narrow(unsigned character, unsigned char *bytes) override
        {
            if (!app.InitializeNls())
                return 0;
            const unsigned byte = app.AnsiNarrow(character);
            if (!byte || app.AnsiWide(byte) != character)
                return 0;
            bytes[0] = (unsigned char)byte;
            return 1;
        }
    } ansi(*this);
    bool supported = false;
    unsigned result = Win98Printf::Format(memory, output, format, arguments, supported, &ansi);
    if (!supported)
    {
        char text[160] = {};
        char name[96] = {};
        for (unsigned i = 0; i + 1 < sizeof name; ++i)
        {
            unsigned char c = 0;
            if (!Read(format + i, &c, 1) || !c)
                break;
            name[i] = c >= 32 ? (char)c : '.';
        }
        snprintf(text, sizeof text, "Wide wsprintf conversion not implemented: \"%s\"", name);
        Fail(text);
    }
    return result;
}
unsigned Application::CommonControlApi(unsigned ordinal, const unsigned *args) {
    if(ordinal==2){
        struct MenuHelpEnvironment final:Win98MenuHelp::Environment {
            Application&app;explicit MenuHelpEnvironment(Application&a):app(a){}
            unsigned CommonModule()override{unsigned n=app.FindModule("COMCTL32.DLL");return n<Application::ModuleCapacity?app.modules[n].handle:0;}
            bool Read(unsigned p,unsigned&v)override{return app.Read(p,&v,4);}
            bool ItemType(unsigned menu,unsigned index,unsigned&type)override{Win98Menu::Info info;info.size=44;info.mask=0x10;bool okay=User::MenuItemInfo(menu,index,0x400,info,0);type=info.type;return okay;}
            bool LastIsRestore(unsigned menu)override{return User::MenuItemID(menu,User::MenuItemCount(menu)-1)==0xf120;}
            void LoadString(unsigned module,unsigned id,unsigned short(&text)[256])override{
                auto half=[](const unsigned char*p){return unsigned(p[0])|(unsigned(p[1])<<8);};unsigned index=module?app.ModuleIndex(module,true):0;if(index>=Application::ModuleCapacity||!app.modules[index].pe)return;
                unsigned size=0;const auto*p=app.modules[index].pe->Resource(6,(id&65535)/16+1,&size);if(!p)return;unsigned at=0;
                for(unsigned i=0;i<16;++i){if(at+2>size)return;unsigned n=half(p+at);at+=2;if(n>(size-at)/2)return;if(i==(id&15)){if(n>255)n=255;for(unsigned j=0;j<n;++j)text[j]=half(p+at+j*2);text[n]=0;return;}at+=n*2;}
            }
            void Text(unsigned h,unsigned flags,const unsigned short*text)override{unsigned p=app.Allocate(512);if(!p)return;if(app.Store(p,text,512))app.Send(h,0x40b,flags,p);app.Free(p);}
            void Simple(unsigned h,bool simple)override{app.Send(h,0x409,simple,0);}
            void Update(unsigned h)override{app.Api("UpdateWindow",&h);}
        } e(*this);Win98MenuHelp::Help(e,args);return 0; // Original export has void return type.
    }

    if (ordinal == 6)
    {
        // Original Windows 98 removes WS_BORDER and CCS_NOMOVEY before creation.
        unsigned klass = String("msctls_statusbar32");
        unsigned create[] = {0,
                             klass,
                             args[1],
                             args[0] & 0xff7fffbfu,
                             (unsigned)-100,
                             (unsigned)-100,
                             10,
                             10,
                             args[2],
                             args[3],
                             image.Base(),
                             0};
        unsigned h = CreateWindow(create);
        Free(klass);
        return h;
    }
    if (ordinal == 4)
    {
        // Original : skip the first info pair, then subtract visible bars.
        Gdi::RECT r = {};
        User::GetClientRect(args[0], &r);
        for (unsigned i = 0; i < 128; ++i)
        {
            unsigned p = args[2] + 8 + i * 8;
            if (!Read32(p))
            {
                Store(args[1], &r, sizeof r);
                return 0;
            }
            unsigned h = User::GetDlgItem(args[0], Read32(p + 4));
            if (h && (User::Style(h) & 0x10000000))
            {
                Gdi::RECT child = {};
                User::GetWindowRect(h, &child);
                Gdi::POINT a = {child.left, child.top}, b = {child.right, child.bottom};
                User::ScreenToClient(args[0], &a);
                User::ScreenToClient(args[0], &b);
                child = {a.x, a.y, b.x, b.y};
                Win98::SubtractRect(&r, &r, &child);
            }
        }
        Fail("Unterminated effective-client control list");
        return 0;
    }
  CommonMemory memory(*this);
  if ((ordinal >= 71 && ordinal <= 73) || ordinal == 350 || ordinal == 357 || ordinal == 365)
      return Win98Strings::Helper(memory, ordinal, args);
  if (ordinal >= 233 && ordinal <= 236)
      return Win98Strings::Call(memory, ordinal, args);
  if (ordinal >= 320 && ordinal <= 340)
    return Win98Arrays::Call(memory, ordinal, args);
  Fail("Unsupported COMCTL32 ordinal");
  return 0;
}
} // namespace X86

#include "x86_toolbar.inc"

#include "x86_property_pages.inc"
