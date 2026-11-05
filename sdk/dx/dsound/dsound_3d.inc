/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Windows DirectSound 3D controls. The existing driver mixes the resulting
   channel gains and sample rate; sqrt/log/pow come from the imported musl. */
static double Dot3(D3DVECTOR a, D3DVECTOR b)
{
    return (double)a.x * b.x + (double)a.y * b.y + (double)a.z * b.z;
}
static D3DVECTOR Sub3(D3DVECTOR a, D3DVECTOR b)
{
    D3DVECTOR r;
    r.x = a.x - b.x;
    r.y = a.y - b.y;
    r.z = a.z - b.z;
    return r;
}
static double Length3(D3DVECTOR v)
{
    return sqrt(Dot3(v, v));
}
static BOOL ValidVector(D3DVECTOR v)
{
    return isfinite(v.x) && isfinite(v.y) && isfinite(v.z);
}
static void Normalize3(D3DVECTOR *v)
{
    double length = Length3(*v);
    v->x = (float)(v->x / length);
    v->y = (float)(v->y / length);
    v->z = (float)(v->z / length);
}
static void SpatialLeave(char *guard)
{
    (void)guard;
    Leave();
}
#define DS3D_METHOD_LOCK()                                                                                                                 \
    Enter();                                                                                                                               \
    char spatial_guard __attribute__((cleanup(SpatialLeave))) = 0
