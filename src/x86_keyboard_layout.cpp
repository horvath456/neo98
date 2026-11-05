// SPDX-License-Identifier: GPL-2.0-or-later
#include "x86_app.h"
#include "win98_keyboard_layout.h"
#include <string.h>
namespace X86 {
bool Application::ConfigureKeyboardLayout(const char *map)
{
    if(cpu || !map)return false;
    unsigned language=0;
    if(!strcmp(map,"DE"))language=0x407;
    else if(!strcmp(map,"US"))language=0x409;
    else return false;
    keyboard_layout_state={};keyboard_layout_state.enabled=1;keyboard_layout_state.count=1;
    keyboard_layout_state.entries[0]={language|(language<<16),1,0};
    return true;
}
struct Application::KeyboardLayoutEnvironment {
    Application &app;
    unsigned temporary_name=0,snapshot_count=0,snapshot[User::WindowCapacity]{};
    bool metadata_open=false,valid=true;
    explicit KeyboardLayoutEnvironment(Application&a):app(a){}
    ~KeyboardLayoutEnvironment(){if(temporary_name)app.Free(temporary_name);}
    unsigned CurrentTask(){return app.ThreadId();}
    unsigned ResolveThread(unsigned id){for(auto&t:app.threads)if(t.id==id&&!t.done&&t.fiber)return id;return 0;}
    unsigned ThreadLayout(unsigned task){for(auto&t:app.threads)if(t.id==task)return t.keyboard_layout;return 0;}
    void SetThreadLayout(unsigned task,unsigned hkl){for(auto&t:app.threads)if(t.id==task){t.keyboard_layout=hkl;return;}}
    void StoreLayout(unsigned p,unsigned hkl){app.Write32(p,hkl);}
    unsigned FirstWindow(){return User::GetTopWindow(0);}
    Win98KeyboardLayout::Window ReadWindow(unsigned h){
        const auto w=User::ActivationWindow(h);
        return {w.next,w.owner,w.popup,w.task,w.style,User::ClassAccess(h,-32,0,0)};
    }
    unsigned ExcludedWindow(){return User::GetDesktopWindow();}
    unsigned ExcludedClass(){return 0;} // The original excluded class has no native registration.
    unsigned Focus(unsigned){return User::GetFocus();}
    bool ValidWindow(unsigned h){return User::IsWindow(h);}
    bool ShellInterested(){return false;} // HSHELL_LANGUAGE consumers are not yet registered.
    void ShellNotify(unsigned,unsigned){}
    void DriverActivate(unsigned hkl){
        // The installed input provider has exactly its configured physical map.
        // A different layout cannot enter the native installed-layout table.
        if(hkl!=app.keyboard_layout_state.entries[0].layout)app.Fail("Keyboard driver layout is unavailable");
    }
    void LanguageMessage(unsigned h,unsigned charset,unsigned hkl){app.Send(h,0x51,charset,hkl);}
    void HexName(unsigned p,unsigned value){char name[9];for(unsigned i=0;i<8;++i)name[i]="0123456789abcdef"[(value>>(28-i*4))&15];name[8]=0;app.Store(p,name,sizeof name);}
    unsigned Read8(unsigned p){unsigned char c=0;if(!app.Read(p,&c,1))valid=false;return c;}
    bool LoaderAvailable(){return true;} // The configured native USB-map provider is installed.
    unsigned ParseName(unsigned p){return Win98KeyboardLayout::HexValue(*this,p);}
    unsigned NameLength(unsigned p){return strlen(app.ReadString(p));}
    unsigned SubstituteName(unsigned p){
        unsigned key=0,node=0;bool created=false;
        if(app.registry.Open(0x80000001,"keyboard layout\\substitutes",false,key,created))return p;
        const Registry::Value *value=nullptr;
        if(!app.registry.Key(key,node) && !app.registry.store.Query(node,app.ReadString(p),value) && value->size>1 && value->size<=10){
            temporary_name=app.Allocate(value->size+1);
            if(temporary_name){app.Store(temporary_name,value->data,value->size);const unsigned char zero=0;app.Store(temporary_name+value->size,&zero,1);p=temporary_name;}
            else {valid=false;app.last_error=8;}
        }
        app.registry.Close(key);return p;
    }
    bool OpenLayoutMetadata(unsigned p){
        char expected[9];unsigned language=app.keyboard_layout_state.entries[0].layout>>16;
        for(unsigned i=0;i<8;++i)expected[i]="0123456789abcdef"[(language>>(28-i*4))&15];expected[8]=0;
        const char *name=app.ReadString(p);metadata_open=valid&&!strcmp(name,expected);
        if(!metadata_open)app.last_error=50;
        return metadata_open;
    }
    bool LayoutVariant(unsigned&){return false;} // No variant layout metadata is installed.
    unsigned LayoutFile(){return metadata_open?1:0;} // Token for the compiled USB-map backend, not a guest DLL handle.
    void CloseLayoutMetadata(){metadata_open=false;}
    bool LoadDriver(unsigned file,unsigned hkl){const bool ok=file==1&&hkl==app.keyboard_layout_state.entries[0].layout;if(!ok)app.last_error=50;return ok;}
    bool UnloadDriver(unsigned){app.last_error=50;return false;} // The sole physical input driver cannot be removed.
    bool ResizeLayoutTable(unsigned n){if(n<=16)return true;app.last_error=8;return false;}
    bool LocaleMetadata(unsigned,unsigned&,unsigned&){return false;}
    void DefaultMetadata(unsigned&signature,unsigned&charset){signature=1;charset=0;} // Native DE/US provider uses the ANSI character set.
    bool SuppressShell(){return false;}
    bool HasWindows(){return User::GetTopWindow(0)!=0;}
    unsigned Snapshot(){return User::WindowSnapshot(0,snapshot,User::WindowCapacity,snapshot_count)?1:0;}
    unsigned SnapshotEnd(){return ~0u;}
    unsigned SnapshotWindow(unsigned,unsigned i){return i<snapshot_count?snapshot[i]:SnapshotEnd();}
    void ConsumeSnapshot(unsigned,unsigned i){if(i<snapshot_count)snapshot[i]=0;}
    void FreeSnapshot(unsigned){snapshot_count=0;}
    void DriverSelect(unsigned hkl){DriverActivate(hkl);}
    unsigned VariantName(unsigned,unsigned){app.last_error=50;return 0;} // No variant keyboard driver is installed.
};
unsigned Application::KeyboardLayoutApi(ApiName name,const unsigned *a)
{
    KeyboardLayoutEnvironment e(*this);
    switch (name.id)
    {
    API_CASE("GetKeyboardLayout")return Win98KeyboardLayout::Get(e,keyboard_layout_state,a[0]);
    API_CASE("GetKeyboardLayoutList")return Win98KeyboardLayout::List(e,keyboard_layout_state,a[0],a[1]);
    API_CASE("GetKeyboardLayoutNameA")return Win98KeyboardLayout::Name(e,keyboard_layout_state,a[0]);
    API_CASE("ActivateKeyboardLayout")return Win98KeyboardLayout::Activate(e,keyboard_layout_state,a[0],a[1]);
    API_CASE("LoadKeyboardLayoutA"){
        // The original public USER entry validates the input string before
        // entering the keyboard-driver bookkeeping body.
        ReadString(a[0]);if(state==Failed)return 0;
        return Win98KeyboardLayout::Load(e,keyboard_layout_state,a[0],a[1]);
    }
    API_CASE("UnloadKeyboardLayout")return Win98KeyboardLayout::Unload(e,keyboard_layout_state,a[0]);
    default:
        break;
    }
    Fail("Unknown keyboard-layout entry");return 0;
}
} // namespace X86

