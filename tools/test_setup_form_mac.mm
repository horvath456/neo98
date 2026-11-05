// SPDX-License-Identifier: GPL-2.0-or-later
// Standalone, windowless AppKit test. Includes the real assistant so field
// editing and page transitions run without a guest or an installation.
// clang++ -std=c++17 -fobjc-arc tools/test_setup_form_mac.mm -framework AppKit
// -framework UniformTypeIdentifiers -o build/test-setup-form-mac
#include "host_setup.mm"
#include <cassert>

static void Check(bool okay, const char *name)
{
    printf("%s %s\n", okay ? "PASS" : "FAIL", name);
    if (!okay) exit(1);
}

static void Capture(NSView *view, NSString *path)
{
    NSBitmapImageRep *image = [view bitmapImageRepForCachingDisplayInRect:view.bounds];
    [view cacheDisplayInRect:view.bounds toBitmapImageRep:image];
    [[image representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:path atomically:YES];
}

int main()
{
    @autoreleasepool
    {
        setenv("SDL_VIDEODRIVER", "dummy", 1);
        setenv("SDL_AUDIODRIVER", "dummy", 1);
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
        AssistantMenu();
        NSMenu *editMenu = [NSApp.mainMenu itemAtIndex:1].submenu;
        Check([editMenu itemAtIndex:0].action == @selector(undo:) &&
              [[editMenu itemAtIndex:0].keyEquivalent isEqual:@"z"] &&
              [editMenu itemAtIndex:1].action == @selector(redo:) &&
              [editMenu itemAtIndex:1].keyEquivalentModifierMask == (NSEventModifierFlagCommand | NSEventModifierFlagShift),
              "Edit menu exposes native Undo and Redo shortcuts");
        NEO98Assistant *assistant = [NEO98Assistant new];
        assistant->headless = YES;
        [assistant build:"/Example/Windows 98"];
        NSWindow *window = assistant->window;
        [window makeFirstResponder:assistant->keyField];
        NSTextView *editor = (NSTextView *)assistant->keyField.currentEditor;
        Check(editor != nil && ![assistant->keyField isKindOfClass:NSSecureTextField.class], "visible native product key field");
        auto contents = [&](NSString *expected, NSUInteger caret, const char *name) {
            if (![editor.string isEqual:expected] || editor.selectedRange.location != caret)
                printf("actual=%s caret=%lu expected=%s caret=%lu\n", editor.string.UTF8String, (unsigned long)editor.selectedRange.location, expected.UTF8String, (unsigned long)caret);
            Check([editor.string isEqual:expected] && editor.selectedRange.location == caret, name);
        };
        auto replace = [&](NSString *value) {
            editor.selectedRange = NSMakeRange(0, editor.string.length);
            [editor insertText:value replacementRange:editor.selectedRange];
        };
        for (NSString *letter in @[@"a", @"b", @"c", @"d", @"e", @"f"])
            [editor insertText:letter replacementRange:editor.selectedRange];
        contents(@"ABCDE-F", 7, "typing uppercases and inserts separator");
        editor.selectedRange = NSMakeRange(6, 0);
        [editor doCommandBySelector:@selector(deleteBackward:)];
        contents(@"ABCDF", 4, "backspace across separator deletes preceding character");
        replace(@"abcde-" "fghij-" "klmno-" "pqrst-" "uvwxy");
        contents(@"ABCDE-" "FGHIJ-" "KLMNO-" "PQRST-" "UVWXY", 29, "paste formatted lowercase key");
        replace(@" abcde fghij\nklmno pqrst uvwxy ");
        contents(@"ABCDE-" "FGHIJ-" "KLMNO-" "PQRST-" "UVWXY", 29, "paste whitespace-separated key");
        replace(@"abcdefghijklmnopqrstuvwxy");
        contents(@"ABCDE-" "FGHIJ-" "KLMNO-" "PQRST-" "UVWXY", 29, "paste unformatted key");
        editor.selectedRange = NSMakeRange(6, 5);
        [editor insertText:@"12345" replacementRange:editor.selectedRange];
        contents(@"ABCDE-" "12345-" "KLMNO-" "PQRST-" "UVWXY", 11, "replace selected block without moving to end");
        editor.selectedRange = NSMakeRange(5, 0);
        [editor doCommandBySelector:@selector(deleteForward:)];
        contents(@"ABCDE-2345K-LMNOP-QRSTU-VWXY", 6, "forward delete across separator");
        [editor insertText:@"z" replacementRange:editor.selectedRange];
        contents(@"ABCDE-" "Z2345-" "KLMNO-" "PQRST-" "UVWXY", 7, "insert in middle retains caret");
        replace(@"");
        contents(@"", 0, "select all and delete");
        [editor breakUndoCoalescing];
        [editor.undoManager removeAllActions];
        NSPasteboard *clipboard = [NSPasteboard pasteboardWithUniqueName];
        [clipboard setString:@"abcde12345fghij67890klmno" forType:NSPasteboardTypeString];
        Check([editor readSelectionFromPasteboard:clipboard], "native pasteboard insertion");
        [clipboard releaseGlobally];
        contents(@"ABCDE-" "12345-" "FGHIJ-" "67890-" "KLMNO", 29, "pasted key is grouped and visible");
        [editor breakUndoCoalescing];
        [editor.undoManager undo];
        contents(@"", 0, "undo paste");
        [editor.undoManager redo];
        contents(@"ABCDE-" "12345-" "FGHIJ-" "67890-" "KLMNO", 29, "redo paste");
        replace(@"");
        for (NSUInteger i = 0; i < 29; ++i)
            [editor insertText:[@"abcde-" "fghij-" "klmno-" "pqrst-" "uvwxy" substringWithRange:NSMakeRange(i, 1)] replacementRange:editor.selectedRange];
        contents(@"ABCDE-" "FGHIJ-" "KLMNO-" "PQRST-" "UVWXY", 29, "typing full key with manual separators");
        editor.selectedRange = NSMakeRange(4, 4);
        [editor insertText:@"123" replacementRange:editor.selectedRange];
        contents(@"ABCD1-" "23HIJ-" "KLMNO-" "PQRST-" "UVWXY", 8, "replacement spanning a separator");
        replace(@"");
        // Validate rejected input directly, without the system alert sound.
        for (NSString *bad in @[@"ABCDEFGHIJKLMNOPQRSTUVWXYZ", @"ABCDE!", @"ÄBCDE"])
        {
            NSString *proposed = bad;
            NSRange selection = NSMakeRange(bad.length, 0);
            Check(![assistant->keyField.formatter isPartialStringValid:&proposed proposedSelectedRange:&selection
                       originalString:@"" originalSelectedRange:NSMakeRange(0, 0) errorDescription:nil] && !proposed.length,
                  "invalid or overlong key is rejected without truncation");
        }
        [window makeFirstResponder:nil];
        assistant->isoField.stringValue = @"Windows 98 Second Edition.7z";
        Capture(window.contentView, @"build/setup-form.png");
        const NSRect keyFrame = assistant->keyField.frame;
        for (int round = 0; round < 3; ++round)
        {
            [assistant showForm:NO];
            Check(assistant->logScroll.hidden && assistant->isoField.hidden && !assistant->previewView.hidden,
                  "progress page hides form and technical details");
            Check(NSMinY(assistant->progress.frame) - NSMaxY(assistant->previewView.frame) >= 24 &&
                  NSMinY(assistant->previewView.frame) - NSMaxY(assistant->cancelButton.frame) >= 20 &&
                  NSMaxX(assistant->cancelButton.frame) == 576, "preview spacing and right-aligned Cancel");
            assistant->statusLabel.stringValue = @"Preparing Windows Setup…";
            if (!round) Capture(window.contentView, @"build/setup-progress.png");
            [assistant toggleDetails:nil];
            Check(!assistant->logScroll.hidden && NSMinY(assistant->previewView.frame) - NSMaxY(assistant->logScroll.frame) >= 20,
                  "expanded log has space below preview");
            if (!round) Capture(window.contentView, @"build/setup-details.png");
            [assistant toggleDetails:nil];
            [assistant showForm:YES];
            Check(NSEqualRects(keyFrame, assistant->keyField.frame) && !assistant->keyField.hidden && assistant->previewView.hidden,
                  "returning to form restores fields without layout drift");
        }
        [window close];
        puts("SETUP FORM PASS");
    }
}
