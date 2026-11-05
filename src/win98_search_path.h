// SPDX-License-Identifier: GPL-2.0-or-later
// Windows 98 SearchPathA validation and search order. Directory and file queries
// are supplied by the environment.
#pragma once
namespace Win98SearchPath {
using U=unsigned;
enum Step{ApplicationDirectory,CurrentDirectory,SystemDirectory,WindowsDirectory,PathVariable};
enum:U{Steps=5,Mode=0,InvalidParameter=87,Limit=0x104};
// Search directories in the order selected by the mode.
inline const Step*Order(U mode){
 static const Step tables[3][Steps]={
  {ApplicationDirectory,CurrentDirectory,SystemDirectory,WindowsDirectory,PathVariable},
  {CurrentDirectory,WindowsDirectory,SystemDirectory,ApplicationDirectory,PathVariable},
  {SystemDirectory,WindowsDirectory,CurrentDirectory,ApplicationDirectory,PathVariable}};
 return tables[mode<2?mode:2];}
// An empty extension string is accepted; anything that does
// not begin with a dot is rejected before the search starts.
inline bool ExtensionAccepted(const char*extension){
 return !extension||!*extension||*extension=='.';}
// A name that already carries a directory or a drive is probed as
// it stands and never enters the step table.
inline bool Qualified(const char*name){
 if(!name)return false;
 if(name[0]&&name[1]==':')return true;
 for(const char*p=name;*p;++p)if(*p=='\\'||*p=='/')return true;
 return false;}
// The extension argument is only appended when the name itself has none. The
// scan stops at a separator so that "a.b\\c" still counts as extensionless.
inline bool NeedsExtension(const char*name){
 bool dot=false;
 for(const char*p=name;*p;++p){
  if(*p=='\\'||*p=='/'||*p==':')dot=false;
  else if(*p=='.')dot=true;}
 return !dot;}
// A result that does not fit reports the required size
// including the terminator and writes neither the buffer nor lpFilePart.
inline bool Fits(U length,U capacity){return length&&length<=capacity;}
}
