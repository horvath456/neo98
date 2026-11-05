/* SPDX-License-Identifier: GPL-2.0-or-later */
/* DSOUND.DLL for ARM64 processes: buffers, notifications and 3D controls.


   A buffer is a buffer of the driver (NEO98DX.DLL): memory the mixer of the
   port reads at its play cursor while it renders, the way a sound card with a
   DirectSound driver mixes its buffers in hardware. This DLL keeps the objects,
   checks what a program hands in the way DirectSound does, and turns volume
   and pan into the amplitude of each channel; it mixes nothing itself. */
#include <windows.h>
#include <dsound.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "neo98dx_abi.h"

typedef struct DsFace
{
    const void *table;
    void *object;
} DsFace;

struct DsBuffer;

typedef struct DsObject
{
    DsFace face;
    LONG references;
    HWND window;
    DWORD level;                 /* DSSCL_, 0 before SetCooperativeLevel */
    DWORD speaker;
    struct DsBuffer *buffers;
    struct DsBuffer *primary;
    WAVEFORMATEX primary_format;
    DS3DLISTENER listener, audible_listener;
} DsObject;

typedef struct DsBuffer
{
    DsFace face, identity, notify, spatial_face, listener_face;
    LONG references;
    DsObject *ds;                /* null once the DirectSound object is gone */
    struct DsBuffer *next;
    BOOL primary;
    DWORD driver;                /* handle of the driver's buffer, 0 for a primary buffer nobody writes */
    BYTE *memory;
    DWORD bytes;
    DWORD flags;                 /* DSBCAPS_ of its description */
    WAVEFORMATEX format;
    LONG volume, pan;
    DWORD frequency;
    DS3DBUFFER spatial, audible_spatial;
} DsBuffer;

static const GUID iid_unknown = {0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static CRITICAL_SECTION section;
static BOOL section_ready;
/* The bytes the primary buffer has when a program writes it itself. */
#define PRIMARY_BYTES 32768u

static LONG AddRef(LONG *count) { return __atomic_add_fetch(count, 1, __ATOMIC_SEQ_CST); }
static LONG DropRef(LONG *count) { return __atomic_sub_fetch(count, 1, __ATOMIC_SEQ_CST); }
static void Enter(void) { if (section_ready) EnterCriticalSection(&section); }
static void Leave(void) { if (section_ready) LeaveCriticalSection(&section); }

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)instance;
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
    {
        InitializeCriticalSection(&section);
        section_ready = TRUE;
    }
    return TRUE;
}

static BOOL PcmFormat(const WAVEFORMATEX *format)
{
    if (!format || format->wFormatTag != WAVE_FORMAT_PCM) return FALSE;
    if (format->nChannels != 1 && format->nChannels != 2) return FALSE;
    if (format->wBitsPerSample != 8 && format->wBitsPerSample != 16) return FALSE;
    if (format->nSamplesPerSec < DSBFREQUENCY_MIN || format->nSamplesPerSec > 100000) return FALSE;
    return format->nBlockAlign == format->nChannels * format->wBitsPerSample / 8;
}

/* ------------------------------------------------------- IDirectSoundBuffer */
static DsBuffer *Buffer(void *face) { return (DsBuffer *)((DsFace *)face)->object; }

static void BufferDestroy(DsBuffer *buffer)
{
    if (buffer->driver) DsDrvDestroyBuffer(buffer->driver);
    if (buffer->ds)
    {
        DsBuffer **link;
        Enter();
        for (link = &buffer->ds->buffers; *link; link = &(*link)->next)
            if (*link == buffer) { *link = buffer->next; break; }
        if (buffer->ds->primary == buffer) buffer->ds->primary = NULL;
        Leave();
    }
    free(buffer);
}

static HRESULT WINAPI Buffer_QueryInterface(void *face, REFIID iid, void **out)
{
    if (!out) return DSERR_INVALIDPARAM;
    *out = NULL;
    DsBuffer *buffer = Buffer(face);
    if (!iid) return E_NOINTERFACE;
    if (IsEqualGUID(iid, &iid_unknown))
        *out = &buffer->identity;
    else if (IsEqualGUID(iid, &IID_IDirectSoundBuffer))
        *out = &buffer->face;
    else if (IsEqualGUID(iid, &IID_IDirectSoundNotify) && (buffer->flags & DSBCAPS_CTRLPOSITIONNOTIFY))
        *out = &buffer->notify;
    else if (IsEqualGUID(iid, &IID_IDirectSound3DBuffer) && !buffer->primary && (buffer->flags & DSBCAPS_CTRL3D))
        *out = &buffer->spatial_face;
    else if (IsEqualGUID(iid, &IID_IDirectSound3DListener) && buffer->primary && (buffer->flags & DSBCAPS_CTRL3D))
        *out = &buffer->listener_face;
    else return E_NOINTERFACE;
    AddRef(&buffer->references);
    return S_OK;
}

