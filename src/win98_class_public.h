// SPDX-License-Identifier: GPL-2.0-or-later
// USER32 and USER32->USER class argument/return conversion.
#pragma once
namespace Win98ClassPublic
{
using U=unsigned;
inline U Word(U n){return n&65535;}
inline int SignedWord(U n){return static_cast<short>(n);}
// USER32; thunk stack mapping is an architecture provider.
template<class E> U RegisterEx(E&e,U source)
{
    if(!source)return Word(e.Register16(0));
    e.ProbeRead(source);e.ProbeRead(source+47);
    typename E::Scratch scratch(e,34);const U dest=scratch.address;
    e.Write32(dest,e.Read32(source)-14);e.Write32(dest+4,e.Read32(source+4));e.Write32(dest+8,e.Read32(source+8));
    e.Write16(dest+12,e.Read32(source+12));e.Write16(dest+14,e.Read32(source+16));
    e.Write16(dest+16,e.Module16(e.Read32(source+20)));
    e.Write16(dest+18,e.Read32(source+24));e.Write16(dest+20,e.Read32(source+28));e.Write16(dest+22,e.Read32(source+32));
    const U menu=e.MapString(e.Read32(source+36)),name=e.MapString(e.Read32(source+40));
    e.Write32(dest+24,menu);e.Write32(dest+28,name);e.Write16(dest+32,e.Read32(source+44));
    const U result=Word(e.Register16(dest));e.UnmapString(menu);e.UnmapString(name);return result;
}
// USER32.
template<class E> U Register(E&e,U source)
{
    typename E::Scratch scratch(e,48);const U ex=scratch.address;e.Write32(ex,48);
    for(U i=0;i<10;++i)e.Write32(ex+4+i*4,e.Read32(source+i*4));e.Write32(ex+44,0);return RegisterEx(e,ex);
}
// USER32 thunk USER. Caller cbSize stays untouched; success is atom.
template<class E> U GetInfoEx(E&e,U instance,U name,U output)
{
    if(!name||!output)return 0;
    typename E::Scratch scratch(e,34);const U converted=scratch.address;e.Write32(converted,34);
    const U module=instance?e.Module16(instance):0,mapped=e.MapString(name);
    const U result=Word(e.GetInfo16(module,mapped,converted));
    if(result)
    {
        e.Write32(output+4,e.Read32(converted+4));e.Write32(output+8,e.Procedure32(e.Read32(converted+8)));
        e.Write32(output+12,e.Read16(converted+12));e.Write32(output+16,e.Read16(converted+14));e.Write32(output+20,instance);
        e.Write32(output+24,e.Read16(converted+18));e.Write32(output+28,e.Read16(converted+20));e.Write32(output+32,e.Read16(converted+22));
        e.Write32(output+36,e.UnmapPointer(e.Read32(converted+24)));e.Write32(output+40,name);e.Write32(output+44,e.Read16(converted+32));
    }
    e.UnmapString(mapped);return result;
}
// USER32: non-Ex wrapper returns exactly BOOL1.
template<class E> U GetInfo(E&e,U instance,U name,U output)
{
    typename E::Scratch scratch(e,48);const U ex=scratch.address;e.Write32(ex,48);
    if(!GetInfoEx(e,instance,name,ex))return 0;
    for(U i=0;i<10;++i)e.Write32(output+i*4,e.Read32(ex+4+i*4));e.Write32(output+16,instance);return 1;
}
// USER. The thunk narrows the signed index before its jump tables.
template<class E> U Access(E&e,U window,U index,U value,U flags)
{
    const int offset=SignedWord(index);
    if(offset<0)
    {
        if(offset < -34 || offset > -8 || (offset&1))return 0;
        if(flags&1)
        {
            if(offset==-22||offset==-28||offset==-30)return 0;
            if(offset==-8)value=e.MapString(value);
            else if(offset==-16)value=Word(e.Module16(value));
            else if(offset==-24)value=e.Procedure16(value);
        }
    }
    U result=e.Access16(Word(window),Word(index),value,Word(flags));
    if(offset==-8)result=e.UnmapPointer(result);
    else if(offset==-16)result=e.Module32(result);
    else if(offset==-24)result=e.Procedure32(result);
    else if(offset==-22||offset==-28||offset==-30)result=0;
    return result;
}
} // namespace Win98ClassPublic
