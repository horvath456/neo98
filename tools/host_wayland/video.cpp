// SPDX-License-Identifier: GPL-2.0-or-later
// The GPU copy of Kodi's picture (video.h): EGL on GBM of the render node
// without a surface, the dmabuf as an external texture, drawn into a
// texture of the copy's size and read back with glReadPixels.
#include "video.h"
#include "compositor.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <gbm.h>
#include <drm_fourcc.h>
#include <algorithm>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <sys/eventfd.h>
#include <thread>
#include <unistd.h>
#include <vector>
namespace HostWayland
{
namespace
{
// The largest copy: a 4K Kodi window gives a 1080p picture.
constexpr unsigned MaxWidth = 1920, MaxHeight = 1080;

struct Job
{
    int fd = -1;
    uint32_t offset = 0, stride = 0, width = 0, height = 0, format = 0;
    uint64_t modifier = 0;
    unsigned serial = 0;
    // The part to copy, in buffer pixels, and the size of the copy.
    unsigned x = 0, y = 0, w = 0, h = 0, out_w = 0, out_h = 0;
};

// The part of a width x height picture the video fills, with its
// proportions, in the middle.
void Crop(unsigned width, unsigned height, double aspect, unsigned &x, unsigned &y, unsigned &w, unsigned &h)
{
    x = y = 0; w = width; h = height;
    if (aspect <= 0 || !width || !height) return;
    if (w > h * aspect) { const unsigned c = std::max(1u, unsigned(h * aspect + 0.5)); x = (w - c) / 2; w = c; }
    else { const unsigned c = std::max(1u, unsigned(w / aspect + 0.5)); y = (h - c) / 2; h = c; }
}
void Fit(unsigned w, unsigned h, unsigned &out_w, unsigned &out_h)
{
    out_w = w; out_h = h;
    if (out_w > MaxWidth) { out_h = std::max(1u, unsigned(uint64_t(out_h) * MaxWidth / out_w)); out_w = MaxWidth; }
    if (out_h > MaxHeight) { out_w = std::max(1u, unsigned(uint64_t(out_w) * MaxHeight / out_h)); out_h = MaxHeight; }
}

const char *VertexShader = R"(
attribute vec2 corner;
uniform vec4 part;
varying vec2 at;
void main() {
    at = part.xy + corner * part.zw;
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
)";
// Blue, green, red, alpha in the bytes glReadPixels writes: 0xFFRRGGBB on a
// little-endian host.
const char *FragmentShader = R"(
#extension GL_OES_EGL_image_external : require
precision mediump float;
uniform samplerExternalOES picture;
varying vec2 at;
void main() {
    vec4 c = texture2D(picture, at);
    gl_FragColor = vec4(c.b, c.g, c.r, 1.0);
}
)";
} // namespace

struct Video::State
{
    std::thread thread;
    std::mutex lock;
    std::condition_variable wake;
    bool stopping = false, started = false, failed = false;
    int event = -1;
    // Main thread: the buffer last handed over.
    unsigned fed = 0;
    // Shared: the job waiting or running, the buffers done with.
    Job job;
    bool busy = false;
    std::vector<unsigned> finished;
    // Shared: the newest picture.
    std::vector<uint32_t> pixels;
    unsigned width = 0, height = 0, serial = 0;
    // Main thread: a picture handed out last (Latest).
    std::vector<uint32_t> shown;

    // The copy thread's EGL and GL.
    int node = -1;
    gbm_device *gbm = nullptr;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    PFNEGLCREATEIMAGEKHRPROC create_image = nullptr;
    PFNEGLDESTROYIMAGEKHRPROC destroy_image = nullptr;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC target_texture = nullptr;
    bool modifiers = false;
    GLuint program = 0, source = 0, target = 0, framebuffer = 0, corners = 0;
    GLint part = -1;
    unsigned target_w = 0, target_h = 0;
    std::vector<uint32_t> copy;

    bool Open(const std::string &render_node, std::string &error);
    void Close();
    bool Copy(const Job &job);
    void Run();
    void Signal() { const uint64_t one = 1; if (event >= 0 && write(event, &one, sizeof one) < 0) {} }
};