static ULONG WINAPI Buffer_AddRef(void *face) { return (ULONG)AddRef(&Buffer(face)->references); }

static ULONG WINAPI Buffer_Release(void *face)
{
    DsBuffer *buffer = Buffer(face);
    const LONG left = DropRef(&buffer->references);
    if (left <= 0) BufferDestroy(buffer);
    return left > 0 ? (ULONG)left : 0;
}

/* The primary buffer has memory only for a program that writes it
   (DSSCL_WRITEPRIMARY); the driver's buffer is made when it is first needed. */
static HRESULT PrimaryMemory(DsBuffer *buffer)
{
    NEO98DX_SOUND sound;
    HRESULT result;
    if (buffer->driver) return DS_OK;
    if (!buffer->ds || buffer->ds->level != DSSCL_WRITEPRIMARY) return DSERR_PRIOLEVELNEEDED;
    memset(&sound, 0, sizeof sound);
    sound.size = sizeof sound;
    sound.flags = NEO98DX_SOUND_PRIMARY;
    sound.rate = buffer->format.nSamplesPerSec;
    sound.channels = buffer->format.nChannels;
    sound.bitsPerSample = buffer->format.wBitsPerSample;
    sound.bytes = PRIMARY_BYTES - PRIMARY_BYTES % buffer->format.nBlockAlign;
    result = DsDrvCreateBuffer(&sound, sizeof sound);
    if (result != DS_OK) return result;
    buffer->driver = sound.handle;
    buffer->memory = (BYTE *)(ULONG_PTR)sound.memory;
    buffer->bytes = sound.bytes;
    return DS_OK;
}

static HRESULT Usable(DsBuffer *buffer)
{
    if (!buffer->ds) return DSERR_UNINITIALIZED;   /* its DirectSound object is gone */
    if (buffer->primary) return PrimaryMemory(buffer);
    return DS_OK;
}

static HRESULT WINAPI Buffer_GetCaps(void *face, LPDSBCAPS caps)
{
    DsBuffer *buffer = Buffer(face);
    if (!caps || caps->dwSize != sizeof *caps) return DSERR_INVALIDPARAM;
    /* The card mixes every buffer; a program that asked for a software
       buffer is told it has one, the two do not differ here. */
    caps->dwFlags = buffer->flags | ((buffer->flags & DSBCAPS_LOCSOFTWARE) ? 0 : DSBCAPS_LOCHARDWARE);
    caps->dwBufferBytes = buffer->bytes;
    caps->dwUnlockTransferRate = 0;
    caps->dwPlayCpuOverhead = 0;
    return DS_OK;
}

static HRESULT WINAPI Buffer_GetCurrentPosition(void *face, LPDWORD play, LPDWORD write)
{
    DsBuffer *buffer = Buffer(face);
    const HRESULT result = Usable(buffer);
    if (result != DS_OK) return result;
    return DsDrvGetPosition(buffer->driver, play, write);
}

static HRESULT WINAPI Buffer_GetFormat(void *face, LPWAVEFORMATEX format, DWORD size, LPDWORD written)
{
    DsBuffer *buffer = Buffer(face);
    if (!format)
    {
        if (!written) return DSERR_INVALIDPARAM;
        *written = sizeof(WAVEFORMATEX);
        return DS_OK;
    }
    if (size > sizeof(WAVEFORMATEX)) size = sizeof(WAVEFORMATEX);
    memcpy(format, &buffer->format, size);
    if (written) *written = size;
    return DS_OK;
}

static HRESULT WINAPI Buffer_GetVolume(void *face, LPLONG volume)
{
    DsBuffer *buffer = Buffer(face);
    if (!volume) return DSERR_INVALIDPARAM;
    if (!(buffer->flags & DSBCAPS_CTRLVOLUME)) return DSERR_CONTROLUNAVAIL;
    *volume = buffer->volume;
    return DS_OK;
}

static HRESULT WINAPI Buffer_GetPan(void *face, LPLONG pan)
{
    DsBuffer *buffer = Buffer(face);
    if (!pan) return DSERR_INVALIDPARAM;
    if (!(buffer->flags & DSBCAPS_CTRLPAN)) return DSERR_CONTROLUNAVAIL;
    *pan = buffer->pan;
    return DS_OK;
}