#include "usb_keys.h"
#include <stdio.h>
namespace X86 {
unsigned Application::KeyboardGameApi(ApiName name,const unsigned *a){
    const bool german=(threads[current_thread].keyboard_layout&65535)==0x407;
    const auto key=[&](unsigned code,bool scan,bool extended){
        UsbKeys::Key found{};
        static const UsbKeys::Key modifiers[]={{0xa0,0x2a,false},{0xa1,0x36,false},{0xa2,0x1d,false},{0xa3,0x1d,true},{0xa4,0x38,false},{0xa5,0x38,true}};
        for(auto k:modifiers)if(scan?k.scan==code&&k.extended==extended:k.vk==code)return k;
        for(unsigned usage=4;usage<256;++usage){auto k=UsbKeys::Map(usage,german,true);
            if(k.vk&&(scan?k.scan==code&&k.extended==extended:k.vk==code)){found=k;break;}}
        return found;
    };
    switch (name.id)
    {
    API_CASE("SetCursorPos"){
        // Clamp to the guest's own current screen, not to the fixed 800x600
        // canvas of the host window. A program that switched to a smaller
        // mode (DirectDraw exclusive, ChangeDisplaySettingsA) sees
        // SM_CXSCREEN/SM_CYSCREEN and every window rect in that mode's size
        // (ApplyScreenMetrics, src/user_screen.inc); GetCursorPos and
        // SetCursorPos have to agree with that size or a program that
        // recentres on its own screen keeps a fixed bias between the real
        // pointer and the one it draws itself.
        int width=800,height=600,depth=32; User::ScreenMode(width,height,depth);
        int x=(int)a[0],y=(int)a[1];
        if(x<0)x=0;if(width>0&&x>=width)x=width-1;
        if(y<0)y=0;if(height>0&&y>=height)y=height-1;
        // clamps to the ClipCursor rectangle.
        User::ConfinePointer(x,y);
        // A pointer the program places is no movement of the mouse; DINPUT.VXD
        // only reports the mickeys of the hand (src/x86_dinput_vxd.cpp).
        DinputWarp(x,y);
        dinput_warping=true;
        MouseButtons(x,y,User::PhysicalMouseButtons());
        dinput_warping=false;
        // The pointer of the host follows, the way the original moves the real
        // pointer. Without it a program that centres the cursor after every
        // movement, as the mouse emulation of DINPUT does, reads the distance
        // from the centre as its next step. MoveHostCursor gets the
        // same guest-screen coordinates as MouseButtons above; the host's
        // provider converts them to its own fixed canvas
        // (tools/neo98_host.cpp).
        MoveHostCursor(x,y);
        return 1;
    }
    API_CASE("ChangeDisplaySettingsA"){
        // USER hands its two arguments to the same that serves
        // ChangeDisplaySettingsExA, so the mode list and the change are the
        // ones of src/x86_user_session.cpp. Only the argument checks of this
        // narrower entry stay here.
        if(a[1]&~0x1000001fu)return ~3u;
        if(!a[0])return ChangeDisplaySettings(0,a[1]);
        unsigned short size=0;if(!Read(a[0]+36,&size,2)||size<124||!virtual_memory.Access(a[0],size,false))return ~4u;
        const unsigned fields=Read32(a[0]+40);
        // dmDisplayFlags is not display orientation. Win98 accepts Serious
        // Sam's value 4, and the mode chosen by A and ExA is the same for
        // 0, 1, 2, 4, 8 and ~0 (display_flags_fixture.c).
        // Keep the separate check of the refresh rate this driver offers.
        if((fields&0x400000)&&Read32(a[0]+120)>1&&Read32(a[0]+120)!=60)return ~1u;
        return ChangeDisplaySettings(a[0],a[1]);
    }
    API_CASE("MapVirtualKeyA"){
        unsigned code=a[0];
        if(a[1]==0){if(code==0x10)code=0xa0;if(code==0x11)code=0xa2;if(code==0x12)code=0xa4;return key(code,false,false).scan;}
        if(a[1]==1||a[1]==3){unsigned vk=key(code&255,true,false).vk;
            // MAPVK_VSC_TO_VK prefers navigation keys over numpad aliases.
            static const unsigned navigation[]={0x24,0x26,0x21,0,0x25,0,0x27,0,0x23,0x28,0x22,0x2d,0x2e};
            if((code&255)>=0x47&&(code&255)<=0x53&&navigation[(code&255)-0x47])vk=navigation[(code&255)-0x47];
            if(a[1]==1){if(vk==0xa0||vk==0xa1)vk=0x10;if(vk==0xa2||vk==0xa3)vk=0x11;if(vk==0xa4||vk==0xa5)vk=0x12;}return vk;}
        if(a[1]==2){if((code>='A'&&code<='Z')||(code>='0'&&code<='9')||code==32)return code;
            if(code>=0x60&&code<=0x69)return '0'+code-0x60;
            switch(code){case 0x6a:return '*';case 0x6b:return '+';case 0x6d:return '-';case 0x6e:return german?',':'.';case 0x6f:return '/';}
            static const unsigned vk[]={0xba,0xbb,0xbc,0xbd,0xbe,0xbf,0xc0,0xdb,0xdc,0xdd,0xde};
            static const unsigned us[]={';','=',',','-','.','/','\x60','[','\\',']','\''};
            static const unsigned de[]={0xfc,'+',',','-','.', '#',0xf6,0xdf,0x80000000u|'^',0x80000000u|0xb4,0xe4};
            for(unsigned i=0;i<11;++i)if(code==vk[i])return german?de[i]:us[i];
        }return 0;
    }
    API_CASE("GetKeyNameTextA"){
        if((int)a[2]<=0||!virtual_memory.Access(a[1],a[2],true))return 0;
        auto k=key((a[0]>>16)&255,true,(a[0]&(1<<24))!=0);char text[40]={};const char *label=nullptr;
        if(k.vk>='A'&&k.vk<='Z')text[0]=(char)k.vk;
        else if(k.vk>='0'&&k.vk<='9')text[0]=(char)k.vk;
        else if(k.vk>=0x70&&k.vk<=0x7b)snprintf(text,sizeof text,"F%u",k.vk-0x6f);
        else if(k.vk>=0x60&&k.vk<=0x69)snprintf(text,sizeof text,"Num %u",k.vk-0x60);
        else switch(k.vk){
            case 8:label=german?"R\xfc""cktaste":"Backspace";break;case 9:label="Tab";break;
            case 13:label=german?"Eingabe":"Enter";break;case 27:label="Esc";break;case 32:label=german?"Leertaste":"Space";break;
            case 0xa0:case 0xa1:label=german?"Umschalt":"Shift";break;
            case 0xa2:case 0xa3:label=german?"Strg":"Ctrl";break;case 0xa4:case 0xa5:label="Alt";break;
            case 0x25:label=german?"Links":"Left";break;case 0x26:label=german?"Nach oben":"Up";break;
            case 0x27:label=german?"Rechts":"Right";break;case 0x28:label=german?"Nach unten":"Down";break;
            case 0x2d:label=german?"Einfg":"Insert";break;case 0x2e:label=german?"Entf":"Delete";break;
            case 0x24:label=german?"Pos1":"Home";break;case 0x23:label=german?"Ende":"End";break;
            default:{unsigned args[]={k.vk,2};text[0]=(char)KeyboardGameApi("MapVirtualKeyA",args);break;}
        }
        if(label)strcpy(text,label);unsigned n=strlen(text);if(n>=a[2])n=a[2]-1;Store(a[1],text,n);const char zero=0;Store(a[1]+n,&zero,1);return n;
    }
    default:
        break;
    }
    return 0;
}
}
