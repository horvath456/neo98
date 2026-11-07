// SPDX-License-Identifier: GPL-2.0-or-later
#include "x86_app.h"
#include "win98_clipboard.h"
#include "host_clipboard.h"
#include "win98_nls.h"
#ifdef NEO98_RUNTIME_CONTENT
#include "windows_runtime_data.h"
#else
#include "../build/generated/nls_tables.h"
#endif
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <new>
namespace X86 {
namespace {
using namespace Win98Clipboard;
constexpr unsigned DataCapacity=256, FormatCapacity=512, ByteQuota=16*1024*1024;
struct ClipboardStorage {
    State state; Entry entries[FormatCapacity] = {};
    struct Data { unsigned char *bytes=nullptr; unsigned size=0,generation=0; } data[DataCapacity];
    unsigned bytes=0,generation=0;
    // The clipboard of the host (src/host_clipboard.h). `host_owned` holds
    // from the moment the session took an offer until a program empties the
    // clipboard: the entries with a null handle are then the host's to render.
    const HostClipboard::Provider *provider=nullptr;
    bool host_pending=false,host_owned=false,publishing=false,reading=false;
    unsigned host_kinds=0,host_deferred=0,drop_effect_format=0;
    HostClipboard::Content guest;
    uint16_t *guest_text=nullptr; char *guest_files=nullptr; unsigned char *guest_image=nullptr;
};
ClipboardStorage *SharedClipboard() {
    static ClipboardStorage *value=nullptr;
    if(!value) { void *p=malloc(sizeof(ClipboardStorage)); if(p)value=new(p)ClipboardStorage; }
    return value;
}
bool MemoryFormat(unsigned format) {
    return format!=0x80 && format!=2 && format!=3 && format!=9 && format!=14 && format!=0x82 && format!=0x83 && format!=0x8e;
}
// CF_BITMAP, CF_PALETTE and CF_DSPBITMAP carry a GDI object, not a block
// (USER, class 1). Under Win98 a GDI handle is one word for both
// sides, so the entry keeps the handle itself and the USER32 thunk passes it
// through unchanged; here the word names the object by the rule of Gdi::Names.
bool GdiFormat(unsigned format) { return format==2 || format==9 || format==0x82; }
const Win98Nls::Table AnsiPage{OriginalCodePage1252,sizeof OriginalCodePage1252};
constexpr Word TextFormat=1,UnicodeFormat=13,DropFormat=15,DibFormat=8;
// DROPEFFECT_COPY | DROPEFFECT_LINK, what Explorer's "Copy" names.
constexpr unsigned CopyEffect=5;
// The owner word USER writes into a GDI object it takes over, so the
// object outlives the task that made it. It is USER's, not a
// task's: no process id and no module token (bit 15 with a module index)
// takes this value, so FreeGdi and ByeByeGDI of a task leave the object alone.
constexpr unsigned ClipboardObjectOwner=0xffff;
}
struct Application::ClipboardContext final : Win98Clipboard::Environment {
    Application &app; ClipboardStorage &storage;
    struct Alias { unsigned guest=0; Word data=0; bool current=false;unsigned generation=0; } aliases[512];
    bool destroying=false;
    ClipboardContext(Application &a,ClipboardStorage &s):app(a),storage(s){}
    Win98Clipboard::State &Core(){return storage.state;}
    ClipboardStorage::Data *Data(Word h) { return h>=0x400 && !(h&7) && (h-0x400)/8<DataCapacity && storage.data[(h-0x400)/8].bytes ? &storage.data[(h-0x400)/8] : nullptr; }
    Word CurrentTask()override{return Word(app.ThreadId());}
    Word ExpectedVersion(Word)override { const unsigned version=app.image.SubsystemVersion();return Word(((version>>8)&0xff00)|(version&255)); }
    unsigned Locale(Word)override{return app.DefaultLocaleApi("GetThreadLocale");}
    unsigned WindowHandle(unsigned h)override{return h;}
    Word Context()override{return 0;} // no Win16 scheduler/input context on ARM
    void InputMode(bool)override{}
    unsigned Send(unsigned h,Word msg,unsigned wp,unsigned lp,bool)override {
        // Rendering must finish before GetClipboardData resumes. Route through
        // USER's synchronous send so the owning process/thread runs the callback.
        return !destroying && app.state!=Application::Failed && User::IsWindow(h) ? app.SendFromNative(h,msg,wp,lp) : 0;
    }
    Entry *ResizeTable(Entry*,Word bytes)override {
        if(!bytes || bytes>sizeof storage.entries){app.last_error=8;return nullptr;}
        return storage.entries;
    }
    void FreeTable(Entry*)override{}
    void ReferenceAtom(Word f)override{app.ClipboardAtom(f,1,nullptr,0);}
    void DeleteAtom(Word f)override{app.ClipboardAtom(f,2,nullptr,0);}
    Word AtomName(Word f,unsigned char *out,Word n)override{return Word(app.ClipboardAtom(f,0,out,n));}
    Word GlobalAlloc(Word,unsigned size)override {
        if(!size || size>ByteQuota-storage.bytes){app.last_error=8;return 0;}
        for(unsigned i=0;i<DataCapacity;++i)if(!storage.data[i].bytes){
            auto *p=static_cast<unsigned char*>(malloc(size));
            if(!p){app.last_error=8;return 0;}
            memset(p,0,size);storage.data[i]={p,size,++storage.generation};storage.bytes+=size;return Word(0x400+i*8);
        }
        app.last_error=8;return 0;
    }
    unsigned GlobalSize(Word h)override{auto*d=Data(h);return d?d->size:0;}
    unsigned char *GlobalLock(Word h)override{auto*d=Data(h);return d?d->bytes:nullptr;}
    unsigned char *GlobalData(Word h)override{return GlobalLock(h);}
    void GlobalUnlock(Word)override{}
    Word GlobalReAlloc(Word h,unsigned size,Word flags)override{return !size && flags==0x2080 && Data(h)?h:0;}
    void GlobalFree(Word h)override {
        if(auto*d=Data(h)){DiscardMapping(h);storage.bytes-=d->size;free(d->bytes);*d={};}
    }
    void SetObjectOwner(Word h)override{Gdi::SetObjectOwner(h,ClipboardObjectOwner);}
    // FreeEntry, class 1: the object belongs to USER, which deletes it.
    void DeleteObject(Word h)override{Gdi::SysDeleteObject(h);}
    bool ValidMetaFile(Word)override{return false;}
    Word CheckMetaFile(Word)override{return 0xffff;}
    void DeleteMetaFile(Word,bool)override{app.last_error=50;}
    void DiscardMapping(Word h)override { for(auto&a:aliases)if(a.data==h)a.current=false; }
    unsigned To32(Word format,Word h)override {
        if(GdiFormat(format))return Gdi::FullHandle(h);
        if(!MemoryFormat(format)){app.last_error=50;return 0;}
        auto*d=Data(h);if(!d)return 0;
        for(auto&a:aliases)if(a.current && a.data==h && a.generation==d->generation && app.AllocationSize(a.guest)>=d->size)return a.guest;
        Alias *slot=nullptr;for(auto&a:aliases)if(!a.guest || !app.AllocationSize(a.guest)){slot=&a;break;}
        if(!slot){app.last_error=8;return 0;}
        unsigned guest=app.Allocate(d->size);if(!guest){app.last_error=8;return 0;}
        if(!app.Store(guest,d->bytes,d->size)){app.Free(guest);return 0;}
        *slot={guest,h,true,d->generation};return guest;
    }
    Word To16(Word format,unsigned guest,bool)override {
        if(GdiFormat(format)){if(!Gdi::FullHandle(guest)){app.last_error=6;return 0;}return Word(guest);}
        if(!MemoryFormat(format)){app.last_error=50;return 0;}
        const unsigned size=app.LocalMemoryApi("GlobalSize",&guest);
        if(!size){app.last_error=6;return 0;}
        Word h=GlobalAlloc(0x2002,size);if(!h)return 0;
        const unsigned pointer=app.LocalMemoryApi("GlobalLock",&guest);
        const bool copied=pointer && app.Read(pointer,GlobalData(h),size);
        if(pointer)app.LocalMemoryApi("GlobalUnlock",&guest);
        if(!copied){GlobalFree(h);return 0;}
        // String conversions are NUL-terminated in the original KEYBOARD calls.
        // Reject malformed buffers before exposing their size to that converter.
        if((format==1 || format==7) && !memchr(GlobalData(h),0,size)){GlobalFree(h);app.last_error=87;return 0;}
        return h;
    }
    unsigned ConvertSpecial(unsigned,bool)override{app.last_error=50;return 0;}
    bool PaletteDisplay()override{return false;}
    Word CreateSystemPalette()override{app.last_error=50;return 0;}
    Word ConvertGraphics(Word,Word,Word)override{app.last_error=50;return 0;}
    void TranslateText(const unsigned char*src,unsigned char*dst,bool to_oem)override {
        const Win98Nls::Table ansi{OriginalCodePage1252,sizeof OriginalCodePage1252};
        const Win98Nls::Table oem=app.system_locale.oem==850 ? Win98Nls::Table{OriginalCodePage850,sizeof OriginalCodePage850} : Win98Nls::Table{OriginalCodePage437,sizeof OriginalCodePage437};
        const auto&from=to_oem?ansi:oem;const auto&to=to_oem?oem:ansi;
        do { *dst++=static_cast<unsigned char>(to.Narrow(from.Wide(*src))); } while(*src++);
    }
};
bool Application::InitializeClipboard() {
    if(clipboard)return true;
    auto*shared=SharedClipboard();void*p=shared?malloc(sizeof(ClipboardContext)):nullptr;
    if(!p){last_error=8;return false;}
    clipboard=new(p)ClipboardContext(*this,*shared);
    BindClipboardProviders();
    return true;
}
void Application::BindClipboardProviders() {
    User::SetClipboardCallbacks(this,[](void*p,unsigned hwnd,bool write,char*text,unsigned length){return static_cast<Application*>(p)->EditClipboard(hwnd,write,text,length);},
        [](void*p,unsigned hwnd){static_cast<Application*>(p)->DestroyClipboardWindow(hwnd);});
}
void Application::FreeClipboard() {
    if(!clipboard)return;
    auto &e=*clipboard;auto&s=e.Core();e.destroying=true;
    if(s.owner&&User::WindowProcess(s.owner)==process_id)Win98Clipboard::OwnerDestroyed(e,s);
    for(auto&t:threads)if(t.id)Win98Clipboard::TaskExited(e,s,Word(t.id));
    if(e.storage.host_owned && !s.count){e.storage.host_owned=false;e.storage.host_pending=true;}
    if(s.viewer&&User::WindowProcess(s.viewer)==process_id)s.viewer=0;
    for(auto&a:e.aliases)if(a.guest && AllocationSize(a.guest))Free(a.guest);

    e.~ClipboardContext();free(clipboard);clipboard=nullptr;
}
void Application::DetachClipboardThread() {
    if (!clipboard) return;
    Win98Clipboard::TaskExited(*clipboard, clipboard->Core(), clipboard->CurrentTask());
    auto &storage = clipboard->storage;
    if (storage.host_owned && !clipboard->Core().count) { storage.host_owned = false; storage.host_pending = true; }
}
void Application::ClipboardAllocationFreed(unsigned address) {
    if(clipboard)for(auto&a:clipboard->aliases)if(a.guest==address)a={};
}
void Application::DestroyClipboardWindow(unsigned hwnd) {
    if(!clipboard)return;
    auto &e=*clipboard;auto&s=e.Core();
    if(s.owner==hwnd)Win98Clipboard::OwnerDestroyed(e,s);
    // Viewer-chain removal is the application WM_DESTROY protocol. A dead
    // head cannot be called by this HWND provider; do not invent its successor.
    if(s.viewer==hwnd)s.viewer=0;
    if(s.open_window==hwnd){s.open_window=0;s.open_task=0;}
}
unsigned Application::ClipboardApi(ApiName name,const unsigned*a) {
    if(!InitializeClipboard())return 0;
    // A program that asks about the clipboard must not see what the host
    // already replaced: the offer is taken now, whoever owns the clipboard.
    clipboard->storage.reading=true;ServeHostClipboard();clipboard->storage.reading=false;
    auto&e=*clipboard;auto&s=e.Core();
    switch (name.id)
    {
    API_CASE("OpenClipboard") {
        if(a[0] && !User::IsWindow(a[0])){last_error=1400;return 0;}
        return Win98Clipboard::Open(e,s,a[0]);
    }
    API_CASE("CloseClipboard") {
        const bool changed=s.changed && s.open_task==e.CurrentTask();
        const unsigned result=Win98Clipboard::Close(e,s);
        if(result && changed)PublishClipboard();
        return result;
    }
    API_CASE("EmptyClipboard") {
        const unsigned result=Win98Clipboard::Empty(e,s);
        if(result)e.storage.host_owned=false;
        return result;
    }
    API_CASE("GetClipboardData") {
        RenderHostClipboard(a[0]);
        return Win98Clipboard::Get32(e,s,Word(a[0]));
    }
    API_CASE("SetClipboardData") {
        const Word format=Word(a[0]);
        if(!MemoryFormat(format)&&!GdiFormat(format)){last_error=50;return 0;}
        const Word data=a[1]?e.To16(format,a[1],true):0;
        if(a[1] && !data)return 0;
        const Word result=Win98Clipboard::Set(e,s,format,data,1);
        // The native transfer provider has no process mapping-list sweeper.
        // Roll back a copied block which did not become a clipboard entry.
        if(data && !result && !GdiFormat(format))e.GlobalFree(data);
        return result?a[1]:0;
    }
    API_CASE("CountClipboardFormats")return s.count;
    API_CASE("EnumClipboardFormats")return Win98Clipboard::Enumerate(e,s,Word(a[0]));
    API_CASE("IsClipboardFormatAvailable")return Win98Clipboard::Available32(s,a[0]);
    API_CASE("GetPriorityClipboardFormat") {
        // The original public thunk narrows each DWORD and the count to WORD.
        // Bound its temporary storage explicitly; validate the complete input.
        if(a[1]>65535 || (a[1] && (!a[0] || !virtual_memory.Access(a[0],a[1]*4,false)))){last_error=87;return 0xffffffff;}
        Word *formats=a[1]?static_cast<Word*>(malloc(a[1]*sizeof(Word))):nullptr;
        if(a[1] && !formats){last_error=8;return 0xffffffff;}
        for(unsigned i=0;i<a[1];++i)formats[i]=Word(Read32(a[0]+i*4));
        const unsigned result=Win98Clipboard::Priority(s,formats,Word(a[1]));free(formats);return result;
    }
    API_CASE("GetClipboardOwner")return s.owner;
    API_CASE("GetOpenClipboardWindow")return s.open_window;
    API_CASE("GetClipboardViewer")return s.viewer;
    API_CASE("GetClipboardSequenceNumber")return s.sequence;
    API_CASE("SetClipboardViewer") {
        if(!User::IsWindow(a[0])){last_error=1400;return 0;}
        return Win98Clipboard::SetViewer(e,s,a[0]);
    }
    API_CASE("ChangeClipboardChain") {
        if(!User::IsWindow(a[0]) || (a[1] && !User::IsWindow(a[1]))){last_error=1400;return 0;}
        return static_cast<int16_t>(Win98Clipboard::ChangeChain(e,s,a[0],a[1]));
    }
    API_CASE("GetClipboardFormatNameA") {
        const unsigned n=a[2]&65535;
        if(!n || n>32767 || !virtual_memory.Access(a[1],n,true)){last_error=87;return 0;}
        unsigned char text[256]={};const unsigned size=Win98Clipboard::Name(e,Word(a[0]),text,Word(n));
        Store(a[1],text,size+1);return size;
    }
    default:
        break;
    }
    return 0;
}
unsigned Application::EditClipboard(unsigned hwnd,bool write,char*text,unsigned length) {
    if(!clipboard)return 0;
    auto&e=*clipboard;auto&s=e.Core();
    if(!Win98Clipboard::Open(e,s,hwnd))return 0;
    unsigned result=0;
    bool changed=false;
    if(write) {
        if(Win98Clipboard::Empty(e,s)) {
            e.storage.host_owned=false;changed=true;
            const Word h=e.GlobalAlloc(0x2042,length+1);
            if(h){memcpy(e.GlobalData(h),text,length);e.GlobalData(h)[length]=0;if(Win98Clipboard::Set(e,s,1,h,0))result=length;else e.GlobalFree(h);}
        }
    } else {
        RenderHostClipboard(1);
        const Word h=Win98Clipboard::Get(e,s,1,0);auto*d=e.Data(h);
        if(d && length){while(result+1<length && result<d->size && d->bytes[result]){text[result]=d->bytes[result];++result;}text[result]=0;}
    }
    Win98Clipboard::Close(e,s);
    if(changed)PublishClipboard();
    return result;
}

// --- The clipboard of the host, src/host_clipboard.h ---

// Takes the clipboard for what the host offered, the way a program takes it:
// EmptyClipboard (the owner hears WM_DESTROYCLIPBOARD), SetClipboardData with
// a null handle per format, CloseClipboard (the viewers hear
// WM_DRAWCLIPBOARD). The window is null, so the clipboard has no owner and
// GetClipboardData asks the provider instead (RenderHostClipboard). Waits
// while a program holds the clipboard open.
void Application::ServeHostClipboard() {
    auto*shared=SharedClipboard();
    if(!shared || !shared->host_pending || !shared->provider || state==Failed || state==Exited)return;
    if(!InitializeClipboard())return;
    auto&e=*clipboard;auto&s=e.Core();
    if(s.open_task || s.notifying)return;
    // Prefer taking a host offer on the owner's next message-loop turn.
    // After the bounded deferral, synchronous cross-process delivery also
    // reaches WM_DESTROYCLIPBOARD (ClipboardContext::Send).
    if(!shared->reading && s.owner && User::IsWindow(s.owner) && User::WindowProcess(s.owner)!=process_id && shared->host_deferred<32) {
        if(!shared->host_deferred++)User::PostMessage(s.owner,0,0,0);
        return;
    }
    shared->host_pending=false;shared->host_deferred=0;
    const unsigned kinds=shared->host_kinds;
    if(!kinds || !Win98Clipboard::Open(e,s,0))return;
    if(Win98Clipboard::Empty(e,s)) {
        shared->host_owned=true;
        if(kinds&HostClipboard::Text){Win98Clipboard::Set(e,s,TextFormat,0,0);Win98Clipboard::Set(e,s,UnicodeFormat,0,0);}
        if(kinds&HostClipboard::Files) {
            if(!shared->drop_effect_format)shared->drop_effect_format=RegisterMessage("Preferred DropEffect");
            Win98Clipboard::Set(e,s,DropFormat,0,0);
            if(shared->drop_effect_format)Win98Clipboard::Set(e,s,Word(shared->drop_effect_format),0,0);
        }
        // CF_BITMAP follows from CF_DIB as for any program.
        if(kinds&HostClipboard::Image)Win98Clipboard::Set(e,s,DibFormat,0,0);
        if(log){char text[80];snprintf(text,sizeof text,"HOST CLIPBOARD offer taken kinds=%u formats=%u",kinds,unsigned(s.count));log(text);}
    }
    Win98Clipboard::Close(e,s);
}
// GetClipboardData for a format of the host that nobody fetched yet. The
// text arrives as UTF-16: CF_UNICODETEXT keeps it, CF_TEXT is the ANSI code
// page with the characters the page lacks narrowed as WideCharToMultiByte
// narrows them (the best-fit table of the page, else its default
// character). Line ends become CR LF. CF_OEMTEXT and CF_LOCALE follow from
// CF_TEXT as for any program.
void Application::RenderHostClipboard(unsigned wanted) {
    if(!clipboard)return;
    auto&e=*clipboard;auto&s=e.Core();auto&shared=e.storage;
    if(!shared.host_owned || !shared.provider || s.owner || e.CurrentTask()!=s.open_task || wanted>0xffff)return;
    const Word format=wanted==7?TextFormat:wanted==2?DibFormat:Word(wanted);
    const int at=Win98Clipboard::Find(s,format);
    if(at<0 || s.entries[at].data)return;
    unsigned char *bytes=nullptr;unsigned size=0;
    if(format==TextFormat || format==UnicodeFormat) {
        uint16_t *units=nullptr;unsigned count=0;
        if(!shared.provider->Text(shared.provider->context,&units,&count))return;
        // Twice the room: a lone CR or LF becomes the pair.
        if(count<ByteQuota/8)bytes=static_cast<unsigned char*>(malloc((size_t(count)*2+1)*(format==UnicodeFormat?2:1)));
        if(bytes) {
            auto put=[&](unsigned c){ if(format==UnicodeFormat){bytes[size++]=c&255;bytes[size++]=(c>>8)&255;}else bytes[size++]=static_cast<unsigned char>(c<128?c:AnsiPage.Narrow(c)); };
            for(unsigned i=0;i<count && units[i];++i) {
                const unsigned c=units[i];
                if(c=='\r'){put('\r');put('\n');if(i+1<count && units[i+1]=='\n')++i;}
                else if(c=='\n'){put('\r');put('\n');}
                else put(c);
            }
            put(0);
        }
        free(units);
    } else if(format==DropFormat) {
        char *list=nullptr;unsigned length=0;
        if(!shared.provider->Files(shared.provider->context,&list,&length))return;
        // DROPFILES: pFiles, pt, fNC, fWide; then the list and its second NUL.
        if(length && length<ByteQuota/2)bytes=static_cast<unsigned char*>(malloc(20+length+1));
        if(bytes){memset(bytes,0,20+length+1);bytes[0]=20;memcpy(bytes+20,list,length);size=20+length+1;}
        free(list);
    } else if(format==DibFormat) {
        if(!shared.provider->Image || !shared.provider->Image(shared.provider->context,&bytes,&size))return;
        if(size>ByteQuota-shared.bytes){free(bytes);bytes=nullptr;size=0;}
    } else if(format==shared.drop_effect_format && format) {
        bytes=static_cast<unsigned char*>(malloc(4));
        if(bytes){bytes[0]=CopyEffect;bytes[1]=bytes[2]=bytes[3]=0;size=4;}
    }
    if(!bytes){if(log){char text[64];snprintf(text,sizeof text,"HOST CLIPBOARD render format=%u refused",unsigned(format));log(text);}return;}
    const Word handle=size?e.GlobalAlloc(0x2042,size):0;
    if(handle){memcpy(e.GlobalData(handle),bytes,size);s.entries[at].data=handle;}
    free(bytes);
    if(log){char text[96];snprintf(text,sizeof text,"HOST CLIPBOARD render format=%u bytes=%u %s",unsigned(format),size,handle?"ok":"refused");log(text);}
}
// After a program changed the clipboard and closed it: read the text and the
// file list as a clipboard viewer reads them in WM_DRAWCLIPBOARD, which makes
// the owner render a format it set with a null handle (WM_RENDERFORMAT). The host takes the copy from its own loop.
void Application::PublishClipboard() {
    if(!clipboard)return;
    auto&e=*clipboard;auto&s=e.Core();auto&shared=e.storage;
    if(shared.publishing || shared.host_owned || !shared.provider || state==Failed || state==Exited)return;
    shared.publishing=true;
    uint16_t *text=nullptr;unsigned units=0;char *files=nullptr;unsigned files_bytes=0,effect=0;
    unsigned char *image=nullptr;unsigned image_bytes=0;
    if(Win98Clipboard::Open(e,s,0)) {
        Word format=Win98Clipboard::Find(s,UnicodeFormat)>=0?UnicodeFormat:0;
        // Keep the first offered byte-text format. USER appends its synthesized
        // counterpart; passing OEM through that ANSI copy loses box characters.
        if(!format) for(unsigned i=0;i<s.count;++i)
            if(s.entries[i].format==TextFormat || s.entries[i].format==7) {format=s.entries[i].format;break;}
        const Win98Nls::Table oem=system_locale.oem==850 ? Win98Nls::Table{OriginalCodePage850,sizeof OriginalCodePage850} : Win98Nls::Table{OriginalCodePage437,sizeof OriginalCodePage437};
        if(format) if(auto*d=e.Data(Win98Clipboard::Get(e,s,format,0))) {
            const unsigned limit=format==UnicodeFormat?d->size/2:d->size;
            text=static_cast<uint16_t*>(malloc((size_t(limit)+1)*2));
            for(unsigned i=0;text && i<limit;++i) {
                const unsigned c=format==UnicodeFormat?unsigned(d->bytes[i*2]|d->bytes[i*2+1]<<8):(format==7?oem:AnsiPage).Wide(d->bytes[i]);
                if(!c)break;
                text[units++]=uint16_t(c);
            }
        }
        if(Win98Clipboard::Find(s,DropFormat)>=0) if(auto*d=e.Data(Win98Clipboard::Get(e,s,DropFormat,0))) if(d->size>20) {
            const unsigned start=unsigned(d->bytes[0]|d->bytes[1]<<8|d->bytes[2]<<16)|unsigned(d->bytes[3])<<24;
            const bool wide=d->bytes[16]||d->bytes[17];
            if(start>=20 && start<d->size)files=static_cast<char*>(malloc(d->size-start+2));
            // The list ends at an empty name.
            for(unsigned i=start;files;) {
                const unsigned begin=files_bytes;
                for(;;) {
                    unsigned c=0;
                    if(wide){if(i+1<d->size)c=d->bytes[i]|d->bytes[i+1]<<8;i+=2;if(c>127)c=AnsiPage.Narrow(c);}
                    else {if(i<d->size)c=d->bytes[i];++i;}
                    files[files_bytes++]=char(c);
                    if(!c)break;
                }
                if(files_bytes==begin+1){--files_bytes;break;}
            }
            if(files && !files_bytes){free(files);files=nullptr;}
        }
        if(files && !shared.drop_effect_format)shared.drop_effect_format=RegisterMessage("Preferred DropEffect");
        if(files && shared.drop_effect_format && Win98Clipboard::Find(s,Word(shared.drop_effect_format))>=0)
            if(auto*d=e.Data(Win98Clipboard::Get(e,s,Word(shared.drop_effect_format),0))) if(d->size>=4) effect=d->bytes[0];
        // CF_DIB the program set itself, not the one USER would make of a CF_BITMAP.
        if(const int at=Win98Clipboard::Find(s,DibFormat);at>=0 && s.entries[at].data!=0xffff)
            if(auto*d=e.Data(Win98Clipboard::Get(e,s,DibFormat,0))) if(d->size>40) {
                image=static_cast<unsigned char*>(malloc(d->size));
                if(image){memcpy(image,d->bytes,d->size);image_bytes=d->size;}
            }
        Win98Clipboard::Close(e,s);
    }
    free(shared.guest_text);free(shared.guest_files);free(shared.guest_image);
    shared.guest_image=image;shared.guest.image=image;shared.guest.image_bytes=image_bytes;
    shared.guest_text=text;shared.guest_files=files;
    shared.guest.text=text;shared.guest.text_units=text?units:0;
    shared.guest.files=files;shared.guest.files_bytes=files_bytes;shared.guest.effect=effect;
    ++shared.guest.generation;
    shared.publishing=false;
    if(log){char line[112];snprintf(line,sizeof line,"HOST CLIPBOARD guest generation=%u text=%u files=%u effect=%u image=%u",shared.guest.generation,shared.guest.text_units,files_bytes,effect,image_bytes);log(line);}
}
namespace HostClipboard {
void SetProvider(const Provider *provider) { if(auto*shared=SharedClipboard())shared->provider=provider; }
void Offer(unsigned kinds) {
    auto*shared=SharedClipboard();
    if(!shared || !shared->provider)return;
    shared->host_kinds=kinds;shared->host_pending=kinds!=0;shared->host_deferred=0;
    if(!kinds)return;
    // A thread that rests in GetMessage takes the offer when WM_NULL wakes
    // it; the message means nothing to its window.
    const unsigned window=User::GetForegroundWindow();
    if(window && User::IsWindow(window))User::PostMessage(window,0,0,0);
}
bool OfferPending() { auto*shared=SharedClipboard();return shared && shared->host_pending; }
const Content &Guest() { static const Content none;auto*shared=SharedClipboard();return shared?shared->guest:none; }
} // namespace HostClipboard
} // namespace X86