static HRESULT ApplySpatial(DsBuffer *buffer)
{
    if (!buffer->driver || !buffer->ds)
        return DS_OK;
    const DS3DBUFFER *b = &buffer->audible_spatial;
    const DS3DLISTENER *l = &buffer->ds->audible_listener;
    LONG volume = buffer->volume, pan = 0;
    double rate = buffer->frequency;
    if (b->dwMode != DS3DMODE_DISABLE)
    {
        D3DVECTOR position = b->dwMode == DS3DMODE_HEADRELATIVE ? b->vPosition : Sub3(b->vPosition, l->vPosition);
        double distance = Length3(position), effective = fmin(distance, b->flMaxDistance), gain = 1;
        /* DSOUND 4.09 Pan3D: 5 dB headroom, inverse-distance rolloff and
           a 3/2-power channel attenuation (volume/pan are centibels). */
        volume -= 500;
        if (effective > b->flMinDistance)
            gain = b->flMinDistance / (b->flMinDistance + l->flRolloffFactor * (effective - b->flMinDistance));
        if (distance > 0)
        {
            D3DVECTOR right;
            right.x = 1;
            right.y = right.z = 0;
            if (b->dwMode != DS3DMODE_HEADRELATIVE)
            {
                right.x = l->vOrientTop.y * l->vOrientFront.z - l->vOrientTop.z * l->vOrientFront.y;
                right.y = l->vOrientTop.z * l->vOrientFront.x - l->vOrientTop.x * l->vOrientFront.z;
                right.z = l->vOrientTop.x * l->vOrientFront.y - l->vOrientTop.y * l->vOrientFront.x;
            }
            double length = Length3(right);
            double side = length > 0 ? Dot3(position, right) / (distance * length) : 0;
            double away = fmax(0, 1 - fabs(side));
            LONG attenuation = away > 0 ? (LONG)(-3000 * log10(away)) : 10000;
            pan = side < 0 ? -attenuation : attenuation;
            double axis = Length3(b->vConeOrientation);
            if (axis > 0 && b->dwInsideConeAngle < 360)
            {
                double cosine = -Dot3(position, b->vConeOrientation) / (distance * axis);
                const double radians = 3.14159265358979323846 / 360;
                double inner = cos(b->dwInsideConeAngle * radians), outer = cos(b->dwOutsideConeAngle * radians);
                double fraction = cosine >= inner ? 0 : cosine <= outer ? 1 : (inner - cosine) / (inner - outer);
                gain *= exp2(b->lConeOutsideVolume * fraction / 600);
            }
            D3DVECTOR velocity = b->dwMode == DS3DMODE_HEADRELATIVE ? b->vVelocity : Sub3(b->vVelocity, l->vVelocity);
            double radial = Dot3(velocity, position) / distance * l->flDistanceFactor * l->flDopplerFactor;
            /* Original DSOUND uses relative radial velocity and 359.66 m/s,
               bounded to half that speed, then truncates the sample rate. */
            radial = fmax(-179.83, fmin(179.83, radial));
            rate *= 359.66 / (359.66 + radial);
        }
        volume += gain > 0 ? (LONG)(2000 * log10(gain)) : DSBVOLUME_MIN;
        if (distance >= b->flMaxDistance && (buffer->flags & DSBCAPS_MUTE3DATMAXDISTANCE))
            volume = DSBVOLUME_MIN;
    }
    DWORD left = Amplitude(volume - (pan > 0 ? pan : 0));
    DWORD right = Amplitude(volume + (pan < 0 ? pan : 0));
    if (rate < DSBFREQUENCY_MIN)
        rate = DSBFREQUENCY_MIN;
    if (rate > 100000)
        rate = 100000;
    HRESULT result = DsDrvSetVolumePan(buffer->driver, left, right);
    return result == DS_OK ? DsDrvSetFrequency(buffer->driver, (DWORD)rate) : result;
}
static void ApplyListener(DsObject *ds)
{
    for (DsBuffer *b = ds->buffers; b; b = b->next)
        if (!b->primary && (b->flags & DSBCAPS_CTRL3D))
            ApplySpatial(b);
}
static BOOL ValidSpatial(const DS3DBUFFER *p)
{
    return p && p->dwSize == sizeof *p && ValidVector(p->vPosition) && ValidVector(p->vVelocity) && ValidVector(p->vConeOrientation) &&
           Length3(p->vConeOrientation) > 0 && p->dwInsideConeAngle <= p->dwOutsideConeAngle && p->dwOutsideConeAngle <= 360 &&
           p->lConeOutsideVolume >= -10000 && p->lConeOutsideVolume <= 0 && p->flMinDistance > 0 && isfinite(p->flMinDistance) &&
           p->flMaxDistance > 0 && isfinite(p->flMaxDistance) && p->dwMode <= DS3DMODE_DISABLE;
}
static BOOL ValidListener(const DS3DLISTENER *p)
{
    return p && p->dwSize == sizeof *p && ValidVector(p->vPosition) && ValidVector(p->vVelocity) && ValidVector(p->vOrientFront) &&
           ValidVector(p->vOrientTop) && Length3(p->vOrientFront) > 0 && Length3(p->vOrientTop) > 0 && p->flDistanceFactor > 0 &&
           isfinite(p->flDistanceFactor) && p->flRolloffFactor >= 0 && p->flRolloffFactor <= 10 && p->flDopplerFactor >= 0 &&
           p->flDopplerFactor <= 10;
}
static HRESULT SpatialSet(void *face, const DS3DBUFFER *p, DWORD offset, DWORD size, DWORD apply)
{
    DsBuffer *b = Buffer(face);
    if (!b->ds)
        return DSERR_UNINITIALIZED;
    if (apply > DS3D_DEFERRED || !ValidSpatial(p))
        return DSERR_INVALIDPARAM;
    DS3DBUFFER normalized = *p;
    Normalize3(&normalized.vConeOrientation);
    p = &normalized;
    Enter();
    memcpy((BYTE *)&b->spatial + offset, (const BYTE *)p + offset, size);
    if (apply == DS3D_IMMEDIATE)
    {
        memcpy((BYTE *)&b->audible_spatial + offset, (const BYTE *)p + offset, size);
        ApplySpatial(b);
    }
    Leave();
    return DS_OK;
}
static HRESULT ListenerSet(void *face, const DS3DLISTENER *p, DWORD offset, DWORD size, DWORD apply)
{
    DsObject *ds = Buffer(face)->ds;
    if (!ds)
        return DSERR_UNINITIALIZED;
    if (apply > DS3D_DEFERRED || !ValidListener(p))
        return DSERR_INVALIDPARAM;
    DS3DLISTENER normalized = *p;
    Normalize3(&normalized.vOrientFront);
    Normalize3(&normalized.vOrientTop);
    p = &normalized;
    Enter();
    memcpy((BYTE *)&ds->listener + offset, (const BYTE *)p + offset, size);
    if (apply == DS3D_IMMEDIATE)
    {
        memcpy((BYTE *)&ds->audible_listener + offset, (const BYTE *)p + offset, size);
        ApplyListener(ds);
    }
    Leave();
    return DS_OK;
}
static HRESULT WINAPI Spatial_GetAllParameters(void *face, LPDS3DBUFFER out)
{
    DS3D_METHOD_LOCK();
    if (!out || out->dwSize != sizeof *out)
        return DSERR_INVALIDPARAM;
    if (!Buffer(face)->ds)
        return DSERR_UNINITIALIZED;
    *out = Buffer(face)->audible_spatial;
    return DS_OK;
}
static HRESULT WINAPI Spatial_SetAllParameters(void *face, LPCDS3DBUFFER p, DWORD apply)
{
    DS3D_METHOD_LOCK();
    return SpatialSet(face, p, 0, sizeof *p, apply);
}
static HRESULT WINAPI Listener_GetAllParameters(void *face, LPDS3DLISTENER out)
{
    DS3D_METHOD_LOCK();
    if (!out || out->dwSize != sizeof *out)
        return DSERR_INVALIDPARAM;
    if (!Buffer(face)->ds)
        return DSERR_UNINITIALIZED;
    *out = Buffer(face)->ds->audible_listener;
    return DS_OK;
}
static HRESULT WINAPI Listener_SetAllParameters(void *face, LPCDS3DLISTENER p, DWORD apply)
{
    DS3D_METHOD_LOCK();
    return ListenerSet(face, p, 0, sizeof *p, apply);
}
static HRESULT WINAPI Listener_CommitDeferredSettings(void *face)
{
    DS3D_METHOD_LOCK();
    DsObject *ds = Buffer(face)->ds;
    if (!ds)
        return DSERR_UNINITIALIZED;
    Enter();
    ds->audible_listener = ds->listener;
    for (DsBuffer *b = ds->buffers; b; b = b->next)
        b->audible_spatial = b->spatial;
    ApplyListener(ds);
    Leave();
    return DS_OK;
}

