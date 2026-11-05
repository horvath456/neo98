// SPDX-License-Identifier: GPL-2.0-or-later
// The pasteboard of the Mac for the clipboard of the guest; see
// tools/host_pasteboard.h.
#import <AppKit/AppKit.h>
#include "host_pasteboard.h"
#include <pthread.h>

namespace
{
NSPasteboard *board;
bool private_board;
// AppKit's pasteboard belongs to the main thread here: the host's loop and
// the guest's calls run on it.
bool Usable() { return board && pthread_main_np(); }
NSArray<NSURL *> *FileUrls()
{
    return [board readObjectsForClasses:@[ NSURL.class ]
                                options:@{NSPasteboardURLReadingFileURLsOnlyKey : @YES}] ?: @[];
}
// The picture of the pasteboard as 8-bit RGBA, rows from the top, drawn over
// white.
bool Pixels(std::vector<unsigned char> &rgba, int &width, int &height)
{
    NSString *type = [board availableTypeFromArray:@[ NSPasteboardTypePNG, NSPasteboardTypeTIFF ]];
    NSData *data = type ? [board dataForType:type] : nil;
    NSBitmapImageRep *source = data ? [NSBitmapImageRep imageRepWithData:data] : nil;
    CGImageRef image = source.CGImage;
    if (!image) return false;
    width = (int)CGImageGetWidth(image);
    height = (int)CGImageGetHeight(image);
    if (width <= 0 || height <= 0 || width > 8192 || height > 8192) return false;
    rgba.assign(size_t(width) * height * 4, 255);
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef context = CGBitmapContextCreate(rgba.data(), width, height, 8, size_t(width) * 4, space,
                                                 kCGImageAlphaNoneSkipLast | kCGBitmapByteOrderDefault);
    CGColorSpaceRelease(space);
    if (!context) return false;
    CGContextSetRGBFillColor(context, 1, 1, 1, 1);
    CGContextFillRect(context, CGRectMake(0, 0, width, height));
    CGContextSetInterpolationQuality(context, kCGInterpolationNone);
    CGContextDrawImage(context, CGRectMake(0, 0, width, height), image);
    CGContextRelease(context);
    return true;
}
unsigned Little(const unsigned char *p, int bytes)
{
    unsigned value = 0;
    for (int i = 0; i < bytes; ++i) value |= unsigned(p[i]) << (8 * i);
    return value;
}
// A packed DIB as an image rep. BI_RGB with 1, 4, 8, 16, 24 or 32 bits and
// BI_BITFIELDS with 16 or 32; nil for anything else (RLE, JPEG, a header this
// does not know).
NSBitmapImageRep *RepOfDib(const std::vector<unsigned char> &dib)
{
    if (dib.size() < 40) return nil;
    const unsigned header = Little(&dib[0], 4), bits = Little(&dib[14], 2), compression = Little(&dib[16], 4);
    const int width = (int)Little(&dib[4], 4), signed_height = (int)Little(&dib[8], 4);
    const int height = signed_height < 0 ? -signed_height : signed_height;
    if (header < 40 || header > dib.size() || width <= 0 || height <= 0 || width > 16384 || height > 16384) return nil;
    if (compression != 0 && !(compression == 3 && (bits == 16 || bits == 32))) return nil;
    if (bits != 1 && bits != 4 && bits != 8 && bits != 16 && bits != 24 && bits != 32) return nil;
    unsigned colours = bits <= 8 ? Little(&dib[32], 4) : 0;
    if (bits <= 8 && (!colours || colours > (1u << bits))) colours = 1u << bits;
    unsigned masks[3] = {0x7c00, 0x3e0, 0x1f};
    if (bits == 32) { masks[0] = 0xff0000; masks[1] = 0xff00; masks[2] = 0xff; }
    size_t at = header;
    if (compression == 3) {
        // The masks follow a 40-byte header; a longer one holds them itself.
        if (dib.size() < 52) return nil;
        for (int i = 0; i < 3; ++i) masks[i] = Little(&dib[40 + i * 4], 4);
        if (header == 40) at += 12;
    }
    const size_t palette = at;
    at += size_t(colours) * 4;
    const size_t stride = ((size_t(width) * bits + 31) / 32) * 4;
    if (at > dib.size() || stride * height > dib.size() - at) return nil;
    NSBitmapImageRep *rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr
                                                                    pixelsWide:width
                                                                    pixelsHigh:height
                                                                 bitsPerSample:8
                                                               samplesPerPixel:3
                                                                      hasAlpha:NO
                                                                      isPlanar:NO
                                                                colorSpaceName:NSDeviceRGBColorSpace
                                                                   bytesPerRow:width * 3
                                                                  bitsPerPixel:24];
    // The colours of a Windows bitmap are sRGB; the numbers stay as they are.
    rep = [rep bitmapImageRepByRetaggingWithColorSpace:NSColorSpace.sRGBColorSpace] ?: rep;
    if (!rep) return nil;
    auto channel = [](unsigned pixel, unsigned mask) -> unsigned char {
        if (!mask) return 0;
        int shift = 0, width = 0;
        while (!((mask >> shift) & 1)) ++shift;
        while ((mask >> (shift + width)) & 1) ++width;
        const unsigned value = (pixel & mask) >> shift;
        return (unsigned char)(width >= 8 ? value >> (width - 8) : value * 255 / ((1u << width) - 1));
    };
    for (int y = 0; y < height; ++y) {
        const unsigned char *row = &dib[at + stride * (signed_height < 0 ? y : height - 1 - y)];
        unsigned char *out = rep.bitmapData + size_t(y) * rep.bytesPerRow;
        for (int x = 0; x < width; ++x, out += 3) {
            if (bits <= 8) {
                unsigned index = bits == 8 ? row[x] : bits == 4 ? (row[x / 2] >> (x & 1 ? 0 : 4)) & 15 : (row[x / 8] >> (7 - (x & 7))) & 1;
                if (index >= colours) index = 0;
                const unsigned char *entry = &dib[palette + index * 4];
                out[0] = entry[2]; out[1] = entry[1]; out[2] = entry[0];
            } else if (bits == 24) {
                out[0] = row[x * 3 + 2]; out[1] = row[x * 3 + 1]; out[2] = row[x * 3];
            } else {
                const unsigned pixel = Little(row + x * (bits / 8), bits / 8);
                for (int i = 0; i < 3; ++i) out[i] = channel(pixel, masks[i]);
            }
        }
    }
    return rep;
}
} // namespace

