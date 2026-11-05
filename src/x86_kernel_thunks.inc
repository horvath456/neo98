// SPDX-License-Identifier: GPL-2.0-or-later
// Connect KERNEL32 Win16/Win32 bridge helpers to native providers. Resolve
// transitions through x86_flat_thunk.cpp and stop unsupported targets with a
// diagnostic.
#include "win98_flat_thunk.h"
#include "win98_segment_map.h"
#include "win98_thunk_bridges.h"
namespace X86{
namespace{
constexpr unsigned ThunkSupportRet=0,ThunkSupportTables=0x1000,ThunkTokenTableBytes=0x400;
}
unsigned Application::SegmentNode(unsigned segmented){
 if(!(segmented>>16))return 0;
 if(!selector_nodes)selector_nodes=Allocate(0x10000);
 return selector_nodes?selector_nodes+((segmented>>16)&~7u):0;
}
unsigned Application::SegmentMap(unsigned pointer,bool arena){
 const unsigned result=selectors.Map(pointer,arena);
 if(result>>16){
  const unsigned node=SegmentNode(result);unsigned char bytes[8];
  if(!node){selectors.Unmap(result);return 0;}
  selectors.Descriptor(result>>16,bytes);Store(node,bytes,8);
  // A 16-bit module hosted in this process (src/x86_thunk_hosted.cpp) loads
  // the selector from the LDT, as the original's code does after QT_Thunk:
  // W95INF16 hands SMapLS pointers straight to SETUPX.
  if(win16.state)Win16Descriptor(result>>16);
 }
 return result;
}
// A frame slot that SMapLS_IP_EBP_n has rewritten carries the address of the
// descriptor record, not the flat pointer the caller passed in. A native
// service that runs below such a thunk reads its pointer arguments through
// this, so it sees the flat pointer again. Everything else passes unchanged.
unsigned Application::SegmentSlotLinear(unsigned slot_value){
 if(!selector_nodes||slot_value<selector_nodes+8||slot_value>=selector_nodes+0x8000||
    ((slot_value-selector_nodes)&7))return slot_value;
 return selectors.Linear(((slot_value-selector_nodes)|7)<<16);
}
void Application::SegmentUnmapNode(unsigned node){
 if(selector_nodes&&node>=selector_nodes+8&&node<selector_nodes+0x8000&&!((node-selector_nodes)&7)){
  const unsigned selector=(node-selector_nodes)|7;
  selectors.Unmap(selector<<16);
  if(win16.state&&(selector>>3)*8+8<=Win16LdtBytes){
   const unsigned char zero[8]={};Store(Win16LdtBase+(selector>>3)*8,zero,8);
  }
 }
}
struct Application::ThunkMemory{
 Application&app;
 unsigned Read32(unsigned a){return app.Read32(a);}
 void Write32(unsigned a,unsigned v){app.Write32(a,v);}
};
// One 64 KiB executable region per process: a RET byte for Callbackn and the
// token tables the connector publishes at ThunkData32+8.
unsigned Application::ThunkSupportPage(){
 if(thunk_support)return thunk_support;
 const unsigned args[]={0,0x10000,0x3000,0x40};
 const unsigned saved=last_error;thunk_support=Api("VirtualAlloc",args);last_error=saved;
 if(!thunk_support){Fail("Thunk support page allocation failed");return 0;}
 const unsigned char ret=0xc3;Store(thunk_support+ThunkSupportRet,&ret,1);
 return thunk_support;
}
unsigned Application::ThunkToken(unsigned connection,unsigned index){return ThunkTokenBase|(connection<<8)|(index&0xff);}
// 256 tokens per connection, one per index byte the templates can form.
unsigned Application::ThunkTokenTable(unsigned connection){
 if(connection>=ThunkConnectionCapacity||!ThunkSupportPage())return 0;
 const unsigned table=thunk_support+ThunkSupportTables+connection*ThunkTokenTableBytes;
 for(unsigned i=0;i<256;++i)Write32(table+i*4,ThunkToken(connection,i));
 return table;
}
// The same table for a block whose 16-bit side is a Win16 catalogue module
// (Route::Native16 in src/dll_routes.h). Each index byte of the generated
// 32-bit stubs carries the dispatch identity of the catalogue export the
// original target table named; every other index stays zero, so a transition
// the original never made stops the run in QT_Thunk instead of reaching a
// routine.
unsigned Application::Win16CatalogTokenTable(unsigned connection,unsigned handle,const unsigned char *ordinals,unsigned count){
 if(connection>=ThunkConnectionCapacity||!ThunkSupportPage())return 0;
 const unsigned table=thunk_support+ThunkSupportTables+connection*ThunkTokenTableBytes;
 for(unsigned i=0;i<256;++i){
  const unsigned ordinal=i<count?ordinals[i]:0;
  Write32(table+i*4,ordinal?Win16CatalogTarget(handle,ordinal):0);
 }
 return table;
}
Application::ThunkConnection *Application::ThunkTokenDecode(unsigned token,unsigned&index){
 if((token&0xffff0000u)!=ThunkTokenBase)return nullptr;
 const unsigned connection=(token>>8)&0xff;index=token&0xff;
 if(connection>=thunk_connection_count||!thunk_connections[connection].connected)return nullptr;
 return &thunk_connections[connection];
}
void Application::ThunkTargetFail(const char *entry,unsigned token){
 char message[160];snprintf(message,sizeof message,"%s target %08x is not a connected flat thunk",entry,token);Fail(message);
}
// KERNEL32. EDX names the 16-bit target; here it is a token from a
// table this provider wrote. The stub's return address is on top of the stack
// and its 16-bit words lie above it.
bool Application::QtThunkEntry(unsigned token){
 const unsigned sp=Register(Reg::Esp),ret=Read32(sp),bp=Register(Reg::Ebp);
 unsigned index=0;auto*connection=ThunkTokenDecode(token,index);
 unsigned result=0,words=0;
 if(!connection){const unsigned bytes=Win98ThunkBridges::QuickArgumentBytes(bp,sp+4);
  if(!NativeWin16Dispatch(token,sp+4,bytes,result,words)&&(state==Failed||!FlatThunkHostedCall(token,sp+4,bytes,result,words))){if(state!=Failed)ThunkTargetFail("QT_Thunk",token);return false;}}
 else if(!FlatThunkDispatch(connection,index,bp,sp+4,Win98ThunkBridges::QuickArgumentBytes(bp,sp+4),ret,result,words))return false;
 Register(Reg::Eax,Win98ThunkBridges::ResultLow(result));Register(Reg::Edx,Win98ThunkBridges::ResultHigh(result));
 Register(Reg::Esp,sp+4+Win98FlatThunk::ArgumentBytes(words));Register(Reg::Eip,ret);pending=0;return true;
}
// KERNEL32, reached through the import or through the CL-indexed
// template of a connected block, which loads EDX first.
bool Application::FtPrologEntry(unsigned edx){
 Win98ThunkBridges::Registers in;
 in.eax=Register(Reg::Eax);in.ecx=Register(Reg::Ecx);in.edx=edx;in.ebx=Register(Reg::Ebx);
 in.esp=Register(Reg::Esp);in.ebp=Register(Reg::Ebp);in.esi=Register(Reg::Esi);in.edi=Register(Reg::Edi);
 const unsigned ret=Read32(in.esp);ThunkMemory memory{*this};
 const auto out=Win98ThunkBridges::Prolog(memory,in,ret,Read32(Teb()),FtThunkHandler);
 if(state==Failed)return false;
 Write32(Teb(),Win98ThunkBridges::HandlerRecord(out.ebp));
 Register(Reg::Eax,out.eax);Register(Reg::Edx,edx);Register(Reg::Ebp,out.ebp);Register(Reg::Esp,out.esp);Register(Reg::Esi,out.esi);
 Register(Reg::Eflags,Register(Reg::Eflags)&~0x400u);Register(Reg::Eip,ret);pending=0;return true;
}
// KERNEL32. The frame FT_Prolog built holds the target and the
// pointer mask; the arguments lie between ESP+4 and EBP-0x40.
bool Application::FtThunkEntry(){
 using namespace Win98ThunkBridges;
 const unsigned sp=Register(Reg::Esp),ret=Read32(sp),bp=Register(Reg::Ebp);
 const unsigned token=Read32(bp-Target),mask=Read32(bp-PointerMask);
 Write32(bp-ThunkReturn,ret);Write32(Teb(),Read32(bp-PreviousHandler));
 unsigned index=0;auto*connection=ThunkTokenDecode(token,index);
 if(!connection){unsigned result=0,words=0;
  const unsigned bytes=QuickArgumentBytes(bp,sp+4);
  if(!NativeWin16Dispatch(token,sp+4,bytes,result,words)&&(state==Failed||!FlatThunkHostedCall(token,sp+4,bytes,result,words))){if(state!=Failed)ThunkTargetFail("FT_Thunk",token);return false;}
  Register(Reg::Eax,ResultLow(result));Register(Reg::Edx,ResultHigh(result));Register(Reg::Esp,sp+4+Win98FlatThunk::ArgumentBytes(words));Register(Reg::Eip,ret);pending=0;return true;
 }
 if(log&&mask){char text[160];snprintf(text,sizeof text,"FLATTHUNK pointer mask=%08x arguments=%08x",mask,sp+4);log(text);}
 unsigned result=0,words=0;
 if(!FlatThunkDispatch(connection,index,bp,sp+4,QuickArgumentBytes(bp,sp+4),ret,result,words))return false;
 Register(Reg::Eax,ResultLow(result));Register(Reg::Edx,ResultHigh(result));
 Register(Reg::Esp,sp+4+Win98FlatThunk::ArgumentBytes(words));Register(Reg::Eip,ret);pending=0;return true;
}
// KERNEL32.. FT_Exitn, entered by a jump from the stub.
bool Application::FtExitEntry(unsigned bytes){
 if(!Win98ThunkBridges::ExitBytesValid(bytes)){Fail("Unsupported FT_Exit size");return false;}
 Win98ThunkBridges::Registers in;in.ebp=Register(Reg::Ebp);in.ebx=Register(Reg::Ebx);
 Write32(Teb(),Read32(in.ebp-Win98ThunkBridges::PreviousHandler));
 ThunkMemory memory{*this};const auto out=Win98ThunkBridges::Exit(memory,in);
 if(state==Failed)return false;
 const unsigned ret=Read32(out.esp);
 Register(Reg::Eax,out.eax);Register(Reg::Edi,out.edi);Register(Reg::Esi,out.esi);Register(Reg::Ebx,out.ebx);
 Register(Reg::Ebp,out.ebp);Register(Reg::Esp,out.esp+4+bytes);Register(Reg::Eip,ret);pending=0;return true;
}
// KERNEL32 for a non-unwind exception inside an FT frame, resumed
// through the dispatcher's context record instead of RtlUnwind: fs:[0] is
// restored, EAX takes the error result chosen by the saved CH, the callee
// registers come back from the frame, and the stub's own return address is
// taken with CH>>2 argument dwords removed. "frame" is the registration
// record at EBP-0x2c.
bool Application::FtThunkFault(unsigned frame,unsigned context){
 using namespace Win98ThunkBridges;
 const unsigned bp=frame+PreviousHandler,saved_ecx=Read32(bp-SavedEcx),saved_eax=Read32(bp-SavedEax);
 const unsigned result=FaultResult(saved_ecx,saved_eax),count=FaultArgumentDwords(saved_ecx);
 Write32(Teb(),Read32(bp-PreviousHandler));
 const unsigned esp=bp+4,ret=Read32(esp);
 Write32(context+0xb0,result);Write32(context+0xac,count);Write32(context+0x9c,Read32(bp-SavedEdi));Write32(context+0xa0,Read32(bp-SavedEsi));
 Write32(context+0xa4,Read32(bp-SavedEbx));Write32(context+0xa8,ret);Write32(context+0xb4,Read32(bp));Write32(context+0xc4,esp+4+count*4);Write32(context+0xb8,ret);
 if(log){char text[160];snprintf(text,sizeof text,"FLATTHUNK fault frame=%08x result=%08x dwords=%u resume=%08x",bp,result,count,ret);log(text);}
 return state!=Failed;
}
// KERNEL32.. Callbackn: the procedure is the last argument and its
// slot receives the return address; the stub jumps there with a RET on top.
bool Application::CallbackEntry(unsigned bytes){
 if(!Win98ThunkBridges::CallbackBytesValid(bytes)){Fail("Unsupported Callback size");return false;}
 if(!ThunkSupportPage())return false;
 const unsigned sp=Register(Reg::Esp);ThunkMemory memory{*this};
 const unsigned procedure=Win98ThunkBridges::Callback(memory,sp,bytes,thunk_support+ThunkSupportRet);
 if(state==Failed)return false;
 Register(Reg::Edx,procedure);Register(Reg::Eip,procedure);pending=0;return true;
}
// KERNEL32 : take a free entry from the process's callback pages or
// add a page. The original allocates 0xc8c bytes from KERNEL's heap and maps
// a code selector over them; this provider takes a 64 KiB executable region
// and installs a native LDT descriptor for that page.
unsigned Application::CallbackEntryTake(){
 using namespace Win98ThunkBridges;
 for(unsigned page=thunk_callback_pages;page;page=Read32(page+PageNext)){
  const unsigned entry=Read32(page+PageFreeHead);if(!entry)continue;
  Write32(page+PageFreeHead,Read32(entry+EntryNextFree));
  Write32(entry+EntryOwner,process_id);const unsigned char marker=FreeMarker;Store(entry,&marker,1);
  return entry;
 }
 const unsigned args[]={0,0x10000,0x3000,0x40};const unsigned saved=last_error;
 const unsigned page=Api("VirtualAlloc",args);last_error=saved;
 if(!page)return 0;
 Write32(page+PageSelector,SegmentMap(page)>>16);
 Write32(page+PageFreeHead,EntryAddress(page,0));
 for(unsigned i=0;i<EntryCount;++i){
  const unsigned entry=EntryAddress(page,i);const unsigned char marker=FreeMarker;Store(entry,&marker,1);
  Write32(entry+EntryNextFree,i+1<EntryCount?EntryAddress(page,i+1):0);Write32(entry+EntryOwner,0);Write32(entry+EntryPage,page);
 }
 Write32(page+PageNext,thunk_callback_pages);thunk_callback_pages=page;
 if(log){char text[96];snprintf(text,sizeof text,"CALLBACKPAGE page=%08x selector=%04x",page,Read32(page+PageSelector));log(text);}
 return CallbackEntryTake();
}
// Is the pointer one of this process's entries?
unsigned Application::CallbackEntryFind(unsigned pointer,bool segmented){
 using namespace Win98ThunkBridges;
 if(!pointer)return 0;
 for(unsigned page=thunk_callback_pages;page;page=Read32(page+PageNext)){
  if(segmented?SegmentedEntryOf(Read32(page+PageSelector),pointer):EntryOf(page,pointer))return segmented?selectors.Linear(pointer):pointer;
 }
 return 0;
}
// Return the thunk to its page's free list.
void Application::CallbackEntryRelease(unsigned entry){
 using namespace Win98ThunkBridges;
 const unsigned char marker=FreeMarker;Store(entry,&marker,1);Write32(entry+EntryOwner,0);
 const unsigned page=Read32(entry+EntryPage);Write32(entry+EntryNextFree,Read32(page+PageFreeHead));Write32(page+PageFreeHead,entry);
}
unsigned Application::CallbackApi(ApiName name,const unsigned *a){
 using namespace Win98ThunkBridges;
 if(API_IS(name,"AllocLSCallback")){
  const unsigned entry=CallbackEntryTake();if(!entry)return 0;
  unsigned char code[LsLength];LsTemplate(code,entry,a[0],a[1]);Store(entry,code,sizeof code);return entry;
 }
 if(API_IS(name,"AllocSLCallback")){
  const unsigned entry=CallbackEntryTake();if(!entry)return 0;
  unsigned char code[SlLength];SlTemplate(code,a[0],a[1]);Store(entry,code,sizeof code);
  const unsigned page=Read32(entry+EntryPage),offset=entry-page;
  if(!SegmentedOffsetValid(offset)){Fail("SL callback entry beyond its selector");return 0;}
  return SegmentedAddress(Read32(page+PageSelector),offset);
 }
 const bool segmented=strstr(name,"SL")!=nullptr;
 const unsigned entry=CallbackEntryFind(a[0],segmented);
 if(API_IS(name,"FreeLSCallback")||API_IS(name,"FreeSLCallback")){
  if(!entry)return 0;
  const unsigned owner=Read32(entry+EntryOwner);
  if(owner&&owner==process_id)CallbackEntryRelease(entry);
  return entry;
 }
 unsigned char first=0;const bool owned=entry&&Read32(entry+EntryOwner)==process_id&&Read(entry,&first,1)&&first==(segmented?SlMarker:LsMarker);
 if(API_IS(name,"IsLSCallback")||API_IS(name,"IsSLCallback"))return owned?IsResult(entry):0;
 if(!owned)return 0;
 if(API_IS(name,"GetLSCallbackTarget"))return Read32(entry+LsTargetQuirk);
 if(API_IS(name,"GetLSCallbackTemplate"))return LsTemplateTarget(entry,Read32(entry+LsRelative));
 return Read32(entry+SlTarget); // GetSLCallbackTarget and GetSLCallbackTemplate share /.
}
// KERNEL32 /0x1a43: module aliases are WORDs, never PE bases. The native
// table replaces the NE module database; lifetime follows the loader record.
unsigned Application::ThunkInstanceApi(unsigned instance,bool segmented_to_linear,bool preserve_null){
 if(segmented_to_linear)instance&=0xffffu;
 if(!instance){
  if(preserve_null)return 0;
  const unsigned args[]={0};
  instance=Api("GetModuleHandleA",args);
  if(segmented_to_linear)return instance;
 }
 if(segmented_to_linear){
  for(const auto &module:native_win16_modules)if(module.handle==instance)return module.memory;
  for(const auto &module:modules)
   if(module.kind&&!module.unloading&&module.win16_alias==instance)return module.handle;
  return 0;
 }
 if(instance<0x10000u)return instance;
 for(const auto &module:native_win16_modules)if(module.handle&&module.memory==instance)return module.handle;
 // ModuleIndex answers ModuleCapacity for a handle the process has not
 // loaded. The answer is the table bound itself, so it must be compared
 // against that bound; a separate copy of the number goes stale when the
 // table grows (capacity can expand) and the miss is then read
 // as a slot.
 const unsigned index=ModuleIndex(instance);
 if(index>=ModuleCapacity)return 0;
 auto &module=modules[index];
 if(!module.win16_alias){
  if(next_module_alias>0xffffu)return 0;
  module.win16_alias=next_module_alias;next_module_alias+=8;
 }
 return module.win16_alias;
}
unsigned Application::ThunkBridgeApi(ApiName name,const unsigned *a){
 if(!strncmp(name,"Callback",8))return CallbackEntry(Win98ThunkBridges::NameNumber(name)),0;
 if(!strncmp(name,"FT_Exit",7))return FtExitEntry(Win98ThunkBridges::NameNumber(name)),0;
 switch (name.id)
 {
 API_CASE("FT_Prolog")return FtPrologEntry(Register(Reg::Edx)),0;
 API_CASE("FT_Thunk")return FtThunkEntry(),0;
 API_CASE("QT_Thunk")return QtThunkEntry(Register(Reg::Edx)),0;
 // Register adapters since PLAN.md step 8: the original released and retook
 // the Win16 mutex here, NEO98 only moves the counter at fs:[0x1e].
 // Prolog returns the current thread identity; Epilog preserves EAX/EDX.
 API_CASE("K32Thk1632Prolog"){ThunkLockCount(0xffff);return ThreadId();}
 API_CASE("K32Thk1632Epilog"){ThunkLockCount((ThunkLockCount()+1)&65535);return Register(Reg::Eax);}
 API_CASE("MapLS")return SegmentMap(a[0]);
 API_CASE("SMapLS"){
  unsigned mapped=SegmentMap(Register(Reg::Eax));Register(Reg::Edx,SegmentNode(mapped));return mapped;
 }
 API_CASE("UnMapLS"){selectors.Unmap(a[0]);return 0;}
 API_CASE("SUnMapLS"){SegmentUnmapNode(Register(Reg::Ecx));return Register(Reg::Eax);}
 API_CASE("UnMapSLFixArray"){
  for(unsigned i=0;i<a[0];++i)selectors.Unfix(Read32(a[1]+4*i));return Register(Reg::Eax);
 }
 API_CASE("MapSL")return selectors.Linear(a[0]);
 API_CASE("MapSLFix")return selectors.Fix(a[0]);
 API_CASE("MapHInstLS")return ThunkInstanceApi(Register(Reg::Eax),false,false);
 API_CASE("MapHInstLS_PN")return ThunkInstanceApi(Register(Reg::Eax),false,true);
 API_CASE("MapHModuleLS")return ThunkInstanceApi(a[0],false,false);
 API_CASE("MapHInstSL")return ThunkInstanceApi(Register(Reg::Eax),true,false);
 API_CASE("MapHInstSL_PN")return ThunkInstanceApi(Register(Reg::Eax),true,true);
 API_CASE("MapHModuleSL")return ThunkInstanceApi(a[0],true,false);
 // PLAN.md step 8: no universal thunks. The original loads a 16-bit DLL
 // through KRNL386; without a NE loader UTRegister answers FALSE and
 // UTUnRegister answers zero, as KERNEL32 does when the load fails.
 API_CASE("UTRegister") API_CASE("UTUnRegister")return 0;
 default:
     break;
 }
 if(strstr(name,"Callback"))return CallbackApi(name,a);
 Fail(name);return 0;
}
}
