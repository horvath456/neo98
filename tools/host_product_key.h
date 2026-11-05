// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#import <Foundation/Foundation.h>

// Let AppKit keep native selection, paste and undo behavior. Only normalize
// the proposed edit, preserving the caret's position among the key characters.
@interface NEO98ProductKeyFormatter : NSFormatter
@end

@implementation NEO98ProductKeyFormatter
- (NSString *)stringForObjectValue:(id)value
{
    return [value isKindOfClass:NSString.class] ? value : @"";
}
- (BOOL)getObjectValue:(out id *)value forString:(NSString *)string errorDescription:(out NSString **)error
{
    if (value) *value = string;
    return YES;
}
- (BOOL)isPartialStringValid:(NSString **)partial proposedSelectedRange:(NSRangePointer)selection
              originalString:(NSString *)original originalSelectedRange:(NSRange)oldSelection
            errorDescription:(NSString **)error
{
    NSString *candidate = *partial;
    const NSUInteger caret = MIN(selection->location, candidate.length);
    NSMutableString *characters = [NSMutableString string];
    NSUInteger logicalCaret = 0;
    for (NSUInteger i = 0; i < candidate.length; ++i)
    {
        unichar c = [candidate characterAtIndex:i];
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
        {
            [characters appendFormat:@"%C", c];
            if (i < caret) ++logicalCaret;
        }
        else if (c != '-' && ![NSCharacterSet.whitespaceAndNewlineCharacterSet characterIsMember:c])
        {
            *partial = original; *selection = oldSelection;
            return NO;
        }
    }
    if (characters.length > 25)
    {
        *partial = original; *selection = oldSelection;
        return NO;
    }
    NSMutableString *formatted = [NSMutableString string];
    for (NSUInteger i = 0; i < characters.length; ++i)
    {
        if (i && i % 5 == 0) [formatted appendString:@"-"];
        [formatted appendFormat:@"%C", [characters characterAtIndex:i]];
    }
    if ([formatted isEqual:*partial]) return YES;
    *partial = formatted;
    const NSUInteger separators = characters.length ? (characters.length - 1) / 5 : 0;
    *selection = NSMakeRange(logicalCaret + MIN(logicalCaret / 5, separators), 0);
    return NO;
}
@end
