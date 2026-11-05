// SPDX-License-Identifier: GPL-2.0-or-later
// Included within USER's native window provider. Original state remains separate
// from host handles, fixed window-pool storage, the paint pass and message delivery.
namespace {
Win98Scroll::Owner ScrollOwner(Window&w){return{w.scroll_allocated,w.style,w.nonclient_flags,w.scroll};}
void ScrollCommit(HWND h,Window&w,const Win98Scroll::Owner&o,const Win98Scroll::Effects&e,bool *layout){
 w.scroll_allocated=o.allocated;w.scroll=o.records;w.style=o.style;
 if(layout)*layout=e.layout;else if(e.layout)Layout(h);
 if(e.layout||e.paint)dirty=true;
}
}
unsigned ScrollState(HWND h,unsigned op,unsigned bar,Win98Scroll::Info*info,unsigned value,bool *layout){
 if(layout)*layout=false;
 auto*w=Find(h);if(!w||bar>3||bar==2)return 0;
 if((op==0||op==1)&&(!info||!Win98Scroll::Valid(*info)))return 0;
 // SCROLLBAR owns an inline record, initially zero. It must not acquire
 // nonclient WS_[HV]SCROLL styles or the nonclient default range 0..100.
 if(w->common==10){
  if(op==4)return(w->scroll.disabled>>((bar==1)*2))&3;
  if(!w->scroll_allocated){w->scroll={};w->scroll.bars[0].maximum=w->scroll.bars[1].maximum=0;w->scroll_allocated=true;}
  auto&record=w->scroll.bars[bar==1];
  if(op==0){unsigned tracked=0;bool tracking=ScrollTrackPosition(h,bar,tracked);return Win98Scroll::Query(record,*info,tracking,tracked);}
  if(op==1){unsigned visible=0,result=0;bool changed=Win98Scroll::Update(record,*info,visible,result);if(changed&&(value&65535))dirty=true;return result;}
  if(op==3){if(value&~3u)return 0;bool changed=Win98Scroll::Enable(w->scroll,bar,value);if(changed)dirty=true;return changed;}
  return 0;
 }
 auto o=ScrollOwner(*w);Win98Scroll::Effects e;
 if(op==0){
  if(!o.allocated)return 0;
  // USER. SIF_TRACKPOS answers the live position of the running
  // tracker, but only when the tracked window and bar are the ones asked
  // about; otherwise it answers the bar's own position. The original reads
  // the tracking record through its data word 10DEh and compares its window
  // at +0x18 and its bar at +0x1c. COMCTL32's list view reads exactly this
  // while it handles SB_THUMBTRACK, so without it a thumb drag moves
  // nothing.
  unsigned tracked=0;const bool tracking=ScrollTrackPosition(h,bar,tracked);
  return Win98Scroll::Query(o.records.bars[bar==1],*info,tracking,tracked);
 }
 if(op==1)e=Win98Scroll::Set(o,bar,*info,value&65535,IsVisible(h));
 else if(op==2)e=Win98Scroll::Show(o,bar,(value&65535)!=0);
 else if(op==3){value&=65535;if(value&~3u)return 0;Win98Scroll::EnableWindow(o,bar,value,IsVisible(h),true,true,e);}
 else return 0;
 ScrollCommit(h,*w,o,e,layout);return e.result;
}