static HRESULT WINAPI Spatial_GetConeAngles(void *face, DWORD *out0, DWORD *out1)
{
    DS3D_METHOD_LOCK();
    if (!out0 || !out1)
        return DSERR_INVALIDPARAM;
    if (!Buffer(face)->ds)
        return DSERR_UNINITIALIZED;
    *out0 = Buffer(face)->audible_spatial.dwInsideConeAngle;
    *out1 = Buffer(face)->audible_spatial.dwOutsideConeAngle;
    return DS_OK;
}
static HRESULT WINAPI Spatial_SetConeAngles(void *face, DWORD value0, DWORD value1, DWORD apply)
{
    DS3D_METHOD_LOCK();
    if (!Buffer(face)->ds)
        return DSERR_UNINITIALIZED;
    DS3DBUFFER p = Buffer(face)->spatial;
    p.dwInsideConeAngle = value0;
    p.dwOutsideConeAngle = value1;
    return SpatialSet(face, &p, offsetof(DS3DBUFFER, dwInsideConeAngle), sizeof p.dwInsideConeAngle + sizeof p.dwOutsideConeAngle, apply);
}

static HRESULT WINAPI Spatial_GetConeOrientation(void *face, D3DVECTOR *out0)
{
    DS3D_METHOD_LOCK();
    if (!out0)
        return DSERR_INVALIDPARAM;
    if (!Buffer(face)->ds)
        return DSERR_UNINITIALIZED;
    *out0 = Buffer(face)->audible_spatial.vConeOrientation;
    return DS_OK;
}
static HRESULT WINAPI Spatial_SetConeOrientation(void *face, D3DVALUE x0, D3DVALUE y0, D3DVALUE z0, DWORD apply)
{
    DS3D_METHOD_LOCK();
    if (!Buffer(face)->ds)
        return DSERR_UNINITIALIZED;
    DS3DBUFFER p = Buffer(face)->spatial;
    p.vConeOrientation.x = x0;
    p.vConeOrientation.y = y0;
    p.vConeOrientation.z = z0;
    return SpatialSet(face, &p, offsetof(DS3DBUFFER, vConeOrientation), sizeof p.vConeOrientation, apply);
}

