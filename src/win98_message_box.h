// SPDX-License-Identifier: GPL-2.0-or-later
// USER32 public records and USER2 message-box selection/command decisions.
#pragma once
namespace Win98MessageBox {
using U=unsigned;
struct Parameters { U size=40,owner=0,instance=0,text=0,caption=0,style=0,
    icon=0,context=0,callback=0,language=0; };
struct Buttons { U count=0,selected=0,ids[4]{},labels[4]{}; };
// USER. The private type6/7 entries overlap the tables.
inline Buttons SelectButtons(U style) {
    const unsigned char starts[]={0,2,5,12,9,16,1,2};
    const unsigned char counts[]={1,2,3,3,2,2,1,0};
    const unsigned char ids[]={1,9,1,2,9,3,4,5,9,6,7,9,6,7,2,9,4,2,9};
    const unsigned char labels[]={1,9,1,2,9,6,5,7,9,3,4,9,3,4,2,9,5,2,9};
    Buttons b;U type=style&15;if(type>7)return b;
    b.count=counts[type]+((style&0x4000)!=0);b.selected=(style>>8)&15;
    if(b.selected>=b.count)b.selected=0;
    for(U i=0;i<b.count;++i){b.ids[i]=ids[starts[type]+i];b.labels[i]=labels[starts[type]+i];}
    return b;
}
// USER. A custom icon still reserves the exclamation layout slot.
inline U Icon(U style) {
    switch(style&0xf0){case 0x10:return 0x7f01;case 0x20:return 0x7f02;
        case 0x30:case 0x80:return 0x7f03;case 0x40:return 0x7f04;default:return 0;}
}
// USER, native handles/strings replace segmented representations.
template<class E> U Run(E&e,Parameters p) {
    if((p.style&0x3000)==0x3000)p.style&=~0x1000u;
    if((p.style&0x3000)==0x1000)p.style&=~0x4000u;
    if(!p.caption)p.caption=e.DefaultCaption();
    Buttons buttons=SelectButtons(p.style);
    U icon=p.style&0xf0,result=0;
    if((p.style&0x3000)==0x1000&&(!icon||icon==0x10)) {
        if(e.DesktopReady())result=e.Dialog(p,buttons);
        if(!result)result=e.HardError(p,buttons);
    }else result=e.Dialog(p,buttons);
    return result;
}
// USER. OK/Cancel require an existing button; help never closes.
enum Action { Ignore,End,Help };
inline Action Command(U command,bool present) {
    command&=65535;
    if(command==9)return Help;
    if(command==1||command==2)return present?End:Ignore;
    return command>=3&&command<=7?End:Ignore;
}
// USER. The template writer gives an MB_OK box one button with the
// table id IDOK. WM_INITDIALOG then looks that button up again with
// GetDlgItem(hwnd,IDOK) and writes IDCANCEL into its control id, so the Escape
// and system-close commands find a target at all. 0F59..0F6F repairs the public
// result back to IDOK. The renumbering is a dialog-initialization decision, not
// a template decision: before WM_INITDIALOG the button really carries IDOK.
// Lookup is the id the fragment passes to GetDlgItem, zero when it does not
// look anything up; ActualButton is the id it then writes into that record.
inline U Lookup(U style){return (style&15)?0:1;}
inline U ActualButton(U style,U id){return !(style&15)&&id==1?2:id;}
inline U Result(U style,U value){if(short(value)==-1)return 0;return !(style&15)&&value?1:value;}
// USER32:. cbSize is copied, not consulted by this wrapper.
template<class E> U Indirect(E&e,U source) {
    U record[10];for(U i=0;i<10;++i)record[i]=e.Read(source+i*4);
    typename E::Scratch scratch(e,336);U title=scratch.address,text=title+80;
    if(record[4]<65536){if(record[2]){e.LoadString(record[2],record[4]&65535,title,80);record[4]=title;}else record[4]=0;}
    if(record[3]<65536){if(record[2]){e.LoadString(record[2],record[3]&65535,text,256);record[3]=text;}else record[3]=0;}
    Parameters p{record[0],record[1],record[2],record[3],record[4],record[5],record[6],record[7],record[8],record[9]};
    return e.Show(p);
}
// USER32:. Both conversions run even if allocation fails.
template<class E> U Wide(E&e,U owner,U text,U title,U style,U language) {
    U first=0,second=0,n=e.Convert(text,0,0);
    if(n){first=e.Allocate(n);e.Convert(text,first,n);}
    n=e.Convert(title,0,0);if(n){second=e.Allocate(n);e.Convert(title,second,n);}
    Parameters p;p.owner=owner;p.text=first;p.caption=second;p.style=style;p.language=language&65535;
    U result=e.Show(p);if(first)e.Free(first);if(second)e.Free(second);return result;
}
}
