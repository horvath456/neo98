/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Direct3D 7 in the DDRAW.DLL of ARM64 processes: flexible vertex formats,
   and the vertex pipeline in software for the two places where a program
   gets its results back, IDirect3DVertexBuffer7::ProcessVertices and
   IDirect3DDevice7::ComputeSphereVisibility. What a device draws is
   transformed and lit by the rasterizer, not here. */
#include "d3d_private.h"

/* The layout a flexible vertex format describes: position, blend weights,
   normal, the reserved DWORD of D3DFVF_LVERTEX, diffuse, specular and up to
   eight texture coordinate sets of one to four floats. */
BOOL D3dLayoutOf(DWORD fvf, D3dLayout *l)
{
    DWORD at, i;
    memset(l, 0, sizeof *l);
    l->normal = l->reserved = l->diffuse = l->specular = -1;
    for (i = 0; i < 8; ++i) l->texture[i] = -1;
    if (fvf & (D3DFVF_RESERVED0 | D3DFVF_RESERVED2)) return FALSE;
    switch (fvf & D3DFVF_POSITION_MASK)
    {
    case D3DFVF_XYZ: at = 12; break;
    case D3DFVF_XYZRHW: at = 16; l->transformed = TRUE; break;
    case D3DFVF_XYZB1: at = 16; l->weights = 1; break;
    case D3DFVF_XYZB2: at = 20; l->weights = 2; break;
    case D3DFVF_XYZB3: at = 24; l->weights = 3; break;
    case D3DFVF_XYZB4: at = 28; l->weights = 4; break;
    case D3DFVF_XYZB5: at = 32; l->weights = 5; break;
    default: return FALSE;
    }
    if (fvf & D3DFVF_NORMAL)
    {
        /* A transformed vertex has no normal. */
        if (l->transformed) return FALSE;
        l->normal = (int)at;
        at += 12;
    }
    if (fvf & D3DFVF_RESERVED1) { l->reserved = (int)at; at += 4; }
    if (fvf & D3DFVF_DIFFUSE) { l->diffuse = (int)at; at += 4; }
    if (fvf & D3DFVF_SPECULAR) { l->specular = (int)at; at += 4; }
    l->texture_sets = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    if (l->texture_sets > 8) return FALSE;
    for (i = 0; i < l->texture_sets; ++i)
    {
        const DWORD bits = (fvf >> (16 + 2 * i)) & 3;
        const DWORD floats = bits == D3DFVF_TEXTUREFORMAT2 ? 2 : bits == D3DFVF_TEXTUREFORMAT1 ? 1 : bits == D3DFVF_TEXTUREFORMAT3 ? 3 : 4;
        l->texture[i] = (int)at;
        l->texture_floats[i] = floats;
        at += floats * 4;
    }
    l->stride = at;
    return TRUE;
}

void D3dStridedOf(const BYTE *vertices, DWORD fvf, const D3dLayout *l, D3DDRAWPRIMITIVESTRIDEDDATA *out)
{
    DWORD i;
    (void)fvf;
    memset(out, 0, sizeof *out);
    out->position.lpvData = (LPVOID)vertices;
    out->position.dwStride = l->stride;
    if (l->normal >= 0) { out->normal.lpvData = (LPVOID)(vertices + l->normal); out->normal.dwStride = l->stride; }
    if (l->diffuse >= 0) { out->diffuse.lpvData = (LPVOID)(vertices + l->diffuse); out->diffuse.dwStride = l->stride; }
    if (l->specular >= 0) { out->specular.lpvData = (LPVOID)(vertices + l->specular); out->specular.dwStride = l->stride; }
    for (i = 0; i < l->texture_sets; ++i)
    {
        out->textureCoords[i].lpvData = (LPVOID)(vertices + l->texture[i]);
        out->textureCoords[i].dwStride = l->stride;
    }
}

/* Strided vertices into the packed layout of their format, from vertex
   `first` on. The position carries the blend weights or the RHW behind it. */
