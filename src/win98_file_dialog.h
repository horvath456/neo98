// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98FileDialog {
using U=unsigned;
inline int S16(U v){return int(v&32767)-int(v&32768);}
// The decision between
// Explorer and the legacy dialog is original; each dialog engine is a provider.
template<class E> U Run(E&e,U ofn,bool save){
 if(!ofn){e.Error(2);return 0;}if(e.Read(ofn)!=76){e.Error(1);return 0;}
 U flags=e.Read(ofn+52);
 bool explorer=(flags&0x80000)||!(flags&0x2e0);
 if(explorer&&!(e.Compatibility()&8)){
  e.Error(0);
  // ENABLEINCLUDENOTIFY requires Explorer + a hook.
  if((flags&0x400000)&&(!(flags&0x80000)||!(flags&0x20))){e.Error(2);return 0;}
  return e.Explorer(ofn,save);
 }
 if((flags&0x20)&&!e.Read(ofn+68)){e.Error(11);return 0;}
 if(!(flags&0x20))e.Write(ofn+68,0);
 return e.Legacy(ofn,save);
}
// COMDLG32. Packed low WORD filename offset/error,
// high WORD extension position. CharNext/CharLower remain charset providers.
template<class E> U Parse(E&e,U base,U spaces,U trim,U slash){
 U p=base,ext=0,parts=0,unc=0,tail=0,afterdot=0,wild=0,chars=0;
 int file=0;U c=e.Byte(p);
 if(!c){file=-10;goto finish;}
 if(e.Byte(p+1)==':'){
  c=e.Lower(c);if(c<'a'||c>'z'){file=-2;goto finish;}
  p=e.Next(e.Next(p));
 }
 c=e.Byte(p);
 if(c=='\\'||(c=='/'&&!slash))goto slash_start;
 if(c=='.'){
  ++p;if(e.Byte(p)=='.')++p;c=e.Byte(p);
  if(!c){file=-1;goto finish;}
  if(c=='\\'||(c=='/'&&!slash)){++p;goto begin;}
  file=0;goto finish;
 }
 goto begin;
slash_start:
 c=e.Byte(++p);
 if(c=='.'){
  c=e.Byte(++p);
  if(c=='\\'||(c=='/'&&!slash)){++p;goto begin;}
  if(!c){file=-1;goto finish;}
  --p;goto begin;
 }
 if(c=='\\'&&e.Byte(p-1)==c){++p;parts=~0u;unc=1;goto begin;}
 if(c=='/'&&!slash){file=-6;goto finish;}
begin:
 if(!e.Byte(p)){file=-1;goto finish;}
 tail=ext=chars=afterdot=wild=0;file=0;
 for(;;){
  c=e.Byte(p);if(!c)break;
  if(c<32){file=-5;goto finish;}
  if(c==' '&&!spaces){
   e.Byte(p,0);U at=p+1;while(e.Byte(at)==' ')++at;
   if(e.Byte(at)){e.Byte(p,' ');file=-7;goto finish;}break;
  }
  if(c==':'||c=='<'||c=='>'||c=='"'||c=='|'||(c=='/'&&slash)){
   file=-5;goto finish;
  }
  if((c=='+'||c==','||c==';'||c=='='||c=='['||c==']')&&!spaces){file=-5;goto finish;}
  if(c=='.'){
   if(slash)ext=p-base;
   else if(!(chars&65535)){
    file=int(p-base);++p;if(e.Byte(p)=='.')++p;
    if(!e.Byte(p)){file=-1;goto finish;}++p;chars=1;continue;
   }else{ext=0;++p;afterdot=1;continue;}
  }
  if(c=='*'||c=='?')wild=1;
  if(c=='\\'||(c=='/'&&!slash)){
   ++parts;if(wild){file=-11;goto finish;}
   if(!(chars&65535)){file=-6;goto finish;}
   ++p;if(!(parts&65535)&&!e.Byte(p)){file=-13;goto finish;}
   tail=chars=ext=afterdot=0;continue;
  }
  if(afterdot){if((++tail&65535)==1)ext=p-base;}
  else if((++chars&65535)==1)file=int(p-base);
  p=e.Next(p);
 }
 if((parts&65535)==65535){file=-13;goto finish;}
 if(unc&&(!(parts&65535)||(parts==1&&!(chars&65535)))){e.Byte(p,0);file=-1;goto finish;}
 if(!(chars&65535)){file=-1;goto finish;}
 if(!slash){
  if(trim&&p>base&&e.Byte(p-1)=='.'&&e.Byte(e.Next(p-2))=='.'){e.Byte(p-1,0);goto packed;}
  if(tail&65535)goto packed;
 }
finish:
 if(!slash)ext=p-base;
packed:
 return ((ext&65535)<<16)|(U(file)&65535);
}
// unpack the lexical parser and distinguish dot offset
// from extension offset. The two output fields have distinct meanings.
template<class E> int Filename(E&e,U base,U&dot,U&extension){
 U v=Parse(e,base,1,0,1);int at=S16(v>>16);dot=U(at);
 extension=at&&e.Byte(base+U(at))?U(at+1):0;return S16(v);
}
// extension copied through a 260-byte local and trimmed.
template<class E> void AppendExtension(E&e,U name,U extension,bool wildcard){
 if(!extension||!e.Byte(extension))return;U at=e.Length(name)&65535;
 if(wildcard)e.Byte(name+(at++&65535),'*');e.Byte(name+(at++&65535),'.');
 U n=0;while(n<260&&e.Byte(extension+n))++n;
 while(n&&e.Byte(extension+n-1)==' ')--n;
 U begin=0;while(begin<n&&e.Byte(extension+begin)==' ')++begin;
 for(U i=begin;i<n;++i)e.Byte(name+(at&65535)+i-begin,e.Byte(extension+i));
 e.Byte(name+(at&65535)+n-begin,0);
}
// combo index publication and default-extension selection.
// Storage/allocation and lexical wildcard search are providers.
template<class E> void Filter(E&e,U ofn,U selected,U pattern,bool fixed_extension){
 e.Write(ofn+52,e.Read(ofn+52)&~0x10000000u);if(S16(selected)<0)return;selected=U(S16(selected));
 U custom=e.Read(ofn+16);e.Write(ofn+24,selected+((custom&&e.Byte(custom))?0:1));
 if(!e.Byte(pattern)||fixed_extension||!e.Read(ofn+60))return;
 U dot=e.Find(pattern,'.');
 if(dot){U extension=e.Duplicate(dot+1);if(extension){U semi=e.Find(extension,';');if(semi)e.Byte(semi,0);
   if(!e.Wild(extension)){e.Extension(extension);e.Release(extension);return;}e.Release(extension);}}
 e.Extension(e.Read(ofn+60));
}
// single-file output path. Parsing, charset comparison,
// process CWD, title truncation, readonly control and recent files are providers.
template<class E> void Output(E&e,U ofn,U path,bool unc=false){
 U dot=0,extension=0;int file=Filename(e,path,dot,extension);
 e.Word(ofn+56,U(file));e.Word(ofn+58,extension);e.Write(ofn+52,e.Read(ofn+52)&~0x400u);
 U def=e.Read(ofn+60);
 if(def&&extension){char short_ext[4]{};for(U i=0;i<3&&e.Byte(def+i);++i)short_ext[i]=char(e.Byte(def+i));
  if(e.CompareExtension(short_ext,path+extension))e.Write(ofn+52,e.Read(ofn+52)|0x400);}
 U out=e.Read(ofn+28),needed=e.Length(path)+1;if(e.Read(ofn+52)&0x200)++needed;
 if(out){
  U capacity=e.Read(ofn+32);
  if(needed<=(capacity&65535)){
   e.Copy(out,path);if(e.Read(ofn+52)&0x200)e.Byte(out+needed-1,0);
   if(!(e.Read(ofn+52)&8)&&!unc&&file){U ch=e.Byte(out+U(file));e.Byte(out+U(file),0);e.ChangeDirectory(out);e.Byte(out+U(file),ch);}
  }else{
   e.Byte(out,needed&255);if(capacity>=2)e.Byte(out+1,(needed>>8)&255);if(capacity>=3)e.Byte(out+2,0);
  }
 }
 if(!e.ExcludedRecent(path))e.Recent(path);
 U title=e.Read(ofn+36);
 if(title){U length=e.Length(path+U(file)),cap=e.Read(ofn+40);
  if(length>=cap)e.Truncate(path+U(file),cap-1);e.Copy(title,path+U(file));}
 if(!(e.Read(ofn+52)&4)){U flags=e.Read(ofn+52);e.Write(ofn+52,e.ReadOnly()?flags|1:flags&~1u);}
}
// Notification/legacy-hook veto precedes modal teardown;
// cancellation also restores the starting process directory.
template<class E> void Close(E&e,U ofn,U result){
 U custom=e.Read(ofn+16);
 if(custom){U at=e.Length(custom)+1,pattern=e.Pattern();if(at+e.Length(pattern)<e.Read(ofn+20))e.Copy(custom+at,pattern);}
 if(result==1&&e.HasHook()){if(e.Notify(~605u))return;if(e.LegacyFileOK())return;}
 e.ReleaseView();e.Cleanup();
 if((e.Read(ofn+52)&8)||e.Cancelled()){U initial=e.InitialDirectory();if(e.Byte(initial))e.ChangeDirectory(initial);}
 e.End(result);
}
}