namespace HostPasteboard
{
bool Open(const char *name)
{
    @autoreleasepool {
        Close();
        const std::string wanted = name ? name : "general";
        if (wanted == "general") board = NSPasteboard.generalPasteboard;
        else if (wanted == "unique") { board = [NSPasteboard pasteboardWithUniqueName]; private_board = true; }
        else board = [NSPasteboard pasteboardWithName:[NSString stringWithUTF8String:wanted.c_str()]];
        return board != nil;
    }
}
bool Active() { return board != nil; }
bool Private() { return board != nil && private_board; }
std::string Name()
{
    @autoreleasepool { return board ? std::string(board.name.UTF8String ?: "") : std::string(); }
}
long ChangeCount() { return Usable() ? (long)board.changeCount : 0; }
unsigned Kinds()
{
    if (!Usable()) return 0;
    @autoreleasepool {
        unsigned kinds = 0;
        if ([board availableTypeFromArray:@[ NSPasteboardTypeString ]]) kinds |= Text;
        if ([board canReadObjectForClasses:@[ NSURL.class ] options:@{NSPasteboardURLReadingFileURLsOnlyKey : @YES}])
            kinds |= Files;
        // The Finder adds the icon of a copied file as a picture; the file is
        // what was copied.
        if (!(kinds & Files) && [board availableTypeFromArray:@[ NSPasteboardTypePNG, NSPasteboardTypeTIFF ]]) kinds |= Image;
        return kinds;
    }
}
bool ReadText(std::u16string &text)
{
    text.clear();
    if (!Usable()) return false;
    @autoreleasepool {
        NSString *string = [board stringForType:NSPasteboardTypeString];
        if (!string) return false;
        string = string.precomposedStringWithCanonicalMapping;
        text.resize(string.length);
        if (string.length) [string getCharacters:(unichar *)&text[0] range:NSMakeRange(0, string.length)];
        return true;
    }
}
bool ReadFiles(std::vector<std::string> &paths)
{
    paths.clear();
    if (!Usable()) return false;
    @autoreleasepool {
        for (NSURL *url in FileUrls())
            if (url.isFileURL && url.path.fileSystemRepresentation)
                paths.push_back(url.path.fileSystemRepresentation);
        return !paths.empty();
    }
}
bool ReadImage(std::vector<unsigned char> &dib)
{
    dib.clear();
    if (!Usable()) return false;
    @autoreleasepool {
        std::vector<unsigned char> rgba;
        int width = 0, height = 0;
        if (!Pixels(rgba, width, height)) return false;
        const size_t stride = (size_t(width) * 3 + 3) & ~size_t(3);
        dib.assign(40 + stride * height, 0);
        auto put = [&](size_t at, unsigned value, int bytes) { for (int i = 0; i < bytes; ++i) dib[at + i] = (unsigned char)(value >> (8 * i)); };
        put(0, 40, 4); put(4, (unsigned)width, 4); put(8, (unsigned)height, 4); put(12, 1, 2); put(14, 24, 2);
        put(20, (unsigned)(stride * height), 4);
        put(24, 3780, 4); put(28, 3780, 4); // 96 dots an inch
        for (int y = 0; y < height; ++y) {
            const unsigned char *in = &rgba[size_t(y) * width * 4];
            unsigned char *out = &dib[40 + stride * (height - 1 - y)];
            for (int x = 0; x < width; ++x, in += 4, out += 3) { out[0] = in[2]; out[1] = in[1]; out[2] = in[0]; }
        }
        return true;
    }
}
long WritePng(const std::vector<unsigned char> &png)
{
    if (!Usable()) return 0;
    @autoreleasepool {
        [board clearContents];
        [board setData:[NSData dataWithBytes:png.data() length:png.size()] forType:NSPasteboardTypePNG];
        return (long)board.changeCount;
    }
}
long Write(const std::u16string *text, const std::vector<std::string> *files, const std::vector<unsigned char> *dib)
{
    if (!Usable()) return 0;
    @autoreleasepool {
        [board clearContents];
        NSMutableArray *objects = [NSMutableArray array];
        if (files)
            for (const auto &path : *files)
                if (NSString *name = [NSFileManager.defaultManager stringWithFileSystemRepresentation:path.c_str()
                                                                                             length:path.size()])
                    [objects addObject:[NSURL fileURLWithPath:name]];
        if (objects.count) [board writeObjects:objects];
        if (text) {
            NSString *string = [NSString stringWithCharacters:(const unichar *)text->data() length:text->size()];
            // Beside files the string joins the items the URLs made; alone it
            // is the content.
            if (objects.count) [board addTypes:@[ NSPasteboardTypeString ] owner:nil];
            [board setString:string forType:NSPasteboardTypeString];
        }
        if (NSBitmapImageRep *rep = dib ? RepOfDib(*dib) : nil) {
            NSData *png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
            NSData *tiff = rep.TIFFRepresentation;
            NSMutableArray *types = [NSMutableArray array];
            if (png) [types addObject:NSPasteboardTypePNG];
            if (tiff) [types addObject:NSPasteboardTypeTIFF];
            [board addTypes:types owner:nil];
            if (png) [board setData:png forType:NSPasteboardTypePNG];
            if (tiff) [board setData:tiff forType:NSPasteboardTypeTIFF];
        }
        return (long)board.changeCount;
    }
}
std::string Describe()
{
    std::string out;
    if (!Usable()) return out;
    @autoreleasepool {
        if (NSString *string = [board stringForType:NSPasteboardTypeString]) {
            out += "text ";
            const char *utf8 = string.UTF8String ?: "";
            static const char digits[] = "0123456789abcdef";
            for (const char *p = utf8; *p; ++p) {
                out += digits[(unsigned char)*p >> 4];
                out += digits[(unsigned char)*p & 15];
            }
            out += '\n';
        }
        for (NSURL *url in FileUrls())
            if (url.isFileURL) out += std::string("file ") + (url.path.fileSystemRepresentation ?: "") + "\n";
        std::vector<unsigned char> rgba;
        int width = 0, height = 0;
        if (Pixels(rgba, width, height)) {
            char line[64];
            snprintf(line, sizeof line, "image %d %d", width, height);
            out += line;
            for (int i = 0; i < width * height && i < 64; ++i) {
                snprintf(line, sizeof line, " %02x%02x%02x", rgba[i * 4], rgba[i * 4 + 1], rgba[i * 4 + 2]);
                out += line;
            }
            out += '\n';
        }
    }
    return out;
}
void Close()
{
    if (board && private_board) [board releaseGlobally];
    board = nil;
    private_board = false;
}
} // namespace HostPasteboard
