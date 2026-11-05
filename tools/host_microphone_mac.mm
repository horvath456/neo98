// SPDX-License-Identifier: GPL-2.0-or-later
// The microphone permission of macOS for tools/host_audio.h.
// SDL opens the microphone over CoreAudio and does not look at the
// permission; a device without it opens and records silence. The host asks
// AVFoundation first, so a refusal is known and can be told once, and the
// question itself is asked without waiting: Windows records silence until
// the user has answered (Sound::InputWaiting). Neo98.app carries
// NSMicrophoneUsageDescription for that question (tools/mac_app.py).
//
#import <AVFoundation/AVFoundation.h>
#include <atomic>
#include "host_microphone_mac.h"

namespace HostMicrophone
{
namespace
{
// 0 no answer yet, 1 granted, 2 refused.
std::atomic<int> answer{0};
std::atomic<bool> asked{false};
} // namespace

Permission Current()
{
    switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio])
    {
    case AVAuthorizationStatusAuthorized: return Granted;
    case AVAuthorizationStatusNotDetermined: return Undetermined;
    default: return Refused;
    }
}

void Ask()
{
    if (asked.exchange(true)) return;
    answer = 0;
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                             completionHandler:^(BOOL granted) {
                               answer = granted ? 1 : 2;
                               asked = false;
                             }];
}

Permission Answer()
{
    const int value = answer.load();
    if (value == 1) return Granted;
    if (value == 2) return Refused;
    // The question may have been answered in the system settings meanwhile.
    return Current();
}
} // namespace HostMicrophone
