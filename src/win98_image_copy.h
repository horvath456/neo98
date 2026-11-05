// SPDX-License-Identifier: GPL-2.0-or-later
// CopyImage validation, image-kind selection, sizing, and ownership behavior.
#pragma once
namespace Win98ImageCopy {
using U=unsigned;
enum:U{
 Bitmap=0,Icon=1,Cursor=2,
 Monochrome=0x0001,CopyReturnOrg=0x0004,CopyDeleteOrg=0x0008,
 DefaultSize=0x0040,VgaColor=0x0080,CopyFromResource=0x4000,
 ReservedFlags=0x0f00,      // Windows 98 rejects every bit in this mask
 InvalidParameter=0x6001,   // parameter error identifier
 InvalidFlags=0xe002,       // flag error identifier
};
// Zero means the call is accepted.
inline U Validate(U type,U flags){
 if(type>Cursor)return InvalidParameter;
 if(flags&ReservedFlags)return InvalidFlags;
 return 0;}
// A display without colour cursors turns every cursor copy into a
// monochrome copy before anything else looks at the flags.
inline U CursorFlags(U type,U flags,bool colour_cursors){
 return type==Cursor&&!colour_cursors?flags|Monochrome:flags;}
// Shared size calculation for width and height.
inline U Extent(U requested,U flags,U system_metric,U source){
 if(requested)return requested;
 if(flags&DefaultSize)return system_metric;
 return source;}
// The same-size fast path only survives when the source format
// already satisfies the requested one; otherwise the worker rebuilds.
inline bool FormatMatches(U flags,U planes,U bits){
 const U depth=planes*bits;
 if((flags&Monochrome)&&depth!=1)return false;
 if((flags&VgaColor)&&depth!=4)return false;
 return true;}
enum Route{
 Reject,       // validation failed
 FromResource, // LR_COPYFROMRESOURCE
 ReturnOriginal, // LR_COPYRETURNORG on an exact match
 Duplicate,    // same size and format
 Rebuild,      // different size or format
};
// for icons and cursors. width/height are already the effective
// extents; source_width/source_height are the image's own.
inline Route Select(U flags,U width,U height,U source_width,U source_height,U planes,U bits){
 if(flags&CopyFromResource)return FromResource;
 if(width!=source_width||height!=source_height)return Rebuild;
 if(!FormatMatches(flags,planes,bits))return Rebuild;
 return flags&CopyReturnOrg?ReturnOriginal:Duplicate;}
// The bitmap worker has no LR_COPYRETURNORG branch; it decides
// the target format and always builds a new bitmap.
inline void BitmapFormat(U flags,U source_planes,U source_bits,U&planes,U&bits){
 if(flags&Monochrome){planes=1;bits=1;return;}
 if((flags&VgaColor)&&!(source_planes==1&&source_bits==1)){planes=1;bits=4;return;}
 planes=source_planes;bits=source_bits;}
// A result equal to the source is never destroyed, so
// LR_COPYRETURNORG and LR_COPYDELETEORG together keep the original alive.
inline bool DeleteSource(U flags,U source,U result){
 return (flags&CopyDeleteOrg)&&result&&result!=source;}
}
