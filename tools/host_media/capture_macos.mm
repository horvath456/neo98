// SPDX-License-Identifier: GPL-2.0-or-later
// Kodi's window through ScreenCaptureKit (tools/host_media/capture.h).
#include "capture.h"
#import <CoreGraphics/CoreGraphics.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

namespace
{
std::mutex picture_lock;
std::vector<unsigned> picture;
int picture_w = 0, picture_h = 0;
unsigned picture_serial = 0;
// 0 nothing, 1 looking for Kodi's window, 2 recording.
std::atomic<int> state{0};
std::atomic<bool> asked{false};
std::chrono::steady_clock::time_point last_try{};
// The video's width / height (0: the whole window), the one the stream
// uses, and Kodi's window in points.
std::atomic<double> want_aspect{0};
double used_aspect = -1;
CGRect kodi_frame{};
}

API_AVAILABLE(macos(12.3))
@interface NEO98KodiCapture : NSObject <SCStreamOutput, SCStreamDelegate>
@property(strong) SCStream *stream;
@end

@implementation NEO98KodiCapture
- (void)stream:(SCStream *)stream didOutputSampleBuffer:(CMSampleBufferRef)buffer ofType:(SCStreamOutputType)type
{
    if (type != SCStreamOutputTypeScreen || !CMSampleBufferIsValid(buffer)) return;
    // Only complete frames carry a picture (idle ones repeat the last).
    CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(buffer, false);
    if (!attachments || !CFArrayGetCount(attachments)) return;
    NSDictionary *info = (__bridge NSDictionary *)CFArrayGetValueAtIndex(attachments, 0);
    NSNumber *status = info[SCStreamFrameInfoStatus];
    if (!status || status.integerValue != SCFrameStatusComplete) return;
    CVImageBufferRef image = CMSampleBufferGetImageBuffer(buffer);
    if (!image || CVPixelBufferLockBaseAddress(image, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) return;
    const int w = (int)CVPixelBufferGetWidth(image), h = (int)CVPixelBufferGetHeight(image);
    const size_t stride = CVPixelBufferGetBytesPerRow(image);
    const unsigned char *base = (const unsigned char *)CVPixelBufferGetBaseAddress(image);
    if (base && w > 0 && h > 0)
    {
        std::lock_guard<std::mutex> guard(picture_lock);
        picture.resize(size_t(w) * h);
        for (int y = 0; y < h; ++y) memcpy(&picture[size_t(y) * w], base + y * stride, size_t(w) * 4);
        picture_w = w; picture_h = h; ++picture_serial;
    }
    CVPixelBufferUnlockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
}
- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error
{
    fprintf(stderr, "NEO98 HOST media capture stopped: %s\n", error.localizedDescription.UTF8String);
    state = 0;
}
@end

namespace
{
NEO98KodiCapture *capture API_AVAILABLE(macos(12.3)) = nil;
dispatch_queue_t frames;

// Only the video inside Kodi's window: Kodi fits it there with bars, which a
// window of another shape (a 32:9 screen) makes wide. Pixels of the window
// at twice its points, the part at most 1920 x 1080.
API_AVAILABLE(macos(12.3))
void Configure(SCStreamConfiguration *config, double aspect)
{
    const double W = kodi_frame.size.width, H = kodi_frame.size.height;
    CGRect part = CGRectMake(0, 0, W, H);
    if (aspect > 0.2 && aspect < 5 && W > 0 && H > 0)
    {
        if (W / H > aspect) { part.size.width = H * aspect; part.origin.x = (W - part.size.width) / 2; }
        else { part.size.height = W / aspect; part.origin.y = (H - part.size.height) / 2; }
    }
    config.sourceRect = part;
    double w = part.size.width * 2, h = part.size.height * 2;
    if (w > 1920) { h = h * 1920 / w; w = 1920; }
    if (h > 1080) { w = w * 1080 / h; h = 1080; }
    config.width = (size_t)w & ~1u;
    config.height = (size_t)h & ~1u;
    config.pixelFormat = kCVPixelFormatType_32BGRA;
    config.minimumFrameInterval = CMTimeMake(1, 60);
    config.showsCursor = NO;
    config.queueDepth = 4;
    used_aspect = aspect;
}

API_AVAILABLE(macos(12.3))
void Begin()
{
    [SCShareableContent getShareableContentExcludingDesktopWindows:YES onScreenWindowsOnly:NO
                                                  completionHandler:^(SCShareableContent *content, NSError *error) {
        if (!content) {
            fprintf(stderr, "NEO98 HOST media capture: no windows (%s)\n", error.localizedDescription.UTF8String);
            state = 0;
            return;
        }
        // Kodi's main window: the largest normal window of org.xbmc.kodi.
        SCWindow *kodi = nil;
        for (SCWindow *window in content.windows) {
            if (![window.owningApplication.bundleIdentifier isEqualToString:@"org.xbmc.kodi"] || window.windowLayer != 0) continue;
            if (window.frame.size.width < 200 || window.frame.size.height < 150) continue;
            if (!kodi || window.frame.size.width * window.frame.size.height > kodi.frame.size.width * kodi.frame.size.height) kodi = window;
        }
        dispatch_async(dispatch_get_main_queue(), ^{
            if (state != 1) return;
            if (!kodi) { state = 0; return; }  // Kodi has no window yet: Start tries again
            SCContentFilter *filter = [[SCContentFilter alloc] initWithDesktopIndependentWindow:kodi];
            SCStreamConfiguration *config = [SCStreamConfiguration new];
            kodi_frame = kodi.frame;
            Configure(config, want_aspect);
            capture = [NEO98KodiCapture new];
            capture.stream = [[SCStream alloc] initWithFilter:filter configuration:config delegate:capture];
            if (!frames) frames = dispatch_queue_create("neo98.kodi.capture", DISPATCH_QUEUE_SERIAL);
            NSError *added = nil;
            if (![capture.stream addStreamOutput:capture type:SCStreamOutputTypeScreen sampleHandlerQueue:frames error:&added]) {
                fprintf(stderr, "NEO98 HOST media capture: %s\n", added.localizedDescription.UTF8String);
                capture = nil; state = 0; return;
            }
            state = 2;
            fprintf(stderr, "NEO98 HOST media capture of Kodi's window (%.0fx%.0f points) as %ux%u\n", kodi_frame.size.width,
                    kodi_frame.size.height, unsigned(config.width), unsigned(config.height));
            [capture.stream startCaptureWithCompletionHandler:^(NSError *failed) {
                if (!failed) return;
                fprintf(stderr, "NEO98 HOST media capture: %s\n", failed.localizedDescription.UTF8String);
                state = 0;
            }];
        });
    }];
}
}

namespace HostMediaCapture
{
bool Allowed()
{
    if (@available(macOS 12.3, *)) {
        // The programs ask with every status; macOS is asked again at most
        // every two seconds until it says yes.
        static std::atomic<bool> yes{false};
        static std::atomic<long long> checked{0};
        if (yes) return true;
        const long long now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (now - checked < 2000 && checked) return false;
        checked = now;
        const bool allowed = CGPreflightScreenCaptureAccess();
        static std::atomic<int> said{-1};
        if (said.exchange(allowed) != (int)allowed)
            fprintf(stderr, "NEO98 HOST media screen recording %s\n", allowed ? "allowed" : "not allowed by macOS");
        if (allowed) { yes = true; return true; }
        if (!asked.exchange(true))
            dispatch_async(dispatch_get_main_queue(), ^{ CGRequestScreenCaptureAccess(); });
    }
    return false;
}

void Start()
{
    if (@available(macOS 12.3, *)) {
        // Without the permission every try would bring macOS's question
        // back; Allowed asks once.
        if (state != 0 || !Allowed()) return;
        const auto now = std::chrono::steady_clock::now();
        if (now - last_try < std::chrono::seconds(1)) return;
        last_try = now;
        state = 1;
        Begin();
    }
}

void Stop()
{
    if (@available(macOS 12.3, *)) {
        if (state == 0 && !capture) return;
        state = 0;
        if (capture) {
            [capture.stream stopCaptureWithCompletionHandler:^(NSError *) {}];
            capture = nil;
        }
        std::lock_guard<std::mutex> guard(picture_lock);
        picture.clear(); picture_w = picture_h = 0;
    }
}

void Aspect(double aspect)
{
    if (@available(macOS 12.3, *)) {
        if (aspect == want_aspect.exchange(aspect)) return;
        dispatch_async(dispatch_get_main_queue(), ^{
            if (state != 2 || !capture || std::abs(want_aspect - used_aspect) < 0.005) return;
            SCStreamConfiguration *config = [SCStreamConfiguration new];
            Configure(config, want_aspect);
            fprintf(stderr, "NEO98 HOST media capture of the video (%.2f) as %ux%u\n", double(want_aspect), unsigned(config.width), unsigned(config.height));
            [capture.stream updateConfiguration:config completionHandler:^(NSError *failed) {
                if (failed) fprintf(stderr, "NEO98 HOST media capture: %s\n", failed.localizedDescription.UTF8String);
            }];
        });
    }
}

bool Latest(const std::function<void(const unsigned *, int, int)> &use, unsigned &serial)
{
    std::lock_guard<std::mutex> guard(picture_lock);
    if (picture.empty() || picture_serial == serial) return false;
    use(picture.data(), picture_w, picture_h); serial = picture_serial;
    return true;
}
}
