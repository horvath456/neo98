// SPDX-License-Identifier: GPL-2.0-or-later
// Included inside USER's native window provider, after Fill/Edge/At.
// USER's pressed-page state, the word pair at its data 0748h. One bar at a
// time holds a page region down, and inverts that region while it does.
Win98Scroll::Press scroll_press;

// The thumb a running drag draws. The stored bar position does not move until
// the drag ends with SB_THUMBPOSITION, so the geometry gets the tracked value.
HWND scroll_thumb_window=0;unsigned scroll_thumb_vertical=0,scroll_thumb_position=0;
bool scroll_thumb_active=false;
// The pushed arrow. USER keeps the two press bits in the window word at 3Ah;
// the paint pass needs the window, the axis and which end is down (0 or 1).
HWND arrow_window=0;unsigned arrow_vertical=0,arrow_part=0xffff;

// The geometry of one bar inside a known screen rectangle. Answers false when
// the bar has no drawable extent.
bool ScrollBarCalc(HWND h,const Window &w,unsigned vertical,const Gdi::RECT &rect,
                   Win98Scroll::Geometry &g)
{
 if(!w.scroll_allocated){w.scroll=Win98Scroll::Window{};w.scroll_allocated=true;}
 Win98Scroll::Bar bar=w.scroll.bars[vertical];
 if(scroll_thumb_active&&scroll_thumb_window==h&&scroll_thumb_vertical==vertical)
  bar.position=scroll_thumb_position;
 if(!Win98Scroll::Calculate(bar,vertical?rect.top:rect.left,
  vertical?rect.bottom:rect.right,vertical?rect.left:rect.top,vertical?rect.right:rect.bottom,
  settings.Word(vertical?0x306:0x308),g))return false;
 return g.start<g.end&&g.cross_start<g.cross_end;
}
// A nonclient bar sits beside the client rectangle the geometry core produced.
bool ScrollGeometry(HWND h,const Window &w, unsigned vertical, Gdi::POINT origin,
                    Gdi::RECT &rect, Win98Scroll::Geometry &g)
{
 const auto c=w.client;
 rect=At(vertical?Gdi::RECT{c.right,c.top,c.right+settings.Word(0x2e2),c.bottom}:
  Gdi::RECT{c.left,c.bottom,c.right,c.bottom+settings.Word(0x2e4)},origin);
 return ScrollBarCalc(h,w,vertical,rect,g);
}
// USER. A SCROLLBAR control is one bar filling its whole window; its
// axis comes from SBS_VERT rather than from a nonclient flag.
bool ScrollControlGeometry(HWND h,const Window &w,Gdi::RECT &rect,
                           Win98Scroll::Geometry &g,unsigned &vertical)
{
 vertical=(w.style&1)!=0;
 const auto o=Origin(w);
 rect={o.x,o.y,o.x+w.width,o.y+w.height};
 return ScrollBarCalc(h,w,vertical,rect,g);
}

// USER asks the owner for the track brush with WM_CTLCOLORSCROLLBAR.
// The native window queue is asynchronous, so the answer arrives through this
// named provider. Without one the owner's default answer stands: USER's own
// dither brush, which is what the implementation then paints.
void ScrollPaintAt(HWND h,const Window&w,unsigned vertical,const Win98Scroll::Geometry&g){
 auto part=[&](int a,int z){return vertical?Gdi::RECT{g.cross_start,a,g.cross_end,z}:Gdi::RECT{a,g.cross_start,z,g.cross_end};};
 unsigned disabled=(w.scroll.disabled>>(vertical*2))&3;
 const unsigned*palette=Win98Frame::SystemColors();
 const unsigned owner_brush=scroll_brush?scroll_brush(scroll_brush_context,h,screen,0x0137u):0;
 // USER. A brush other than COLOR_BTNHIGHLIGHT's and other than USER's
 // own dither first gets a flat sunken edge on the two long sides.
 unsigned edge=0,flags=0;
 const bool custom=owner_brush&&Win98Scroll::TrackEdge(owner_brush,0x100u+Win98Frame::ColorBtnHighlight,
  0x380u,vertical,edge,flags);
 auto track=[&](int a,int z){if(a>=z)return;auto r=part(a,z);
  if(custom){Win98Draw::DrawEdge(screen,&r,edge,flags);
   if(!Gdi::FillRect(screen,&r,owner_brush))dirty=true;return;}
  if(owner_brush){if(!Gdi::FillRect(screen,&r,owner_brush))dirty=true;return;}
  for(int y=r.top;y<r.bottom;++y)for(int x=r.left;x<r.right;++x)
   Fill({x,y,x+1,y+1},palette[(x+y)&1?Win98Frame::ColorBtnHighlight:Win98Frame::ColorBtnFace]);};
 struct TrackProvider {
  decltype(track)&fill;decltype(part)&rect;
  void Track(int a,int z){fill(a,z);}
  void Thumb(int a,int z){auto r=rect(a,z);Win98Draw::ButtonFrame(screen,&r,0,0);}
  // USER, a DSTINVERT over the rectangle the tracker stored.
  void Invert(const Win98Scroll::Press&p){
   Gdi::PatBlt(screen,p.left,p.top,p.right-p.left,p.bottom-p.top,Gdi::DSTINVERT);}
 }provider{track,part};
 Win98Scroll::DrawTrack(provider,g,disabled,vertical,h,&scroll_press);
 // The original DFC_SCROLL path draws both the frame and the Marlett arrow.
 // USER draws a held arrow pushed for as long as the pointer stays on
 // it. Part 0 is the arrow before the track, part 1 the one after it.
 const bool pressed_here=arrow_window==h&&arrow_vertical==vertical;
 for(unsigned end=0;end<2;++end){
  auto r=part(end?g.track_end:g.start,end?g.end:g.track_start);if(r.left>=r.right||r.top>=r.bottom)continue;
  const unsigned direction=vertical?(end?1u:0u):(end?3u:2u);
  unsigned state=direction|(disabled&(1u<<end)?(unsigned)Win98Frame::Inactive:0u);
  if(pressed_here&&arrow_part==end)state|=Win98Frame::Pushed;
  Win98Frame::DrawFrameControl(screen,&r,Win98Frame::Scroll,state);
 }
}
void ScrollPaint(HWND h,const Window&w,unsigned vertical,Gdi::POINT origin){
 Gdi::RECT rect;Win98Scroll::Geometry g;
 if(!ScrollGeometry(h,w,vertical,origin,rect,g))return;
 ScrollPaintAt(h,w,vertical,g);
}
// SBS_SIZEBOX and SBS_SIZEGRIP draw the size box instead of a bar.
void ScrollControlPaint(HWND h,const Window&w){
 const auto o=Origin(w);
 Gdi::RECT box={o.x,o.y,o.x+w.width,o.y+w.height};
 if(box.left>=box.right||box.top>=box.bottom)return;
 if(w.style&0x18){
  Win98Frame::DrawFrameControl(screen,&box,Win98Frame::Scroll,8); // DFCS_SCROLLSIZEGRIP
  return;
 }
 Gdi::RECT rect;Win98Scroll::Geometry g;unsigned vertical=0;
 if(!ScrollControlGeometry(h,w,rect,g,vertical))return;
 ScrollPaintAt(h,w,vertical,g);
}