static HRESULT WINAPI Buffer_GetFrequency(void *face, LPDWORD frequency)
{
    DsBuffer *buffer = Buffer(face);
    if (!frequency) return DSERR_INVALIDPARAM;
    if (!(buffer->flags & DSBCAPS_CTRLFREQUENCY)) return DSERR_CONTROLUNAVAIL;
    *frequency = buffer->frequency;
    return DS_OK;
}

static HRESULT WINAPI Buffer_GetStatus(void *face, LPDWORD status)
{
    DsBuffer *buffer = Buffer(face);
    DWORD state = 0;
    if (!status) return DSERR_INVALIDPARAM;
    *status = 0;
    if (!buffer->ds) return DSERR_UNINITIALIZED;
    /* A primary buffer nobody writes plays whatever the card mixes. */
    if (!buffer->driver)
    {
        *status = buffer->primary ? DSBSTATUS_PLAYING | DSBSTATUS_LOOPING : 0;
        return DS_OK;
    }
    DsDrvGetStatus(buffer->driver, &state);
    if (state & NEO98DX_STATUS_PLAYING) *status |= DSBSTATUS_PLAYING;
    if (state & NEO98DX_STATUS_LOOPING) *status |= DSBSTATUS_LOOPING;
    return DS_OK;
}

static HRESULT WINAPI Buffer_Initialize(void *face, LPDIRECTSOUND ds, LPCDSBUFFERDESC desc)
{
    (void)face;
    (void)ds;
    (void)desc;
    return DSERR_ALREADYINITIALIZED;
}

static HRESULT WINAPI Buffer_Lock(void *face, DWORD offset, DWORD bytes, LPVOID *first, LPDWORD first_bytes, LPVOID *second,
                                  LPDWORD second_bytes, DWORD flags)
{
    DsBuffer *buffer = Buffer(face);
    DWORD head;
    HRESULT result;
    if (!first || !first_bytes || (flags & ~(DSBLOCK_FROMWRITECURSOR | DSBLOCK_ENTIREBUFFER))) return DSERR_INVALIDPARAM;
    *first = NULL;
    *first_bytes = 0;
    if (second) *second = NULL;
    if (second_bytes) *second_bytes = 0;
    result = Usable(buffer);
    if (result != DS_OK) return result;
    if (flags & DSBLOCK_FROMWRITECURSOR)
    {
        result = DsDrvGetPosition(buffer->driver, NULL, &offset);
        if (result != DS_OK) return result;
    }
    if (flags & DSBLOCK_ENTIREBUFFER) bytes = buffer->bytes;
    if (offset >= buffer->bytes || !bytes || bytes > buffer->bytes) return DSERR_INVALIDPARAM;
    head = bytes < buffer->bytes - offset ? bytes : buffer->bytes - offset;
    *first = buffer->memory + offset;
    *first_bytes = head;
    if (bytes > head)
    {
        /* The stretch wraps: without room for the second part the caller
           gets the first alone. */
        if (second && second_bytes)
        {
            *second = buffer->memory;
            *second_bytes = bytes - head;
        }
    }
    return DS_OK;
}

static HRESULT WINAPI Buffer_Play(void *face, DWORD reserved, DWORD priority, DWORD flags)
{
    DsBuffer *buffer = Buffer(face);
    HRESULT result;
    (void)priority;
    if (reserved || (flags & ~DSBPLAY_LOOPING)) return DSERR_INVALIDPARAM;
    if (!buffer->ds) return DSERR_UNINITIALIZED;
    if (buffer->primary)
    {
        /* The primary buffer only loops. Unless the program writes it, the
           card plays it anyway and there is nothing to start. */
        if (!(flags & DSBPLAY_LOOPING)) return DSERR_INVALIDPARAM;
        if (buffer->ds->level != DSSCL_WRITEPRIMARY) return DS_OK;
    }
    result = Usable(buffer);
    if (result != DS_OK) return result;
    return DsDrvPlay(buffer->driver, flags);
}

static HRESULT WINAPI Buffer_SetCurrentPosition(void *face, DWORD position)
{
    DsBuffer *buffer = Buffer(face);
    if (!buffer->ds) return DSERR_UNINITIALIZED;
    if (buffer->primary) return DSERR_INVALIDCALL;
    return DsDrvSetPosition(buffer->driver, position);
}

/* The format of the primary buffer is the format the card mixes in. This
   card mixes at one fixed format (44.100 Hz, 16 bit, stereo) and converts
   every buffer as it reads it, so the wish is noted and answered, and a
   program that writes the primary buffer gets it in the format it set. */