static HRESULT WINAPI Spatial_GetConeOutsideVolume(void *face, LONG *out0)
{
    DS3D_METHOD_LOCK();
    if (!out0)
        return DSERR_INVALIDPARAM;
    if (!Buffer(face)->ds)
        return DSERR_UNINITIALIZED;
    *out0 = Buffer(face)->audible_spatial.lConeOutsideVolume;
    return DS_OK;
}
static HRESULT WINAPI Spatial_SetConeOutsideVolume(void *face, LONG value0, DWORD apply)
{
    DS3D_METHOD_LOCK();
    if (!Buffer(face)->ds)
        return DSERR_UNINITIALIZED;
    DS3DBUFFER p = Buffer(face)->spatial;
    p.lConeOutsideVolume = value0;
    return SpatialSet(face, &p, offsetof(DS3DBUFFER, lConeOutsideVolume), sizeof p.lConeOutsideVolume, apply);
}

static HRESULT WINAPI Spatial_GetMaxDistance(void *face, D3DVALUE *out0)
{
    DS3D_METHOD_LOCK();
    if (!out0)
        return DSERR_INVALIDPARAM;
    if (!Buffer(face)->ds)
        return DSERR_UNINITIALIZED;
    *out0 = Buffer(face)->audible_spatial.flMaxDistance;
    return DS_OK;
}
static HRESULT WINAPI Spatial_SetMaxDistance(void *face, D3DVALUE value0, DWORD apply)
{
    DS3D_METHOD_LOCK();
    if (!Buffer(face)->ds)
        return DSERR_UNINITIALIZED;
    DS3DBUFFER p = Buffer(face)->spatial;
    p.flMaxDistance = value0;
    return SpatialSet(face, &p, offsetof(DS3DBUFFER, flMaxDistance), sizeof p.flMaxDistance, apply);
}

static HRESULT WINAPI Spatial_GetMinDistance(void *face, D3DVALUE *out0)
{
    DS3D_METHOD_LOCK();
    if (!out0)
        return DSERR_INVALIDPARAM;
    if (!Buffer(face)->ds)
        return DSERR_UNINITIALIZED;
    *out0 = Buffer(face)->audible_spatial.flMinDistance;
    return DS_OK;
}
static HRESULT WINAPI Spatial_SetMinDistance(void *face, D3DVALUE value0, DWORD apply)
{
    DS3D_METHOD_LOCK();
    if (!Buffer(face)->ds)
        return DSERR_UNINITIALIZED;
    DS3DBUFFER p = Buffer(face)->spatial;
    p.flMinDistance = value0;
    return SpatialSet(face, &p, offsetof(DS3DBUFFER, flMinDistance), sizeof p.flMinDistance, apply);
}

