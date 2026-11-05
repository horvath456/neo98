// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win98_shell_search.h"
namespace Win98ShellPathSyntax {
using U=unsigned;
struct Environment:Win98ShellSearch::Environment{virtual void Put(U address,U value,bool wide)=0;virtual void Move(U dst,U src,U bytes)=0;virtual U Prev(U first,U at,bool wide)=0;};
inline U Get(Environment&e,U p,bool wide){return wide?e.Word(p):e.Byte(p);}
inline U Next(Environment&e,U p,bool wide){return wide?p+2:e.Next(p);}
inline U Length(Environment&e,U p,bool wide){return Win98ShellSearch::Length(e,p,wide)/(wide?2:1);}
inline void Copy(Environment&e,U dst,U src,bool wide){U unit=wide?2:1;for(;;){U c=Get(e,src,wide);e.Put(dst,c,wide);if(!c)return;dst+=unit;src+=unit;}}
// Quotes toggle state; only a literal unquoted space splits.
inline U Args(Environment&e,U p,bool wide){if(!p)return 0;bool quoted=false;while(U c=Get(e,p,wide)){if(c=='"')quoted=!quoted;else if(!quoted&&c==' ')return p+(wide?2:1);p=Next(e,p,wide);}return p;}
// Remove command-line arguments while preserving the path.
inline void RemoveArgs(Environment&e,U p,bool wide){if(!p)return;U at=Args(e,p,wide);if(Get(e,at,wide))e.Put(at-(wide?2:1),0,wide);else{at=e.Prev(p,at,wide);if(Get(e,at,wide)==' ')e.Put(at,0,wide);}}
// Requoting is allowed; the resulting length must stay below 260.
inline void Quote(Environment&e,U p,bool wide){if(!p||!Win98ShellSearch::Chr(e,p,' ',wide))return;U n=Length(e,p,wide),unit=wide?2:1;if(n+2>=260)return;e.Move(p+unit,p,(n+1)*unit);e.Put(p,'"',wide);e.Put(p+(n+1)*unit,'"',wide);e.Put(p+(n+2)*unit,0,wide);}
// Only a matching first/last quote pair is removed.
inline void Unquote(Environment&e,U p,bool wide){if(!p)return;U n=Length(e,p,wide),unit=wide?2:1;if(Get(e,p,wide)=='"'&&Get(e,p+(n-1)*unit,wide)=='"'){e.Put(p+(n-1)*unit,0,wide);e.Move(p,p+unit,(n-1)*unit);}}
// Trim ordinary spaces, preserving tabs and DBCS trail bytes.
inline void Blanks(Environment&e,U p,bool wide){if(!p)return;U src=p;while(Get(e,src,wide)==' ')src+=wide?2:1;if(src!=p)Copy(e,p,src,wide);U last=p;for(U at=p;U c=Get(e,at,wide);at=Next(e,at,wide))if(c!=' ')last=at;if(Get(e,last,wide))e.Put(Next(e,last,wide),0,wide);}
// Backslash and space reset the last dot; slash does not.
inline U Extension(Environment&e,U p,bool wide){if(!p)return 0;U dot=0;while(U c=Get(e,p,wide)){if(c=='.')dot=p;else if(c==' '||c=='\\')dot=0;p=Next(e,p,wide);}return dot?dot:p;}
// A trailing separator does not become the filename.
inline U FileName(Environment&e,U p,bool wide){if(!p)return 0;U result=p,unit=wide?2:1;while(U c=Get(e,p,wide)){if(c=='\\'||c=='/'||c==':'){U next=Get(e,p+unit,wide);if(next&&next!='\\'&&next!='/')result=p+unit;}p=Next(e,p,wide);}return result;}
// A double backslash advances twice; other separators once.
inline U Component(Environment&e,U p,bool wide){if(!p||!Get(e,p,wide))return 0;U found=Win98ShellSearch::Chr(e,p,'\\',wide),unit=wide?2:1;if(!found)return p+Length(e,p,wide)*unit;return found+(Get(e,found+unit,wide)=='\\'?2:1)*unit;}
// Skip the root prefix according to drive and network-path syntax.
inline U AddExtension(Environment&e,U p,U extension,bool wide,bool rename){if(!p||!extension)return 0;U at=Extension(e,p,wide),unit=wide?2:1;if(!rename&&Get(e,at,wide))return 0;U count=Length(e,extension,wide)+(rename?(at-p)/unit:Length(e,p,wide));if(count>=260)return 0;if(!rename)at=p+Length(e,p,wide)*unit;Copy(e,at,extension,wide);return 1;}
inline void RemoveExtension(Environment&e,U p,bool wide){if(p){U at=Extension(e,p,wide);if(Get(e,at,wide))e.Put(at,0,wide);}}
// ANSI explicitly rejects a DBCS drive prefix.
inline U Drive(Environment&e,U p,bool wide){if(!p)return ~0u;U c=Get(e,p,wide);if(!wide&&e.Lead(c)||Get(e,p+(wide?2:1),wide)!=':')return ~0u;if(c>='a'&&c<='z')return c-'a';if(c>='A'&&c<='Z')return c-'A';return ~0u;}
// Forward slash is accepted by this predicate.
inline U FileSpec(Environment&e,U p,bool wide){if(!p)return 0;while(U c=Get(e,p,wide)){if(c=='\\'||c==':')return 0;p=Next(e,p,wide);}return 1;}
// Internal decimal StrToInt conversion is used by ParseIconLocation.
inline U Decimal(Environment&e,U p,bool wide){bool negative=Get(e,p,wide)=='-';U unit=wide?2:1,result=0;if(negative)p+=unit;for(U c=Get(e,p,wide);c>='0'&&c<='9';c=Get(e,p+=unit,wide))result=result*10+c-'0';return negative?0-result:result;}
// The first comma wins, including a comma inside quotes.
inline U IconLocation(Environment&e,U p,bool wide){if(!p)return 0;U result=0,comma=Win98ShellSearch::Chr(e,p,',',wide);if(comma){e.Put(comma,0,wide);result=Decimal(e,comma+(wide?2:1),wide);}Unquote(e,p,wide);Blanks(e,p,wide);return result;}
// These classifiers have no NLS dependencies.
inline U CharType(U c,bool wide){c&=wide?65535:255;switch(c){case ' ':case ',':case ';':return 1;case '*':case '?':return 4;case '/':case '\\':case ':':return 8;case '"':case '<':case '>':case '|':return 0;}return c>32?3:0;}
inline U Call(Environment&e,U op,const U*a){bool w=op&1;switch(op/2){case 0:return Args(e,a[0],w);case 1:RemoveArgs(e,a[0],w);break;case 2:Quote(e,a[0],w);break;case 3:Unquote(e,a[0],w);break;case 4:Blanks(e,a[0],w);break;case 5:return Extension(e,a[0],w);case 6:return FileName(e,a[0],w);case 7:return Component(e,a[0],w);case 8:return AddExtension(e,a[0],a[1],w,false);case 9:return AddExtension(e,a[0],a[1],w,true);case 10:RemoveExtension(e,a[0],w);break;case 11:return Drive(e,a[0],w);case 12:return FileSpec(e,a[0],w);case 13:return IconLocation(e,a[0],w);case 14:return CharType(a[0],w);}return 0;}
}