bool Video::State::Open(const std::string &render_node, std::string &error)
{
    node = open(render_node.c_str(), O_RDWR | O_CLOEXEC);
    if (node < 0) { error = render_node + ": " + strerror(errno); return false; }
    gbm = gbm_create_device(node);
    if (!gbm) { error = "no GBM device on " + render_node; return false; }
    auto platform_display = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
    display = platform_display ? platform_display(EGL_PLATFORM_GBM_KHR, gbm, nullptr) : EGL_NO_DISPLAY;
    if (display == EGL_NO_DISPLAY || !eglInitialize(display, nullptr, nullptr)) { error = "EGL does not open on " + render_node; return false; }
    const char *extensions = eglQueryString(display, EGL_EXTENSIONS);
    auto has = [&](const char *name) {
        const size_t n = strlen(name);
        for (const char *at = extensions; at && (at = strstr(at, name)); at += n)
            if ((at == extensions || at[-1] == ' ') && (at[n] == ' ' || at[n] == 0)) return true;
        return false;
    };
    if (!has("EGL_EXT_image_dma_buf_import") || !has("EGL_KHR_surfaceless_context")) { error = "EGL cannot import dmabufs without a surface"; return false; }
    modifiers = has("EGL_EXT_image_dma_buf_import_modifiers");
    create_image = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(eglGetProcAddress("eglCreateImageKHR"));
    destroy_image = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(eglGetProcAddress("eglDestroyImageKHR"));
    target_texture = reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(eglGetProcAddress("glEGLImageTargetTexture2DOES"));
    if (!create_image || !destroy_image || !target_texture) { error = "EGL has no EGLImage functions"; return false; }
    if (!eglBindAPI(EGL_OPENGL_ES_API)) { error = "EGL has no OpenGL ES"; return false; }
    const EGLint attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    context = eglCreateContext(display, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attributes);
    if (context == EGL_NO_CONTEXT || !eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context))
    { error = "no OpenGL ES context"; return false; }
    auto shader = [&](GLenum kind, const char *text) {
        GLuint s = glCreateShader(kind);
        glShaderSource(s, 1, &text, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) { char log[512] = {}; glGetShaderInfoLog(s, sizeof log, nullptr, log); error = std::string("shader: ") + log; }
        return s;
    };
    const GLuint vertex = shader(GL_VERTEX_SHADER, VertexShader), fragment = shader(GL_FRAGMENT_SHADER, FragmentShader);
    if (!error.empty()) return false;
    program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glBindAttribLocation(program, 0, "corner");
    glLinkProgram(program);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) { error = "the copy program does not link"; return false; }
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "picture"), 0);
    part = glGetUniformLocation(program, "part");
    static const GLfloat quad[] = {0, 0, 1, 0, 0, 1, 1, 1};
    glGenBuffers(1, &corners);
    glBindBuffer(GL_ARRAY_BUFFER, corners);
    glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(0);
    glGenTextures(1, &source);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, source);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, &framebuffer);
    glGenTextures(1, &target);
    if (glGetError() != GL_NO_ERROR) { error = "OpenGL ES failed to set up the copy"; return false; }
    fprintf(stderr, "NEO98 HOST video GPU copy on %s: %s%s\n", render_node.c_str(), reinterpret_cast<const char *>(glGetString(GL_RENDERER)),
            modifiers ? "" : " (without modifiers)");
    return true;
}

void Video::State::Close()
{
    if (display != EGL_NO_DISPLAY)
    {
        if (context != EGL_NO_CONTEXT)
        {
            glDeleteTextures(1, &source); glDeleteTextures(1, &target);
            glDeleteFramebuffers(1, &framebuffer); glDeleteBuffers(1, &corners);
            glDeleteProgram(program);
            eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            eglDestroyContext(display, context);
        }
        eglTerminate(display);
    }
    if (gbm) gbm_device_destroy(gbm);
    if (node >= 0) close(node);
    display = EGL_NO_DISPLAY; context = EGL_NO_CONTEXT; gbm = nullptr; node = -1;
}

bool Video::State::Copy(const Job &j)
{
    std::vector<EGLint> attributes = {EGL_WIDTH, EGLint(j.width), EGL_HEIGHT, EGLint(j.height), EGL_LINUX_DRM_FOURCC_EXT, EGLint(j.format),
                                      EGL_DMA_BUF_PLANE0_FD_EXT, j.fd, EGL_DMA_BUF_PLANE0_OFFSET_EXT, EGLint(j.offset),
                                      EGL_DMA_BUF_PLANE0_PITCH_EXT, EGLint(j.stride)};
    if (modifiers && j.modifier != DRM_FORMAT_MOD_INVALID)
    {
        attributes.insert(attributes.end(), {EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT, EGLint(j.modifier & 0xffffffff),
                                             EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT, EGLint(j.modifier >> 32)});
    }
    attributes.push_back(EGL_NONE);
    EGLImageKHR image = create_image(display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, attributes.data());
    if (image == EGL_NO_IMAGE_KHR) { fprintf(stderr, "NEO98 HOST video dmabuf not imported (EGL 0x%x)\n", eglGetError()); return false; }
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, source);
    target_texture(GL_TEXTURE_EXTERNAL_OES, image);
    if (j.out_w != target_w || j.out_h != target_h)
    {
        glBindTexture(GL_TEXTURE_2D, target);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, GLsizei(j.out_w), GLsizei(j.out_h), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
        target_w = j.out_w; target_h = j.out_h;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, GLsizei(j.out_w), GLsizei(j.out_h));
    // Rows of the copy from the top: the buffer's first row lands in the
    // first row glReadPixels returns.
    glUniform4f(part, float(j.x) / j.width, float(j.y) / j.height, float(j.w) / j.width, float(j.h) / j.height);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    copy.resize(size_t(j.out_w) * j.out_h);
    glReadPixels(0, 0, GLsizei(j.out_w), GLsizei(j.out_h), GL_RGBA, GL_UNSIGNED_BYTE, copy.data());
    destroy_image(display, image);
    return glGetError() == GL_NO_ERROR;
}

