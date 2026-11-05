// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/win98_shell_icons.h"
#include <stdint.h>
#include <string.h>
using U = unsigned;
using Service = U (*)(U,U,U,U,U,U,U);
struct Environment {
    const unsigned char *data; U size; U *out; Service service; bool valid = true, has_output, ids_enabled; U variant;
    bool Valid() const { return valid; }
    U Read(U p, U n) {
        if (p < 0x30000000u || p - 0x30000000u > size || n > size - (p - 0x30000000u)) { valid = false; return 0; }
        U value = 0; for (U i = 0; i < n; ++i) value |= U(data[p - 0x30000000u + i]) << (i * 8); return value;
    }
    U Read8(U p) { return Read(p, 1); } U Read16(U p) { return Read(p, 2); } U Read32(U p) { return Read(p, 4); }
    U Call(U op,U a=0,U b=0,U c=0,U d=0,U e=0,U f=0) { return service(op,a,b,c,d,e,f); }
    U FileSize(U h) { return Call(1,h); } U CreateMapping(U h) { return Call(2,h); }
    U Map(U h) { return Call(3,h); } void Unmap(U p) { Call(4,p); } void CloseMapping(U h) { Call(5,h); }
    bool HasOutput() { return has_output; }
    U Select(U p,U cx,U cy,U flags) { return Call(6,p,cx,cy,flags); }
    U CreateIcon(U p,U bytes,U cx,U cy,U flags) { return Call(7,p,bytes,cx,cy,flags); }
    void StoreId(U i,U id) { if(!ids_enabled)return;if(i<32) out[32+i]=id; else valid=false; }
    void StoreIcon(U i,U icon) { if(i<32) out[i]=icon; else valid=false; }
    U Metric(U i) { return Call(8,i); }
    U Extract(U file,U index,U cx,U cy,U *output,U count) {
        const U result=Call(9,file,index,cx,cy,output?1:0,count);
        if(output) for(U i=0;i<count && i<32;++i) output[i]=0x98000000u+index+i*0x100+cx+cy;
        return result;
    }
    void Output(U *p) { has_output=p!=nullptr; }
    U Extension(const char *) { return Call(10); }
    void Expand(const char *,char *p,U n) { Call(11,n);strcpy(p,variant?"\\\\SERVER\\SAMPLE.EXE":"SAMPLE.EXE"); }
    bool Resolve(const char *,char (&out)[260],bool unc) { strcpy(out,"C:\\SAMPLE.EXE");return Call(12,unc)!=0; }
    void ResolveError(bool unc) { if(!unc)Call(21); }
    U Open(const char *,U access) { return Call(13,access); }
    void PreserveAccessTime(U file) { if(Call(14,file))Call(15,file); }
    U ReadFile(U file,void *p,U bytes) { const U n=Call(16,file,bytes);if(n<=bytes && n<=size)memcpy(p,data,n);return n; }
    void CloseFile(U file) { Call(17,file); }
    U LoadImageFile(const char *,U type,U cx,U cy,U flags) { return Call(18,type,cx,cy,flags); }
    void DestroyIcon(U icon) { Call(19,icon); }
    void FileIcon(U *p,U icon) { *p=icon; }
    U BitmapFile(const char *,U index,U cx,U cy,U *,U count,U flags) { return Call(20,index,cx,cy,count,flags); }
};
extern "C" U shell_icons_call(U operation,const U *args,const unsigned char *data,U size,U *out,Service callback) {
    Environment e{data,size,out,callback,true,args[9]!=0,operation!=8,args[8]}; U result=0,bytes=out[64];
    switch(operation) {
    case 0: result=Win98ShellIcons::Rva(e,args[0],args[1]);break;
    case 1: result=Win98ShellIcons::PeRoot(e,args[0]);break;
    case 2: result=Win98ShellIcons::NeRoot(e,args[0]);break;
    case 3: result=Win98ShellIcons::PeResource(e,args[0],args[1],args[2],args[3],bytes);break;
    case 4: result=Win98ShellIcons::NeResource(e,args[0],args[1],args[2],args[3],bytes);break;
    case 5: result=Win98ShellIcons::MappedExtract(e,args[0],args[1],args[2],args[3],args[4],args[5]);break;
    case 6: result=Win98ShellIcons::ExtractIcon(e,args[0],args[1]);break;
    case 7: result=Win98ShellIcons::ExtractIconEx(e,args[0],args[1],args[6]?out:nullptr,args[7]?out+16:nullptr,args[4]);break;
    case 8: result=Win98ShellIcons::FileExtract(e,"SAMPLE.EXE",args[1],args[2],args[3],args[9]?out:nullptr,args[4],args[5]);break;
    case 9: result=Win98ShellIcons::EmptyExport();break;
    case 10: case 11: e.Call(22,120);result=0;break;
    }
    out[64]=bytes;out[65]=e.Valid();return result;
}
