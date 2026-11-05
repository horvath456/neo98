// SPDX-License-Identifier: GPL-2.0-or-later
// Included inside namespace User. Native objects replace USER heap pointers.
Win98WindowHit::Window HitWindowState(HWND h){
 Win98WindowHit::Window r;auto*w=Find(h);if(!w)return r;SyncWindowRegion(*w);Gdi::RECT outer={};GetWindowRect(h,&outer);auto client=Client(*w);auto origin=Origin(*w);
 r.next=w->next;r.child=w->first;r.parent=h==Desktop?0:Container(*w);r.style=Style(h);r.extended=w->extended;r.task=w->thread;
 if(!ShowsTree(h))r.style&=~0x10000000u;
 r.kind=Equal(w->class_name,"COMBOBOX")?1:w->button?2:0;r.region=w->region;
 r.outer={short(outer.left),short(outer.top),short(outer.right),short(outer.bottom)};
 int x=origin.x+ClientLeft(*w),y=origin.y+ClientTop(*w);r.client={short(x),short(y),short(x+client.right),short(y+client.bottom)};return r;
}
HWND PointWindow(unsigned operation,HWND h,int x,int y,unsigned flags,void*context,PointHit send,unsigned task){
 struct Provider{
  void*context;PointHit send;unsigned task;
  Win98WindowHit::Window Read(unsigned h){return HitWindowState(h);}bool Valid(unsigned h){return IsWindow(h);}unsigned Desktop(){return GetDesktopWindow();}
  // Window regions are native GDI objects in screen coordinates.
  bool RegionContains(unsigned region,Win98WindowHit::Point p){return !region||Gdi::RegionContainsPoint(region,int(p.x),int(p.y));}
  unsigned ComboChild(unsigned){return 0;} // Original ComboBox internal selection block is not allocated by native USER.
  bool ModalAllowed(unsigned){return true;}unsigned ModalWindow(){return 0;} // No system-modal USER window provider.
  unsigned CurrentTask(){return task;}int SizeX(){return settings.Word(0x338);}int SizeY(){return settings.Word(0x33a);}
  unsigned HitMessage(unsigned h,Win98WindowHit::Point p){unsigned lp=(unsigned short)p.x|(unsigned((unsigned short)p.y)<<16);return send?send(context,h,0x84,0,lp):DefaultMessage(h,0x84,0,lp);}
 }e{context,send,task};Win98WindowHit::Point p{x,y};
 if(!operation)return Win98WindowHit::FromPoint(e,p);
 if(!IsWindow(h))return 0;
 if(operation==3)return Win98WindowHit::RealChild(e,h,p);
 if(flags&0xfff8)return 0;return Win98WindowHit::Child(e,h,p,operation==1?0:flags);
}
