// SPDX-License-Identifier: GPL-2.0-or-later
// Owner-draw callbacks run on the window thread. Render only copies completed
// pixels, as it does for popup menus; a callback may yield or destroy its menu.
struct OwnerBarView {
    HWND window=0;HMENU menu=0;unsigned generation=0;
    Gdi::RECT rect{};unsigned char *pixels=nullptr;bool painting=false;
};
OwnerBarView owner_bars[MaxWindows];
void ReleaseOwnerBar(HWND h)
{
    const unsigned slot=(h&1023)-1;
    if(slot>=MaxWindows||owner_bars[slot].window!=h)return;
    delete[] owner_bars[slot].pixels;owner_bars[slot]={};
}
bool OwnerBarDue(unsigned slot)
{
    const auto &w=windows[slot];const auto &v=owner_bars[slot];
    if(!w.used||!w.ready||!w.redraw||v.painting)return false;
    if((active_thread&&w.thread!=active_thread)||(active_process&&w.process!=active_process))return false;
    const auto *m=FindMenu(w.menu);if(!m)return false;
    if(v.window==Handle(slot)&&v.menu==w.menu&&v.generation==dirty.generation)return false;
    for(unsigned i=0;i<m->count;++i)if((m->items[i].type&0x100)||m->items[i].bitmap==0xffff)
        return IsVisible(Handle(slot))&&!Locked(Handle(slot));
    return false;
}
bool PendingOwnerBars()
{
    for(unsigned i=0;i<MaxWindows;++i)if(OwnerBarDue(i))return true;
    return false;
}
unsigned ServeOwnerBars(HWND only)
{
    unsigned painted=0;
    for(unsigned slot=0;slot<MaxWindows;++slot){
        const HWND h=Handle(slot);if((only&&h!=only)||!OwnerBarDue(slot))continue;
        auto *w=Find(h);const auto rect=w->menu_bar;const HMENU handle=w->menu;
        const int width=rect.right-rect.left,height=rect.bottom-rect.top;
        auto &v=owner_bars[slot];
        if(width<=0||height<=0||width>int(settings.metrics[0])||height>int(settings.metrics[1]))continue;
        auto *pixels=new unsigned char[size_t(width)*height*4]{};
        const auto dc=pixels?Gdi::CreateSurfaceDC({pixels,width,height,width*4,32}):0;
        if(!dc){delete[] pixels;continue;}
        v.window=h;v.painting=true;
        Gdi::SetViewportOrgEx(dc,-rect.left,-rect.top);
        Gdi::SelectObject(dc,system_font);
        Fill(dc,rect,Win98Frame::SystemColors()[Win98MenuDraw::ColorMenu]);
        MenuOwnerEnvironment environment;
        for(unsigned i=0;;++i){
            w=Find(h);auto *m=FindMenu(handle);
            if(!w||w->menu!=handle||!m||i>=m->count||i>=w->menu_items)break;
            auto item=m->items[i];if(!(item.type&0x100)&&item.bitmap!=0xffff)continue;
            const auto box=w->menu_item[i];
            item.x=box.left;item.y=box.top;item.width=box.right-box.left;item.height=box.bottom-box.top;
            if(item.type&0x100)OD::DrawOwnerItem(environment,m,handle,item,false,0,1,h,dc);
            else{
                const int saved=Gdi::SaveDC(dc);
                Gdi::SetViewportOrgEx(dc,0,0);
                item.x=box.left-rect.left+3;
                OD::DrawOwnerItem(environment,m,handle,item,true,0,1,h,dc);
                if(saved)Gdi::RestoreDC(dc,saved);
            }
        }
        Gdi::DeleteDC(dc);w=Find(h);
        if(v.window==h){
            v.painting=false;
            if(w&&w->menu==handle&&w->menu_bar.left==rect.left&&w->menu_bar.top==rect.top&&
               w->menu_bar.right==rect.right&&w->menu_bar.bottom==rect.bottom){
                delete[] v.pixels;v.pixels=pixels;pixels=nullptr;
                v.rect=rect;v.menu=handle;v.generation=dirty.generation;
                w->native_invalid=true;++painted;
            }
        }
        delete[] pixels;
    }
    return painted;
}
void PaintOwnerBarItem(const Window &w,Gdi::POINT p,const Gdi::RECT &box)
{
    if(&w==&desktop)return;
    const unsigned slot=unsigned(&w-windows);if(slot>=MaxWindows)return;
    const auto &v=owner_bars[slot];
    if(v.window!=Handle(slot)||v.menu!=w.menu||!v.pixels)return;
    const int width=v.rect.right-v.rect.left,height=v.rect.bottom-v.rect.top;
    Gdi::IntersectClipRect(screen,box.left,box.top,box.right,box.bottom);
    Gdi::CopySurface(screen,p.x+v.rect.left,p.y+v.rect.top,{v.pixels,width,height,width*4,32});
}
