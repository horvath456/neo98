// SPDX-License-Identifier: GPL-2.0-or-later
#include "x86_app.h"
#include "win98_shell_icons.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
namespace X86 {
struct Application::ShellIconEnvironment {
    using U=unsigned;
    static constexpr U Base=0x30000000, Limit=32*1024*1024, Handles=128;
    Application &app;
    unsigned char *mapped=nullptr;
    U mapped_size=0,file_size=0,guest_name=0;
    bool valid=true;
    U *output=nullptr,*large=nullptr,*small=nullptr,large_guest=0,small_guest=0,output_guest=0;
    explicit ShellIconEnvironment(Application &a):app(a) {}
    ~ShellIconEnvironment() { free(mapped); }
    bool Valid() const { return valid && app.state!=Failed; }
    bool Range(U p,U n) {
        if(!mapped || p<Base || p-Base>mapped_size || n>mapped_size-(p-Base)) { valid=false;return false; }
        return true;
    }
    U Read(U p,U n) { if(!Range(p,n))return 0;U result=0;for(U i=0;i<n;++i)result|=U(mapped[p-Base+i])<<(8*i);return result; }
    U Read8(U p) { return Read(p,1); } U Read16(U p) { return Read(p,2); } U Read32(U p) { return Read(p,4); }
    U FileSize(U file) { U error=app.files.Size(file,file_size);if(error)app.last_error=error;return error?~0u:file_size; }
    U CreateMapping(U file) {
        if(mapped || !file_size || file_size>Limit) { app.last_error=file_size?8:1006;return 0; }
        mapped_size=(file_size+4095)&~4095u;
        mapped=static_cast<unsigned char *>(calloc(1,mapped_size));
        if(!mapped) { app.last_error=8;return 0; }
        U old=0,position=0,done=0;
        U error=app.files.Seek(file,0,1,old);
        if(!error)error=app.files.Seek(file,0,0,position);
        if(!error)error=app.files.Read(file,mapped,file_size,done);
        const U restore=app.files.Seek(file,old,0,position);
        if(error || restore || done!=file_size) { app.last_error=error?error:restore?restore:30;free(mapped);mapped=nullptr;mapped_size=0;return 0; }
        return 0x981c0001;
    }
    U Map(U) { return mapped?Base:0; }
    void Unmap(U) { free(mapped);mapped=nullptr;mapped_size=0; }
    void CloseMapping(U) {}
    bool HasOutput() const { return output!=nullptr; }
    void Output(U *p) {
        output=p;output_guest=p && p==large?large_guest:p && p==small?small_guest:0;
        if(output_guest)app.Write32(output_guest,*p);
    }
    void StoreId(U,U) {} // public ExtractIcon[/Ex] does not request resource IDs
    void StoreIcon(U i,U icon) {
        if(i>=Handles) { valid=false;app.last_error=8;return; }
        output[i]=icon;
        if(output_guest)app.Write32(output_guest+i*4,icon);
    }
    void FileIcon(U *p,U icon) { StoreIcon(U(p-output),icon); }
    U Select(U group,U cx,U cy,U flags) {
        if(!Range(group,6))return 0;
        return app.LookupResourceIcon(mapped+group-Base,mapped_size-(group-Base),true,int(cx),int(cy),flags);
    }
    U CreateIcon(U bits,U bytes,U cx,U cy,U flags) {
        if(!Range(bits,bytes))return 0;
        return app.CreateResourceIcon(mapped+bits-Base,bytes,true,0x30000,int(cx),int(cy),flags);
    }
    U Metric(U i) { const U a[]={i};return app.Api("GetSystemMetrics",a); }
    U Extension(const char *) {
        const U a[]={guest_name},extension=app.PathApi("PathFindExtensionA",a);
        unsigned char first=0;if(!app.Read(extension,&first,1) || first!='.')return 0;
        return app.Read32(extension)|0x20202000;
    }
    U Expand(const char *,char *out,U capacity) {
        // FileExtract clears its first output before reading the filename. Read
        // guest bytes here so an output/name alias observes that original order.
        const char *source=app.ReadString(guest_name);
        return Valid()?app.ExpandProcessEnvironment(source,out,capacity):0;
    }
    bool Exists(const char *name,char (&out)[260]) {
        U error=app.files.FullPath(name,out),node=0;
        if(!error)error=app.files.store.Lookup(out,node);
        if(!error && (app.files.store.Get(node)->attributes&16))error=5;
        if(error)app.last_error=error;
        return !error;
    }
    bool Resolve(const char *name,char (&out)[260],bool unc) {
        if(unc) { app.last_error=50;return false; } // no network filesystem provider
        if(strchr(name,'\\') || strchr(name,'/') || strchr(name,':'))return Exists(name,out);
        char directory[260];strcpy(directory,app.modules[0].path);
        char *leaf=strrchr(directory,'\\');if(leaf)*leaf=0;else directory[0]=0;
        const char *locations[]={directory,app.files.current,"C:\\WINDOWS\\SYSTEM","C:\\WINDOWS"};
        for(const char *location:locations) {
            char candidate[260];if(strlen(location)+strlen(name)+2>sizeof candidate)continue;
            snprintf(candidate,sizeof candidate,"%s\\%s",location,name);
            if(Exists(candidate,out))return true;
        }
        char paths[4096]={};const U required=app.ExpandProcessEnvironment("%PATH%",paths,sizeof paths);
        if(required && required<=sizeof paths && strcmp(paths,"%PATH%")) {
            char *p=paths;
            do {
                char *end=strchr(p,';');if(end)*end=0;
                char candidate[260];if(strlen(p)+strlen(name)+2<=sizeof candidate) {
                    snprintf(candidate,sizeof candidate,"%s\\%s",*p?p:app.files.current,name);
                    if(Exists(candidate,out))return true;
                }
                if(!end)break;p=end+1;
            }while(*p);
        }
        app.last_error=2;return false;
    }
    void ResolveError(bool) {} // original GetLastError return is discarded
    U Open(const char *path,U) {
        // The native RAM filesystem has read/write sharing but no access-time
        // metadata. SHELL's FILE_WRITE_ATTRIBUTES retry maps to the same read lease.
        U handle=0,status=0,error=app.files.Open(path,1,1,3,0,handle,status);
        if(error)app.last_error=error;return error?~0u:handle;
    }
    void PreserveAccessTime(U) {} // RAM nodes have no access-time field to change
    U ReadFile(U file,void *out,U bytes) { U done=0,error=app.files.Read(file,out,bytes,done);if(error)app.last_error=error;return error?~0u:done; }
    void CloseFile(U file) { app.files.Close(file); }
    U LoadImageFile(const char *path,U type,U cx,U cy,U flags) {
        const U p=app.String(path);if(!p)return 0;
        const U a[]={0,p,type,cx,cy,flags};const U result=app.ImageApi("LoadImageA",a);app.Free(p);return result;
    }
    void DestroyIcon(U h) { const U a[]={h};app.ImageApi("DestroyIcon",a); }
    U BitmapFile(const char *,U,U,U,U *,U,U) { app.last_error=50;return 0; }
    U Extract(U file,U index,U cx,U cy,U *out,U count) {
        valid=true;guest_name=file;
        // Extension/Expand supply the live filename through guest_name, after
        // the implemented dispatcher publishes its initial output zero.
        return Win98ShellIcons::FileExtract(*this,"",index,cx,cy,out,count,0);
    }
};
unsigned Application::ShellIconApi(ApiName name,const unsigned *a) {
    if(API_IS(name,"ExtractIconW") || API_IS(name,"InternalExtractIconListA") || API_IS(name,"InternalExtractIconListW"))return Win98ShellIcons::EmptyExport();
    ShellIconEnvironment e(*this);
    if(API_IS(name,"ExtractIconA"))return Win98ShellIcons::ExtractIcon(e,a[1],a[2]);
    // Public aliases ExtractIconEx and ExtractIconExA share the original body.
    if(a[1]==~0u || (!a[2] && !a[3]))return Win98ShellIcons::ExtractIconEx(e,a[0],a[1],nullptr,nullptr,a[4]);
    if(a[4]>ShellIconEnvironment::Handles) { last_error=8;return 0; }
    unsigned large[ShellIconEnvironment::Handles]={},small[ShellIconEnvironment::Handles]={};
    e.large=large;e.small=small;e.large_guest=a[2];e.small_guest=a[3];
    const unsigned result=Win98ShellIcons::ExtractIconEx(e,a[0],a[1],a[2]?large:nullptr,a[3]?small:nullptr,a[4]);
    if(a[2] && a[3] && a[4]==1) { Write32(a[2],large[0]);Write32(a[3],small[0]); }
    return result;
}
} // namespace X86