void D3dPack(BYTE *out, const D3DDRAWPRIMITIVESTRIDEDDATA *source, DWORD first, const D3dLayout *l, DWORD count)
{
    const DWORD position_bytes = l->transformed ? 16 : 12 + l->weights * 4;
    DWORD i, set;
    for (i = 0; i < count; ++i, out += l->stride)
    {
        const DWORD n = first + i;
        memcpy(out, (const BYTE *)source->position.lpvData + (size_t)n * source->position.dwStride, position_bytes);
        if (l->normal >= 0) memcpy(out + l->normal, (const BYTE *)source->normal.lpvData + (size_t)n * source->normal.dwStride, 12);
        if (l->reserved >= 0) memset(out + l->reserved, 0, 4);
        if (l->diffuse >= 0) memcpy(out + l->diffuse, (const BYTE *)source->diffuse.lpvData + (size_t)n * source->diffuse.dwStride, 4);
        if (l->specular >= 0)
            memcpy(out + l->specular, (const BYTE *)source->specular.lpvData + (size_t)n * source->specular.dwStride, 4);
        for (set = 0; set < l->texture_sets; ++set)
            memcpy(out + l->texture[set],
                   (const BYTE *)source->textureCoords[set].lpvData + (size_t)n * source->textureCoords[set].dwStride,
                   l->texture_floats[set] * 4);
    }
}

/* Row vectors: a point goes through `a` first, then through `b`. */
void D3dMultiply(D3DMATRIX *out, const D3DMATRIX *a, const D3DMATRIX *b)
{
    const float *x = &a->_11, *y = &b->_11;
    float product[16];
    int row, column, k;
    for (row = 0; row < 4; ++row)
        for (column = 0; column < 4; ++column)
        {
            float sum = 0.0f;
            for (k = 0; k < 4; ++k) sum += x[row * 4 + k] * y[k * 4 + column];
            product[row * 4 + column] = sum;
        }
    memcpy(out, product, sizeof product);
}

static void Transform4(float *out, const float *p, const D3DMATRIX *m)
{
    const float *e = &m->_11;
    int column;
    for (column = 0; column < 4; ++column) out[column] = p[0] * e[column] + p[1] * e[4 + column] + p[2] * e[8 + column] + e[12 + column];
}

static void Rotate3(float *out, const float *v, const float *m9)
{
    int column;
    for (column = 0; column < 3; ++column) out[column] = v[0] * m9[column] + v[1] * m9[3 + column] + v[2] * m9[6 + column];
}

