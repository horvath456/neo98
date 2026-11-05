// SPDX-License-Identifier: GPL-2.0-or-later
// NEO98CAP.DRV, the Video for Windows capture driver of the host camera.
// Win98 programs capture through AVICAP32.DLL, which thunks to the original
// 16-bit AVICAP.DLL; AVICAP opens its channels with MSVIDEO's videoOpen, and
// MSVIDEO calls USER's OpenDriver("msvideo", "drivers", &VIDEO_OPEN_PARMS).
// A SYSTEM.INI line msvideo=neo98cap.drv ends here instead of in a 16-bit
// module: src/win16_driver.cpp keeps the entry in USER's table and hands every
// message of it to Win16CaptureMessage. There is no original to implement;
// the messages, structures and error numbers are those of the Video for
// Windows 1.1 driver interface (msviddrv.h), with 16:16 pointers as MSVIDEO
// passes them. The frames come from the host camera through
// HostDevices::Provider (tools/host_devices/service.h).
// The driver offers one format, uncompressed 24-bit RGB, bottom-up, at any
// size from 16x12 to 640x480; each frame is scaled from the host's 32-bit
// frame with the nearest pixel. VIDEO_IN and VIDEO_EXTERNALIN open,
// VIDEO_OUT and VIDEO_EXTERNALOUT (overlay) do not.
#include "win16_machine.h"
#include "host_devices.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
namespace X86
{
namespace
{
constexpr unsigned DrvLoad = 1, DrvEnable = 2, DrvOpen = 3, DrvClose = 4, DrvDisable = 5, DrvFree = 6,
                   DrvQueryConfigure = 7, DrvConfigure = 8, DrvInstall = 9, DrvRemove = 10;
constexpr unsigned VideoExternalIn = 1, VideoIn = 4;
// DV_ERR_*: DV_ERR_BASE is 1.
constexpr unsigned Ok = 0, BadFormat = 2, Param1 = 11, Param2 = 12, NotSupported = 17, Allocated = 19,
                   NoBuffers = 23, Unprepared = 4;
// DVM_* from DRV_USER (4000h), and the configuration messages from
// DVM_CONFIGURE_START (1000h).
constexpr unsigned DvmGetErrorText = 0x4000, DvmGetApiVersion = 0x4001, DvmDialog = 0x4064,
                   DvmConfigureStorage = 0x4065, DvmFrame = 0x40c8, DvmChannelCaps = 0x40c9, DvmUpdate = 0x40ca,
                   DvmFormat = 0x1002, DvmSourceRect = 0x1004, DvmDestinationRect = 0x1005, DvmStreamAdd = 0x412c,
                   DvmStreamFini = 0x412d, DvmStreamError = 0x412e, DvmStreamPosition = 0x412f,
                   DvmStreamInit = 0x4130, DvmStreamPrepare = 0x4131, DvmStreamReset = 0x4132,
                   DvmStreamStart = 0x4133, DvmStreamStop = 0x4134, DvmStreamUnprepare = 0x4135;
// VIDEOHDR.dwFlags
constexpr unsigned Done = 1, InQueue = 4, KeyFrame = 8;
// VIDEOCONFIGURE_*
constexpr unsigned ConfigureSet = 0x1000, ConfigureGet = 0x2000, ConfigureQuery = 0x8000, QuerySize = 1;
constexpr unsigned Stride(unsigned width) { return (width * 3 + 3) & ~3u; }
} // namespace

unsigned Application::Win16CaptureFar(unsigned far_pointer)
{
    return far_pointer >> 16 ? Win16Linear(far_pointer >> 16, far_pointer & 0xffff) : 0;
}

// One frame of the current format into a VIDEOHDR: lpData at +0,
// dwBufferLength +4, dwBytesUsed +8, dwTimeCaptured +12, dwFlags +20.
bool Application::Win16CaptureFill(unsigned header, unsigned time)
{
    auto &c = win16_capture;
    const unsigned data = Win16CaptureFar(Read32(header)), length = Read32(header + 4);
    const unsigned stride = Stride(c.width), bytes = stride * c.height;
    if (!data || length < bytes)
        return false;
    auto *frame = static_cast<AH_Frame *>(calloc(1, sizeof(AH_Frame)));
    auto *row = static_cast<unsigned char *>(calloc(1, stride));
    if (!frame || !row)
    {
        free(frame);
        free(row);
        return false;
    }
    unsigned done = 0;
    const bool live = HostDevices::current && !HostDevices::current->Frame(frame, sizeof *frame, done) &&
                      frame->width && frame->height && frame->width <= 640 && frame->height <= 480 &&
                      frame->bytes >= frame->width * frame->height * 4;
    for (unsigned y = 0; y < c.height; ++y)
    {
        // The DIB is bottom-up and the host frame top-down.
        if (live)
        {
            const unsigned sy = (c.height - 1 - y) * frame->height / c.height;
            const unsigned char *source = frame->pixels + sy * frame->width * 4;
            for (unsigned x = 0; x < c.width; ++x)
                memcpy(&row[x * 3], source + (x * frame->width / c.width) * 4, 3);
        }
        Store(data + y * stride, row, stride);
    }
    free(row);
    free(frame);
    Write32(header + 8, bytes);
    Write32(header + 12, time);
    Write32(header + 20, (Read32(header + 20) & ~InQueue) | Done | KeyFrame);
    return true;
}

// The callback of a stream, as MSVIDEO's DriverCallback would deliver it:
// MM_DRVM_DATA (3D2h) with the 16-bit video handle and the far header.
void Application::Win16CaptureNotify(unsigned message, unsigned far_header)
{
    auto &c = win16_capture;
    const unsigned kind = c.flags & 0x70000;
    if (!c.callback)
        return;
    if (kind == 0x10000 || kind == 0x20000)
        Win16PostCallback(kind, c.callback, message, c.video & 0xffff, far_header);
    else if (kind == 0x30000)
    {
        const unsigned push[] = {c.video & 0xffff, message & 0xffff, c.instance >> 16, c.instance & 0xffff,
                                 far_header >> 16, far_header & 0xffff, 0, 0};
        bool returned = false;
        Win16HostedCall(c.callback, push, 8, 0, nullptr, &returned);
    }
}

// Pump: the buffers of a running stream that are due by now.
void Application::Win16CaptureServe()
{
    auto &c = win16_capture;
    if (!c.running || !c.queued || !win16.state)
        return;
    const unsigned elapsed = User::MessageClock() - c.start;
    const unsigned due = c.usec ? unsigned(uint64_t(elapsed) * 1000 / c.usec) + 1 : c.frames + 1;
    while (c.queued && c.frames < due)
    {
        const unsigned far_header = c.queue[0], header = Win16CaptureFar(far_header);
        for (unsigned i = 1; i < c.queued; ++i)
            c.queue[i - 1] = c.queue[i];
        --c.queued;
        if (!header || !Win16CaptureFill(header, unsigned(uint64_t(c.frames) * c.usec / 1000)))
        {
            c.error = Param1;
            continue;
        }
        ++c.frames;
        Win16CaptureNotify(0x3d2, far_header);
    }
}

void Application::Win16CaptureCamera(bool on)
{
    auto &c = win16_capture;
    if (!HostDevices::current || on == (c.camera != 0))
        return;
    AH_Status status{};
    if (on && (HostDevices::current->Status(status) || !status.camera_id))
        return;
    AH_Request request{};
    request.version = AH_VERSION;
    request.action = on ? AH_CAMERA_START : AH_CAMERA_STOP;
    request.id = on ? status.camera_id : 0;
    if (!HostDevices::current->Action(request))
        c.camera = on ? status.camera_id : 0;
}

unsigned Application::Win16CaptureMessage(Win16Driver &entry, unsigned message, unsigned first, unsigned second)
{
    auto &c = win16_capture;
    switch (message)
    {
    case DrvLoad: case DrvEnable: case DrvDisable: case DrvFree: case DrvInstall: case DrvRemove:
        return 1;
    case DrvQueryConfigure: case DrvConfigure:
        return 0;
    case DrvOpen:
    {
        // lParam2 is the far VIDEO_OPEN_PARMS: dwSize, fccType, fccComp,
        // dwVersion, dwFlags (+16), dwError (+20). Without it the caller only
        // configures the driver.
        const unsigned parameters = Win16CaptureFar(second);
        if (!parameters)
            return 1;
        const unsigned channel = Read32(parameters + 16);
        const bool known = channel == VideoIn || channel == VideoExternalIn;
        Write32(parameters + 20, known ? Ok : NotSupported);
        if (!known)
            return 0;
        AH_Status status{};
        if (!HostDevices::current || HostDevices::current->Status(status) || !status.camera_id)
        {
            Write32(parameters + 20, 7); // DV_ERR_NOTDETECTED
            return 0;
        }
        entry.channel = channel;
        if (channel == VideoIn && !c.users++)
            Win16CaptureCamera(true);
        return channel;
    }
    case DrvClose:
        if (entry.channel == VideoIn && c.users && !--c.users)
        {
            c.running = false;
            c.queued = 0;
            Win16CaptureCamera(false);
        }
        return 1;
    case DvmGetErrorText:
    {
        // VIDEO_GETERRORTEXT_PARMS: dwError, lpText, dwLength.
        const unsigned parameters = Win16CaptureFar(first);
        if (!parameters)
            return Param1;
        const unsigned text = Win16CaptureFar(Read32(parameters + 4)), length = Read32(parameters + 8);
        char line[64];
        snprintf(line, sizeof line, "NEO98 camera error %u.", Read32(parameters));
        if (text && length)
        {
            const unsigned bytes = unsigned(strlen(line)) + 1 < length ? unsigned(strlen(line)) + 1 : length;
            Store(text, line, bytes);
            if (bytes == length)
                Store(text + length - 1, "", 1);
        }
        return Ok;
    }
    case DvmGetApiVersion:
        if (const unsigned out = Win16CaptureFar(first))
            Write32(out, 4); // VIDEOAPIVERSION
        return Ok;
    case DvmDialog:
        return NotSupported;
    case DvmConfigureStorage:
    case DvmUpdate:
        return Ok;
    case DvmChannelCaps:
    {
        // CHANNEL_CAPS: dwFlags and eight alignment words; nothing to clip or
        // scale, every position and size is good.
        const unsigned out = Win16CaptureFar(first);
        if (!out || second < 36)
            return Param1;
        Write32(out, 0);
        for (unsigned i = 1; i < 9; ++i)
            Write32(out + i * 4, 1);
        return Ok;
    }
    case DvmFormat:
    {
        if (entry.channel != VideoIn)
            return NotSupported;
        // VIDEOCONFIGPARMS: lpdwReturn, lpData1, dwSize1, lpData2, dwSize2.
        const unsigned parameters = Win16CaptureFar(second);
        const unsigned returned = parameters ? Win16CaptureFar(Read32(parameters)) : 0;
        const unsigned data = parameters ? Win16CaptureFar(Read32(parameters + 4)) : 0;
        const unsigned size = parameters ? Read32(parameters + 8) : 0;
        if (first & ConfigureGet)
        {
            if (returned)
                Write32(returned, 40);
            if (first & QuerySize)
                return Ok;
            if (!data || size < 40)
                return Param2;
            const unsigned header[10] = {40, c.width, c.height, 1 | 24u << 16, 0, Stride(c.width) * c.height, 0, 0, 0, 0};
            Store(data, header, 40);
            return Ok;
        }
        if (first & ConfigureSet)
        {
            if (!data || size < 40)
                return Param2;
            unsigned header[10] = {};
            Read(data, header, 40);
            const int width = int(header[1]), height = int(header[2]);
            const unsigned planes = header[3] & 0xffff, bits = header[3] >> 16;
            if (header[0] < 40 || planes != 1 || bits != 24 || header[4] != 0 || width < 16 || width > 640 ||
                height < 12 || height > 480)
                return BadFormat;
            if (first & ConfigureQuery)
                return Ok;
            if (c.running)
                return Allocated;
            c.width = unsigned(width);
            c.height = unsigned(height);
            return Ok;
        }
        return Ok;
    }
    case DvmSourceRect:
    case DvmDestinationRect:
    {
        // lParam1 is a 16-bit RECT of four words, lParam2 the flags.
        const unsigned rect = Win16CaptureFar(first);
        if (rect && (second & ConfigureGet))
        {
            Win16Word(rect, 0);
            Win16Word(rect + 2, 0);
            Win16Word(rect + 4, c.width);
            Win16Word(rect + 6, c.height);
        }
        return Ok;
    }
    case DvmFrame:
    {
        const unsigned header = Win16CaptureFar(first);
        if (entry.channel != VideoIn || !header)
            return Param1;
        return Win16CaptureFill(header, 0) ? Ok : Param1;
    }
    case DvmStreamInit:
    {
        // AVICAP starts the external input as a stream to switch it on;
        // there is nothing behind it to start.
        if (entry.channel == VideoExternalIn)
            return Ok;
        // VIDEO_STREAM_INIT_PARMS: dwMicroSecPerFrame, dwCallback,
        // dwCallbackInst, dwFlags, hVideo.
        const unsigned parameters = Win16CaptureFar(first);
        if (entry.channel != VideoIn || !parameters)
            return Param1;
        if (c.streaming)
            return Allocated;
        c.streaming = true;
        c.running = false;
        c.queued = 0;
        c.frames = 0;
        c.error = 0;
        c.usec = Read32(parameters);
        c.callback = Read32(parameters + 4);
        c.instance = Read32(parameters + 8);
        c.flags = Read32(parameters + 12);
        c.video = Read32(parameters + 16);
        Win16CaptureNotify(0x3d0, 0); // MM_DRVM_OPEN
        return Ok;
    }
    case DvmStreamFini:
        if (entry.channel != VideoIn || !c.streaming)
            return Ok;
        Win16CaptureNotify(0x3d1, 0); // MM_DRVM_CLOSE
        c.streaming = c.running = false;
        c.queued = 0;
        return Ok;
    case DvmStreamPrepare:
    case DvmStreamUnprepare:
        // MSVIDEO marks the header itself when the driver declines.
        return NotSupported;
    case DvmStreamAdd:
    {
        const unsigned header = Win16CaptureFar(first);
        if (!c.streaming || !header)
            return Unprepared;
        if (c.queued == sizeof c.queue / sizeof c.queue[0])
            return NoBuffers;
        Write32(header + 20, (Read32(header + 20) & ~Done) | InQueue);
        c.queue[c.queued++] = first;
        Win16CaptureServe();
        return Ok;
    }
    case DvmStreamStart:
        c.running = true;
        c.start = User::MessageClock();
        c.frames = 0;
        return Ok;
    case DvmStreamStop:
        c.running = false;
        return Ok;
    case DvmStreamReset:
        // Every queued buffer goes back done and empty.
        c.running = false;
        while (c.queued)
        {
            const unsigned far_header = c.queue[--c.queued], header = Win16CaptureFar(far_header);
            if (header)
            {
                Write32(header + 8, 0);
                Write32(header + 20, (Read32(header + 20) & ~InQueue) | Done);
            }
            Win16CaptureNotify(0x3d2, far_header);
        }
        return Ok;
    case DvmStreamError:
    {
        if (const unsigned error = Win16CaptureFar(first))
            Write32(error, c.error);
        if (const unsigned skipped = Win16CaptureFar(second))
            Write32(skipped, 0);
        c.error = 0;
        return Ok;
    }
    case DvmStreamPosition:
    {
        // A 16-bit MMTIME: wType, then the union; TIME_MS is 1.
        const unsigned time = Win16CaptureFar(first);
        if (!time)
            return Param1;
        Win16Word(time, 1);
        Write32(time + 2, c.running ? User::MessageClock() - c.start : 0);
        return Ok;
    }
    default:
        return NotSupported;
    }
}
} // namespace X86
