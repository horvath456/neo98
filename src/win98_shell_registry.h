// SPDX-License-Identifier: GPL-2.0-or-later
// Win98 SHLWAPI user-specific registry objects and ANSI helper family.
#pragma once
namespace Win98ShellRegistry
{
using U = unsigned;
constexpr U CurrentUser = 0x80000001, LocalMachine = 0x80000002, ObjectBytes = 0x114;
// Environment memory tokens cover guest objects and native automatic scratch.
// Scratch(e,n).address owns n temporary bytes for that call's lifetime.
template<class E> U Length(E &e, U p) { U n=0; while(e.Read8(p+n))++n; return n; }
template<class E> void Forward(E &e,U to,U from,U count)
{
    U i=0;while(count-i>=4){e.Write32(to+i,e.Read32(from+i));i+=4;}
    while(i<count){e.Write8(to+i,e.Read8(from+i));++i;}
}
template<class E> void Copy(E &e,U to,U from,U count)
{
    if(to>from && to-from<count){while(count){--count;e.Write8(to+count,e.Read8(from+count));}}
    else Forward(e,to,from,count);
}
template<class E> void StringCopy(E &e,U to,U from)
{ Forward(e,to,from,Length(e,from)+1); }
template<class E> void StringCopyN(E &e,U to,U from,U count)
{
    U n=Length(e,from)+1;if(n>count)n=count;
    if(!n)return;Forward(e,to,from,n-1);e.Write8(to+n-1,0);
}
// Handle length-one and null strings before copying registry data.
template<class E> U Append(E &e,U to,U from,U count)
{ StringCopyN(e,to+Length(e,to),from,count);return to; }
template<class E> U Slash(E &e,U path)
{
    if(!path)return 0;
    U n=e.Read8(path)&&!e.Read8(path+1)?1:Length(e,path);
    if(int(n)>=259)return 0;
    U end=path+n;
    if(e.Read8(path)&&e.Read8(e.CharPrev(path,end))!='\\'){e.Write8(end++,'\\');e.Write8(end,0);}
    return end;
}
template<class E> bool Valid(E &e,U key)
{ return key && !e.BadWrite(key,ObjectBytes) && (e.Read32(key)||e.Read32(key+8)); }
// Return the last actual RegCloseKey status.
template<class E> U Close(E &e,U key)
{
    if(!Valid(e,key))return 87;
    U result=0,value=e.Read32(key+8);
    if(value){result=e.RegClose(value);e.Write32(key+8,0);}
    value=e.Read32(key+12);if(value&&value!=LocalMachine)result=e.RegClose(value);
    value=e.Read32(key);if(value){result=e.RegClose(value);e.Write32(key,0);}
    value=e.Read32(key+4);if(value&&value!=CurrentUser)result=e.RegClose(value);
    e.LocalFree(key);return result;
}
// Duplicate handles and preserve the Windows 98 failure behavior.
template<class E> U Open(E &e,U path,U access,U parent,U output,U ignore_user)
{
    if(!output || (parent&&!Valid(e,parent)))return 87;
    const U key=e.LocalAllocate(0x40,ObjectBytes);e.Write32(output,key);if(!key)return 8;
    if(parent)Copy(e,key,parent,ObjectBytes);
    else {e.Write32(key+4,CurrentUser);e.Write32(key+12,LocalMachine);}
    e.Write32(key+0x110,access);
    const U first=key+(ignore_user?8:0),first_root=first+4;
    const U second=key+(ignore_user?0:8),second_root=second+4;
    if(e.Read8(key+16))Slash(e,key+16);
    Append(e,key+16,path,255-Length(e,key+16));
    U result;
    if(e.Read32(first))result=e.RegOpen(e.Read32(first),path,access,first);
    else
    {
        result=e.Read32(first_root)?e.RegOpen(e.Read32(first_root),key+16,access,first):2;
        StringCopy(e,key+16,path);e.Write32(first_root,0);
    }
    if(result==2)
    {
        if(!ignore_user)
        {
            if(e.Read32(second_root))
            {result=e.RegOpen(e.Read32(second_root),key+16,access,second);e.Write32(second_root,0);}
            else if(e.Read32(second))result=e.RegOpen(e.Read32(second),key+16,access,second);
        }
        else {e.Write32(second,0);e.Write32(second_root,0);}
        e.Write32(first,0);e.Write32(first_root,0);
    }
    else
    {
        if(e.Read32(second))e.RegOpen(e.Read32(second),0,e.Read32(key+0x110),second);
        else
        {
            const U root=e.Read32(second_root);
            if(root && root!=LocalMachine && root!=CurrentUser)
                result=e.RegOpen(root,0,e.Read32(key+0x110),second_root);
        }
        if(e.Read32(second)){e.Write32(second_root,e.Read32(second));e.Write32(second,0);}
    }
    if(result){e.Write32(key,0);e.Write32(key+8,0);e.LocalFree(key);e.Write32(output,0);}
    return result;
}
// Open or create a deferred side, then close a borrowed root.
template<class E> U Deferred(E &e,U key,bool create)
{
    U slot=0,root=0;
    if(!e.Read32(key)){slot=key;root=key+4;}
    if(!e.Read32(key+8)){slot=key+8;root=key+12;}
    U result=0;
    if(root && e.Read32(root))
    {
        const U handle=e.Read32(root);
        result=create?e.RegCreate(handle,key+16,0,e.Read32(key+0x110),slot,0):
                      e.RegOpen(handle,key+16,e.Read32(key+0x110),slot);
        const U old=e.Read32(root);
        if(old!=CurrentUser && old!=LocalMachine)e.RegClose(old);
        e.Write32(root,0);e.Write8(key+16,0);
    }
    return result;
}
// Create the requested registry slot.
template<class E> U CreateSlot(E &e,U slot,U root,U path,U access)
{
    if(!e.Read32(root))return 87;
    const U result=e.RegCreate(e.Read32(root),path,0,access,slot,0);e.Write32(root,0);return result;
}
// Query the selected registry side and apply its fallback policy.
template<class E> U Create(E &e,U path,U access,U parent,U output,U flags)
{
    typename E::Scratch local(e,256);const U combined=local.address;
    for(U i=0;i<256;++i)e.Write8(combined+i,0);
    if(!output || (parent&&!Valid(e,parent)) || !(flags&15))return 87;
    if(parent){StringCopy(e,combined,parent+16);Slash(e,combined);}
    Append(e,combined,path,255-Length(e,combined));
    const U key=e.LocalAllocate(0x40,ObjectBytes);e.Write32(output,key);if(!key)return 8;
    U result=0,machine_path=combined,user_path=combined;
    if(parent)
    {
        Copy(e,key,parent,ObjectBytes);
        if(e.Read32(key+8))
        {e.Write32(key+12,e.Read32(key+8));e.Write32(key+8,0);machine_path=path;if(!(flags&12))e.Write8(key+16,0);}
        if(!(flags&12) && e.Read32(key+12)!=LocalMachine)
            result=e.RegOpen(e.Read32(key+12),0,e.Read32(key+0x110),key+12);
        if(e.Read32(key))
        {e.Write32(key+4,e.Read32(key));e.Write32(key,0);user_path=path;if(!(flags&3))e.Write8(key+16,0);}
        if(!(flags&3) && e.Read32(key+4)!=CurrentUser)
            result=e.RegOpen(e.Read32(key+4),0,e.Read32(key+0x110),key+4);
    }
    else {e.Write32(key+12,LocalMachine);e.Write32(key+4,CurrentUser);}
    e.Write32(key+0x110,access);
    if(!result && (flags&12))result=CreateSlot(e,key+8,key+12,machine_path,access);
    if(!result && (flags&3))result=CreateSlot(e,key,key+4,user_path,e.Read32(key+0x110));
    if((flags&3)&&(flags&12))e.Write8(key+16,0);
    else
    {
        if(e.Read8(key+16))Slash(e,key+16);
        Append(e,key+16,path,255-Length(e,key+16));
    }
    if(result)
    {
        U handle=e.Read32(key);if(handle)e.RegClose(handle);
        handle=e.Read32(key+4);if(handle&&handle!=CurrentUser)e.RegClose(handle);
        handle=e.Read32(key+8);if(handle)e.RegClose(handle);
        handle=e.Read32(key+12);if(handle&&handle!=LocalMachine)e.RegClose(handle);
        e.LocalFree(key);e.Write32(output,0);
    }
    return result;
}
// SHQueryValueExA handles REG_EXPAND_SZ allocation,
// size-only query and output/type updates. Scratch bytes model unspecified stack.
template<class E> U QueryExpanded(E &e,U key,U name,U reserved,U type,U data,U bytes)
{
    typename E::Scratch local(e,12);const U size=local.address,kind=size+4,dummy=size+8;
    if(bytes)e.Write32(size,e.Read32(bytes));
    U result=e.RegQuery(key,name,reserved,kind,data,size);
    if(!result)
    {
        if(e.Read32(kind)==1)
        {if(data && e.Read32(size)<e.Read32(bytes))e.Write8(data+e.Read32(size),0);}
        else if(e.Read32(kind)==2)
        {
            const U temporary=e.LocalAllocate(0x40,data?e.Read32(bytes):e.Read32(size));
            if(!temporary)return 14;
            if(data)
            {
                e.Write32(size,e.Expand(data,temporary,e.Read32(bytes)));
                if(e.Read32(size))
                {
                    e.Write32(size,Length(e,temporary)+1);
                    if(e.Read32(size) && e.Read32(size)<=e.Read32(bytes))StringCopyN(e,data,temporary,e.Read32(bytes));
                    else result=e.LastError();
                }
                else result=e.LastError();
            }
            else
            {
                result=e.RegQuery(key,name,reserved,0,temporary,size);
                if(!result){const U n=e.Expand(temporary,dummy,1);if(n>e.Read32(size))e.Write32(size,n);}
            }
            e.LocalFree(temporary);e.Write32(kind,1);
        }
    }
    if(type)e.Write32(type,e.Read32(kind));
    if(bytes)e.Write32(bytes,e.Read32(size));
    return result;
}
// ANSI registry value query.
template<class E> U QuerySlot(E &e,U key,U slot,U name,U type,U data,U bytes)
{
    const U status=e.Read32(slot)?0:Deferred(e,key,false);
    if(status || !e.Read32(slot))return 87;
    return QueryExpanded(e,e.Read32(slot),name,0,type,data,bytes);
}
// Restore caller capacity before the machine-side fallback.
template<class E> U Query(E &e,U key,U name,U type,U data,U bytes,U ignore_user,U fallback,U fallback_bytes)
{
    const U capacity=bytes?e.Read32(bytes):0;
    if(!Valid(e,key))return 87;
    U result=0;
    if(!ignore_user)result=QuerySlot(e,key,key,name,type,data,bytes);
    if(ignore_user || result)
    {
        if(bytes)e.Write32(bytes,capacity);
        result=QuerySlot(e,key,key+8,name,type,data,bytes);
        if(result && fallback && fallback_bytes && data && capacity>=fallback_bytes)
        {Copy(e,data,fallback,fallback_bytes);if(bytes)e.Write32(bytes,fallback_bytes);result=0;}
    }
    return result;
}
// Read the selected value with its configured fallback.
template<class E> U Get(E &e,U path,U name,U type,U data,U bytes,U ignore_user,U fallback,U fallback_bytes)
{
    const U capacity=bytes?e.Read32(bytes):0;typename E::Scratch local(e,4);
    U result=Open(e,path,1,0,local.address,ignore_user);
    if(!result)
    {
        const U key=e.Read32(local.address);result=Query(e,key,name,type,data,bytes,ignore_user,fallback,fallback_bytes);Close(e,key);
    }
    else if(fallback && fallback_bytes && data && capacity>=fallback_bytes)
    {Copy(e,data,fallback,fallback_bytes);if(bytes)e.Write32(bytes,fallback_bytes);result=0;}
    return result;
}
// ANSI registry value update.
template<class E> U WriteSlot(E &e,U key,U slot,U name,U force,U type,U data,U bytes)
{
    const U result=e.Read32(slot)?0:Deferred(e,key,true);
    bool write=force!=0;
    if(!write)write=QueryExpanded(e,e.Read32(slot),name,0,0,0,0)!=0;
    if(!result && e.Read32(slot) && write)return e.RegSet(e.Read32(slot),name,0,type,data,bytes);
    return result;
}
// Write to the selected registry side.
template<class E> U Write(E &e,U key,U name,U type,U data,U bytes,U flags)
{
    if(!Valid(e,key) || (!(flags&10)&&!(e.Read32(key+0x110)&1)))return 87;
    U result=0;
    if(flags&3)result=WriteSlot(e,key,key,name,flags&2,type,data,bytes);
    if(!result && (flags&12))result=WriteSlot(e,key,key+8,name,flags&8,type,data,bytes);
    return result;
}
// Open the path and set its value.
template<class E> U Set(E &e,U path,U name,U type,U data,U bytes,U flags)
{
    typename E::Scratch local(e,4);U result=Create(e,path,3,0,local.address,flags);
    if(!result){const U key=e.Read32(local.address);result=Write(e,key,name,type,data,bytes,flags);Close(e,key);}return result;
}
// A nonempty key is retained and reports the successful open status.
template<class E> U DeleteEmpty(E &e,U key,U name)
{
    typename E::Scratch local(e,12);const U handle=local.address,children=handle+4,values=handle+8;
    U result=e.RegOpen(key,name,0x2001b,handle);
    if(!result)
    {
        if(!e.RegInfo(e.Read32(handle),children,0,values,0) && !e.Read32(children) && !e.Read32(values))result=e.RegDeleteKey(key,name);
        e.RegClose(e.Read32(handle));
    }
    return result;
}
// Both entry points share side-selection and fallback decisions.
template<class E> U Delete(E &e,U key,U name,U flags,bool empty_key)
{
    if(!Valid(e,key))return 87;
    U result=87;
    if(!flags)
    {
        if(!e.Read32(key)){result=Deferred(e,key,false);if(!e.Read32(key)){if(e.Read32(key+8))flags=16;}}
        if(e.Read32(key))flags=1;
    }
    if(flags&1)
    {
        if(!e.Read32(key))Deferred(e,key,false);
        if(e.Read32(key))
        {
            result=empty_key?DeleteEmpty(e,e.Read32(key),name):e.RegDeleteValue(e.Read32(key),name);
            if(result==2)flags=16;
        }
    }
    if(flags&16)
    {
        if(!e.Read32(key+8))Deferred(e,key,false);
        if(e.Read32(key+8))result=empty_key?DeleteEmpty(e,e.Read32(key+8),name):e.RegDeleteValue(e.Read32(key+8),name);
    }
    return result;
}
// ANSI registry paths share these state checks.
template<class E> U EnumerateSlot(E &e,U kind,U key,U slot,const U *args)
{
    const U status=e.Read32(slot)?0:Deferred(e,key,false);
    if(status || !e.Read32(slot))return 87;
    const U handle=e.Read32(slot);
    if(kind==0)return e.RegEnumKey(handle,args[0],args[1],args[2]);
    if(kind==1)return e.RegEnumValue(handle,args[0],args[1],args[2],args[3],args[4],args[5]);
    return e.RegInfo(handle,args[0],args[1],args[2],args[3]);
}
// Public EnumUSKeyA/EnumUSValueA/QueryInfoUSKeyA. Mode11h is unsupported,
// default enumeration never merges the two views into a synthetic union.
template<class E> U Enumerate(E &e,U kind,U key,const U *args,U flags)
{
    if(!Valid(e,key))return 87;
    if(flags==17)return 120;
    if(flags!=0 && flags!=1 && flags!=16)return 87;
    U result=87;
    if(flags==0 || flags==1)result=EnumerateSlot(e,kind,key,key,args);
    if(flags==16 || (!flags && result && result!=234 && (kind==2 || result!=259)))
        result=EnumerateSlot(e,kind,key,key+8,args);
    return result;
}
// SHGetValueA.
template<class E> U GetValue(E &e,U key,U path,U name,U type,U data,U bytes)
{
    typename E::Scratch local(e,4);e.Write32(local.address,0);U result=0,opened=key;
    if(path && e.Read8(path)){result=e.RegOpen(key,path,1,local.address);opened=e.Read32(local.address);}
    if(opened)
    {
        result=QueryExpanded(e,opened,name,0,type,data,bytes);
        if(e.Read32(local.address))e.RegClose(e.Read32(local.address));
    }
    else if(bytes)e.Write32(bytes,0);
    return result;
}
// SHSetValueA also accepts a null subkey.
template<class E> U SetValue(E &e,U key,U path,U name,U type,U data,U bytes)
{
    typename E::Scratch local(e,8);U result=0,opened=key;
    if(path && e.Read8(path))
    {result=e.RegCreate(key,path,e.EmptyString(),2,local.address,local.address+4);opened=e.Read32(local.address);}
    if(!result){result=e.RegSet(opened,name,0,type,data,bytes);if(opened!=key)e.RegClose(opened);}
    return result;
}
// SHDeleteValueA and SHRegDuplicateHKey.
template<class E> U DeleteValue(E &e,U key,U path,U name)
{
    typename E::Scratch local(e,4);U result=e.RegOpen(key,path,2,local.address);
    if(!result){const U opened=e.Read32(local.address);result=e.RegDeleteValue(opened,name);e.RegClose(opened);}return result;
}
template<class E> U Duplicate(E &e,U key)
{
    typename E::Scratch local(e,4);e.Write32(local.address,0);e.RegOpen(key,0,0x2000000,local.address);return e.Read32(local.address);
}
} // namespace Win98ShellRegistry
