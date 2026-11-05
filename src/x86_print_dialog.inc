// SPDX-License-Identifier: GPL-2.0-or-later
// Included within namespace X86 in x86_common.cpp.
bool Application::InitializeCommonDialog()
{
    if(common_dialog_tls)return true;
    const unsigned slot=TlsApi("TlsAlloc",nullptr);
    if(slot==~0u){Fail("COMDLG32 private TLS allocation failed");return false;}
    common_dialog_tls=slot+1;
    const unsigned args[]={slot,0};TlsApi("TlsSetValue",args);return state!=Failed;
}
void Application::FreeCommonDialog()
{
    delete common_dialog_image; common_dialog_image = nullptr;
    delete[] common_dialog_bytes; common_dialog_bytes = nullptr;
    common_dialog_size = 0; common_dialog_read = common_dialog_valid = false;
    if(common_dialog_tls){unsigned slot=common_dialog_tls-1;TlsApi("TlsFree",&slot);common_dialog_tls=0;}
}
struct Application::PrintEnvironment {
    Application&app;unsigned scratch=0;
    explicit PrintEnvironment(Application&a):app(a){}
    ~PrintEnvironment(){if(scratch)app.Free(scratch);}
    // Cooperative native calls cannot interleave during these simple TLS ops.
    // These replace COMDLG's private critical section, not a separate error slot.
    void ErrorLock(){}void ErrorUnlock(){}
    void ErrorWrite(unsigned value){if(app.InitializeCommonDialog()){unsigned args[]={app.common_dialog_tls-1,value};app.TlsApi("TlsSetValue",args);}}
    unsigned ErrorRead(){if(!app.InitializeCommonDialog())return 0;unsigned index=app.common_dialog_tls-1;return app.TlsApi("TlsGetValue",&index);}
    bool Alive(){return app.state!=Failed&&app.state!=Exited;}
    unsigned Read(unsigned p){return app.Read32(p);}void Write(unsigned p,unsigned v){app.Write32(p,v);}
    unsigned Byte(unsigned p){unsigned char v=0;app.Read(p,&v,1);return v;}
    void Byte(unsigned p,unsigned v){const unsigned char b=v;app.Store(p,&b,1);}
    unsigned Word(unsigned p){unsigned short v=0;app.Read(p,&v,2);return v;}
    void Word(unsigned p,unsigned v){const unsigned short w=v;app.Store(p,&w,2);}
    unsigned Buffer(){if(!scratch){scratch=app.Allocate(384);if(!scratch)app.Fail("Print dialog scratch allocation failed");else {unsigned char zero[384]={};app.Store(scratch,zero,sizeof zero);}}return scratch;}
    unsigned DriverName(){unsigned p=Buffer()+320;const char text[]="winspool";app.Store(p,text,sizeof text);return p;}
    unsigned FilePort(){unsigned p=Buffer()+336;const char text[]="FILE:";app.Store(p,text,sizeof text);return p;}
    unsigned DefaultName(){return Buffer()+352;}
    unsigned Length(unsigned p){return unsigned(strlen(app.ReadString(p)));}
    void CopyString(unsigned d,unsigned s){const char*text=app.ReadString(s);app.Store(d,text,unsigned(strlen(text))+1);}
    bool SameName(unsigned a,unsigned b){unsigned args[]={a,b};return app.Api("lstrcmpA",args)==0;}
    bool IsWindow(unsigned h){return User::IsWindow(h);}
    unsigned ProcessVersion(){return 0x40000;}
    unsigned Profile(const char*section,const char*key,unsigned output,unsigned capacity){
        unsigned sp=app.String(section),kp=app.String(key),empty=app.String("");
        const unsigned args[]={sp,kp,empty,output,capacity};unsigned result=app.Api("GetProfileStringA",args);
        app.Free(empty);app.Free(kp);app.Free(sp);
        if(result&&section[0]=='W'){
            // Missing delimiters make the original stack parser read beyond its
            // initialized string. Reject that provider input instead of inventing bytes.
            unsigned commas=0;for(unsigned i=0;i<result;++i)if(Byte(output+i)==',')++commas;
            if(commas<2)app.Fail("Default printer profile requires device,driver,port delimiters");
        }
        return result;
    }
    bool DeviceExists(unsigned name){
        unsigned output=Buffer()+300;const char*device=app.ReadString(name);
        return Profile("devices",device,output,2)&&Byte(output);
    }
    unsigned Allocate(unsigned size,unsigned flags){unsigned args[]={flags,size};return app.Api("GlobalAlloc",args);}
    unsigned Reallocate(unsigned handle,unsigned size,unsigned flags){unsigned args[]={handle,size,flags};return app.Api("GlobalReAlloc",args);}
    unsigned Lock(unsigned h){return app.Api("GlobalLock",&h);}void Unlock(unsigned h){app.Api("GlobalUnlock",&h);}
    void Free(unsigned h){app.Api("GlobalFree",&h);}
    bool Services(){
        // Native backend supports configuration lookup with no installed printer
        // driver. This replaces dynamic WINSPOOL loading, not its DLL/exports.
        return true;
    }
    unsigned DocumentProperties(unsigned,unsigned,unsigned,unsigned,unsigned,unsigned){
        app.Fail("Configured printer requires an installed native DocumentProperties driver");return 0;
    }
    unsigned LastError(){return app.last_error;}void LastError(unsigned v){app.last_error=v;}
    void DriverError(unsigned,unsigned){app.Fail("Printer driver error dialog provider is unavailable");}
    unsigned CreateDC(bool information,unsigned driver,unsigned device,unsigned port,unsigned mode){unsigned args[]={driver,device,port,mode};return app.GdiApi(information?"CreateICA":"CreateDCA",args);}
    void ClosePrinter(unsigned){app.Fail("Printer handle provider is unavailable");}
    void Warning(unsigned,unsigned){app.Fail("PrintDlg failure warning dialog is not ported");}
    unsigned Interactive(unsigned,Win98PrintDialog::State&){app.Fail("Interactive PrintDlg printer selection is not ported");return 0;}
};
unsigned Application::CommonDialogError(){PrintEnvironment e(*this);return Win98PrintDialog::ExtendedError(e);}
void Application::CommonDialogError(unsigned value){PrintEnvironment e(*this);Win98PrintDialog::Error(e,value);}
unsigned Application::PrintDialogApi(ApiName name,const unsigned*a)
{
    PrintEnvironment e(*this);
    if(API_IS(name,"PrintDlgW"))return Win98PrintDialog::Wide(e,a[0]);
    return Win98PrintDialog::Run(e,a[0]);
}
