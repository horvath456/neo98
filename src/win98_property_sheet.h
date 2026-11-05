// SPDX-License-Identifier: GPL-2.0-or-later
// COMCTL32 4.72 PropertySheet controller and page-template cores.
// Exact entry points and remaining presentation boundary:
#pragma once
namespace Win98PropertySheet {
using U=unsigned;
inline bool HeaderSize(U size){return size==36||size==40||size==52;}
inline bool String(U p){return (p&0xffff0000u)!=0;}
// / : resource identifiers stay integers. No size-dependent
// suppression of selected extended fields: caller must provide the original ABI.
inline bool HeaderString(U flags,U i){
    switch(i){case 0:return flags&4;case 1:return !(flags&0x01402020);
    case 2:return flags&0x40;case 3:return (flags&0x01002000)&&(flags&0x8000)&&!(flags&0x10000);
    default:return (flags&0x01002000)&&(flags&0x80000)&&!(flags&0x100000);}
}
inline U HeaderOffset(U i){return i==0?16:i==1?20:i==2?28:i==3?40:48;}
template<class E> bool ConvertHeader(E&e,U source,U out){
    e.Copy(out,source,e.Read(source));e.Write(out+4,e.Read(out+4)|0x00800000);
    U allocated[5]={};
    for(U i=0;i<5;++i){U field=out+HeaderOffset(i);
        if(HeaderString(e.Read(out+4),i)&&String(e.Read(field))){
            U old=e.Read(field);
            U p=e.CloneA(old);e.Write(field,p);allocated[i]=p;
            if(!p){while(i)if(allocated[--i])e.Free(allocated[i]);return false;}
        }
    }return true;
}
template<class E> void FreeHeader(E&e,U header){
    for(U i=0;i<5;++i)if(HeaderString(e.Read(header+4),i)){
        U p=e.Read(header+HeaderOffset(i));if(String(p))e.Free(p);
    }
}
// ownership transfers even when public-header validation fails.
// Inline descriptors are borrowed; supplied page handles are destroyed backwards.
template<class E> void ReleaseInput(E&e,U header){
    if(e.Read(header+4)&8)return;
    U i=e.Read(header+24);
    while(static_cast<int>(--i)>=0)e.DestroyPage(e.Read(e.Read(header+32)+4*i));
}
// /. Present takes ownership of the allocated state
// and Unicode pages. It is a real modal/modeless UI provider, never a success stub.
template<class E> U Collect(E&e,U header,bool ansi){
    if(!HeaderSize(e.Read(header))||(e.Read(header+4)&0xfc000000u)||e.Read(header+24)>=100){
        ReleaseInput(e,header);return ~0u;
    }
    U state=e.Allocate(0x27c);if(!state){ReleaseInput(e,header);return ~0u;}
    e.Write(state+0x60,0);e.Copy(state+4,header,e.Read(header));
    if(!(e.Read(state+8)&0x100))e.Write(state+0x28,0);
    e.Write(state+0x24,state+0xec);
    U input=e.Read(header+32);
    if(e.Read(state+8)&8){
        U index=0;
        while(static_cast<int>(index)<static_cast<int>(e.Read(state+0x1c))){
            U page=e.CreatePage(input,ansi,e.Read(state+0x60)&8);
            e.Write(e.Read(state+0x24)+index*4,page);
            if(!page){--index;e.Write(state+0x1c,e.Read(state+0x1c)-1);}
            input+=e.Read(input);++index;
        }
    }else{
        e.Write(state+0x1c,0);
        for(U i=0;i<e.Read(header+24);++i){
            U page=e.Read(e.Read(header+32)+i*4);
            if(!e.LocalSize(page-24))page=e.CreatePage(page,ansi,e.Read(state+0x60)&8);
            U count=e.Read(state+0x1c);e.Write(e.Read(state+0x24)+count*4,page);
            if(page)e.Write(state+0x1c,count+1);
        }
    }
    for(U i=0;i<e.Read(state+0x1c);++i){U p=e.Read(state+0x24)+4*i;e.Write(p,e.Read(e.Read(p)-8));}
    if(!(e.Read(state+8)&0x200))for(U i=0;i<e.Read(state+0x1c);++i){
        if(e.Read(e.Read(e.Read(state+0x24)+4*i)+4)&0x20){e.Write(state+8,e.Read(state+8)|0x200);break;}
    }
    return e.Present(state);
}
template<class E> U Ansi(E&e,U header,U scratch){
    if(!HeaderSize(e.Read(header))||!ConvertHeader(e,header,scratch)){ReleaseInput(e,header);return ~0u;}
    U result=Collect(e,scratch,true);FreeHeader(e,scratch);return result;
}
template<class E> U Wide(E&e,U header){return Collect(e,header,false);}
// use each Unicode object's saved public handle on UI-creation
// failure. ANSI shadows must not be mistaken for independent pages.
template<class E> void ReleaseCollected(E&e,U state){
    U i=e.Read(state+0x1c);
    while(static_cast<int>(--i)>=0){U page=e.Read(e.Read(state+0x24)+i*4);e.DestroyPage(e.Read(page-24));}
    e.Free(state);
}
// / BF EDEBAF and BF EDEC48. Named starts retain the original token
// until a matching title replaces it; duplicate matches select the last page.
struct Start {U index,name;};
template<class E> Start BeginStart(E&e,U state,U scratch){
    Start start{e.Read(state+0x20),0};
    if(e.Read(state+8)&0x40){start.name=start.index;
        if(!String(start.name)){e.WriteWord(scratch,0);e.LoadStartString(e.Read(state+0x10),start.name&0xffff,scratch,128);start.name=scratch;}
    }return start;
}
template<class E> void MatchStart(E&e,Start&start,U flags,U pageIndex,U title){
    if((flags&0x40)&&e.CompareWide(start.name,title)==0)start.index=pageIndex;
}
// Ordinary property sheets choose resource 1006/1008. Both original resources
// require TCS_MULTILINE. Wizard selection belongs to the unported cold branches.
inline U StandardTemplate(U flags,bool alternate){return flags&0x01402020?0:1006+(alternate?2:0);}
// temporarily replace only the template style while creating a page;
// the caller restores oldStyle after creation, including exceptional cleanup.
template<class E> U PageStyle(E&e,U data,U&oldStyle){
    U field=data+(e.Word(data+2)==0xffff?12:0);oldStyle=e.Read(field);
    e.Write(field,(oldStyle&0x02000068)|0x40010404);return field;
}
// output is the original 0x1c8-byte PAGEINFO structure. Resource,
// icon, string-table and UTF16-copy operations are explicit providers.
template<class E> U PageInfo(E&e,U page,U out,U language,U wanted){
    e.Zero(out,0x1c8);U flags=e.Read(page+4);
    if(flags&0x8000)return e.LegacyInfo(page,out,language,wanted);
    if(wanted&1){if(flags&2)e.Write(out+8,e.Read(page+16));
        else if(flags&4)e.Write(out+8,e.LoadIcon(e.Read(page+8),e.Read(page+16)));}
    U resource=0,data=0;
    if(flags&1)data=e.Read(page+12);
    else{U found=e.FindDialog(e.Read(page+8),e.Read(page+12),language);if(!found)return 0;
        resource=e.LoadResource(e.Read(page+8),found);if(!resource)return 0;
        data=e.LockResource(resource);if(!data){e.FreeResource(resource);return 0;}}
    bool extended=e.Word(data+2)==0xffff;
    e.Write(out+0x5c,extended);U style=e.Read(data+(extended?12:0));e.Write(out+0x60,style);
    U width=e.Word(data+(extended?22:14)),height=e.Word(data+(extended?24:16));
    if(!extended){width=static_cast<U>(static_cast<short>(width));height=static_cast<U>(static_cast<short>(height));}
    e.Write(out,width);e.Write(out+4,height);
    e.Write(out+0x58,((flags&0x10)||(extended&&(e.Read(data+8)&0x2000)))?1:0);
    if(wanted&6){
        if(flags&8){U title=e.Read(page+20);if(String(title))e.CopyWide(out+0x64,title,0xb2);
            else e.LoadString(e.Read(page+8),title&0xffff,out+0x64,0xb2,language);}
        bool font=style&0x40;
        if(font||!(flags&8)){
            U text=data+(extended?26:18);
            text+=e.Word(text)==0xffff?4:2*(e.WideLength(text)+1);
            text+=2*(e.WideLength(text)+1);
            if(!(flags&8))e.CopyWide(out+0x64,text,0xb2);
            if(font&&(wanted&2)){
                U p=text+2*(e.WideLength(text)+1);e.WriteWord(out+12,e.Word(p));p+=2;
                if(extended){p+=2;e.Write(out+0x50,e.Byte(p++));e.Write(out+0x54,e.Byte(p++));}
                else{e.Write(out+0x50,0);e.Write(out+0x54,1);}
                e.CopyWide(out+14,p,32);
            }
        }
    }
    if(!(flags&1))e.FreeResource(resource);return 1;
}
// /5955: target comes from the tab item's stored page HWND, not from
// page index or dialog owner. Notify builds the 12-byte NMHDR plus lParam.
template<class E> U Notify(E&e,U state,U index,U code,U parameter){
    U tab=e.Read(state+0x3c),target=tab?e.TabPage(tab,index):0;
    return e.Notify(target,e.Read(state),code,parameter);
}
}
