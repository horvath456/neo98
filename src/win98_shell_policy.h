// SPDX-License-Identifier: GPL-2.0-or-later
// Shell policy queries with registry and text providers.
#pragma once
namespace Win98ShellPolicy {
using U=unsigned;
static constexpr char Root[]="Software\\Microsoft\\Windows\\CurrentVersion\\Policies";
struct Entry { U id; const char *group,*name; };
static constexpr Entry Entries[]={
 {0x00000001u,"Explorer","NoRun"},
 {0x00000002u,"Explorer","NoClose"},
 {0x00000004u,"Explorer","NoSaveSettings"},
 {0x00000008u,"Explorer","NoFileMenu"},
 {0x00000010u,"Explorer","NoSetFolders"},
 {0x00000020u,"Explorer","NoSetTaskbar"},
 {0x00000040u,"Explorer","NoDesktop"},
 {0x00000080u,"Explorer","NoFind"},
 {0x00000100u,"Explorer","NoDrives"},
 {0x00000200u,"Explorer","NoDriveAutoRun"},
 {0x00000400u,"Explorer","NoDriveTypeAutoRun"},
 {0x00000800u,"Explorer","NoNetHood"},
 {0x00001000u,"Explorer","NoStartBanner"},
 {0x00002000u,"Explorer","RestrictRun"},
 {0x00004000u,"Explorer","NoPrinterTabs"},
 {0x00008000u,"Explorer","NoDeletePrinter"},
 {0x00010000u,"Explorer","NoAddPrinter"},
 {0x00020000u,"Explorer","NoStartMenuSubFolders"},
 {0x00040000u,"Explorer","MyDocsOnNet"},
 {0x00080000u,"WinOldApp","NoRealMode"},
 {0x00100000u,"Explorer","EnforceShellExtensionSecurity"},
 {0x00400000u,"Explorer","NoCommonGroups"},
 {0x00200000u,"Explorer","LinkResolveIgnoreLinkInfo"},
 {0x01000000u,"Explorer","NoWebMenu"},
 {0x02000000u,"Explorer","NoTrayContextMenu"},
 {0x04000000u,"Explorer","NoViewContextMenu"},
 {0x08000000u,"Explorer","NoNetConnectDisconnect"},
 {0x10000000u,"Explorer","NoLogoff"},
 {0x20000000u,"Explorer","NoSettingsWizards"},
 {0x40000001u,"Explorer","NoInternetIcon"},
 {0x40000002u,"Explorer","NoRecentDocsHistory"},
 {0x40000003u,"Explorer","NoRecentDocsMenu"},
 {0x40000004u,"Explorer","NoActiveDesktop"},
 {0x40000005u,"Explorer","NoActiveDesktopChanges"},
 {0x40000006u,"Explorer","NoFavoritesMenu"},
 {0x40000007u,"Explorer","ClearRecentDocsOnExit"},
 {0x40000008u,"Explorer","ClassicShell"},
 {0x40000009u,"Explorer","NoCustomizeWebView"},
 {0x40000010u,"ActiveDesktop","NoHTMLWallPaper"},
 {0x40000011u,"ActiveDesktop","NoChangingWallPaper"},
 {0x40000012u,"ActiveDesktop","NoComponents"},
 {0x40000013u,"ActiveDesktop","NoAddingComponents"},
 {0x40000014u,"ActiveDesktop","NoDeletingComponents"},
 {0x40000015u,"ActiveDesktop","NoClosingComponents"},
 {0x40000016u,"ActiveDesktop","NoCloseDragDropBands"},
 {0x40000017u,"ActiveDesktop","NoMovingBands"},
 {0x40000018u,"ActiveDesktop","NoEditingComponents"},
 {0x40000019u,"Explorer","NoResolveSearch"},
 {0x4000001au,"Explorer","NoResolveTrack"},
 {0x4000001bu,"Explorer","ForceCopyACLWithFile"},
 {0x4000001cu,"Explorer","NoMSAppLogo5ChannelNotify"},
 {0x4000001du,"Explorer","NoForgetSoftwareUpdate"},
 {0x4000001eu,"Explorer","NoSetActiveDesktop"},
 {0x4000001fu,"Explorer","NoWindowsUpdate"},
 {0x40000020u,"Explorer","NoChangeStartMenu"},
 {0x40000021u,"Explorer","NoFolderOptions"},
 {0x50000024u,"Explorer","NoFileUrl"},
};
struct Cache { U values[57]; Cache(){for(auto &v:values)v=~0u;} };
// cold branch. First ABI argument is never read.
template<class E> U Reset(E &e,Cache &cache,const char *section) {
 if(section){const U result=e.Compare(Root,section);if(result)return result;}
 for(auto &v:cache.values)v=~0u;
 return ~0u;
}
//  with machine query at; table.
template<class E> U Query(E &e,Cache &cache,U policy) {
 U index=0;while(index<57 && Entries[index].id!=policy)++index;
 if(index==57)return 0;
 U &cached=cache.values[index];if(cached!=~0u)return cached;
 char path[108];e.Copy(path,Root);e.Append(path,"\\");e.Append(path,Entries[index].group);
 U result=0,key=0,type=0,bytes=4;bool machine=false;
 if(!e.Open(0x80000002,path,0,0x20019,key)){
  if(!e.Query(key,Entries[index].name,0,type,cached,bytes)){
   result=cached;if(cached!=~0u)machine=true;
  }
  e.Close(key);
 }
 if(!machine && !e.Open(0x80000001,path,0,0x20019,key)){
  bytes=4;if(!e.Query(key,Entries[index].name,0,type,cached,bytes))result=cached;
  e.Close(key);
 }
 return result;
}
}
