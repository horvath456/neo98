// SPDX-License-Identifier: GPL-2.0-or-later
// The camera pictures of the host without a camera window (
// tools/test_camera_frames.py): CopyFrame of tools/host_devices/platform.h
// with pictures larger than 640x480, and on macOS the Mac platform's list of
// cameras. Nothing here opens a camera or asks for camera access.
#include "host_devices/macos.mm"
#include <cassert>
#include <cstdio>
#include <vector>

static std::vector<unsigned char> TestPicture(unsigned width, unsigned height, unsigned stride)
{
    // Left half red, right half green, BGRA; the padding of a row is grey.
    std::vector<unsigned char> bytes(size_t(stride) * height, 0x80);
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x)
        {
            unsigned char *p = bytes.data() + size_t(y) * stride + x * 4;
            p[0] = 0; p[1] = x < width / 2 ? 0 : 255; p[2] = x < width / 2 ? 255 : 0; p[3] = 255;
        }
    return bytes;
}

static void Scaled(unsigned width, unsigned height, unsigned want_width, unsigned want_height)
{
    const unsigned stride = width * 4 + 64;
    const auto bytes = TestPicture(width, height, stride);
    auto frame = std::make_unique<AH_Frame>();
    frame->sequence = 7;
    assert(HostDeviceService::CopyFrame(*frame, bytes.data(), width, height, stride));
    printf("%ux%u -> %ux%u\n", width, height, frame->width, frame->height);
    assert(frame->width == want_width && frame->height == want_height);
    assert(frame->stride == want_width * 4 && frame->bytes == frame->stride * want_height && frame->sequence == 8);
    for (unsigned y = 0; y < frame->height; y += frame->height / 4)
    {
        const unsigned char *row = frame->pixels + y * frame->stride;
        const unsigned char *left = row + 4, *right = row + (frame->width - 2) * 4;
        assert(left[2] == 255 && left[1] == 0 && right[1] == 255 && right[2] == 0);
    }
}

int main()
{
    Scaled(640, 480, 640, 480);
    Scaled(320, 240, 320, 240);
    Scaled(1280, 720, 640, 360);
    Scaled(1920, 1080, 640, 360);
    Scaled(1760, 1328, 636, 480);
    Scaled(1080, 1920, 270, 480);
    {
        auto frame = std::make_unique<AH_Frame>();
        assert(!HostDeviceService::CopyFrame(*frame, nullptr, 640, 480, 2560));
        const auto bytes = TestPicture(16, 16, 64);
        assert(!HostDeviceService::CopyFrame(*frame, bytes.data(), 16, 16, 32));
    }
    @autoreleasepool
    {
        // The Mac's cameras as the host lists them; only enumeration.
        auto platform = HostDeviceService::MakePlatform();
        AH_Status status{};
        std::map<unsigned, HostDeviceService::Medium> media;
        platform->Refresh(status, media);
        unsigned cameras = 0;
        for (unsigned i = 0; i < status.devices; ++i)
            if (!strcmp(status.device[i].kind, "Camera")) { ++cameras; printf("camera: %s\n", status.device[i].name); }
        const auto devices = [AVCaptureDevice devicesWithMediaType:AVMediaTypeVideo].count;
        printf("cameras %u of %lu, camera_id %08x, access %ld, asking %d\n", cameras, (unsigned long)devices,
               status.camera_id, (long)[AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo],
               platform->asking_camera ? 1 : 0);
        assert(cameras == devices && (status.camera_id != 0) == (devices != 0));
        assert(!(status.features & AH_CAMERA) == !devices);
        assert(!platform->asking_camera);
    }
    puts("CAMERA FRAMES PASS");
    return 0;
}
