// SPDX-License-Identifier: GPL-2.0-or-later
// Native RAM-volume and dialog providers. Included in x86_find.cpp.
namespace {
constexpr unsigned FileDialogToken=0x71000012;
struct FileDialogFrame{
 Application*app;FileDialogFrame*next;unsigned ofn,hwnd=0,hook_window=0,scratch=0,thread=0;
 bool save,cancelled=false,initializing=true,closing=false,fixed_extension=false;
 char initial[260]{},directory[260]{},extension[260]{},pattern[260]{"*.*"};
 unsigned patterns[64]{},filters=0,filter=0;
 static FileDialogFrame*head;
 FileDialogFrame(Application*a,unsigned p,bool s):app(a),next(head),ofn(p),thread(a->ThreadId()),save(s){head=this;}
 ~FileDialogFrame(){auto**at=&head;while(*at&&*at!=this)at=&(*at)->next;if(*at)*at=next;}
};
FileDialogFrame*FileDialogFrame::head=nullptr;
FileDialogFrame*FileFrame(Application*a,unsigned hwnd,bool initialize=false){
 for(auto*f=FileDialogFrame::head;f;f=f->next)if(f->app==a&&(f->hwnd==hwnd||(initialize&&!f->hwnd))){if(!f->hwnd)f->hwnd=hwnd;return f;}return nullptr;
}
}
struct Application::FileDialogEnvironment{
 Application&app;FileDialogFrame&f;
 unsigned Read(unsigned p){return app.Read32(p);}void Write(unsigned p,unsigned v){app.Write32(p,v);}
 unsigned Byte(unsigned p){unsigned char v=0;app.Read(p,&v,1);return v;}void Byte(unsigned p,unsigned v){unsigned char b=v;app.Store(p,&b,1);}
 void Word(unsigned p,unsigned v){unsigned short w=v;app.Store(p,&w,2);}
 unsigned Next(unsigned p){return app.Api("CharNextA",&p);}unsigned Lower(unsigned c){return app.CharacterCaseApi("CharLowerA",&c);}
 unsigned Length(unsigned p){return strlen(app.ReadString(p));}
 void Copy(unsigned p,unsigned q){const char*s=app.ReadString(q);app.Store(p,s,strlen(s)+1);}
 unsigned Find(unsigned p,unsigned ch){for(unsigned i=0;i<4096&&Byte(p);++i,p=Next(p))if(Byte(p)==ch)return p;return 0;}
 bool Wild(unsigned p){return Find(p,'*')||Find(p,'?');}
 unsigned Duplicate(unsigned p){return app.String(app.ReadString(p));}void Release(unsigned p){app.Free(p);}
 void Extension(unsigned p){const char*s=app.ReadString(p);unsigned n=strlen(s);if(n>259)n=259;memcpy(f.extension,s,n);f.extension[n]=0;}
 bool CompareExtension(const char*s,unsigned p){unsigned lease=app.String(s),a[]={lease,p};unsigned result=app.Api("lstrcmpiA",a);app.Free(lease);return result!=0;}
 void ChangeDirectory(unsigned p){app.files.SetDirectory(app.ReadString(p));}
 bool ExcludedRecent(unsigned){return false;}void Recent(unsigned){} // Native shell recent-document provider remains separate.
 void Truncate(unsigned p,unsigned n){if(n<260)Byte(p+n,0);}
 bool ReadOnly(){return app.Send(User::GetDlgItem(f.hwnd,1040),0xf0,0,0)!=0;}
 unsigned Pattern(){app.Store(f.scratch+1024,f.pattern,strlen(f.pattern)+1);return f.scratch+1024;}
 bool HasHook(){return f.hook_window&&User::IsWindow(f.hook_window);}
 unsigned Notify(unsigned code){
  if(!HasHook()||!User::IsWindow(f.hwnd))return 0;
  unsigned memory=f.scratch+1536,notification[]={f.hwnd,User::ControlID(f.hwnd),code,f.ofn,0};app.Store(memory,notification,sizeof notification);
  unsigned clear[]={f.hook_window,0,0};app.Api("SetWindowLongA",clear);
  app.Send(f.hook_window,0x4e,notification[1],memory);
  if(!HasHook()||!User::IsWindow(f.hwnd))return 1;
  return app.Api("GetWindowLongA",clear);
 }
 unsigned LegacyFileOK(){
  unsigned name=app.String("commdlg_FileNameOK"),message=app.Api("RegisterWindowMessageA",&name);app.Free(name);
  return HasHook()?app.Send(f.hook_window,message,0,f.ofn):0;
 }
 void ReleaseView(){}void Cleanup(){}bool Cancelled(){return f.cancelled;}
 unsigned InitialDirectory(){app.Store(f.scratch+1280,f.initial,strlen(f.initial)+1);return f.scratch+1280;}
 void End(unsigned result){if(User::IsWindow(f.hwnd))app.EndDialog(f.hwnd,result);}
 bool Alive(){return app.state!=Failed&&app.state!=Exited&&User::IsWindow(f.hwnd);}
 void SetText(unsigned hwnd,const char*text){if(hwnd)User::SetWindowText(hwnd,text);}
 unsigned Add(unsigned hwnd,const char*text,bool combo=false,unsigned data=~0u){
  unsigned p=app.String(text),index=app.Send(hwnd,combo?0x143:0x180,0,p);app.Free(p);
  if(index!=~0u&&data!=~0u)app.Send(hwnd,combo?0x151:0x19a,index,data);return index;
 }
 bool Match(const char*name){
  // Existing original KERNEL find-pattern provider supplies case and wildcard rules.
  char path[260];unsigned n=strlen(f.directory),m=strlen(f.pattern);if(n+m+2>=sizeof path)return false;
  memcpy(path,f.directory,n);if(n&&path[n-1]!='\\')path[n++]='\\';memcpy(path+n,f.pattern,m+1);
  // Match semicolon-separated filter alternatives via actual FindFirst/Next.
  const char*part=f.pattern;
  while(*part){const char*end=strchr(part,';');unsigned length=end?unsigned(end-part):strlen(part);if(n+length>=260)return false;memcpy(path+n,part,length);path[n+length]=0;
   unsigned p=app.String(path),a[]={p,f.scratch+2048};unsigned find=app.FileApi("FindFirstFileA",a);app.Free(p);
   bool found=false;if(find!=~0u){do{char entry[260]{};app.Read(f.scratch+2048+44,entry,260);if(!strcmp(entry,name)){found=true;break;}unsigned next[]={find,f.scratch+2048};if(!app.FileApi("FindNextFileA",next))break;}while(true);app.FileApi("FindClose",&find);}if(found)return true;
   if(!end)break;part=end+1;
  }return false;
 }
 void Refresh(){
  app.files.SetDirectory(f.directory);
  unsigned list=User::GetDlgItem(f.hwnd,1120),where=User::GetDlgItem(f.hwnd,1137);app.Send(list,0x184,0,0);app.Send(where,0x14b,0,0);Add(where,f.directory,true);app.Send(where,0x14e,0,0);
  unsigned directory;if(Files::System().Lookup(f.directory,directory))return;
  if(strlen(f.directory)>3)Add(list,"[..]",false,~0u-1);
  for(unsigned i=0,node;!Files::System().Enumerate(directory,i,node);++i){auto*n=Files::System().Get(node);if(!n)continue;if(n->attributes&0x10){char label[260];snprintf(label,sizeof label,"[%s]",n->name);Add(list,label,false,node);}else if(Match(n->name))Add(list,n->name,false,node);}
  User::InvalidateRect(f.hwnd,nullptr,true);
 }
 bool Directory(const char*path){unsigned node;if(Files::System().Lookup(path,node))return false;auto*n=Files::System().Get(node);return n&&(n->attributes&0x10);}
 bool Navigate(const char*path){char old[260];memcpy(old,app.files.current,260);memcpy(app.files.current,f.directory,260);char full[260];unsigned error=app.files.FullPath(path,full);memcpy(app.files.current,old,260);if(error||!Directory(full))return false;memcpy(f.directory,full,260);Refresh();Notify(~602u);return true;}
 void Filter(unsigned index,bool notify){
  if(index>=f.filters)return;f.filter=index;const char*pattern=app.ReadString(f.patterns[index]);unsigned n=strlen(pattern);if(n>259)n=259;memcpy(f.pattern,pattern,n);f.pattern[n]=0;
  Win98FileDialog::Filter(*this,f.ofn,index,f.patterns[index],f.fixed_extension);Refresh();if(notify)Notify(~606u);
 }
 bool Initialize(){
  unsigned flags=Read(f.ofn+52);memcpy(f.initial,app.files.current,260);memcpy(f.directory,f.initial,260);
  unsigned initial=Read(f.ofn+44);if(initial){char full[260]{};if(!app.files.FullPath(app.ReadString(initial),full)&&Directory(full))memcpy(f.directory,full,260);}
  unsigned title=Read(f.ofn+48);SetText(f.hwnd,title?app.ReadString(title):(f.save?"Save As":"Open"));SetText(User::GetDlgItem(f.hwnd,1),f.save?"&Save":"&Open");SetText(User::GetDlgItem(f.hwnd,1091),f.save?"Save &in:":"Look &in:");
  unsigned file=Read(f.ofn+28);if(file){char value[260];const char*text=app.ReadString(file);unsigned n=strlen(text);if(n>=260){app.last_error=50;return false;}memcpy(value,text,n+1);
   char*slash=strrchr(value,'\\');if(slash){char directory[260];memcpy(directory,value,n+1);directory[slash-value+1]=0;char full[260]{};if(!app.files.FullPath(directory,full)&&Directory(full)){memcpy(f.directory,full,260);memmove(value,slash+1,strlen(slash+1)+1);}}
   SetText(User::GetDlgItem(f.hwnd,1152),value);}
  User::ShowWindow(User::GetDlgItem(f.hwnd,1040),(flags&4)?0:1);User::ShowWindow(User::GetDlgItem(f.hwnd,1038),(flags&0x10)?1:0);app.Send(User::GetDlgItem(f.hwnd,1040),0xf1,flags&1,0);
  if(Read(f.ofn+60))Extension(Read(f.ofn+60));
  unsigned combo=User::GetDlgItem(f.hwnd,1136),custom=Read(f.ofn+16),filter=Read(f.ofn+12);
  auto add=[&](unsigned text,unsigned pattern){if(f.filters==64)return;unsigned index=Add(combo,app.ReadString(text),true,pattern);if(index<64){f.patterns[index]=pattern;++f.filters;}};
  if(custom&&Byte(custom))add(custom,custom+Length(custom)+1);
  for(unsigned count=0;filter&&Byte(filter)&&count<63;++count){unsigned pattern=filter+Length(filter)+1;if(!Byte(pattern))break;add(filter,pattern);filter=pattern+Length(pattern)+1;}
  unsigned selected=Read(f.ofn+24);if(!(custom&&Byte(custom))&&selected)--selected;if(selected>=f.filters)selected=0;
  if(f.filters){app.Send(combo,0x14e,selected,0);Filter(selected,false);}else Refresh();
  if(flags&0x20){unsigned bytes=0;const auto*resource=app.CommonResource(5,400,bytes);if(!resource){app.CommonDialogError(6);return false;}
   f.hook_window=app.CreateDialogTemplate(resource,bytes,f.hwnd,Read(f.ofn+68),f.ofn,false,Read(f.ofn+8));
   if(!f.hook_window)return false;User::ShowWindow(f.hook_window,5);Notify(~600u);
  }
  f.initializing=false;return Alive();
 }
 void Warning(const char*message){app.MessageBox(f.hwnd,message,f.save?"Save As":"Open",0x30);}
 void Accept(){
  char input[260]{};User::GetWindowText(User::GetDlgItem(f.hwnd,1152),input,sizeof input);
  unsigned n=strlen(input);while(n&&input[n-1]==' ')input[--n]=0;unsigned begin=0;while(input[begin]==' ')++begin;if(begin)memmove(input,input+begin,n-begin+1);n=strlen(input);
  bool quoted=n>1&&input[0]=='"'&&input[n-1]=='"';if(quoted){input[n-1]=0;memmove(input,input+1,n-1);}if(!*input)return;
  if(Navigate(input))return;
  if(strchr(input,'*')||strchr(input,'?')){if(strchr(input,'\\')||strchr(input,'/')){Warning("Choose a folder before entering a file filter.");return;}strncpy(f.pattern,input,259);f.pattern[259]=0;Refresh();return;}
  char old[260];memcpy(old,app.files.current,260);memcpy(app.files.current,f.directory,260);char full[260]{};unsigned error=app.files.FullPath(input,full);memcpy(app.files.current,old,260);
  if(error){Warning("The file name or path is not valid.");return;}
  unsigned path=f.scratch,dot=0,extension=0;app.Store(path,full,strlen(full)+1);int filename=Win98FileDialog::Filename(*this,path,dot,extension);
  if(filename<0){Warning("The file name is not valid.");return;}
  if(!dot&&!quoted&&f.extension[0]){unsigned ext=f.scratch+768;app.Store(ext,f.extension,strlen(f.extension)+1);if(strlen(full)+strlen(f.extension)+2>260){Warning("The file name is too long.");return;}Win98FileDialog::AppendExtension(*this,path,ext,false);app.Read(path,full,sizeof full);filename=Win98FileDialog::Filename(*this,path,dot,extension);}
  unsigned flags=Read(f.ofn+52);char parent[260];memcpy(parent,full,260);if(filename>0)parent[filename]=0;
  if((flags&0x800)&&!Directory(parent)){Warning("The path does not exist.");return;}
  unsigned node;bool exists=!Files::System().Lookup(full,node);const auto*object=exists?Files::System().Get(node):nullptr;
  if(!f.save&&!exists&&(flags&0x1000)){if(!(flags&0x2000)||app.MessageBox(f.hwnd,"The file does not exist. Create it?","Open",0x24)!=6)return;}
  if(exists&&f.save&&(flags&2)&&app.MessageBox(f.hwnd,"This file already exists. Replace it?","Save As",0x24)!=6)return;
  if(object&&(object->attributes&1)&&(f.save||(flags&0x8000))){Warning("The file is read-only.");return;}
  // The RAM filesystem provides the original create/delete writability probe.
  if(f.save&&!exists&&(flags&0x800)&&!(flags&0x10000)){
   unsigned h,status;error=app.files.Open(full,2,3,2,0x80,h,status);if(error){Warning("The file cannot be created in this folder.");return;}app.files.Close(h);Files::System().Remove(full,false);
  }
  const unsigned needed=strlen(full)+1;Win98FileDialog::Output(*this,f.ofn,path);
  if(!Alive())return;
  if(Read(f.ofn+32)<needed){app.CommonDialogError(0x3003);Win98FileDialog::Close(*this,f.ofn,0);return;}
  Win98FileDialog::Close(*this,f.ofn,1);
 }
};
unsigned Application::FileDialogApi(ApiName name,const unsigned*a){
 struct E{Application&app;unsigned Read(unsigned p){return app.Read32(p);}void Write(unsigned p,unsigned v){app.Write32(p,v);}void Error(unsigned v){app.CommonDialogError(v);}unsigned Compatibility(){return 0;}
  unsigned Explorer(unsigned p,bool save){return app.BuildFileDialog(p,save);}unsigned Legacy(unsigned,bool){app.last_error=50;app.CommonDialogError(0xffff);return 0;}}e{*this};
 return Win98FileDialog::Run(e,a[0],API_IS(name,"GetSaveFileNameA"));
}
unsigned Application::BuildFileDialog(unsigned ofn,bool save){
 const bool gate=TakeFresh();
 if(!virtual_memory.Access(ofn,76,true)){last_error=87;return 0;}
 unsigned flags=Read32(ofn+52),owner=Read32(ofn+4),capacity=Read32(ofn+32),file=Read32(ofn+28);
 // Native provider currently supplies a single local file, original default
 // Explorer resource, and an optional standard child hook. No shell namespace.
 if(flags&(0x200|0x100|0xc0|0x400000)||capacity>65535||!capacity||!file||!virtual_memory.Access(file,capacity,true)||
    ((flags&0x20)&&!Read32(ofn+68))||(Read32(ofn+36)&&!Read32(ofn+40))){last_error=50;CommonDialogError(0xffff);return 0;}
 if(owner&&!User::IsWindow(owner)){last_error=1400;return 0;}
 auto*held=new FileDialogFrame(this,ofn,save);auto&frame=*held;
 struct Release{FileDialogFrame*frame;~Release(){delete frame;}}release{held};
 frame.scratch=Allocate(4096);if(!frame.scratch){CommonDialogError(9);return 0;}
 unsigned bytes=0;const unsigned char*resource=CommonResource(5,1547,bytes);if(!resource||bytes!=524){Free(frame.scratch);CommonDialogError(6);return 0;}
 unsigned char copy[524];memcpy(copy,resource,bytes);
 // Original geometry/text are retained. The native RAM directory list replaces
 // the hidden shell-view placeholder; the location combo uses ordinary rows.
 unsigned style=0x50910101;memcpy(copy+160,&style,4);style=0x50210203;memcpy(copy+104,&style,4);
 const bool kept=gate&&PushHostFrame(HostFileDialog,0,0);
 if(kept)GiveTail(TailRecord);
 unsigned hwnd=CreateDialogTemplate(copy,bytes,owner,FileDialogToken,ofn,true,CommonInstance);
 if(!hwnd||state==Failed){if(kept)PopHostFrame();Free(frame.scratch);return 0;}
 frame.hwnd=hwnd;
 if(kept)threads[current_thread].host_frames[threads[current_thread].host_frame_count-1].a=hwnd;
 unsigned result=RunModal(hwnd,kept);if(kept)PopHostFrame();
 release.frame=nullptr;return FinishFileDialog(hwnd,result);
}
unsigned Application::FinishFileDialog(unsigned hwnd,unsigned result){
 auto*frame=FileFrame(this,hwnd);if(!frame)return 0;
 Free(frame->scratch);delete frame;return result==1;
}
void Application::DiscardSavedFileDialogs(){
 for(auto*f=FileDialogFrame::head,*next=f;f;f=next){
  next=f->next;if(f->app!=this)continue;
  bool live=false;
  for(unsigned i=0;i<ThreadCapacity;++i){const auto&t=threads[i];
   if(i!=current_thread&&t.id==f->thread&&t.fiber&&t.fiber->Started()&&!t.fiber->Finished()){live=true;break;}}
  if(live)continue;
  Free(f->scratch);delete f;
 }
}
unsigned Application::FileDialogMessage(unsigned hwnd,unsigned message,unsigned wp,unsigned lp){
 auto*f=FileFrame(this,hwnd,message==0x110);if(!f)return 0;FileDialogEnvironment e{*this,*f};
 if(message==2){if(auto*d=FindDialog(hwnd)){if(!d->ended){d->ended=true;d->result=0;}}return 0;}
 if(message==0x110){if(!e.Initialize()){if(User::IsWindow(hwnd))EndDialog(hwnd,0);return 1;}SetFocus(User::GetDlgItem(hwnd,1152));return 0;}
 if(message==0x10){f->cancelled=true;Win98FileDialog::Close(e,f->ofn,0);return 1;}
 if(message==0x111){unsigned id=wp&65535,code=wp>>16;
  if(id==2){f->cancelled=true;Win98FileDialog::Close(e,f->ofn,0);return 1;}
  if(id==1){e.Accept();return 1;}
  if(id==1136&&code==1&&!f->initializing){unsigned index=Send(lp,0x147,0,0);e.Filter(index,true);return 1;}
  if(id==1120&&(code==1||code==2)){
   unsigned list=User::GetDlgItem(hwnd,1120),index=Send(list,0x188,0,0),node=Send(list,0x199,index,0);
   if(node==~0u-1){if(code==2)e.Navigate("..");return 1;}
   auto*object=Files::System().Get(node);if(!object)return 1;
   if(object->attributes&0x10){if(code==2)e.Navigate(object->name);return 1;}
   User::SetWindowText(User::GetDlgItem(hwnd,1152),object->name);e.Notify(~601u);if(code==2)e.Accept();return 1;
  }
 }
 // Explorer CDM_GETSPEC/GETFILEPATH/GETFOLDERPATH and SETCONTROLTEXT.
 if(message>=0x464&&message<=0x466){char text[260]{};
  if(message==0x466)memcpy(text,f->directory,260);else{User::GetWindowText(User::GetDlgItem(hwnd,1152),text,260);if(message==0x465){char old[260];memcpy(old,files.current,260);memcpy(files.current,f->directory,260);char full[260]{};files.FullPath(text,full);memcpy(files.current,old,260);memcpy(text,full,260);}}
  unsigned n=strlen(text)+1;if(lp&&wp>=n)Store(lp,text,n);return n;
 }
 if(message==0x468){User::SetWindowText(User::GetDlgItem(hwnd,wp),ReadString(lp));return 1;}
 if(message==0x469){User::ShowWindow(User::GetDlgItem(hwnd,wp),0);return 1;}
 if(message==0x46a){e.Extension(lp);f->fixed_extension=true;return 1;}
 return 0;
}
