// SPDX-License-Identifier: GPL-2.0-or-later
// Native window storage providers. Included inside namespace User.
void ParentStorage(HWND h,HWND parent,unsigned style,unsigned extended)
{
    if(auto*w=Find(h)){
        if(h==Desktop)return;
        w->parent=parent==Desktop?0:parent;
        w->style=style;w->extended=extended;dirty=true;
    }
}
unsigned ParentLinkService(unsigned op,HWND h,HWND after,HWND parent)
{
    auto*w=Find(h);if(!w||h==Desktop)return 0;
    if(op==3){Unlink(h,*w);return 1;}
    if(op==4){NormalizeEdge(*w);Layout(h);Metrics(*w);dirty=true;return 1;}
    if(op==5)return FrontPosition(*w);
    if(op==6){
        if(!Find(parent)||Container(*w)!=parent)return 0;
        if(after&&(!Find(after)||Container(*Find(after))!=parent))return 0;
        LinkAfter(h,*w,after);Layout(h);dirty=true;return 1;
    }
    return 0;
}