static HRESULT WINAPI Spatial_GetMode(void *face, DWORD *out0)
{
    DS3D_METHOD_LOCK();
    if (!out0)
        return DSERR_INVALIDPARAM;
    if (!Buffer(face)->ds)
        return DSERR_UNINITIALIZED;
    *out0 = Buffer(face)->audible_spatial.dwMode;
    return DS_OK;
}
static HRESULT WINAPI Spatial_SetMode(void *face, DWORD value0, DWORD apply)
{
    DS3D_METHOD_LOCK();
    if (!Buffer(face)->ds)
        return DSERR_UNINITIALIZED;
    DS3DBUFFER p = Buffer(face)->spatial;
    p.dwMode = value0;
    return SpatialSet(face, &p, offsetof(DS3DBUFFER, dwMode), sizeof p.dwMode, apply);
}

static HRESULT WINAPI Spatial_GetPosition(void *face, D3DVECTOR *out0)
{
    DS3D_METHOD_LOCK();
    if (!out0)
        return DSERR_INVALIDPARAM;
    if (!Buffer(face)->ds)
        return DSERR_UNINITIALIZED;
    *out0 = Buffer(face)->audible_spatial.vPosition;
    return DS_OK;
}
static HRESULT WINAPI Spatial_SetPosition(void *face, D3DVALUE x0, D3DVALUE y0, D3DVALUE z0, DWORD apply)
{
    DS3D_METHOD_LOCK();
    if (!Buffer(face)->ds)
        return DSERR_UNINITIALIZED;
    DS3DBUFFER p = Buffer(face)->spatial;
    p.vPosition.x = x0;
    p.vPosition.y = y0;
    p.vPosition.z = z0;
    return SpatialSet(face, &p, offsetof(DS3DBUFFER, vPosition), sizeof p.vPosition, apply);
}

static HRESULT WINAPI Spatial_GetVelocity(void *face, D3DVECTOR *out0)
{
    DS3D_METHOD_LOCK();
    if (!out0)
        return DSERR_INVALIDPARAM;
    if (!Buffer(face)->ds)
        return DSERR_UNINITIALIZED;
    *out0 = Buffer(face)->audible_spatial.vVelocity;
    return DS_OK;
}
static HRESULT WINAPI Spatial_SetVelocity(void *face, D3DVALUE x0, D3DVALUE y0, D3DVALUE z0, DWORD apply)
{
    DS3D_METHOD_LOCK();
    if (!Buffer(face)->ds)
        return DSERR_UNINITIALIZED;
    DS3DBUFFER p = Buffer(face)->spatial;
    p.vVelocity.x = x0;
    p.vVelocity.y = y0;
    p.vVelocity.z = z0;
    return SpatialSet(face, &p, offsetof(DS3DBUFFER, vVelocity), sizeof p.vVelocity, apply);
}

static const struct IDirectSound3DBufferVtbl spatial_table = {
    .QueryInterface = (void *)Buffer_QueryInterface,
    .AddRef = (void *)Buffer_AddRef,
    .Release = (void *)Buffer_Release,
    .GetAllParameters = (void *)Spatial_GetAllParameters,
    .SetAllParameters = (void *)Spatial_SetAllParameters,
    .GetConeAngles = (void *)Spatial_GetConeAngles,
    .SetConeAngles = (void *)Spatial_SetConeAngles,
    .GetConeOrientation = (void *)Spatial_GetConeOrientation,
    .SetConeOrientation = (void *)Spatial_SetConeOrientation,
    .GetConeOutsideVolume = (void *)Spatial_GetConeOutsideVolume,
    .SetConeOutsideVolume = (void *)Spatial_SetConeOutsideVolume,
    .GetMaxDistance = (void *)Spatial_GetMaxDistance,
    .SetMaxDistance = (void *)Spatial_SetMaxDistance,
    .GetMinDistance = (void *)Spatial_GetMinDistance,
    .SetMinDistance = (void *)Spatial_SetMinDistance,
    .GetMode = (void *)Spatial_GetMode,
    .SetMode = (void *)Spatial_SetMode,
    .GetPosition = (void *)Spatial_GetPosition,
    .SetPosition = (void *)Spatial_SetPosition,
    .GetVelocity = (void *)Spatial_GetVelocity,
    .SetVelocity = (void *)Spatial_SetVelocity,
};

