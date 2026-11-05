// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Win98ShellSearch {
using U=unsigned;
struct Environment {virtual ~Environment()=default;virtual U Byte(U)=0;virtual U Word(U)=0;virtual U Next(U)=0;virtual bool Lead(U)=0;virtual bool Dbcs()=0;virtual void EndBeyondTerminator()=0;};
inline U Length(Environment&e,U p,bool wide){U at=p;while(wide?e.Word(at):e.Byte(at))at+=wide?2:1;return at-p;}
// SHLWAPI70BD2C3D/2CB7 and70BD3179. The NUL terminator is never a match.
inline U Chr(Environment&e,U p,U c,bool wide){if(wide){while(U v=e.Word(p)){if(v==(c&65535))return p;p+=2;}return 0;}
 bool dbcs=e.Dbcs();while(U v=e.Byte(p)){if(v==(c&255)&&(!dbcs||!e.Lead(v)||e.Word(p)==(c&65535)))return p;p=dbcs?e.Next(p):p+1;}return 0;
}
// End is exclusive, including when supplied explicitly.
inline U RChr(Environment&e,U p,U end,U c,bool wide){if(!end)end=p+Length(e,p,wide);U result=0;while(p<end){if(wide){if(e.Word(p)==(c&65535))result=p;p+=2;}
 else{U v=e.Word(p);if((v&255)==(c&255)&&(!e.Lead(v&255)||v==(c&65535)))result=p;U next=e.Next(p);if(next==p){e.EndBeyondTerminator();return 0;}p=next;}}return result;
}
// The ANSI search compares only first bytes, even for DBCS.
inline U PBrk(Environment&e,U p,U accept,bool wide){while(U c=wide?e.Word(p):e.Byte(p)){for(U at=accept;;){U v=wide?e.Word(at):e.Byte(at);if(!v)break;if(v==c)return p;at=wide?at+2:e.Next(at);}p=wide?p+2:e.Next(p);}return 0;}
// ANSI count is bytes; Unicode count is WORDs.
inline U Span(Environment&e,U p,U accept,bool wide){U first=p;while(U c=wide?e.Word(p):e.Byte(p)){U at=accept;bool found=false;while(U v=wide?e.Word(at):e.Byte(at)){if(v==c&&(wide||!e.Lead(v)||e.Byte(at+1)==e.Byte(p+1))){found=true;break;}at=wide?at+2:e.Next(at);}if(!found)break;p=wide?p+2:e.Next(p);}return (p-first)/(wide?2:1);}
// These two exports explicitly return zero for null inputs.
inline U CSpan(Environment&e,U p,U reject,bool wide){if(!p||!reject)return 0;U first=p;while(wide?e.Word(p):e.Byte(p)){if(Chr(e,reject,e.Word(p),wide))break;p=wide?p+2:e.Next(p);}return (p-first)/(wide?2:1);}
inline U Call(Environment&e,U operation,const U*a){bool wide=operation&1;switch(operation/2){case 0:return Chr(e,a[0],a[1],wide);case 1:return RChr(e,a[0],a[1],a[2],wide);case 2:return PBrk(e,a[0],a[1],wide);case 3:return Span(e,a[0],a[1],wide);case 4:return CSpan(e,a[0],a[1],wide);}return 0;}
}