static HRESULT WINAPI Buffer_SetFormat(void *face, LPCWAVEFORMATEX format)
{
    DsBuffer *buffer = Buffer(face);
    if (!buffer->ds) return DSERR_UNINITIALIZED;
    if (!buffer->primary) return DSERR_INVALIDCALL;
    if (!format) return DSERR_INVALIDPARAM;
    if (buffer->ds->level < DSSCL_PRIORITY) return DSERR_PRIOLEVELNEEDED;
    if (!PcmFormat(format)) return DSERR_BADFORMAT;
    memcpy(&buffer->format, format, sizeof(PCMWAVEFORMAT));
    buffer->format.cbSize = 0;
    buffer->ds->primary_format = buffer->format;
    if (buffer->driver)
    {
        const HRESULT result = DsDrvSetFormat(buffer->driver, format->nSamplesPerSec, format->nChannels, format->wBitsPerSample);
        if (result != DS_OK) return result;
        buffer->bytes = PRIMARY_BYTES - PRIMARY_BYTES % format->nBlockAlign;
    }
    return DS_OK;
}

/* Hundredths of a decibel as the amplitude of a channel, 0 to FFFFh. */
static DWORD Amplitude(LONG hundredths)
{
    if (hundredths >= 0) return 0xffff;
    if (hundredths <= DSBVOLUME_MIN) return 0;
    return (DWORD)(pow(10.0, (double)hundredths / 2000.0) * 65535.0 + 0.5);
}

#include "dsound_3d.inc"

/* Volume attenuates both channels, pan the one it turns away from. */
static HRESULT ApplyVolume(DsBuffer *buffer)
{
    if (buffer->flags & DSBCAPS_CTRL3D) return ApplySpatial(buffer);
    LONG left = buffer->volume, right = buffer->volume;
    if (buffer->pan > 0) left += -buffer->pan;
    if (buffer->pan < 0) right += buffer->pan;
    if (!buffer->driver) return DS_OK;
    return DsDrvSetVolumePan(buffer->driver, Amplitude(left), Amplitude(right));
}

static HRESULT WINAPI Buffer_SetVolume(void *face, LONG volume)
{
    DsBuffer *buffer = Buffer(face);
    if (!buffer->ds) return DSERR_UNINITIALIZED;
    /* The volume of the primary buffer is the volume of the card; that is
       the mixer device of WINMM, not a control of this buffer. */
    if (!(buffer->flags & DSBCAPS_CTRLVOLUME) || buffer->primary) return DSERR_CONTROLUNAVAIL;
    if (volume > DSBVOLUME_MAX || volume < DSBVOLUME_MIN) return DSERR_INVALIDPARAM;
    buffer->volume = volume;
    return ApplyVolume(buffer);
}

static HRESULT WINAPI Buffer_SetPan(void *face, LONG pan)
{
    DsBuffer *buffer = Buffer(face);
    if (!buffer->ds) return DSERR_UNINITIALIZED;
    if (!(buffer->flags & DSBCAPS_CTRLPAN) || buffer->primary) return DSERR_CONTROLUNAVAIL;
    if (pan > DSBPAN_RIGHT || pan < DSBPAN_LEFT) return DSERR_INVALIDPARAM;
    buffer->pan = pan;
    return ApplyVolume(buffer);
}

static HRESULT WINAPI Buffer_SetFrequency(void *face, DWORD frequency)
{
    DsBuffer *buffer = Buffer(face);
    HRESULT result;
    if (!buffer->ds) return DSERR_UNINITIALIZED;
    if (!(buffer->flags & DSBCAPS_CTRLFREQUENCY) || buffer->primary) return DSERR_CONTROLUNAVAIL;
    if (frequency != DSBFREQUENCY_ORIGINAL && (frequency < DSBFREQUENCY_MIN || frequency > 100000)) return DSERR_INVALIDPARAM;
    result = DsDrvSetFrequency(buffer->driver, frequency);
    if (result == DS_OK) buffer->frequency = frequency ? frequency : buffer->format.nSamplesPerSec;
    if (result == DS_OK && (buffer->flags & DSBCAPS_CTRL3D)) result = ApplySpatial(buffer);
    return result;
}

static HRESULT WINAPI Buffer_Stop(void *face)
{
    DsBuffer *buffer = Buffer(face);
    if (!buffer->ds) return DSERR_UNINITIALIZED;
    if (!buffer->driver) return DS_OK;
    return DsDrvStop(buffer->driver);
}

/* The program wrote the memory of the buffer itself; the card reads it where
   it lies, so there is nothing to carry anywhere, and like the original this
   does not look at the pointers it is given (measured). */
