// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/user.h"
#include <cstdio>
using namespace User;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"USER check failed at %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main()
{
    CHECK(Gdi::LiveObjects()==0&&Initialize());
    CHECK(RegisterClass("Example",0x401000,0,0));CHECK(!RegisterClass("example",0x402000,0,0));
    const auto main=CreateWindow("Example","Application",0,10,10,500,400,0,0,123);
    CHECK(main&&Procedure(main)==0x401000&&Parameter(main)==123);
    const auto child=CreateWindow("EDIT","abc",0x50000004,0,0,492,369,main,99,0);
    CHECK(child&&IsEdit(child)&&Parent(child)==main&&GetDlgItem(main,99)==child);
    CHECK(!CreateWindow("Missing","",0,0,0,1,1,0,0,0));
    auto font=Gdi::CreateFontResource(2);CHECK(font&&SetEditFont(child,font)&&!Gdi::DeleteObject(font));
    CHECK(SetEditSelection(child,1,2));CHECK(ReplaceEditSelection(child,"XYZ",3,true));
    char text[20];CHECK(GetWindowText(child,text,20)==5&&text[1]=='X'&&text[4]=='c');
    CHECK(EditMessage(child,0xc7,0,0)&&TextLength(child)==3);
    CHECK(SetEditSelection(child,1,1));EditMessage(child,0x303,0,0);CHECK(TextLength(child)==3);
    EditMessage(child,0x300,0,0);CHECK(TextLength(child)==3);
    SetEditSelection(child,0,3);EditMessage(child,0x301,0,0);CHECK(ClipboardLength()==3);
    CHECK(SetEditFont(child,0)&&Gdi::DeleteObject(font));
    auto original=DibText::OriginalFont(0);
    auto *source=new unsigned char[original.length];
    for(unsigned i=0;i<original.length;++i)source[i]=original.data[i];
    auto owned=Gdi::CreateFontMemory(source,original.length);CHECK(owned);
    for(unsigned i=0;i<original.length;++i)source[i]=0;
    delete[] source;
    auto metrics_dc=Gdi::CreateCompatibleDC();CHECK(metrics_dc&&Gdi::SelectObject(metrics_dc,owned));
    Gdi::TEXTMETRICA metrics;CHECK(Gdi::GetTextMetricsA(metrics_dc,&metrics)&&metrics.tmHeight==13&&metrics.tmAscent+metrics.tmDescent==13);
    CHECK(!Gdi::DeleteObject(owned)&&Gdi::DeleteDC(metrics_dc)&&Gdi::DeleteObject(owned));
    Message msg;
    while(PeekMessage(&msg,0,0,0,true)){}
    SetFocus(child);Keyboard('X','x',false,false);
    CHECK(PeekMessage(&msg,0,0,0,true)&&msg.message==0x100&&msg.lparam==1);
    CHECK(TranslateMessage(&msg));CHECK(PeekMessage(&msg,0,0,0,true)&&msg.message==0x102&&msg.wparam=='x');
    // Posted messages have priority over captured input, while each category
    // keeps its own order. Filtering can still retrieve input directly.
    KeyboardRaw('X',0,false);
    KeyboardRaw('I','i',true);KeyboardRaw('I',0,false);
    KeyboardRaw(13,13,true);KeyboardRaw(13,0,false);
    CHECK(PostMessage(main,0x8000,11,0)&&PostMessage(main,0x8001,22,0));
    CHECK(PeekMessage(&msg,0,0,0,false)&&msg.message==0x8000&&msg.wparam==11);
    CHECK(GetKeyState('X')&0x8000);
    CHECK(PeekMessage(&msg,child,0x101,0x101,true)&&msg.wparam=='X');
    CHECK(!(GetKeyState('X')&0x8000));
    CHECK(PeekMessage(&msg,0,0,0,true)&&msg.message==0x8000);
    CHECK(PeekMessage(&msg,0,0,0,true)&&msg.message==0x8001);
    CHECK(PeekMessage(&msg,0,0,0,true)&&msg.message==0x100&&msg.wparam=='I');
    CHECK(TranslateMessage(&msg));
    CHECK(PeekMessage(&msg,0,0,0,true)&&msg.message==0x102&&msg.wparam=='i');
    CHECK(PostMessage(main,0x111,0x3000063,child));
    CHECK(PeekMessage(&msg,0,0,0,true)&&msg.message==0x111);
    CHECK(PeekMessage(&msg,0,0,0,true)&&msg.message==0x101&&msg.wparam=='I');
    CHECK(PeekMessage(&msg,0,0,0,true)&&msg.message==0x100&&msg.wparam==13);
    CHECK(PeekMessage(&msg,0,0,0,true)&&msg.message==0x101&&msg.wparam==13);
    for(unsigned i=0;i<256;++i)CHECK(PostMessage(main,0x400+i,i,0));
    CHECK(!PostMessage(main,0x900,0,0));CHECK(PeekMessage(&msg,main,0x480,0x480,false)&&msg.wparam==128);
    CHECK(PeekMessage(&msg,main,0x480,0x480,true));CHECK(!PeekMessage(&msg,main,0x480,0x480,true));
    while(PeekMessage(&msg,0,0,0,true)){}
    CHECK(PostMessage(0,0x12,7,0));CHECK(PeekMessage(&msg,main,0x900,0x900,true)&&msg.wparam==7);
    ShowWindow(main,1);CHECK(Render());CHECK(!Dirty()&&!Render());
    auto dc=GetDC(child);CHECK(dc&&ReleaseDC(child,dc));
    CHECK(DestroyWindow(main)&&!IsWindow(child)&&!IsWindow(main)&&LiveWindows()==0);
    auto next=CreateWindow("Example","Reused",0,0,0,100,100,0,0,0);CHECK(next&&next!=main&&!IsWindow(main));
    Shutdown();CHECK(Gdi::LiveObjects()==0&&ClipboardLength()==0);
    CHECK(Initialize());Shutdown();CHECK(Gdi::LiveObjects()==0);
    std::puts("Native USER: windows, messages, font lifetime, editing, keyboard, idle paint, cleanup passed");
}