static HRESULT WINAPI Listener_GetDistanceFactor(void *face, D3DVALUE *out0)
{
    DS3D_METHOD_LOCK();
    if (!out0)
        return DSERR_INVALIDPARAM;
    if (!Buffer(face)->ds)
        return DSERR_UNINITIALIZED;
    *out0 = Buffer(face)->ds->audible_listener.flDistanceFactor;
    return DS_OK;
}
static HRESULT WINAPI Listener_SetDistanceFactor(void *face, D3DVALUE value0, DWORD apply)
{
    DS3D_METHOD_LOCK();
    if (!Buffer(face)->ds)
        return DSERR_UNINITIALIZED;
    DS3DLISTENER p = Buffer(face)->ds->listener;
    p.flDistanceFactor = value0;
    return ListenerSet(face, &p, offsetof(DS3DLISTENER, flDistanceFactor), sizeof p.flDistanceFactor, apply);
}

static HRESULT WINAPI Listener_GetDopplerFactor(void *face, D3DVALUE *out0)
{
    DS3D_METHOD_LOCK();
    if (!out0)
        return DSERR_INVALIDPARAM;
    if (!Buffer(face)->ds)
        return DSERR_UNINITIALIZED;
    *out0 = Buffer(face)->ds->audible_listener.flDopplerFactor;
    return DS_OK;
}
static HRESULT WINAPI Listener_SetDopplerFactor(void *face, D3DVALUE value0, DWORD apply)
{
    DS3D_METHOD_LOCK();
    if (!Buffer(face)->ds)
        return DSERR_UNINITIALIZED;
    DS3DLISTENER p = Buffer(face)->ds->listener;
    p.flDopplerFactor = value0;
    return ListenerSet(face, &p, offsetof(DS3DLISTENER, flDopplerFactor), sizeof p.flDopplerFactor, apply);
}

static HRESULT WINAPI Listener_GetOrientation(void *face, D3DVECTOR *out0, D3DVECTOR *out1)
{
    DS3D_METHOD_LOCK();
    if (!out0 || !out1)
        return DSERR_INVALIDPARAM;
    if (!Buffer(face)->ds)
        return DSERR_UNINITIALIZED;
    *out0 = Buffer(face)->ds->audible_listener.vOrientFront;
    *out1 = Buffer(face)->ds->audible_listener.vOrientTop;
    return DS_OK;
}
static HRESULT WINAPI Listener_SetOrientation(void *face, D3DVALUE x0, D3DVALUE y0, D3DVALUE z0, D3DVALUE x1, D3DVALUE y1, D3DVALUE z1,
                                              DWORD apply)
{
    DS3D_METHOD_LOCK();
    if (!Buffer(face)->ds)
        return DSERR_UNINITIALIZED;
    DS3DLISTENER p = Buffer(face)->ds->listener;
    p.vOrientFront.x = x0;
    p.vOrientFront.y = y0;
    p.vOrientFront.z = z0;
    p.vOrientTop.x = x1;
    p.vOrientTop.y = y1;
    p.vOrientTop.z = z1;
    return ListenerSet(face, &p, offsetof(DS3DLISTENER, vOrientFront), sizeof p.vOrientFront + sizeof p.vOrientTop, apply);
}