static HRESULT WINAPI Buffer_Unlock(void *face, LPVOID first, DWORD first_bytes, LPVOID second, DWORD second_bytes)
{
    (void)first;
    (void)first_bytes;
    (void)second;
    (void)second_bytes;
    return Buffer(face)->ds ? DS_OK : DSERR_UNINITIALIZED;
}

/* A buffer of this card is never lost. */
static HRESULT WINAPI Buffer_Restore(void *face)
{
    return Buffer(face)->ds ? DS_OK : DSERR_UNINITIALIZED;
}

static const struct IDirectSoundBufferVtbl buffer_table = {
    .QueryInterface = (void *)Buffer_QueryInterface, .AddRef = (void *)Buffer_AddRef, .Release = (void *)Buffer_Release,
    .GetCaps = (void *)Buffer_GetCaps, .GetCurrentPosition = (void *)Buffer_GetCurrentPosition,
    .GetFormat = (void *)Buffer_GetFormat, .GetVolume = (void *)Buffer_GetVolume, .GetPan = (void *)Buffer_GetPan,
    .GetFrequency = (void *)Buffer_GetFrequency, .GetStatus = (void *)Buffer_GetStatus, .Initialize = (void *)Buffer_Initialize,
    .Lock = (void *)Buffer_Lock, .Play = (void *)Buffer_Play, .SetCurrentPosition = (void *)Buffer_SetCurrentPosition,
    .SetFormat = (void *)Buffer_SetFormat, .SetVolume = (void *)Buffer_SetVolume, .SetPan = (void *)Buffer_SetPan,
    .SetFrequency = (void *)Buffer_SetFrequency, .Stop = (void *)Buffer_Stop, .Unlock = (void *)Buffer_Unlock,
    .Restore = (void *)Buffer_Restore};

/* The public entries contain pointer-sized handles; the driver ABI uses
   the runtime's 32-bit kernel handles, like the other NEO98DX calls. */
static HRESULT WINAPI Notify_SetNotificationPositions(void *face, DWORD count, LPCDSBPOSITIONNOTIFY positions)
{
    DsBuffer *buffer = Buffer(face);
    NEO98DX_SOUNDNOTIFY *wire = NULL;
    HRESULT result;
    DWORD i;
    if (!buffer->ds) return DSERR_UNINITIALIZED;
    if (count > DSBNOTIFICATIONS_MAX || (count && !positions)) return DSERR_INVALIDPARAM;
    if (count) {
        wire = malloc(count * sizeof *wire);
        if (!wire) return DSERR_OUTOFMEMORY;
        for (i = 0; i < count; ++i) {
            if ((ULONG_PTR)positions[i].hEventNotify > 0xffffffffu) {
                free(wire);
                return DSERR_INVALIDPARAM;
            }
            wire[i].offset = positions[i].dwOffset;
            wire[i].event = (DWORD)(ULONG_PTR)positions[i].hEventNotify;
        }
    }
    result = DsDrvSetNotifications(buffer->driver, wire, count);
    free(wire);
    return result;
}

static const struct IDirectSoundNotifyVtbl notify_table = {
    .QueryInterface = (void *)Buffer_QueryInterface,
    .AddRef = (void *)Buffer_AddRef, .Release = (void *)Buffer_Release,
    .SetNotificationPositions = (void *)Notify_SetNotificationPositions};

static DsBuffer *NewBuffer(DsObject *ds)
{
    DsBuffer *buffer = (DsBuffer *)calloc(1, sizeof *buffer);
    if (!buffer) return NULL;
    buffer->face.table = &buffer_table;
    buffer->face.object = buffer;
    buffer->identity.table = &buffer_table;
    buffer->identity.object = buffer;
    buffer->notify.table = &notify_table;
    buffer->notify.object = buffer;
    buffer->spatial_face.table = &spatial_table;
    buffer->spatial_face.object = buffer;
    buffer->listener_face.table = &listener_table;
    buffer->listener_face.object = buffer;
    buffer->spatial.dwSize = sizeof buffer->spatial;
    buffer->spatial.dwInsideConeAngle = buffer->spatial.dwOutsideConeAngle = 360;
    buffer->spatial.vConeOrientation.z = 1.0f;
    buffer->spatial.flMinDistance = 1.0f;
    buffer->spatial.flMaxDistance = 1000000000.0f;
    buffer->audible_spatial = buffer->spatial;
    buffer->references = 1;
    buffer->ds = ds;
    Enter();
    buffer->next = ds->buffers;
    ds->buffers = buffer;
    Leave();
    return buffer;
}

/* -------------------------------------------------------------- IDirectSound */
static DsObject *Object(void *face) { return (DsObject *)((DsFace *)face)->object; }

