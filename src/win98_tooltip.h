// SPDX-License-Identifier: GPL-2.0-or-later
// Win98 COMCTL32 5.80 tooltip cores; E owns memory, strings and USER providers.
#pragma once
namespace Win98Tooltip {
using U=unsigned;
// first matching (hwnd,uId), even when duplicate tools exist.
template<class E> U Find(E&e,U t,U query){if(!t||!query||e.Read32(query)>44)return 0;U n=e.Read32(t+0x24),array=e.Read32(t+0x34);for(U i=0;i<n;++i){U p=array+i*44;if(e.Read32(p+8)==e.Read32(query+8)&&e.Read32(p+12)==e.Read32(query+12))return p;}return 0;}
// distinguishes owned strings from resource IDs and LPSTR_CALLBACK.
template<class E> void Text(E&e,U item,U value){U old=e.Read32(item+36);if(old==~0u||!(old&0xffff0000))e.Write32(item+36,0);if(value==~0u||!(value&0xffff0000)){e.SetString(item+36,0);e.Write32(item+36,value);}else e.SetString(item+36,value);}
// +, contiguous TOOLINFO records. Current is rebased on grow.
template<class E> U Add(E&e,U t,U source){U bytes=e.Read32(source);if(bytes>44)return 0;U count=e.Read32(t+0x24),old=e.Read32(t+0x34),array=e.Resize(old,(count+1)*44);if(!array)return 0;U current=e.Read32(t+0x38);if(old&&current)e.Write32(t+0x38,array+U(int(current-old)/44)*44);e.Write32(t+0x34,array);U item=array+count*44;e.Write32(t+0x24,count+1);e.Copy(item,source,bytes);e.Write32(item+36,0);Text(e,item,e.Read32(source+36));if(e.Read32(item+4)&16)e.Hook(t,item,true);U format=e.Read32(item+8)&&e.ValidWindow(e.Read32(item+8))&&!(e.Read32(item+4)&64)?e.NotifyFormat(t,item):2;if(!e.Alive(t))return 1;if(format==2)e.Write32(item+4,e.Read32(item+4)|64);return 1;}
template<class E> void FreeTool(E&e,U t,U item){if(e.Read32(item+4)&16)e.Hook(t,item,false);Text(e,item,0);}
// swap the final record into the hole; do not shift following tools.
template<class E> void Delete(E&e,U t,U query){U item=Find(e,t,query);if(!item)return;if(e.Read32(t+0x38)==item){e.Hide(t);if(!e.Alive(t))return;item=Find(e,t,query);if(!item)return;}if(!e.Alive(t))return;FreeTool(e,t,item);U count=e.Read32(t+0x24)-1;e.Write32(t+0x24,count);U last=e.Read32(t+0x34)+count*44;e.Copy(item,last,44);if(e.Read32(t+0x38)==last)e.Write32(t+0x38,item);}
// and share this replacement body.
template<class E> void Set(E&e,U t,U source){U item=Find(e,t,source);if(!item)return;Text(e,item,0);e.Copy(item,source,e.Read32(source));e.Write32(item+36,0);Text(e,item,e.Read32(source+36));if(e.Read32(t+0x38)==item)e.Refresh(t);}
// update text and resource instance, leaving other fields intact.
template<class E> void UpdateText(E&e,U t,U source){U item=Find(e,t,source);if(!item)return;e.Write32(item+32,e.Read32(source+32));Text(e,item,e.Read32(source+36));if(e.Read32(t+0x38)==item)e.Refresh(t);}
template<class E> void NewRect(E&e,U t,U source){U item=Find(e,t,source);if(item)e.Copy(item+16,source+16,16);}
// BFE ECCC0/ ECD4E: A and W differ for a too-large cbSize.
template<class E> U Get(E&e,U item,U out,bool wide){if(!item||!out)return 0;U bytes=e.Read32(out);if(wide&&bytes>44)return 0;if(bytes>=40){e.Copy(out+4,item+4,32);U text=e.Read32(item+36);if(text==~0u||!(text&0xffff0000))e.Write32(out+36,text);else if(e.Read32(out+36))e.CopyText(e.Read32(out+36),text,wide);}if(bytes>40)e.Write32(out+40,e.Read32(item+40));return bytes<=44;}
// deliberately leaves the final lParam DWORD untouched.
template<class E> bool Ansi(E&e,U source,U out,bool text){e.Write32(out,44);e.Copy(out+4,source+4,32);if(text){U p=e.Read32(source+36);if(p==~0u||!(p&0xffff0000))e.Write32(out+36,p);else{U value=e.ToWide(p);if(!value)return false;e.Write32(out+36,value);e.Write32(out+4,e.Read32(out+4)|0x200);}}return true;}
// /. Delay values are sign-extended WORDs.
template<class E> void SetDelay(E&e,U t,U kind,U value){int n=short(value);if(!kind){if(n>0){e.Write32(t+0x28,n);e.Write32(t+0x2c,n/5);e.Write32(t+0x30,U(n)*10);}else{e.Write32(t+0x28,~0u);e.Write32(t+0x2c,~0u);e.Write32(t+0x30,~0u);}}else if(kind==1)e.Write32(t+0x2c,n);else if(kind==2)e.Write32(t+0x30,n);else if(kind==3)e.Write32(t+0x28,n);}
template<class E> U Delay(E&e,U t,U kind){U offset=kind==1?0x2c:kind==2?0x30:0x28;if(kind>3)return ~0u;U n=e.Read32(t+offset);if(int(n)<0){n=e.DoubleClick();if(kind==1)n/=5;if(kind==2)n*=10;}return n;}
// tracking/private excluded; IDISHWND tests the tool HWND directly.
template<class E> bool Hit(E&e,U item,U hwnd,int x,int y){U flags=e.Read32(item+4);if(flags&0x420)return false;if(flags&1)return hwnd==e.Read32(item+12);return hwnd==e.Read32(item+8)&&x>=int(e.Read32(item+16))&&y>=int(e.Read32(item+20))&&x<int(e.Read32(item+24))&&y<int(e.Read32(item+28));}
// current visible tool gets priority, otherwise search backwards.
template<class E> U ToolAt(E&e,U t,U hwnd,int x,int y){U current=e.Read32(t+0x38);if((e.Read32(t+0x44)&64)&&current&&Hit(e,current,hwnd,x,y))return current;U array=e.Read32(t+0x34);for(U n=e.Read32(t+0x24);n;--n){U p=array+(n-1)*44;if(Hit(e,p,hwnd,x,y))return p;}return 0;}
// / : track activation only changes mode when it toggles.
template<class E> void Activate(E&e,U t,bool active){if(active)e.Write32(t+0x44,e.Read32(t+0x44)|16);else{e.Hide(t);if(e.Alive(t))e.Write32(t+0x44,e.Read32(t+0x44)&~17u);}}
template<class E> U Track(E&e,U t,bool active,U query){U flags=e.Read32(t+0x44);if(!(flags&16))return 1;bool next=active&&query;if(bool(flags&1)==next)return 1;e.Hide(t);if(!e.Alive(t))return 1;e.Write32(t+0x44,e.Read32(t+0x44)^1);if(next){U item=Find(e,t,query);if(item){e.Write32(t+0x38,item);e.Refresh(t);}}return 1;}
// client mouse messages. Nonclient conversion is a provider.
template<class E> void Relay(E&e,U t,U hwnd,U message,U wp,U lp){U flags=e.Read32(t+0x44);if(flags&1)return;if(message==0x201||message==0x204||message==0x207){e.Write32(t+0x44,flags|32);e.Pop(t);return;}if(message==0x202||message==0x205||message==0x208){e.Write32(t+0x44,flags&~32u);return;}if(message!=0x200)return;if((flags&32)&&!e.Buttons()){flags&=~32u;e.Write32(t+0x44,flags);}if(!(flags&16)||(flags&32)||(!(e.Read32(t+8)&1)&&!e.Foreground(hwnd)))return;U item=ToolAt(e,t,hwnd,short(lp),short(lp>>16));if(!item){e.Hide(t);return;}U timer=1;if(flags&0xc0){if(item==e.Read32(t+0x38))timer=e.Read32(t+0x48)==2?0:3;else{e.Hide(t);if(!e.Alive(t))return;e.Write32(t+0x38,item);e.Pop(t);timer=2;}if(e.Read32(t+0x54))e.Timer(t,4);}else e.Write32(t+0x38,item);if(timer)e.Timer(t,timer);}
}
