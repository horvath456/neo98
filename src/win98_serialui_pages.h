// SPDX-License-Identifier: GPL-2.0-or-later
// SERIALUI (offer), (collect), (release callback).
#pragma once
namespace Win98SerialUi {
template<class E> unsigned OfferPage(E&e){
 unsigned page=e.Create();if(!page)return 8;
 if(e.Callback(page)&65535)return 0;
 e.Destroy(page);return 8;
}
template<class E> unsigned CollectPage(E&e,unsigned header,unsigned page){
 unsigned count=e.Read16(header+20);if(count>=1)return 0;
 e.Write32(e.Linear(e.Read32(header+26))+4*count,page);e.Write16(header+20,count+1);return 1;
}
template<class E> unsigned ReleasePage(E&e,unsigned reason,unsigned flags,unsigned applied){
 if(reason==1&&applied==1){e.CopyDcb();if(flags&1){e.Save();e.FreeState();}}
 return 1;
}
}