void Video::State::Run()
{
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context);
    for (;;)
    {
        Job j;
        {
            std::unique_lock<std::mutex> guard(lock);
            wake.wait(guard, [&] { return stopping || (busy && job.fd >= 0); });
            if (stopping) break;
            j = job;
        }
        const bool okay = Copy(j);
        close(j.fd);
        {
            std::lock_guard<std::mutex> guard(lock);
            job.fd = -1;
            busy = false;
            finished.push_back(j.serial);
            if (okay) { pixels.swap(copy); width = j.out_w; height = j.out_h; serial = j.serial; }
        }
        Signal();
    }
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

Video::Video() : state(std::make_unique<State>()) {}
Video::~Video()
{
    State &s = *state;
    if (s.thread.joinable())
    {
        { std::lock_guard<std::mutex> guard(s.lock); s.stopping = true; }
        s.wake.notify_all();
        s.thread.join();
    }
    if (s.job.fd >= 0) close(s.job.fd);
    s.Close();
    if (s.event >= 0) close(s.event);
}

bool Video::Start(const std::string &render_node, std::string &error)
{
    State &s = *state;
    s.event = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (render_node.empty()) { error = "no render node"; return false; }
    if (!s.Open(render_node, error)) { s.Close(); s.failed = true; return false; }
    // The context belongs to the copy thread from now on.
    eglMakeCurrent(s.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    s.started = true;
    s.thread = std::thread([&s] { s.Run(); });
    return true;
}

int Video::Fd() const { return state->event; }

void Video::Feed(Compositor &compositor, double aspect, bool wanted)
{
    State &s = *state;
    if (s.event >= 0) { uint64_t count; if (read(s.event, &count, sizeof count) < 0) {} }
    std::vector<unsigned> done;
    bool busy;
    {
        std::lock_guard<std::mutex> guard(s.lock);
        done.swap(s.finished);
        busy = s.busy;
    }
    // Back to Kodi, unless it is the buffer the compositor still shows.
    for (unsigned serial : done) compositor.Unhold(serial);
    const unsigned newest = compositor.Serial();
    if (!newest)
    {
        // Kodi's window is gone: its last picture with it.
        std::lock_guard<std::mutex> guard(s.lock);
        s.pixels.clear(); s.serial = 0;
        return;
    }
    if (!wanted || newest == s.fed || busy) return;
    Scanout buffer;
    if (s.started && compositor.Buffer(buffer))
    {
        Job j;
        j.fd = fcntl(buffer.fd, F_DUPFD_CLOEXEC, 3);
        if (j.fd < 0) return;
        j.offset = buffer.offset; j.stride = buffer.stride; j.width = buffer.width; j.height = buffer.height;
        j.format = buffer.format; j.modifier = buffer.modifier; j.serial = buffer.serial;
        Crop(j.width, j.height, aspect, j.x, j.y, j.w, j.h);
        Fit(j.w, j.h, j.out_w, j.out_h);
        compositor.Hold(buffer.serial);
        s.fed = newest;
        {
            std::lock_guard<std::mutex> guard(s.lock);
            s.job = j;
            s.busy = true;
        }
        s.wake.notify_all();
        return;
    }
    // wl_shm, or no GPU: the compositor's own copy, cut here.
    s.fed = newest;
    const Picture *picture = compositor.Current();
    if (!picture || !picture->width || !picture->height) return;
    unsigned x, y, w, h;
    Crop(picture->width, picture->height, aspect, x, y, w, h);
    std::vector<uint32_t> cut(size_t(w) * h);
    for (unsigned row = 0; row < h; ++row)
    {
        const uint32_t *from = picture->pixels.data() + size_t(y + row) * picture->width + x;
        uint32_t *to = cut.data() + size_t(row) * w;
        for (unsigned i = 0; i < w; ++i) to[i] = from[i] | 0xff000000u;
    }
    std::lock_guard<std::mutex> guard(s.lock);
    s.pixels.swap(cut); s.width = w; s.height = h; s.serial = picture->serial;
}

bool Video::Latest(const std::function<void(const uint32_t *, unsigned, unsigned)> &use, unsigned &serial)
{
    State &s = *state;
    unsigned w, h;
    {
        std::lock_guard<std::mutex> guard(s.lock);
        if (!s.serial || s.serial == serial || s.pixels.empty()) return false;
        // The copy thread writes into another buffer meanwhile; the one
        // given back here is only read again after the next copy replaced it.
        s.shown.swap(s.pixels);
        w = s.width; h = s.height; serial = s.serial;
    }
    use(s.shown.data(), w, h);
    return true;
}

unsigned Video::Serial() const
{
    std::lock_guard<std::mutex> guard(state->lock);
    return state->serial;
}
}
