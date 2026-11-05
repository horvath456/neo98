// SPDX-License-Identifier: GPL-2.0-or-later
// USER.EXE class lists/records. Local offsets and far pointers are provider tokens.
#pragma once
namespace Win98Classes
{
using U=unsigned;
inline U Word(U v){return v&65535;}
inline int SignedWord(U v){return static_cast<short>(v);}
template<class E> U Local(E&e,U offset){return e.Local(Word(offset));}
template<class E> U Body(E&e,U record){return e.ReadPointer(Local(e,record));}
template<class E> U Length(E&e,U p){U n=0;while(e.Read8(p+n))++n;return n;}
template<class E> void Copy(E&e,U to,U from,U n){for(U i=0;i<n;++i)e.Write8(to+i,e.Read8(from+i));}
// USER. Return the address of the link to the matching record.
template<class E> U FindLink(E&e,U link,U atom,U instance)
{
    while(const U record=e.Read16(Local(e,link)))
    {
        const U local=Local(e,record);
        if(e.Read16(local+6)==Word(atom) && (!Word(instance)||e.Read16(Body(e,record)+10)==Word(instance)))return link;
        link=Word(record+4);
    }
    return 0;
}
// USER. Current local/global/system lists followed by fallback owner.
template<class E> U Find(E&e,U owner,U name,U instance,U mask)
{
    U atom=e.IsString(name)?e.FindAtom(name):Word(name);if(!atom)return 0;
    if(e.Read16(Local(e,0xc4)))mask&=~1u;
    instance=e.ModuleHandle(Word(instance));bool fallback=false;
    for(;;)
    {
        U found=0;
        if(mask&1)found=FindLink(e,Word(owner+20),atom,instance);
        if(!found&&(mask&2))found=FindLink(e,Word(owner+22),atom,0);
        if(!found&&(mask&4))
        {
            const U system=e.Read16(Local(e,0xb0));
            found=FindLink(e,Word(owner+24),atom,system);
            if(!found)found=FindLink(e,0xfa,atom,system);
        }
        if(found)
        {const U record=e.Read16(Local(e,found));return fallback&&(e.Read8(Local(e,record)+11)&2)?0:found;}
        const U other=e.Read16(Local(e,0xe28));if(owner==other)return 0;
        owner=other;mask&=3;fallback=true;
    }
}
// USER, excluding its public error-reporting wrapper.
template<class E> bool Validate(E&e,U source)
{
    if(e.BadRead(source,34)||e.Read32(source)!=34)return false;
    U instance=e.Read16(source+16);
    if(e.BadInstance(instance))
    {instance=e.StackInstance();e.Write16(source+16,instance);if(e.ExpectedVersion(instance)>=0x30a)return false;}
    const U expected=e.ExpectedVersion(instance);const bool modern=expected>=0x30a,new_style=expected>=0x400;
    const U proc=e.Read32(source+8);if(proc?e.BadCode(proc):modern)return false;
    const U menu=e.Read32(source+24),name=e.Read32(source+28);
    if(menu && (e.IsString(menu)?e.BadString(menu,256):!Word(menu)))return false;
    if(e.IsString(name)?e.BadString(name,256):!Word(name))return false;
    const U brush=e.Read16(source+22);
    if(brush>29&&e.ObjectType(brush)!=2){if(modern)return false;e.Write16(source+22,0);}
    for(U offset=18;offset<=20;offset+=2)if(e.Read16(source+offset)&&!e.ValidIcon(e.Read16(source+offset))&&new_style)return false;
    const U style=e.Read32(source+4),invalid=style&(new_style?0xe7fe0114u:0xe7ff0010u);
    if(invalid){if(new_style||modern)return false;e.Write32(source+4,style&0x1800ffef);}
    for(U offset=12;offset<=14;offset+=2)
    {
        const int extra=SignedWord(e.Read16(source+offset));
        if(extra<0){if(modern)return false;e.Write16(source+offset,0);}
        else if(extra>40&&new_style)return false;
    }
    if(!modern&&(e.ApplicationFlags()&0x100))
    {e.Write16(source+12,e.Read16(source+12)+4);e.Write16(source+14,e.Read16(source+14)+4);}
    return !e.Read16(source+32)||e.ValidIcon(e.Read16(source+32));
}
// USER. The caller supplies the already converted WNDCLASSEX16.
template<class E> U Register(E&e,U source)
{
    const U instance=e.Read16(source+16);U module=e.ModuleHandle(instance),owner;
    if(e.Read8(source+5)&0x80)
    {
        e.Write32(source+4,e.Read32(source+4)&0x10007fff);owner=e.CurrentOwner();
        if(owner==e.Read16(Local(e,0xe28))&&(e.Read8(source+5)&0x40)&&!e.AllowedGlobalProcedure(e.Read32(source+8)))return 0;
        if(!module||!e.ModuleIsLibrary(module))return 0;
    }
    else owner=e.Read16(Local(e,0xe28));
    if(Find(e,owner,e.Read32(source+28),instance,1)){e.DuplicateClassError();return 0;}
    bool global=(e.Read8(source+5)&0x40)!=0;
    const U expected=e.ExpectedVersion(module);if(!global&&expected<0x300)global=true;
    if(global&&Find(e,owner,e.Read32(source+28),instance,2))return 0;
    bool system=(e.Read16(source+6)&0x800)!=0;
    if(!system&&module==e.Read16(Local(e,0xb0)))
    {if(SignedWord(e.CurrentExpectedVersion())>=0x400)return 0;system=true;}
    const U record=e.LocalAllocate(0x40,Word(e.Read16(source+12)+12));if(!record)return 0;
    const U body=e.BodyAllocate(26);if(!body){e.LocalFree(record);return 0;}
    const U p=Local(e,record);e.Write32(p+8,e.Read32(source+4));e.WritePointer(p,body);
    e.Write16(body+24,e.Read16(source+14));e.Write32(body+4,e.Read32(source+8));
    e.Write16(body+8,e.Read16(source+12));e.Write16(body+10,instance);
    e.Write16(body+12,e.Read16(source+18));e.Write16(body+14,e.Read16(source+20));
    e.Write16(body+16,e.Read16(source+22));e.Write32(body+18,e.Read32(source+24));
    e.Write16(body+22,e.Read16(source+32));e.Write16(body+10,module);
    if(global)e.Write8(p+9,e.Read8(p+9)|0x40);
    const U atom=e.AddAtom(e.Read32(source+28));e.Write16(p+6,atom);
    if(!atom){e.LocalFree(record);e.BodyFree(body);return 0;}
    const U menu=e.Read32(source+24);
    if(e.IsString(menu))
    {
        const U length=Word(e.StringLength(menu));
        if(!length)e.Write32(body+18,0);
        else
        {
            const U copy=e.LocalAllocate(0x40,Word(length+1));
            if(!copy){e.DeleteAtom(atom);e.LocalFree(record);e.BodyFree(body);return 0;}
            e.StringCopy(Local(e,copy),menu);e.WriteStringPointer(body+18,Local(e,copy));
        }
    }
    if(e.Read16(body+16)>29)e.BrushReference(e.Read16(body+16),true);
    const U kind=e.ProcedureKind(body+4);e.Write8(p+11,e.Read8(p+11)|(kind&255));
    U link;
    if(e.Read8(p+11)&8)link=0xfa;
    else link=Word(owner+(system?24:global?22:20));
    e.Write16(p+4,e.Read16(Local(e,link)));e.Write16(Local(e,link),record);
    return expected>0x300?e.Read16(p+6):1;
}
// USER. cbSize is caller-owned; return is the class atom, not BOOL1.
template<class E> U GetInfo(E&e,U instance,U name,U output)
{
    if(!Word(instance))instance=e.Read16(Local(e,0xae));
    const U link=Find(e,e.CurrentOwner(),name,instance,7);if(!link)return 0;
    const U record=e.Read16(Local(e,link)),p=Local(e,record),body=Body(e,record);
    e.Write32(output+4,e.Read32(p+8));e.Write32(output+8,e.Read32(body+4));
    e.Write16(output+12,e.Read16(body+8));e.Write16(output+14,e.Read16(body+24));
    e.Write16(output+18,e.Read16(body+12));e.Write16(output+20,e.Read16(body+14));
    e.Write16(output+22,e.Read16(body+16));e.Write32(output+24,e.Read32(body+18));
    e.Write16(output+32,e.Read16(body+22));e.Write16(output+16,instance);e.Write32(output+28,name);
    if((e.Read8(p+11)&3)==1)e.Write32(output+8,e.ProcedureAlias(e.Read32(output+8)));
    e.Write32(output+4,e.Read32(output+4)&0x17fff);return e.Read16(p+6);
}
// USER. Icon/brush and DC realization are separate explicit providers.
template<class E> void Destroy(E&e,U owner,U link,U flags)
{
    const U record=e.Read16(Local(e,link)),p=Local(e,record),body=Body(e,record);
    if(e.Read16(body+2))e.DestroyClassDC(e.Read16(body+2));
    if(e.Read16(Local(e,0x3e6)))e.NotifyClassAtomRemoved(e.Read16(p+6));
    e.DeleteAtom(e.Read16(p+6));e.ReleaseClassBrush(owner,record);
    if(e.Read16(body+22)&&!e.ReleaseDerivedIcon(record))e.ClearIconCache(e.Read16(body+22));
    const U menu=e.Read32(body+18);if(e.IsString(menu))e.LocalFree(e.LocalOffset(e.StringPointer(menu)));
    e.Write16(Local(e,link),e.Read16(p+4));e.BodyFree(body);e.LocalFree(record);(void)flags;
}
// USER, for SignalProc32 with USIG_DLL_UNLOAD_WIN16 (which
// passes the owner at local E28h): the three class lists of that owner lose
// every class of the module. A class that
// still has windows is first handed on, which is not implemented;
// the environment answers that case.
template<class E> void DestroyModule(E&e,U owner,U module)
{
    for(U list=0;list<3;++list)
    {
        U link=Word(owner+20+2*list);
        while(const U record=e.Read16(Local(e,link)))
        {
            if(Word(module)&&e.Read16(Body(e,record)+10)!=Word(module)){link=Word(record+4);continue;}
            if(e.Read16(Body(e,record))&&!e.ClassWindowsRemain(record))return;
            Destroy(e,owner,link,1);
        }
    }
}
// USER: live-window count is checked before any destruction side effect.
template<class E> U Unregister(E&e,U name,U instance)
{
    const U owner=e.CurrentOwner(),link=Find(e,owner,name,instance,3);if(!link)return 0;
    if(e.Read16(Body(e,e.Read16(Local(e,link)))))return 0;
    Destroy(e,owner,link,0);return 1;
}
// USER.
template<class E> U GetName(E&e,U window,U output,U capacity)
{return e.AtomName(e.Read16(Local(e,e.Read16(window+0x24))+6),output,Word(capacity));}
// USER. A brush shared by another class or by the system is retained.
template<class E> bool OwnsBrush(E&e,U owner,U record)
{
    const U brush=e.Read16(Body(e,record)+16);
    for(U i=0;i<29;++i)if(e.Read16(Local(e,0x111e + i*2))==brush)return false;
    for(U list=0;list<3;++list)
        for(U next=e.Read16(Local(e,owner+20+list*2));next;next=e.Read16(Local(e,next)+4))
            if(next!=record&&e.Read16(Body(e,next)+16)==brush)return false;
    return true;
}
// USER.
template<class E> void ReleaseBrush(E&e,U owner,U record)
{
    const U brush=e.Read16(Body(e,record)+16);
    if(brush>29&&e.ObjectType(brush)==2&&OwnsBrush(e,owner,record))e.DeleteBrush(brush);
}
// USER. Validate a new brush before changing references or the record.
template<class E> U SetBrush(E&e,U owner,U record,U value)
{
    const U body=Body(e,record),old=e.Read16(body+16);value=Word(value);
    if(value>29&&e.ObjectType(value)!=2)return 0;
    if(old!=value)
    {
        if(old>29&&OwnsBrush(e,owner,record))e.BrushReference(old,false);
        if(value>29)e.BrushReference(value,true);
        e.Write16(body+16,value);
    }
    return old;
}
// USER and 01E6. Only the internally derived small icon is owned here.
template<class E> bool ReleaseSmallIcon(E&e,U record)
{
    const U p=Local(e,record);if(!(e.Read8(p+11)&4))return false;
    const U body=Body(e,record);e.DestroyDerivedIcon(e.Read16(body+22),8);
    e.Write8(p+11,e.Read8(p+11)&0xfb);e.Write16(body+22,0);return true;
}
template<class E> void DeriveSmallIcon(E&e,U record)
{
    const U body=Body(e,record),p=Local(e,record);
    const U icon=e.CopySmallIcon(e.Read16(Local(e,0xae)),e.Read16(body+12),1,
                               e.Read16(Local(e,0x340)),e.Read16(Local(e,0x342)),0x4000);
    e.Write16(body+22,icon);if(icon)e.Write8(p+11,e.Read8(p+11)|4);
}
// USER. Frame invalidation/enumeration stays with the window provider.
template<class E> U SetIcon(E&e,U window,int index,U value)
{
    const U record=e.Read16(window+0x24),p=Local(e,record),body=Body(e,record);
    U old=e.Read16(body+(index==-14?12:22));value=Word(value);if(old==value)return old;
    bool notify=true;
    if(index==-14)
    {
        if(e.Read16(body+22)&&!ReleaseSmallIcon(e,record))notify=false;
        e.Write16(body+12,value);
    }
    else
    {
        if(e.Read8(p+11)&4){ReleaseSmallIcon(e,record);old=0;}
        e.Write16(body+22,value);
    }
    if(e.Read16(body+12)&&!e.Read16(body+22))DeriveSmallIcon(e,record);
    if(notify)e.RefreshClassIcons(window,record,SignedWord(e.Read16(body))>1);
    return old;
}
// USER. Remove references from the five small-icon cache slots.
template<class E> void ClearSmallIconCache(E&e,U icon)
{for(U i=0;i<5;++i){const U slot=Local(e,0x10f6+4*i);if(e.Read16(slot)==Word(icon))e.Write16(slot,0);}}
// USER, class branch. Procedure-kind detection may rewrite the provider token.
template<class E> void SetProcedure(E&e,U window,U value)
{
    typename E::Scratch scratch(e,4);e.Write32(scratch.address,value);
    const U kind=e.ProcedureKind(scratch.address),record=e.Read16(window+0x24),p=Local(e,record);
    e.Write8(p+11,(e.Read8(p+11)&0xfc)|(kind&255));e.Write32(Body(e,record)+4,e.Read32(scratch.address));
}
// USER. Transfer matching windows in the current owner's subtree.
template<class E> void Reassign(E&e,U window,U old_record,U new_record,U owner)
{
    if(e.Read16(window+0x24)==old_record && e.WindowOwner(window)==owner &&
       window!=e.Window(e.Read32(Local(e,0xaa8))) && window!=e.Window(e.Read32(Local(e,0x240))) &&
       window!=e.Window(e.Read32(Local(e,0x24e))) && window!=e.Window(e.Read32(Local(e,0xb6c))))
    {
        e.Write16(window+0x24,new_record);const U next=Body(e,new_record),old=Body(e,old_record);
        e.Write16(next,e.Read16(next)+1);e.Write16(old,e.Read16(old)-1);if(!e.Read16(old))return;
    }
    U child=e.Read32(window+4);
    while(child)
    {
        Reassign(e,e.Window(child),old_record,new_record,owner);
        if(!e.Read16(Body(e,old_record)))return;
        child=e.Read32(e.Window(child));
    }
}
// USER. Copy a system class into an owner-specific class before mutation.
template<class E> U Clone(E&e,U window,U old_record)
{
    const U old=Body(e,old_record),bytes=Word(e.Read16(old+8)+12);
    const U record=e.LocalAllocate(0x40,bytes);if(!record)return 0;
    const U body=e.BodyAllocate(26);if(!body){e.LocalFree(record);return 0;}
    const U p=Local(e,record);e.MemoryCopy(p,Local(e,old_record),bytes);e.MemoryCopy(body,old,26);
    e.Write8(p+11,e.Read8(p+11)&0xf7);e.WritePointer(p,body);e.Write16(body,1);
    const U atom=e.Read16(Local(e,old_record)+6);
    if(atom>=0xc000)
    {
        typename E::Scratch name(e,32);e.AtomName(atom,name.address,32);e.AddAtom(e.StringToken(name.address));
    }
    else e.AddAtom(atom);
    const U owner=e.WindowOwner(window),link=Word(owner+24);
    e.Write16(p+4,e.Read16(Local(e,link)));e.Write16(Local(e,link),record);
    e.Write16(window+0x24,record);e.Write16(old,e.Read16(old)-1);
    if(e.Read16(old))Reassign(e,e.Window(e.Read32(Local(e,0xaa8))),old_record,record,owner);
    return record;
}
// USER, including 16-bit signed offset/length arithmetic.
template<class E> U Access(E&e,U window,U offset,U value,U flags)
{
    U record=e.Read16(window+0x24),p=Local(e,record),body=Body(e,record);
    if((flags&1)&&(e.Read8(p+11)&8))
    {
        if(e.Read16(p+6)==0x8002)e.WarnSystemDialogClass();
        record=e.CloneSystemClass(window,record);if(!record)return 0;p=Local(e,record);body=Body(e,record);
    }
    const int index=SignedWord(offset);U old;
    if(index>=0)
    {
        const U bytes=(flags&2)?4:2;
        if(SignedWord(offset+bytes)>SignedWord(e.Read16(body+8)))return 0;
        const U target=Local(e,record+Word(offset)+12);old=e.Read16(target);
        if(flags&2)old|=e.Read16(target+2)<<16;
        if(flags&1){e.Write16(target,value);if(flags&2)e.Write16(target+2,value>>16);}return old;
    }
    switch(index)
    {
    case -34:case -14:
        if(flags&1)return e.SetClassIcon(window,index,Word(value));
        if(index==-14)return e.Read16(body+12);
        if((e.Read8(window+0x39)&1)&&(e.Read8(p+11)&4))return 0;
        return e.Read16(body+22);
    case -32:return e.Read16(p+6);
    case -26:
        old=e.Read32(p+8)&0x17fff;
        if(flags&1)e.Write32(p+8,(e.Read32(p+8)&0xfffe8000)|value);return old;
    case -24:
        old=e.Read32(body+4);if((e.Read8(p+11)&3)==1)old=e.ProcedureAlias(old);
        if(flags&1)e.SetClassProcedure(window,value);return old;
    case -20:
        old=e.Read16(body+8);if((flags&1)&&!(e.Read8(window+0x2e)&0x40))e.Write16(body+8,value);return old;
    case -18:
        old=e.Read16(body+24);if(flags&1)e.Write16(body+24,value);return old;
    case -16:return e.Read16(body+10);
    case -12:old=e.Read16(body+14);if(flags&1)e.Write16(body+14,value);return old;
    case -10:old=e.Read16(body+16);if(flags&1)e.SetClassBrush(e.WindowOwner(window),record,Word(value));return old;
    case -8:return e.Read32(body+18);
    default:return 0;
    }
}
} // namespace Win98Classes