static HRESULT WINAPI Ds_QueryInterface(void *face, REFIID iid, void **out)
{
    if (!out) return DSERR_INVALIDPARAM;
    *out = NULL;
    if (!iid || (!IsEqualGUID(iid, &iid_unknown) && !IsEqualGUID(iid, &IID_IDirectSound))) return E_NOINTERFACE;
    AddRef(&Object(face)->references);
    *out = face;
    return S_OK;
}

static ULONG WINAPI Ds_AddRef(void *face) { return (ULONG)AddRef(&Object(face)->references); }

static ULONG WINAPI Ds_Release(void *face)
{
    DsObject *ds = Object(face);
    const LONG left = DropRef(&ds->references);
    if (left > 0) return (ULONG)left;
    /* The buffers of a DirectSound object that goes are silent and unusable;
       the program still releases the ones it holds. */
    Enter();
    while (ds->buffers)
    {
        DsBuffer *buffer = ds->buffers;
        ds->buffers = buffer->next;
        if (buffer->driver) DsDrvDestroyBuffer(buffer->driver);
        buffer->driver = 0;
        buffer->memory = NULL;
        buffer->ds = NULL;
        buffer->next = NULL;
    }
    Leave();
    free(ds);
    return 0;
}

static HRESULT WINAPI Ds_CreateSoundBuffer(void *face, LPCDSBUFFERDESC desc, LPDIRECTSOUNDBUFFER *out, IUnknown *outer)
{
    DsObject *ds = Object(face);
    const DWORD known = DSBCAPS_PRIMARYBUFFER | DSBCAPS_STATIC | DSBCAPS_LOCHARDWARE | DSBCAPS_LOCSOFTWARE | DSBCAPS_CTRL3D |
                        DSBCAPS_CTRLFREQUENCY | DSBCAPS_CTRLPAN | DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLPOSITIONNOTIFY |
                        DSBCAPS_STICKYFOCUS | DSBCAPS_GLOBALFOCUS | DSBCAPS_GETCURRENTPOSITION2 | DSBCAPS_MUTE3DATMAXDISTANCE |
                        DSBCAPS_LOCDEFER;
    DsBuffer *buffer;
    NEO98DX_SOUND sound;
    HRESULT result;
    DWORD bytes;
    if (!out) return DSERR_INVALIDPARAM;
    *out = NULL;
    if (outer) return DSERR_NOAGGREGATION;
    /* The description of DirectX 7 and the shorter one before it. */
    if (!desc || (desc->dwSize != sizeof(DSBUFFERDESC) && desc->dwSize != sizeof(DSBUFFERDESC1))) return DSERR_INVALIDPARAM;
    if (desc->dwFlags & ~known) return DSERR_INVALIDPARAM;
    if ((desc->dwFlags & DSBCAPS_LOCHARDWARE) && (desc->dwFlags & DSBCAPS_LOCSOFTWARE)) return DSERR_INVALIDPARAM;
    /* DirectSound makes buffers before a cooperative level is set (measured
       against the original); the level then counts as the normal one. */
    if ((desc->dwFlags & DSBCAPS_CTRL3D) && (desc->dwFlags & DSBCAPS_LOCHARDWARE)) return DSERR_INVALIDCALL;
    if (desc->dwFlags & DSBCAPS_PRIMARYBUFFER)
    {
        if (desc->dwBufferBytes || desc->lpwfxFormat) return DSERR_INVALIDPARAM;
        if (ds->primary)
        {
            AddRef(&ds->primary->references);
            *out = (LPDIRECTSOUNDBUFFER)&ds->primary->face;
            return DS_OK;
        }
        buffer = NewBuffer(ds);
        if (!buffer) return DSERR_OUTOFMEMORY;
        buffer->primary = TRUE;
        buffer->flags = desc->dwFlags;
        buffer->format = ds->primary_format;
        buffer->frequency = buffer->format.nSamplesPerSec;
        ds->primary = buffer;
        *out = (LPDIRECTSOUNDBUFFER)&buffer->face;
        return DS_OK;
    }
    if (!PcmFormat(desc->lpwfxFormat)) return desc->lpwfxFormat ? DSERR_BADFORMAT : DSERR_INVALIDPARAM;
    if (desc->dwBufferBytes < DSBSIZE_MIN || desc->dwBufferBytes > DSBSIZE_MAX) return DSERR_INVALIDPARAM;
    /* A size that is no whole number of frames grows to the next one
       (measured: 44.101 bytes of four-byte frames become 44.104). */
    bytes = desc->dwBufferBytes + desc->lpwfxFormat->nBlockAlign - 1;
    bytes -= bytes % desc->lpwfxFormat->nBlockAlign;
    memset(&sound, 0, sizeof sound);
    sound.size = sizeof sound;
    sound.flags = (desc->dwFlags & DSBCAPS_STATIC) ? NEO98DX_SOUND_STATIC : 0;
    if (desc->dwFlags & (DSBCAPS_GLOBALFOCUS | DSBCAPS_STICKYFOCUS)) sound.flags |= NEO98DX_SOUND_BACKGROUND;
    sound.bytes = bytes;
    sound.rate = desc->lpwfxFormat->nSamplesPerSec;
    sound.channels = desc->lpwfxFormat->nChannels;
    sound.bitsPerSample = desc->lpwfxFormat->wBitsPerSample;
    result = DsDrvCreateBuffer(&sound, sizeof sound);
    if (result != DS_OK) return result;
    buffer = NewBuffer(ds);
    if (!buffer)
    {
        DsDrvDestroyBuffer(sound.handle);
        return DSERR_OUTOFMEMORY;
    }
    buffer->driver = sound.handle;
    buffer->memory = (BYTE *)(ULONG_PTR)sound.memory;
    buffer->bytes = bytes;
    buffer->flags = desc->dwFlags;
    memcpy(&buffer->format, desc->lpwfxFormat, sizeof(PCMWAVEFORMAT));
    buffer->frequency = buffer->format.nSamplesPerSec;
    if (buffer->flags & DSBCAPS_CTRL3D) ApplySpatial(buffer);
    *out = (LPDIRECTSOUNDBUFFER)&buffer->face;
    return DS_OK;
}