static float Dot(const float *a, const float *b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

static void Normalize(float *v)
{
    const float length = sqrtf(Dot(v, v));
    if (length > 0.0f) { v[0] /= length; v[1] /= length; v[2] /= length; }
}

/* The matrix normals go through: the transpose of the inverse of the upper
   3x3 of world times view. For a singular matrix the 3x3 itself. */
static void NormalMatrix(float *out9, const D3DMATRIX *m)
{
    const float a = m->_11, b = m->_12, c = m->_13, d = m->_21, e = m->_22, f = m->_23, g = m->_31, h = m->_32, i = m->_33;
    const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (det == 0.0f)
    {
        out9[0] = a; out9[1] = b; out9[2] = c; out9[3] = d; out9[4] = e; out9[5] = f; out9[6] = g; out9[7] = h; out9[8] = i;
        return;
    }
    /* The cofactors, divided by the determinant: inverse transposed. */
    out9[0] = (e * i - f * h) / det; out9[1] = (f * g - d * i) / det; out9[2] = (d * h - e * g) / det;
    out9[3] = (c * h - b * i) / det; out9[4] = (a * i - c * g) / det; out9[5] = (b * g - a * h) / det;
    out9[6] = (b * f - c * e) / det; out9[7] = (c * d - a * f) / det; out9[8] = (a * e - b * d) / det;
}

static void ColorOf(float *out, DWORD argb)
{
    out[0] = (float)((argb >> 16) & 255) / 255.0f;
    out[1] = (float)((argb >> 8) & 255) / 255.0f;
    out[2] = (float)(argb & 255) / 255.0f;
    out[3] = (float)(argb >> 24) / 255.0f;
}

static DWORD Channel(float value)
{
    if (!(value > 0.0f)) return 0;
    if (value >= 1.0f) return 255;
    return (DWORD)(value * 255.0f + 0.5f);
}

static DWORD Pack(const float *rgba) { return (Channel(rgba[3]) << 24) | (Channel(rgba[0]) << 16) | (Channel(rgba[1]) << 8) | Channel(rgba[2]); }

static DWORD ReadColor(const D3DDP_PTRSTRIDE *element, DWORD index)
{
    DWORD value;
    memcpy(&value, (const BYTE *)element->lpvData + (size_t)index * element->dwStride, 4);
    return value;
}

/* The colour a material source names for one vertex: the material, the
   diffuse or the specular colour of the vertex; a colour the vertex does not
   have is the material's. */
static void Source(float *out, DWORD source, BOOL color_vertex, const D3DCOLORVALUE *material, const float *diffuse, const float *specular)
{
    const float *from = (const float *)material;
    if (color_vertex && source == D3DMCS_COLOR1 && diffuse) from = diffuse;
    else if (color_vertex && source == D3DMCS_COLOR2 && specular) from = specular;
    memcpy(out, from, 16);
}

/* The lighting of Direct3D 7 in camera space for one vertex: ambient,
   diffuse and specular of every enabled light. */
static void Light(const D3dDevice *device, const float *position, const float *normal, const float *vertex_diffuse,
                  const float *vertex_specular, float *diffuse_out, float *specular_out)
{
    const DWORD *rs = device->state.render;
    const D3DMATERIAL7 *material = &device->state.material;
    const D3DMATRIX *view = &device->state.matrix[D3D_MATRIX_VIEW];
    const BOOL color_vertex = rs[D3DRENDERSTATE_COLORVERTEX] != 0;
    float md[4], ma[4], ms[4], me[4], ambient[4], total[3] = {0, 0, 0}, shine[3] = {0, 0, 0}, viewer[3] = {0, 0, -1};
    float view9[9] = {view->_11, view->_12, view->_13, view->_21, view->_22, view->_23, view->_31, view->_32, view->_33};
    DWORD n;
    int k;
    Source(md, rs[D3DRENDERSTATE_DIFFUSEMATERIALSOURCE], color_vertex, &material->diffuse, vertex_diffuse, vertex_specular);
    Source(ma, rs[D3DRENDERSTATE_AMBIENTMATERIALSOURCE], color_vertex, &material->ambient, vertex_diffuse, vertex_specular);
    Source(ms, rs[D3DRENDERSTATE_SPECULARMATERIALSOURCE], color_vertex, &material->specular, vertex_diffuse, vertex_specular);
    Source(me, rs[D3DRENDERSTATE_EMISSIVEMATERIALSOURCE], color_vertex, &material->emissive, vertex_diffuse, vertex_specular);
    ColorOf(ambient, rs[D3DRENDERSTATE_AMBIENT]);
    if (rs[D3DRENDERSTATE_LOCALVIEWER])
    {
        viewer[0] = -position[0]; viewer[1] = -position[1]; viewer[2] = -position[2];
        Normalize(viewer);
    }
    for (n = 0; n < device->light_count; ++n)
    {
        const D3dLight *entry = &device->lights[n];
        const D3DLIGHT7 *light = &entry->data;
        float to_light[3], attenuation = 1.0f, spot = 1.0f, cosine;
        if (!entry->enabled) continue;
        if (light->dltType == D3DLIGHT_DIRECTIONAL)
        {
            Rotate3(to_light, &light->dvDirection.x, view9);
            to_light[0] = -to_light[0]; to_light[1] = -to_light[1]; to_light[2] = -to_light[2];
            Normalize(to_light);
        }
        else
        {
            float at[4], distance;
            Transform4(at, &light->dvPosition.x, view);
            to_light[0] = at[0] - position[0]; to_light[1] = at[1] - position[1]; to_light[2] = at[2] - position[2];
            distance = sqrtf(Dot(to_light, to_light));
            if (distance > light->dvRange) continue;
            Normalize(to_light);
            attenuation = light->dvAttenuation0 + light->dvAttenuation1 * distance + light->dvAttenuation2 * distance * distance;
            attenuation = attenuation > 0.0f ? 1.0f / attenuation : 0.0f;
            if (light->dltType == D3DLIGHT_SPOT)
            {
                float axis[3], rho;
                const float inner = cosf(light->dvTheta * 0.5f), outer = cosf(light->dvPhi * 0.5f);
                Rotate3(axis, &light->dvDirection.x, view9);
                Normalize(axis);
                rho = -Dot(to_light, axis);
                if (rho <= outer) spot = 0.0f;
                else if (rho < inner && inner > outer) spot = powf((rho - outer) / (inner - outer), light->dvFalloff);
            }
        }
        attenuation *= spot;
        cosine = Dot(normal, to_light);
        for (k = 0; k < 3; ++k)
        {
            const float *ld = &light->dcvDiffuse.r, *la = &light->dcvAmbient.r;
            total[k] += ma[k] * la[k] * attenuation;
            if (cosine > 0.0f) total[k] += md[k] * ld[k] * cosine * attenuation;
        }
        if (rs[D3DRENDERSTATE_SPECULARENABLE] && cosine > 0.0f)
        {
            float half[3] = {to_light[0] + viewer[0], to_light[1] + viewer[1], to_light[2] + viewer[2]}, gloss;
            Normalize(half);
            gloss = Dot(normal, half);
            if (gloss > 0.0f)
            {
                const float *ls = &light->dcvSpecular.r;
                gloss = powf(gloss, material->power) * attenuation;
                for (k = 0; k < 3; ++k) shine[k] += ms[k] * ls[k] * gloss;
            }
        }
    }
    for (k = 0; k < 3; ++k)
    {
        diffuse_out[k] = me[k] + ma[k] * ambient[k] + total[k];
        specular_out[k] = shine[k];
    }
    diffuse_out[3] = md[3];
    specular_out[3] = 0.0f;
}

/* ProcessVertices: untransformed vertices of the source through world, view,
   projection and viewport into the transformed vertices of the destination,
   lit when the operation says so and lighting is on. The destination keeps
   the colours and texture coordinates both formats have. Vertex blending
   transforms through each selected world matrix before applying its weight. */
HRESULT D3dProcessVertices(D3dDevice *device, DWORD operation, BYTE *dest, DWORD dest_fvf, const D3DDRAWPRIMITIVESTRIDEDDATA *source,
                           DWORD source_fvf, DWORD count, DWORD flags)
{
    D3dLayout in, out;
    D3DMATRIX world_view[4], all[4];
    const D3DVIEWPORT7 *viewport;
    float normal9[9];
    DWORD blend, matrix;
    BOOL light;
    DWORD i, set, clip_union = 0, clip_intersection = 0x3f;
    D3DCLIPSTATUS *extents;
    (void)flags;
    if (!device || !(operation & D3DVOP_TRANSFORM)) return DDERR_INVALIDPARAMS;
    if (!D3dLayoutOf(source_fvf, &in) || !D3dLayoutOf(dest_fvf, &out)) return D3DERR_INVALIDVERTEXFORMAT;
    if (in.transformed || !out.transformed) return DDERR_INVALIDPARAMS;
    if (!source->position.lpvData) return DDERR_INVALIDPARAMS;
    viewport = &device->state.viewport;
    extents = &device->clip_status;
    blend = device->state.render[D3DRENDERSTATE_VERTEXBLEND];
    if (blend > 3 || blend > in.weights) return DDERR_INVALIDPARAMS;
    for (matrix = 0; matrix <= blend; ++matrix)
    {
        D3dMultiply(&world_view[matrix], &device->state.matrix[D3D_MATRIX_WORLD + matrix], &device->state.matrix[D3D_MATRIX_VIEW]);
        D3dMultiply(&all[matrix], &world_view[matrix], &device->state.matrix[D3D_MATRIX_PROJECTION]);
        if (!matrix) NormalMatrix(normal9, &world_view[0]);
    }
    light = (operation & D3DVOP_LIGHT) && device->state.render[D3DRENDERSTATE_LIGHTING] && in.normal >= 0 && source->normal.lpvData;
    for (i = 0; i < count; ++i, dest += out.stride)
    {
        float position[3], clip[4], screen[4], rhw, weights[4] = {1, 0, 0, 0};
        DWORD component, clipped = 0;
        DWORD diffuse = 0xffffffffu, specular = 0;
        const BOOL has_diffuse = in.diffuse >= 0 && source->diffuse.lpvData, has_specular = in.specular >= 0 && source->specular.lpvData;
        memcpy(position, (const BYTE *)source->position.lpvData + (size_t)i * source->position.dwStride, 12);
        if (blend)
        {
            float remaining = 1.0f;
            memcpy(weights, (const BYTE *)source->position.lpvData + (size_t)i * source->position.dwStride + 12, blend * 4);
            for (matrix = 0; matrix < blend; ++matrix) remaining -= weights[matrix];
            weights[blend] = remaining;
        }
        Transform4(clip, position, &all[0]);
        if (blend)
        {
            for (component = 0; component < 4; ++component) clip[component] *= weights[0];
            for (matrix = 1; matrix <= blend; ++matrix)
            {
                float part[4];
                Transform4(part, position, &all[matrix]);
                for (component = 0; component < 4; ++component) clip[component] += weights[matrix] * part[component];
            }
        }
        rhw = clip[3] != 0.0f ? 1.0f / clip[3] : 1.0f;
        screen[0] = (float)viewport->dwX + (clip[0] * rhw + 1.0f) * (float)viewport->dwWidth * 0.5f;
        screen[1] = (float)viewport->dwY + (1.0f - clip[1] * rhw) * (float)viewport->dwHeight * 0.5f;
        screen[2] = viewport->dvMinZ + clip[2] * rhw * (viewport->dvMaxZ - viewport->dvMinZ);
        screen[3] = rhw;
        if (operation & D3DVOP_CLIP)
        {
            if (clip[0] < -clip[3]) clipped |= 1;
            if (clip[0] > clip[3]) clipped |= 2;
            if (clip[1] > clip[3]) clipped |= 4;
            if (clip[1] < -clip[3]) clipped |= 8;
            if (clip[2] < 0) clipped |= 16;
            if (clip[2] > clip[3]) clipped |= 32;
            clip_union |= clipped;
            clip_intersection &= clipped;
        }
        if ((operation & D3DVOP_EXTENTS) && !clipped)
        {
            if (screen[0] < extents->minx) extents->minx = screen[0];
            if (screen[0] > extents->maxx) extents->maxx = screen[0];
            if (screen[1] < extents->miny) extents->miny = screen[1];
            if (screen[1] > extents->maxy) extents->maxy = screen[1];
        }
        /* Original D3D7 leaves clipped vertices in homogeneous coordinates,
           with X/Y mapped to [0,W]. It stores W, not its reciprocal. */
        if (clipped)
        {
            screen[0] = (clip[0] + clip[3]) * 0.5f;
            screen[1] = (clip[1] + clip[3]) * 0.5f;
            screen[2] = clip[2];
            screen[3] = clip[3];
        }
        memcpy(dest, screen, 16);
        if (has_diffuse) diffuse = ReadColor(&source->diffuse, i);
        if (has_specular) specular = ReadColor(&source->specular, i);
        if (light)
        {
            float camera[4], model_normal[3], normal[3], vd[4], vs[4], lit[4], shine[4];
            Transform4(camera, position, &world_view[0]);
            memcpy(model_normal, (const BYTE *)source->normal.lpvData + (size_t)i * source->normal.dwStride, 12);
            Rotate3(normal, model_normal, normal9);
            if (blend)
            {
                for (component = 0; component < 4; ++component) camera[component] *= weights[0];
                /* Original D3D7 ProcessVerticesStrided weights only WORLD's
                   normal; WORLD1..3 affect position, not this normal. The
                   following NORMALIZENORMALS step also acts on that result. */
                for (component = 0; component < 3; ++component) normal[component] *= weights[0];
                for (matrix = 1; matrix <= blend; ++matrix)
                {
                    float part[4];
                    Transform4(part, position, &world_view[matrix]);
                    for (component = 0; component < 4; ++component) camera[component] += weights[matrix] * part[component];
                }
            }
            if (device->state.render[D3DRENDERSTATE_NORMALIZENORMALS]) Normalize(normal);
            ColorOf(vd, diffuse);
            ColorOf(vs, specular);
            Light(device, camera, normal, has_diffuse ? vd : NULL, has_specular ? vs : NULL, lit, shine);
            diffuse = Pack(lit);
            specular = (Pack(shine) & 0x00ffffffu) | (specular & 0xff000000u);
        }
        if (out.diffuse >= 0) memcpy(dest + out.diffuse, &diffuse, 4);
        if (out.specular >= 0) memcpy(dest + out.specular, &specular, 4);
        for (set = 0; set < out.texture_sets; ++set)
        {
            const DWORD bytes = out.texture_floats[set] * 4;
            memset(dest + out.texture[set], 0, bytes);
            if (set < in.texture_sets && source->textureCoords[set].lpvData)
            {
                const DWORD have = in.texture_floats[set] * 4;
                memcpy(dest + out.texture[set], (const BYTE *)source->textureCoords[set].lpvData + (size_t)i * source->textureCoords[set].dwStride,
                       have < bytes ? have : bytes);
            }
        }
    }
    if ((operation & D3DVOP_CLIP) && count)
        extents->dwStatus = (extents->dwStatus & ~0x3f000u) | (extents->dwStatus & (clip_intersection << 12)) | clip_union;
    if (operation & D3DVOP_EXTENTS)
    {
        /* The original expands the accumulated 2D bounds once per call,
           even if every vertex was clipped, then limits them to the viewport. */
        extents->minx = fmaxf((float)viewport->dwX, extents->minx - 1.0f);
        extents->maxx = fminf((float)(viewport->dwX + viewport->dwWidth), extents->maxx + 1.0f);
        extents->miny = fmaxf((float)viewport->dwY, extents->miny - 1.0f);
        extents->maxy = fminf((float)(viewport->dwY + viewport->dwHeight), extents->maxy + 1.0f);
    }
    return D3D_OK;
}

/* ComputeSphereVisibility: every sphere against the six planes of the view
   frustum, taken out of world times view times projection in model space.
   The Win98 D3D7 runtime returns clip-union bits 0..5 and clip-intersection
   bits 12..17, not the D3DVIS_* pairs declared in the SDK headers. */
HRESULT D3dSphereVisibility(D3dDevice *device, const D3DVECTOR *centers, const D3DVALUE *radii, DWORD count, DWORD *results)
{
    D3DMATRIX all;
    const float *m;
    float planes[6][4];
    DWORD i;
    int p, k;
    D3dMultiply(&all, &device->state.matrix[D3D_MATRIX_WORLD], &device->state.matrix[D3D_MATRIX_VIEW]);
    D3dMultiply(&all, &all, &device->state.matrix[D3D_MATRIX_PROJECTION]);
    m = &all._11;
    for (k = 0; k < 4; ++k)
    {
        const float x = m[k * 4], y = m[k * 4 + 1], z = m[k * 4 + 2], w = m[k * 4 + 3];
        planes[0][k] = w + x;   /* left: x >= -w */
        planes[1][k] = w - x;
        planes[2][k] = w - y;   /* top: y <= w */
        planes[3][k] = w + y;
        planes[4][k] = z;       /* near: z >= 0 */
        planes[5][k] = w - z;
    }
    for (p = 0; p < 6; ++p)
    {
        const float length = sqrtf(Dot(planes[p], planes[p]));
        if (length > 0.0f)
            for (k = 0; k < 4; ++k) planes[p][k] /= length;
    }
    for (i = 0; i < count; ++i)
    {
        DWORD flags = 0;
        for (p = 0; p < 6; ++p)
        {
            const float distance = Dot(planes[p], &centers[i].x) + planes[p][3];
            if (distance < radii[i]) flags |= 1u << p;
            if (distance < -radii[i]) flags |= 1u << (p + 12);
        }
        results[i] = flags;
    }
    return D3D_OK;
}
