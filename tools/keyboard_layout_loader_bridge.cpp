// SPDX-License-Identifier: GPL-2.0-or-later
#include "keyboard_layout_bridge.cpp"
struct LoaderFixture {Fixture base;U loader,metadata,variant_ok,variant,file,load_ok,resize_ok,locale_ok,signature,charset,suppress,has_windows,snapshot_ok,unload_ok,substitute;};
extern "C" U keyboard_loader_case(LoaderFixture *f,U op,U arg,U flags,unsigned char*out,U*trace){
 struct E{LoaderFixture&f;unsigned char*out;U*t;
  void Event(U k,U a=0,U b=0,U c=0){U n=t[0];t[1+n++]=k;t[1+n++]=a;t[1+n++]=b;t[1+n++]=c;t[0]=n;}
  U Read8(U p){return out[p];}U ParseName(U p){return HexValue(*this,p);}U NameLength(U p){U n=0;while(out[p+n])++n;return n;}
  bool LoaderAvailable(){Event(10);return f.loader;}U SubstituteName(U p){Event(11);return f.substitute?0x280:p;}
  bool OpenLayoutMetadata(U p){Event(12,ParseName(p));return f.metadata;}bool LayoutVariant(U&v){Event(13);v=f.variant;return f.variant_ok;}
  U LayoutFile(){Event(14);return f.file;}void CloseLayoutMetadata(){Event(15);}
  bool LoadDriver(U file,U h){Event(16,file,h);return f.load_ok;}bool UnloadDriver(U h){Event(17,h);return f.unload_ok;}
  bool ResizeLayoutTable(U n){Event(18,n);return f.resize_ok;}bool LocaleMetadata(U lang,U&s,U&c){Event(19,lang);s=f.signature;c=f.charset;return f.locale_ok;}
  void DefaultMetadata(U&s,U&c){s=f.signature;c=f.charset&255;}
  bool SuppressShell(){return f.suppress;}bool HasWindows(){return f.has_windows;}
  U Snapshot(){Event(20);return f.snapshot_ok?1:0;}U SnapshotEnd(){return ~0u;}U SnapshotWindow(U,U i){return i<7?i+1:SnapshotEnd();}
  void ConsumeSnapshot(U,U){}void FreeSnapshot(U){Event(21);}
  U CurrentTask(){Event(1);return f.base.current;}U ResolveThread(U){return 0;}U ThreadLayout(U t){return f.base.layouts[t];}void SetThreadLayout(U t,U h){f.base.layouts[t]=h;}
  U FirstWindow(){return f.base.first;}Window ReadWindow(U w){return f.base.windows[w];}U ExcludedWindow(){return f.base.excluded;}U ExcludedClass(){return f.base.excluded_class;}
  U Focus(U){return f.base.focus;}bool ValidWindow(U h){Event(3,h);return h&&h<9;}
  bool ShellInterested(){Event(4);return f.base.shell;}void ShellNotify(U w,U h){Event(5,w,h);}void DriverActivate(U h){Event(6,h);}void DriverSelect(U h){Event(22,h);}
  void LanguageMessage(U w,U c,U h){Event(7,w,c,h);}
 }e{*f,out,trace};trace[0]=0;
 if(op==6)return HexValue(e,arg,flags);if(op==7)return Load(e,f->base.state,arg,flags);return Unload(e,f->base.state,arg);
}