static HRESULT WINAPI Ds_GetCaps(void *face, LPDSCAPS caps)
{
    NEO98DX_SOUNDCAPS driver;
    (void)face;
    if (!caps || caps->dwSize != sizeof *caps) return DSERR_INVALIDPARAM;
    memset(&driver, 0, sizeof driver);
    if (DsDrvGetCaps(&driver, sizeof driver) != DS_OK) return DSERR_GENERIC;
    memset(caps, 0, sizeof *caps);
    caps->dwSize = sizeof *caps;
    /* A card with a driver of its own that mixes every format at any rate. */
    caps->dwFlags = DSCAPS_PRIMARYMONO | DSCAPS_PRIMARYSTEREO | DSCAPS_PRIMARY8BIT | DSCAPS_PRIMARY16BIT | DSCAPS_CONTINUOUSRATE |
                    DSCAPS_SECONDARYMONO | DSCAPS_SECONDARYSTEREO | DSCAPS_SECONDARY8BIT | DSCAPS_SECONDARY16BIT;
    caps->dwMinSecondarySampleRate = DSBFREQUENCY_MIN;
    caps->dwMaxSecondarySampleRate = 100000;
    caps->dwPrimaryBuffers = 1;
    caps->dwMaxHwMixingAllBuffers = caps->dwMaxHwMixingStaticBuffers = caps->dwMaxHwMixingStreamingBuffers = driver.voices;
    caps->dwFreeHwMixingAllBuffers = caps->dwFreeHwMixingStaticBuffers = caps->dwFreeHwMixingStreamingBuffers = driver.voicesFree;
    return DS_OK;
}

static HRESULT WINAPI Ds_DuplicateSoundBuffer(void *face, LPDIRECTSOUNDBUFFER original, LPDIRECTSOUNDBUFFER *out)
{
    DsObject *ds = Object(face);
    DsBuffer *source = original ? Buffer(original) : NULL, *buffer;
    DWORD handle = 0;
    HRESULT result;
    if (!out) return DSERR_INVALIDPARAM;
    *out = NULL;
    if (!source || source->ds != ds) return DSERR_INVALIDPARAM;
    if (source->primary) return DSERR_INVALIDCALL;
    result = DsDrvDuplicateBuffer(source->driver, &handle);
    if (result != DS_OK) return result;
    buffer = NewBuffer(ds);
    if (!buffer)
    {
        DsDrvDestroyBuffer(handle);
        return DSERR_OUTOFMEMORY;
    }
    buffer->driver = handle;
    buffer->memory = source->memory;
    buffer->bytes = source->bytes;
    buffer->flags = source->flags;
    buffer->format = source->format;
    buffer->frequency = source->frequency;
    buffer->volume = source->volume;
    buffer->pan = source->pan;
    buffer->spatial = source->spatial;
    buffer->audible_spatial = source->audible_spatial;
    if (buffer->frequency != buffer->format.nSamplesPerSec) DsDrvSetFrequency(handle, buffer->frequency);
    ApplyVolume(buffer);
    *out = (LPDIRECTSOUNDBUFFER)&buffer->face;
    return DS_OK;
}

