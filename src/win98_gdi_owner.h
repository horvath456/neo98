// SPDX-License-Identifier: GPL-2.0-or-later
// GDI object ownership and private-object flags. Validate object kinds and
// ownership before mutation or deletion; native stores provide handles and
// memory.
#pragma once
namespace Win98GdiOwner {
using U=unsigned;
enum:U{
 SignatureOffset=2,  // WORD, GDI object header
 OwnerOffset=0xc,    // WORD, exchanged by SETOBJECTOWNER
 PrivateFlag=0x2000, // high-byte bit 0x20, set by MAKEOBJECTPRIVATE
 StockFlag=0x8000,   // high-byte bit 0x80, system objects
 SignatureMask=0x5fff,
 DeleteMask=0xa0,    // DELETEOBJECT refuses stock and private objects
 SysDeleteMask=0x80, // SYSDELETEOBJECT refuses only stock objects
};
// The two flag bits never reach a gate or a type table.
inline U Masked(U signature){return signature&SignatureMask;}
// ISGDIOBJECT. Zero for handle zero and for every masked signature
// outside 0x4f47..0x4f53; otherwise the original returns signature-0x4f46.
inline U Classify(U handle,U signature){
 if(!handle)return 0;
 U index=(Masked(signature)-0x4f46)&65535;
 return index&&index<=0xd?index:0;}
// gate in front of SETOBJECTOWNER and SYSDELETEOBJECT.
inline bool OwnerGate(U signature){U s=Masked(signature);return s>=0x4f47&&s<=0x4f52;}
// gate in front of DELETEOBJECT.
inline bool DeleteGate(U signature){U s=Masked(signature);return s>=0x4f47&&s<=0x4f51;}
// gate in front of RECTVISIBLE and the other DC entries.
inline bool DcGate(U signature){return ((Masked(signature)-0x4f4d)&65535)<=3;}
// The worker itself performs no gate and no range check; it exchanges
// the owner WORD and reports the previous value.
inline U ExchangeOwner(U&owner,U value){U previous=owner&65535;owner=value&65535;return previous;}
// Returns the old flag as its bit value, not as a boolean.
inline U ExchangePrivate(U&signature,bool make_private){
 U previous=signature&PrivateFlag;
 signature=(signature&~U(PrivateFlag))|(make_private?U(PrivateFlag):0u);
 return previous;}
// A matching bit stops the deletion and still reports success; the
// surviving object keeps both flags because the clear happens afterwards.
inline bool DeleteRefused(U signature,U mask){return ((signature>>8)&mask)!=0;}
// Both flags are cleared before the object is really destroyed.
inline U ClearFlags(U signature){return signature&~U(PrivateFlag|StockFlag);}
// with its handle bounds checks, over the GDI heap selector. Bit 1 of
// the handle selects the external handle table, whose resolver stays
// a provider. E supplies Read16(offset), Read32(offset), External(handle) and
// Handle. Returns the masked signature or zero.
template<class E>U Signature(E&e,U handle){
 handle&=65535;
 if(!handle)return 0;
 if(handle&2){
  if(handle<=e.Read16(6))return 0;
  U object=0,signature=e.External(handle,object);
  return object?Masked(signature):0;}
 if(handle&3)return 0;                      // handles are dword aligned
 if(handle<0x80)return 0;                   // below the first handle
 if(handle>e.Read16(e.HandleBase()+0x70))return 0; // heap handle limit
 U object=e.Read32(e.HandleBase()+handle);  //
 if(object<0x10000)return 0;                //
 return Masked(e.Read16(object+SignatureOffset));}
// and resolve their handle without those checks: bit 1 selects
// the external table, where a 0xff type byte aborts, and any other handle is
// dereferenced straight through the handle table.
template<class E>U Object(E&e,U handle,bool&aborted){
 handle&=65535;aborted=false;
 if(handle&2){
  if(e.Read8((handle&~U(1))+2)==0xff){aborted=true;return 0;}
  return e.Read16(handle&~U(1));}
 return e.Read32(e.HandleBase()+handle);}
// in full. Bit 1 of the handle selects the external handle table,
// whose type byte 0xff aborts and whose bit 0x40 adds one indirection through
// the ordinary handle table. E supplies Read8/Read16/Read32 over the GDI heap
// selector and Handle.
template<class E>U IsGdiObject(E&e,U handle){
 handle&=65535;
 if(!handle)return 0;
 U object=0;
 if(!(handle&2))object=e.Read32(e.HandleBase()+handle);
 else{
  const U type=e.Read8(handle+2);
  if(type==0xff)return 0;
  object=e.Read16(handle);
  if(type&0x40)object=e.Read32(e.HandleBase()+object);}
 return Classify(handle,e.Read16(object+SignatureOffset));}
// in full, the SETOBJECTOWNER worker. The external branch clears only
// bit 0 of the handle and follows one WORD reference.
template<class E>U ExchangeOwnerAt(E&e,U handle,U owner){
 handle&=65535;
 U object=0;
 if(handle&2){
  const U reference=handle&~U(1);
  if(e.Read8(reference+2)==0xff)return 0;
  object=e.Read16(reference);}
 else object=e.Read32(e.HandleBase()+handle);
 const U previous=e.Read16(object+OwnerOffset);
 e.Write16(object+OwnerOffset,owner&65535);
 return previous;}
// in full, the MAKEOBJECTPRIVATE worker, ordinary handles only. Bit 1
// of the handle enters the external resolver, a provider here.
// Handle zero leaves without writing anything, so the original still
// returns the GDI data selector the entry loaded into AX. That value
// is an artefact of the segmented build and has no native equivalent, so this
// implementation returns zero; the comparison observes the original value.
template<class E>U MakePrivateAt(E&e,U handle,bool make_private){
 handle&=65535;
 if(!handle)return 0;
 const U object=e.Read32(e.HandleBase()+handle);
 U signature=e.Read16(object+SignatureOffset);
 const U previous=ExchangePrivate(signature,make_private);
 e.Write16(object+SignatureOffset,signature);
 return previous;}
// GDI32 : a zero result from the owner validation is the only
// original path that skips the 16-bit call, and it returns zero.
inline bool OwnerAccepted(U validated_owner){return validated_owner!=0;}
// GDI32 zero-extends the WORD the 16-bit side returns.
inline U Result(U word){return word&65535;}
}
