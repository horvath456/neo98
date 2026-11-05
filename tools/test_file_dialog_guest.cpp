// SPDX-License-Identifier: GPL-2.0-or-later
#define private public
#include "../src/x86_app.h"
#include "fixture_native_modules.h"
#undef private
#include "../src/gdi_font_catalog.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
static std::vector<unsigned char>load(const char*p){FILE*f=fopen(p,"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);std::vector<unsigned char>b(n);assert(fread(b.data(),1,n,f)==size_t(n));fclose(f);return b;}
int main(int argc,char**argv){assert(argc==3||argc==5);if(argc==5){auto common=load(argv[3]);Files::System().MakeDirectory("C:\\WINDOWS");Files::System().MakeDirectory("C:\\WINDOWS\\SYSTEM");assert(!Files::System().Seed("C:\\WINDOWS\\SYSTEM\\COMDLG32.DLL",common.data(),common.size()));}auto exe=load(argv[1]),font=load(argv[2]);if(argc==5){auto sans=load(argv[4]);assert(GdiFonts::InstallFontFile("SSERIFE.FON",sans.data(),sans.size())==6);assert(GdiFonts::InstallFnt("FIXEDSYS",font.data(),font.size(),true));GdiFonts::SetRole(GdiFonts::FixedRole,"FIXEDSYS");Gdi::SetDefaultFont(DibText::OriginalFont(0).data,DibText::OriginalFont(0).length);}X86::Application app(nullptr);assert(FixtureModules::KeepNative(app));assert(app.Start(exe.data(),exe.size(),font.data(),font.size(),"FILEDLG.EXE"));unsigned previous=0,actions=0,total=0;
 for(unsigned turn=0;turn<3000&&app.Status()!=X86::Application::Exited&&app.Status()!=X86::Application::Failed;++turn){app.Pump(32);if(!app.WaitingForInput())continue;
  unsigned windows[192],count=0;User::WindowSnapshot(0,windows,192,count);unsigned dialog=0;char caption[128]{};
  for(unsigned i=0;i<count;++i){char text[128]{};User::GetWindowText(windows[i],text,sizeof text);if(!strncmp(text,"File family ",12)&&User::IsVisible(windows[i])){dialog=windows[i];memcpy(caption,text,128);break;}}
  if(!dialog)continue;if(dialog!=previous){previous=dialog;actions=0;assert(User::GetFocus()==User::GetDlgItem(dialog,1152));printf("File dialog input hwnd=%u focus=%u title=%s\n",dialog,User::GetFocus(),caption);}
  if(actions>1)continue;
  if(!strcmp(caption,"File family cancel")){app.RawKey(27,0,true);app.RawKey(27,0,false);actions=2;++total;continue;}
  if(!actions){const char*name=!strcmp(caption,"File family open")?"written.txt":!strcmp(caption,"File family small")?"too-long":!strcmp(caption,"File family destroy")?"destroyed":"written";unsigned p=app.String(name);User::PostMessage(User::GetDlgItem(dialog,1152),12,0,p);app.Pump(16);app.Free(p);}
  app.RawKey(13,13,true);app.RawKey(13,0,false);++actions;++total;
 }
 if(app.Status()!=X86::Application::Exited||app.ExitCode())fprintf(stderr,"FILE DIALOG state=%u exit=%u calls=%u error=%s\n",app.Status(),app.ExitCode(),app.ApiCalls(),app.Error());
 assert(app.Status()==X86::Application::Exited&&!app.ExitCode());unsigned node;assert(!Files::System().Lookup("C:\\FDTEST\\written.txt",node));auto*file=Files::System().Get(node);assert(file&&file->size==9&&!memcmp(file->data,"real file",9));printf("File dialog PASS calls=%u callbacks=%u actions=%u real_bytes=%u\n",app.ApiCalls(),app.Callbacks(),total,file->size);
}