static HRESULT WINAPI Listener_GetPosition(void *face, D3DVECTOR *out0)
{
    DS3D_METHOD_LOCK();
    if (!out0)
        return DSERR_INVALIDPARAM;
    if (!Buffer(face)->ds)
        return DSERR_UNINITIALIZED;
    *out0 = Buffer(face)->ds->audible_listener.vPosition;
    return DS_OK;
}
static HRESULT WINAPI Listener_SetPosition(void *face, D3DVALUE x0, D3DVALUE y0, D3DVALUE z0, DWORD apply)
{
    DS3D_METHOD_LOCK();
    if (!Buffer(face)->ds)
        return DSERR_UNINITIALIZED;
    DS3DLISTENER p = Buffer(face)->ds->listener;
    p.vPosition.x = x0;
    p.vPosition.y = y0;
    p.vPosition.z = z0;
    return ListenerSet(face, &p, offsetof(DS3DLISTENER, vPosition), sizeof p.vPosition, apply);
}

static HRESULT WINAPI Listener_GetRolloffFactor(void *face, D3DVALUE *out0)
{
    DS3D_METHOD_LOCK();
    if (!out0)
        return DSERR_INVALIDPARAM;
    if (!Buffer(face)->ds)
        return DSERR_UNINITIALIZED;
    *out0 = Buffer(face)->ds->audible_listener.flRolloffFactor;
    return DS_OK;
}
static HRESULT WINAPI Listener_SetRolloffFactor(void *face, D3DVALUE value0, DWORD apply)
{
    DS3D_METHOD_LOCK();
    if (!Buffer(face)->ds)
        return DSERR_UNINITIALIZED;
    DS3DLISTENER p = Buffer(face)->ds->listener;
    p.flRolloffFactor = value0;
    return ListenerSet(face, &p, offsetof(DS3DLISTENER, flRolloffFactor), sizeof p.flRolloffFactor, apply);
}

static HRESULT WINAPI Listener_GetVelocity(void *face, D3DVECTOR *out0)
{
    DS3D_METHOD_LOCK();
    if (!out0)
        return DSERR_INVALIDPARAM;
    if (!Buffer(face)->ds)
        return DSERR_UNINITIALIZED;
    *out0 = Buffer(face)->ds->audible_listener.vVelocity;
    return DS_OK;
}
static HRESULT WINAPI Listener_SetVelocity(void *face, D3DVALUE x0, D3DVALUE y0, D3DVALUE z0, DWORD apply)
{
    DS3D_METHOD_LOCK();
    if (!Buffer(face)->ds)
        return DSERR_UNINITIALIZED;
    DS3DLISTENER p = Buffer(face)->ds->listener;
    p.vVelocity.x = x0;
    p.vVelocity.y = y0;
    p.vVelocity.z = z0;
    return ListenerSet(face, &p, offsetof(DS3DLISTENER, vVelocity), sizeof p.vVelocity, apply);
}

static const struct IDirectSound3DListenerVtbl listener_table = {
    .QueryInterface = (void *)Buffer_QueryInterface,
    .AddRef = (void *)Buffer_AddRef,
    .Release = (void *)Buffer_Release,
    .GetAllParameters = (void *)Listener_GetAllParameters,
    .SetAllParameters = (void *)Listener_SetAllParameters,
    .GetDistanceFactor = (void *)Listener_GetDistanceFactor,
    .SetDistanceFactor = (void *)Listener_SetDistanceFactor,
    .GetDopplerFactor = (void *)Listener_GetDopplerFactor,
    .SetDopplerFactor = (void *)Listener_SetDopplerFactor,
    .GetOrientation = (void *)Listener_GetOrientation,
    .SetOrientation = (void *)Listener_SetOrientation,
    .GetPosition = (void *)Listener_GetPosition,
    .SetPosition = (void *)Listener_SetPosition,
    .GetRolloffFactor = (void *)Listener_GetRolloffFactor,
    .SetRolloffFactor = (void *)Listener_SetRolloffFactor,
    .GetVelocity = (void *)Listener_GetVelocity,
    .SetVelocity = (void *)Listener_SetVelocity,
    .CommitDeferredSettings = (void *)Listener_CommitDeferredSettings,
};
