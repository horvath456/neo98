// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Original Win98 USER layout bookkeeping. Native tasks, windows, keyboard
// drivers and registry access are explicit providers, not original addresses.
namespace Win98KeyboardLayout {
using U=unsigned;
struct Entry { U layout, signature, charset; };
struct State { U enabled=0,count=0,active=0,notified=0;Entry entries[16]{}; };
struct Window { U next=0,owner=0,popup=0,task=0,style=0,atom=0; };
// USER. LOWORD(thread)==1 selects the system's active entry.
template<class E> U Get(E&e,const State&s,U thread) {
 if(!s.enabled)return 0;
 if((thread&65535)==1)return s.entries[s.active].layout;
 const U task=thread?e.ResolveThread(thread):e.CurrentTask();
 return task?e.ThreadLayout(task):0;
}
// USER. The public thunk narrows capacity to WORD; null output
// requests the loaded count even when the caller supplies capacity zero.
template<class E> U List(E&e,const State&s,U capacity,U output) {
 U n=s.count;if(!n || !output)return n;
 capacity&=65535;if(capacity<n)n=capacity;
 for(U i=0;i<n;++i)e.StoreLayout(output+i*4,s.entries[i].layout);
 return n;
}
inline U Find(const State&s,U hkl) { for(U i=0;i<s.count;++i)if(s.entries[i].layout==hkl)return i;return ~0u; }
// USER, for its actual callers' -1, 0 and +1 directions.
inline U Neighbor(const State&s,U hkl,int direction) {
 const U i=Find(s,hkl);if(i==~0u)return i;
 if(direction==1)return i+1==s.count?0:i+1;
 if(direction==-1)return i?i-1:s.count-1;
 return i;
}
// USER. Driver notification precedes the per-task HKL exchange;
// WM_INPUTLANGCHANGE observes the new HKL, and the previous HKL is returned.
template<class E> U Change(E&e,State&s,U window,U task,U hkl,U) {
 const U i=Find(s,hkl);if(i==~0u)return 0;
 const U charset=s.entries[i].charset;
 if(s.notified!=hkl){s.notified=hkl;if(e.ShellInterested())e.ShellNotify(window,hkl);}
 e.DriverActivate(hkl);
 const U previous=e.ThreadLayout(task);e.SetThreadLayout(task,hkl);
 e.LanguageMessage(window,charset,hkl);
 return previous;
}
// USER. Window/focus selection is retained; a thread without an
// eligible top-level window does not successfully activate a layout.
template<class E> U Activate(E&e,State&s,U hkl,U flags) {
 if(!s.enabled)return 0;
 const U task=e.CurrentTask();U target=0;
 for(U h=e.FirstWindow();h;h=e.ReadWindow(h).next){
  const auto w=e.ReadWindow(h);
  if(w.task!=task || h==e.ExcludedWindow() || (w.style&0x08000000) || w.atom==e.ExcludedClass())continue;
  U owner=w.owner;if(owner){while(e.ReadWindow(owner).owner)owner=e.ReadWindow(owner).owner;if(e.ReadWindow(owner).popup!=h)continue;}
  if(w.popup==h){target=h;break;}
 }
 if(!target)return 0;
 const U focus=e.Focus(task);if(e.ValidWindow(focus) && e.ReadWindow(focus).task==task)target=focus;
 U reorder=~0u;
 if(hkl&0xffff0000){reorder=Find(s,hkl);if(reorder==~0u)return 0;}
 else if(hkl<=1){const U selected=Neighbor(s,e.ThreadLayout(task),hkl?1:-1);if(selected==~0u)return 0;hkl=s.entries[selected].layout;}
 else {for(U i=0;i<s.count;++i)if((s.entries[i].layout&65535)==hkl){reorder=i;break;}if(reorder==~0u)return 0;hkl=s.entries[reorder].layout;}
 const U previous=Change(e,s,target,task,hkl,flags&65535);
 if((flags&8) && reorder!=~0u && reorder){const auto first=s.entries[0];s.entries[0]=s.entries[reorder];s.entries[reorder]=first;}
 return previous;
}
// USER, with variant registry enumeration as a provider.
template<class E> U Name(E&e,const State&s,U output) {
 const U hkl=e.ThreadLayout(e.CurrentTask()),high=hkl>>16;
 if(high<0xe000){e.HexName(output,high);return 1;}
 if(high<0xf000)return 0;
 if(high==65535){e.HexName(output,s.entries[s.active].layout);return 1;}
 return e.VariantName(high&4095,output);
}
// USER. Empty strings yield zero. After a lowercase a-f
// digit, the original scan-table cursor rejects later uppercase A-F digits.
// Invalid digits and values above the supplied unsigned limit yield FFFFFFFF.
template<class E> U HexValue(E&e,U p,U maximum=~0u) {
 U value=0;bool lower=false;for(;;){const U ch=e.Read8(p++);if(!ch)return value;U digit;
  if(ch>='0'&&ch<='9')digit=ch-'0';else if(ch>='a'&&ch<='f'){digit=ch-'a'+10;lower=true;}
  else if(ch>='A'&&ch<='F'&&!lower)digit=ch-'A'+10;else return ~0u;
  value=(value<<4)+digit;if(value>maximum)return ~0u;
 }
}
// USER, with the registry, locale service, window snapshot and
// keyboard driver calls exposed through E. Installed entries carry all ten
// original payload bytes even when their native storage is widened.
template<class E> U Load(E&e,State&s,U name,U flags) {
 flags&=65535;if(!s.enabled || !e.LoaderAvailable())return 0;
 U hkl=e.ParseName(name),index=0,previous=0;
 while(index<s.count && (s.entries[index].layout&65535)!=(hkl&65535))++index;
 if(index<s.count)previous=s.entries[index].layout;
 if(flags&2)name=e.SubstituteName(name);
 if(e.NameLength(name)>8)return 0;
 U identifier=e.ParseName(name);
 if(!e.OpenLayoutMetadata(name))return 0;
 if(identifier>>16){U variant=0;if(e.LayoutVariant(variant))identifier=(variant&65535)|0xf000;else identifier=0xfffe;}
 hkl=(hkl&65535)|(identifier<<16);
 const U file=e.LayoutFile();e.CloseLayoutMetadata();
 if(!file || !e.LoadDriver(file,hkl))return 0;
 if(index==s.count){if(!e.ResizeLayoutTable(s.count+1)){e.UnloadDriver(hkl);return 0;}++s.count;}
 if(flags&8){while(index){s.entries[index]=s.entries[index-1];--index;}}
 Entry value{hkl,0,0};if(!e.LocaleMetadata(hkl&65535,value.signature,value.charset))e.DefaultMetadata(value.signature,value.charset);
 s.entries[index]=value;
 if(previous && !e.SuppressShell() && e.HasWindows()){
  const U snapshot=e.Snapshot();if(snapshot){
   for(U n=0;;++n){const U w=e.SnapshotWindow(snapshot,n);if(w==e.SnapshotEnd())break;
    if(w){const U task=e.ReadWindow(w).task;if((e.ThreadLayout(task)&65535)==(hkl&65535))e.SetThreadLayout(task,hkl);}
    e.ConsumeSnapshot(snapshot,n);
   }e.FreeSnapshot(snapshot);
  }
 }
 if(!(flags&0x80) && e.ShellInterested()){s.notified=0;e.ShellNotify(0,0);}
 if(flags&1)Activate(e,s,hkl,0);
 return hkl;
}
// USER. The last installed layout and layouts in use by a
// window's task cannot be unloaded. A driver failure restores active indexing.
template<class E> U Unload(E&e,State&s,U hkl) {
 if(!s.enabled || s.count<2 || !e.LoaderAvailable())return 0;
 const U index=Find(s,hkl);if(index==~0u)return 0;
 if(e.HasWindows()){
  const U snapshot=e.Snapshot();if(!snapshot)return 0;
  for(U n=0;;++n){const U w=e.SnapshotWindow(snapshot,n);if(w==e.SnapshotEnd())break;
   if((e.ThreadLayout(e.ReadWindow(w).task)&65535)==(hkl&65535)){e.FreeSnapshot(snapshot);return 0;}
   e.ConsumeSnapshot(snapshot,n);
  }e.FreeSnapshot(snapshot);
 }
 const U active=s.active;
 if(active==index){e.DriverSelect(s.entries[index?0:1].layout);s.active=0;}
 else if(active>index)--s.active;
 if(!e.UnloadDriver(hkl)){
  if(active==index){s.active=active;e.DriverSelect(s.entries[active].layout);}
  else if(active>index)++s.active;
  return 0;
 }
 for(U i=index+1;i<s.count;++i)s.entries[i-1]=s.entries[i];
 --s.count;
 if(!e.SuppressShell() && e.ShellInterested()){s.notified=0;e.ShellNotify(0,0);}
 return 1;
}
} // namespace Win98KeyboardLayout