static HRESULT WINAPI Ds_SetCooperativeLevel(void *face, HWND window, DWORD level)
{
    DsObject *ds = Object(face);
    if (!window || !IsWindow(window) || level < DSSCL_NORMAL || level > DSSCL_WRITEPRIMARY) return DSERR_INVALIDPARAM;
    ds->window = window;
    ds->level = level;
    return DS_OK;
}

static HRESULT WINAPI Ds_Compact(void *face)
{
    return Object(face)->level >= DSSCL_PRIORITY ? DS_OK : DSERR_PRIOLEVELNEEDED;
}

static HRESULT WINAPI Ds_GetSpeakerConfig(void *face, LPDWORD config)
{
    if (!config) return DSERR_INVALIDPARAM;
    *config = Object(face)->speaker;
    return DS_OK;
}

static HRESULT WINAPI Ds_SetSpeakerConfig(void *face, DWORD config)
{
    if ((config & 0xff) < DSSPEAKER_HEADPHONE || (config & 0xff) > DSSPEAKER_SURROUND) return DSERR_INVALIDPARAM;
    Object(face)->speaker = config;
    return DS_OK;
}

static HRESULT WINAPI Ds_Initialize(void *face, LPCGUID device)
{
    (void)face;
    (void)device;
    return DSERR_ALREADYINITIALIZED;
}

static const struct IDirectSoundVtbl object_table = {
    .QueryInterface = (void *)Ds_QueryInterface, .AddRef = (void *)Ds_AddRef, .Release = (void *)Ds_Release,
    .CreateSoundBuffer = (void *)Ds_CreateSoundBuffer, .GetCaps = (void *)Ds_GetCaps,
    .DuplicateSoundBuffer = (void *)Ds_DuplicateSoundBuffer, .SetCooperativeLevel = (void *)Ds_SetCooperativeLevel,
    .Compact = (void *)Ds_Compact, .GetSpeakerConfig = (void *)Ds_GetSpeakerConfig,
    .SetSpeakerConfig = (void *)Ds_SetSpeakerConfig, .Initialize = (void *)Ds_Initialize};

/* ------------------------------------------------------------------ exports */
HRESULT WINAPI DirectSoundCreate(LPCGUID device, LPDIRECTSOUND *out, IUnknown *outer)
{
    static const GUID none;
    DsObject *ds;
    if (!out) return DSERR_INVALIDPARAM;
    *out = NULL;
    if (outer) return DSERR_NOAGGREGATION;
    /* One card: the default device, which a program may name with no GUID. */
    if (device && !IsEqualGUID(device, &none)) return DSERR_NODRIVER;
    if (DxDrvVersion() != NEO98DX_VERSION) return DSERR_NODRIVER;
    /* The card is opened here, as DirectSound opens its device when the
       object is made, so the first buffer that plays finds it running. */
    if (DsDrvOpen() != DS_OK) return DSERR_NODRIVER;
    ds = (DsObject *)calloc(1, sizeof *ds);
    if (!ds) return DSERR_OUTOFMEMORY;
    ds->face.table = &object_table;
    ds->face.object = ds;
    ds->references = 1;
    ds->listener.dwSize = sizeof ds->listener;
    ds->listener.vOrientFront.z = ds->listener.vOrientTop.y = 1.0f;
    ds->listener.flDistanceFactor = ds->listener.flRolloffFactor = ds->listener.flDopplerFactor = 1.0f;
    ds->audible_listener = ds->listener;
    /* Stereo speakers twenty degrees apart, what DirectSound answers on a
       fresh Windows 98 (measured: 00140004h). */
    ds->speaker = DSSPEAKER_STEREO | 20u << 16;
    /* What the primary buffer answers before a program sets its format: the
       22 kHz, 8 bit, stereo a card of Windows 98 starts with. */
    ds->primary_format.wFormatTag = WAVE_FORMAT_PCM;
    ds->primary_format.nChannels = 2;
    ds->primary_format.nSamplesPerSec = 22050;
    ds->primary_format.wBitsPerSample = 8;
    ds->primary_format.nBlockAlign = 2;
    ds->primary_format.nAvgBytesPerSec = 44100;
    *out = (LPDIRECTSOUND)&ds->face;
    return DS_OK;
}

HRESULT WINAPI DirectSoundEnumerateA(LPDSENUMCALLBACKA callback, LPVOID context)
{
    if (!callback) return DSERR_INVALIDPARAM;
    callback(NULL, "Primary Sound Driver", "", context);
    return DS_OK;
}