namespace X86 {
unsigned Application::ShellBuiltinIcon(unsigned resource,unsigned width,unsigned height,unsigned flags){
 // Reuse the original mapped PE/NE extraction core for the supplied DLL resource.
 const char *path="C:\\WINDOWS\\SYSTEM\\SHELL32.DLL";unsigned node=0;
 if(files.store.Lookup(path,node)){Fail("Shell icon initialization needs C:\\WINDOWS\\SYSTEM\\SHELL32.DLL resources");return 0;}
 unsigned status=0,file=0;if(files.Open(path,1,1,3,0,file,status)){last_error=2;return 0;}
 ShellIconEnvironment e(*this);unsigned icon=0;e.output=&icon;
 Win98ShellIcons::MappedExtract(e,file,0u-resource,width,height,1,flags);files.Close(file);return icon;
}
unsigned Application::ShellExtractIconPair(const char *path,unsigned resource,unsigned cx,unsigned cy,unsigned flags,unsigned &large,unsigned &small){
 ShellIconEnvironment e(*this);unsigned output[2]={};unsigned name=String(path);if(!name)return 0;
 e.guest_name=name;unsigned result=Win98ShellIcons::FileExtract(e,"",resource,cx,cy,output,2,flags);
 Free(name);large=output[0];small=output[1];return result;
}
}
