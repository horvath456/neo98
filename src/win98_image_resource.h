// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_image_directory.h"
// Connected USER18 resource-loading and shared-image control paths. Heap,
// module/resource access, OEM drawing and GDI raster creation are providers.
namespace Win98ImageResource {
using Word=uint16_t;using Handle=unsigned;
struct Name { unsigned value=0; bool text=false; };
struct Record {Handle next=0,module=0,image=0;Word name=0,flags=0,references=0;};
struct Block {const unsigned char*bytes=nullptr;unsigned size=0;};
struct Environment {
    virtual ~Environment()=default;
    virtual Win98ImageDirectory::Display Display()=0;
    virtual Handle SystemInstance()=0;
    virtual Handle SystemModule()=0;
    virtual Handle CompatibilityInstance()=0;
    virtual Word ExpectedVersion()=0;
    virtual Handle ApplicationTable()=0;
    virtual Handle TaskTable()=0;
    virtual Handle SystemTable()=0;
    virtual bool Win32Module(Handle)=0;
    virtual Handle ModuleIdentity(Handle)=0;
    virtual Handle Head(Handle table)=0;
    virtual void Head(Handle table,Handle first)=0;
    virtual Record&Item(Handle record)=0;
    virtual Handle AllocateRecord()=0;
    virtual void ReleaseRecord(Handle)=0;
    virtual Word FindAtom(Name)=0;
    virtual Word AddAtom(Name)=0;
    virtual void DeleteAtom(Word)=0;
    virtual Handle FindResource(Handle module,Name name,Word type)=0;
    virtual Handle LoadResource(Handle module,Handle resource)=0;
    virtual Block LockResource(Handle)=0;
    virtual void UnlockResource(Handle)=0;
    virtual bool FreeResource(Handle)=0;
    virtual Handle GlobalHandle(Handle)=0;
    virtual Word GlobalFlags(Handle)=0;
    virtual Handle CreateBitmap(Block,Word cx,Word cy,Word flags)=0;
    virtual Handle CreateIcon(Block,bool icon,Handle owner,Word cx,Word cy,Word flags)=0;
    virtual Handle LoadFile(Name,Word type,Word cx,Word cy,Word flags)=0;
    virtual Handle DrawSystemBitmap(Word resource,Word cx,Word cy,Word flags)=0;
    virtual void BeforeDestroy(Handle)=0;
    virtual Handle FreeImage(Handle)=0;
    virtual void FixSystemImage(Handle)=0;
    virtual void PublishSystemImage(bool icon,unsigned index,Handle)=0;
    virtual void CustomizeSystemImages(bool icon)=0;
};
inline Word KeyFlags(Name n,bool icon){return Word((icon?0:3)|(n.text?4:0));}
inline Handle SharedFind(Environment&e,Handle table,Handle module,Name name,bool icon){ //
    const Word atom=name.text?e.FindAtom(name):Word(name.value);
    if(name.text&&!atom)return 0;
    const Word flags=KeyFlags(name,icon);
    for(Handle h=e.Head(table);h;h=e.Item(h).next){auto&r=e.Item(h);
        if(r.module==module&&r.name==atom&&r.flags==flags){++r.references;return r.image;}}
    return 0;
}
inline bool SharedAdd(Environment&e,Handle table,Handle module,Name name,Handle image,bool icon){ //
    const Handle h=e.AllocateRecord();if(!h)return false;auto&r=e.Item(h);
    r={};r.flags=KeyFlags(name,icon);r.image=image;r.references=1;r.module=module;
    r.name=name.text?e.AddAtom(name):Word(name.value);r.next=e.Head(table);e.Head(table,h);return true;
}
inline Handle SharedRecord(Environment&e,Handle table,Handle image){ //
    for(Handle h=e.Head(table);h;h=e.Item(h).next)if(e.Item(h).image==image)return h;
    return 0;
}
inline void SharedRemove(Environment&e,Handle table,Handle previous){ //
    const Handle h=previous?e.Item(previous).next:e.Head(table);if(!h)return;
    const Record r=e.Item(h);if(r.flags&4)e.DeleteAtom(r.name);if(r.image)e.FreeImage(r.image);
    if(previous)e.Item(previous).next=r.next;else e.Head(table,r.next);e.ReleaseRecord(h);
}
inline void ReleaseResource(Environment&e,Handle resource,Handle module,Word flags){ //
    if(!e.FreeResource(resource)&&e.GlobalHandle(resource)&&(e.GlobalFlags(resource)&0x100))
        if(module==e.SystemInstance()||(flags&0x8000))e.FreeResource(resource);
}
inline Handle FindImageResource(Environment&e,Handle module,Name name,Word type,Word cx,Word cy,Word flags){ //
    Handle resource=0;
    if(type==1||type==3){
        Handle group=e.FindResource(module,name,Word(type+11));if(!group)return 0;
        group=e.LoadResource(module,group);if(!group)return 0;
        const Block b=e.LockResource(group);Word id=0;
        if(b.bytes){ // Windows 98 validates header before calling directory selection.
            if(Win98ImageDirectory::Read(b.bytes)==0 &&
               (Win98ImageDirectory::Read(b.bytes+2)==1||Win98ImageDirectory::Read(b.bytes+2)==2))
                {id=Win98ImageDirectory::Lookup(e.Display(),b.bytes,type==3,cx,cy,flags);e.UnlockResource(group);}
        }
        if(id)resource=e.FindResource(module,{id,false},type);
        ReleaseResource(e,group,module,flags);
    }else if(type==2)resource=e.FindResource(module,name,type);
    return resource?e.LoadResource(module,resource):0;
}
inline Handle LoadResourceImage(Environment&e,Handle owner,Handle module,Name name,Word type,Word cx,Word cy,Word flags){ //
    if(flags&0x10)return e.LoadFile(name,type,cx,cy,flags);
    const Handle resource=FindImageResource(e,module,name,type,cx,cy,flags);if(!resource)return 0;
    Handle result=0;const Block data=e.LockResource(resource);
    if(data.bytes){
        if(type==1||type==3)result=e.CreateIcon(data,type==3,owner,cx,cy,flags);
        else if(type==2)result=e.CreateBitmap(data,cx,cy,flags);
        e.UnlockResource(resource);
    }
    ReleaseResource(e,resource,module,flags);return result;
}
inline Handle LoadIconCursor(Environment&e,Handle module,Name name,Word type,Word cx,Word cy,Word flags){ //
    if(module==e.SystemInstance()&&!name.text){
        const Word id=Word(name.value);Word mapped=id;bool alias=false;
        if(type==3&&id==0x7f87){mapped=0x7f05;alias=true;}
        else if(type!=3&&(id==0x7f81||id==0x7f80)){mapped=id==0x7f80?0x7f86:id;alias=true;}
        if(alias){if(e.ExpectedVersion()>=0x400)return 0;name={mapped,false};}
    }
    Handle owner=0,table=0,key=0;
    if(flags&0x8000){
        owner=e.SystemInstance();
        if(module==e.SystemInstance()){table=e.SystemTable();key=e.SystemModule();}
        else{table=e.Win32Module(module)?e.TaskTable():e.ApplicationTable();key=e.ModuleIdentity(module);}
        const Handle found=SharedFind(e,table,key,name,type==3);if(found)return found;
    }
    const auto d=e.Display();cx=Win98ImageDirectory::Dimension(d,cx,type==3,flags,0,false);
    cy=Win98ImageDirectory::Dimension(d,cy,type==3,flags,0,true);
    const Handle image=LoadResourceImage(e,owner,module,name,type,cx,cy,flags);
    if(image&&(flags&0x8000))SharedAdd(e,table,key,name,image,type==3);
    return image;
}
inline bool OemBitmap(Word id,Word version){
    if(id==0x7fff||id==0x7ff2)return true;
    constexpr Word ids[]={0x7ff6,0x7ff9,0x7ff8,0x7ff7,0x7fe2,0x7ff0,0x7fe6,0x7fe0,0x7fee,0x7fe4,0x7fde,0x7ffc,0x7ffa,0x7ff5,0x7ff3,0x7ffb,0x7ffd,0x7ff4,0x7fe3,0x7fed,0x7fea,0x7feb,0x7fe8,0x7fef,0x7fe5,0x7fdf,0x7ffe,0x7ff1,0x7fe7,0x7fe1,0x7fec,0x7fe9};
    (void)version;for(Word value:ids)if(value==id)return true;return false;
}
inline bool RejectedOemBitmap(Word id,Word version){
    if(version<0x400)return false;
    return id==0x7fff||id==0x7ffc||id==0x7ffa||id==0x7ff5||id==0x7ff3||id==0x7ffb||id==0x7ffd||id==0x7ff4;
}
inline Handle Load(Environment&e,Handle module,Name name,Word type,Word cx,Word cy,Word flags){ //
    const auto d=e.Display();if(!(d.flags&0x40)&&type==2)flags|=0x80;
    if(!module||module==e.CompatibilityInstance())module=e.SystemInstance();
    if(flags&0x10)flags&=0x7fff;
    if(type==0){
        if(module==e.SystemInstance()&&!name.text&&OemBitmap(Word(name.value),e.ExpectedVersion())){
            if(RejectedOemBitmap(Word(name.value),e.ExpectedVersion()))return 0;
            return e.DrawSystemBitmap(Word(name.value),cx,cy,flags);
        }
        return LoadResourceImage(e,0,module,name,2,cx,cy,flags);
    }
    if(type==2){if(!(d.flags&1))flags|=1;return LoadIconCursor(e,module,name,1,cx,cy,flags);}
    if(type==1)return LoadIconCursor(e,module,name,3,cx,cy,flags);
    return Word(type-2); // Raw body result; the public entry rejects invalid types.
}
inline Handle Create(Environment&e,Block data,Word icon,unsigned version,Word cx,Word cy,Word flags){ //
    (void)version; // USER's body does not consume size/version after the public probe.
    const auto d=e.Display();cx=Win98ImageDirectory::Dimension(d,cx,icon,flags,0,false);
    cy=Win98ImageDirectory::Dimension(d,cy,icon,flags,0,true);
    if(!icon&&!(d.flags&1))flags|=1;return e.CreateIcon(data,icon!=0,0,cx,cy,flags);
}
struct ReleaseResult {bool found;bool last;};
inline ReleaseResult SharedRelease(Environment&e,Handle table,Handle image){ //
    const Handle h=SharedRecord(e,table,image);if(!h)return {false,false};auto&r=e.Item(h);--r.references;return {true,r.references==0};
}
inline unsigned Destroy(Environment&e,Handle image,Word flags){ //
    e.BeforeDestroy(image);
    if(!(flags&8)){
        const Handle tables[]={e.TaskTable(),e.ApplicationTable()};
        for(Handle table:tables){
            const auto result=SharedRelease(e,table,image);if(result.found)return (flags&4)?1:unsigned(result.last);
            if(e.TaskTable()==e.ApplicationTable())break;
        }
        if(SharedRelease(e,e.SystemTable(),image).found)return 0;
    }
    const Handle result=e.FreeImage(image);return flags&1?result:1;
}
inline void InitializeSystemImages(Environment&e,bool icon){ // USER, customize is a provider
    constexpr Word icons[]={0x7f00,0x7f03,0x7f02,0x7f01,0x7f04,0x7f05};
    constexpr Word cursors[]={0x7f00,0x7f01,0x7f02,0x7f03,0x7f04,0x7f82,0x7f83,0x7f84,0x7f85,0x7f86,0x7f88,0x7f8a,0x7f8b,0x7f77,0x7f89,0x7f81};
    for(int i=icon?5:15;i>=0;--i){
        const Handle h=Load(e,e.SystemInstance(),{unsigned(i+100),false},icon?1:2,0,0,icon?0x40:0x41);
        e.FixSystemImage(h);e.PublishSystemImage(icon,unsigned(i),h);
        SharedAdd(e,e.SystemTable(),e.SystemModule(),{icon?icons[i]:cursors[i],false},h,icon);
    }
    e.CustomizeSystemImages(icon);
}
} // namespace Win98ImageResource
