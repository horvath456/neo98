/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Guest side of tools/test_opengl_guest.py: a Win32 program that draws a
   turning pyramid with OpenGL 1.1 through the original OPENGL32.DLL.

   A 640x480 popup window at the top left corner, ChoosePixelFormat and
   SetPixelFormat for an RGBA format with a 16-bit depth buffer,
   wglCreateContext and wglMakeCurrent, glFrustum, glTranslatef and
   glRotatef, glClear, glBegin(GL_TRIANGLES) with one colour per face,
   depth test and back face culling, SwapBuffers. Lighting is off, so every
   face keeps its colour, and the test can check exact colours in the host
   image.

   Without SINGLE the format is double buffered and each frame ends in
   SwapBuffers; with SINGLE it draws straight into the window and ends each
   frame in glFinish, which is the span path of the generic implementation.
   ARRAYS draws the same faces with glDrawArrays out of client arrays.
   TEXTURED draws a four-colour quad with a translated texture matrix.
   Without INTERACTIVE the program draws a fixed number of frames, ends on a
   known angle and waits in GetMessage, so the screenshot shows the last
   frame. With INTERACTIVE it turns until Escape or the close box. Every
   answer goes out through OutputDebugStringA as "GLPYR name=value". */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void OutputDebugStringA(const char *);
API void ExitProcess(U);
API U RegisterClassA(const void *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, void *);
API U DefWindowProcA(U, U, U, U);
API U ShowWindow(U, int);
API U SetForegroundWindow(U);
API U GetDC(U);
API U GetMessageA(void *, U, U, U);
API U PeekMessageA(void *, U, U, U, U);
API U TranslateMessage(const void *);
API U DispatchMessageA(const void *);
API void PostQuitMessage(int);
API short GetAsyncKeyState(int);
API int ChoosePixelFormat(U, const void *);
API U SetPixelFormat(U, int, const void *);
API int DescribePixelFormat(U, int, U, void *);
API U SwapBuffers(U);
API U wglCreateContext(U);
API U wglMakeCurrent(U, U);
API U wglDeleteContext(U);
API void *wglGetProcAddress(const char *);
#ifdef MULTITEXTURE
typedef void (__stdcall *SelectTexture)(U);
typedef void (__stdcall *MultiCoord)(U, float, float);
static SelectTexture active_texture, client_texture;
static MultiCoord multi_coord;
static void coord(float, float);
#endif
API void glClearColor(float, float, float, float);
API void glClear(U);
API void glEnable(U);
API void glDisable(U);
API void glDepthFunc(U);
API void glCullFace(U);
API void glFrontFace(U);
API void glShadeModel(U);
API void glViewport(int, int, int, int);
API void glMatrixMode(U);
API void glLoadIdentity(void);
API void glFrustum(double, double, double, double, double, double);
API void glTranslatef(float, float, float);
API void glRotatef(float, float, float, float);
API void glBegin(U);
API void glEnd(void);
API void glColor3ub(unsigned char, unsigned char, unsigned char);
API void glVertex3f(float, float, float);
API void glEnableClientState(U);
API void glVertexPointer(int, U, int, const void *);
API void glColorPointer(int, U, int, const void *);
API void glDrawArrays(U, int, int);
API void glBindTexture(U, U);
API void glTexParameteri(U, U, int);
API void glTexImage2D(U, int, int, int, int, int, U, U, const void *);
API void glTexCoord2f(float, float);
API void glFinish(void);
API U glGetError(void);
API const char *glGetString(U);
API void glGetFloatv(U, float *);

#ifdef MULTITEXTURE
static void coord(float u, float v) { glTexCoord2f(u, v); multi_coord(0x84c1, u, v); }
#else
#define coord glTexCoord2f
#endif

/* The MSVC target wants this symbol from any program that uses floating point. */
int _fltused = 1;

static char line[256];
static void number(const char *label, U value)
{
    static const char digits[] = "0123456789abcdef";
    U at = 0, i = 0;
    while (label[i]) line[at++] = label[i++];
    line[at++] = '=';
    for (i = 0; i < 8; ++i) line[at++] = digits[(value >> (28 - i * 4)) & 15];
    line[at] = 0;
    OutputDebugStringA(line);
}
static void text(const char *label, const char *value)
{
    U at = 0, i = 0;
    while (label[i]) line[at++] = label[i++];
    line[at++] = '=';
    for (i = 0; value && value[i] && at < sizeof line - 1; ++i) line[at++] = value[i] == ' ' ? '_' : value[i];
    line[at] = 0;
    OutputDebugStringA(line);
}
static void fail(U code) { number("GLPYR fail", code); ExitProcess(code); }

/* The pyramid: apex at the top, a square base, four coloured sides, each
   counter-clockwise seen from outside, which is the front side of OpenGL.
   Front (z=+1) red, right (x=+1) green, back (z=-1) blue, left (x=-1)
   yellow. The base is left out; the camera looks from above. */
static const float faces[4][3][3] = {
    {{0, 1, 0}, {-1, -1, 1}, {1, -1, 1}},
    {{0, 1, 0}, {1, -1, 1}, {1, -1, -1}},
    {{0, 1, 0}, {1, -1, -1}, {-1, -1, -1}},
    {{0, 1, 0}, {-1, -1, -1}, {-1, -1, 1}},
};
static const unsigned char colors[4][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 0}};
#ifdef ARRAYS
static float vertex_array[12][3];
static unsigned char color_array[12][4];
#endif

static void draw(float angle)
{
    int f, v;
    glClear(0x4100);   /* GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT */
    glMatrixMode(0x1700);   /* GL_MODELVIEW */
    glLoadIdentity();
#ifdef TEXTURED
    (void)angle;
    glColor3ub(255, 255, 255);
    glBegin(0x0007);   /* GL_QUADS */
    coord(0, 0); glVertex3f(-0.8f, -0.8f, 0);
    coord(1, 0); glVertex3f(0.8f, -0.8f, 0);
    coord(1, 1); glVertex3f(0.8f, 0.8f, 0);
    coord(0, 1); glVertex3f(-0.8f, 0.8f, 0);
    glEnd();
#else
    /* The eye at (0, 1.2, 4) looking at the origin. */
    glTranslatef(0.0f, 0.0f, -4.176123f);
    glRotatef(16.69924f, 1.0f, 0.0f, 0.0f);
    glRotatef(angle, 0.0f, 1.0f, 0.0f);
#ifdef ARRAYS
    glDrawArrays(0x0004, 0, 12);
#else
    glBegin(0x0004);   /* GL_TRIANGLES */
    for (f = 0; f < 4; ++f)
    {
        glColor3ub(colors[f][0], colors[f][1], colors[f][2]);
        for (v = 0; v < 3; ++v)
            glVertex3f(faces[f][v][0], faces[f][v][1], faces[f][v][2]);
    }
    glEnd();
#endif
#endif
    (void)f; (void)v;
}

static U message[7];

static U __stdcall window_procedure(U window, U msg, U wp, U lp)
{
    if (msg == 0x0010 || msg == 0x0002) { PostQuitMessage(0); return 0; }   /* WM_CLOSE, WM_DESTROY */
    return DefWindowProcA(window, msg, wp, lp);
}

/* A straight run of 600 x87 instructions without a jump, like the matrix
   code of the KIDS200 games. Unicorn's i386 translator leaves
   temporaries behind on each of them; previously, such a block overran
   the translator's 512 and the host crashed. 600 exchanges keep the order,
   so the stack ends as it started: 2 on top of 1. Answers 0x21. */
static U x87_run(void)
{
    int top, below;
    __asm__ volatile("fld1\n\tfld1\n\tfadd %%st(0), %%st(0)\n\t"
                     ".rept 600\n\tfxch %%st(1)\n\t.endr\n\t"
                     "fistpl %0\n\tfistpl %1"
                     : "=m"(top), "=m"(below));
    return (U)(top << 4 | below);
}

void __stdcall mainCRTStartup(void)
{
    U window, dc, context, i, frame;
    U class_fields[10];
    U pfd[10];   /* PIXELFORMATDESCRIPTOR, 40 bytes */
    unsigned char *p = (unsigned char *)pfd;
    int format;
    float angle;

    number("GLPYR x87", x87_run());
    for (i = 0; i < 10; ++i) class_fields[i] = 0;
    class_fields[0] = 0x20;   /* CS_OWNDC */
    class_fields[1] = (U)window_procedure;
    class_fields[9] = (U) "GLPyramid";
    RegisterClassA(class_fields);
    /* WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS */
    window = CreateWindowExA(0, "GLPyramid", "OpenGL Pyramide", 0x96000000u, 0, 0, 640, 480, 0, 0, 0, 0);
    number("GLPYR window", window ? 1 : 0);
    if (!window) fail(1);
    ShowWindow(window, 5);
    SetForegroundWindow(window);
    dc = GetDC(window);

    for (i = 0; i < 10; ++i) pfd[i] = 0;
    p[0] = 40; p[2] = 1;   /* nSize, nVersion */
#ifdef SINGLE
    pfd[1] = 0x4 | 0x20;   /* PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL */
#else
    pfd[1] = 0x4 | 0x20 | 0x1;   /* | PFD_DOUBLEBUFFER */
#endif
    p[8] = 0;    /* PFD_TYPE_RGBA */
    p[9] = 32;   /* cColorBits */
    p[23] = 16;  /* cDepthBits */
    format = ChoosePixelFormat(dc, pfd);
    number("GLPYR format", (U)format);
    if (format <= 0) fail(2);
    number("GLPYR setformat", SetPixelFormat(dc, format, pfd));
    DescribePixelFormat(dc, format, 40, pfd);
    number("GLPYR flags", pfd[1]);
    context = wglCreateContext(dc);
    number("GLPYR context", context ? 1 : 0);
    if (!context) fail(3);
    number("GLPYR current", wglMakeCurrent(dc, context));
    text("GLPYR vendor", glGetString(0x1f00));
    text("GLPYR renderer", glGetString(0x1f01));
    text("GLPYR version", glGetString(0x1f02));
#ifdef BATCH
    /* More setters than fit in one queue, followed by a synchronous query. */
    {
        float color[4] = {0, 0, 0, 0};
        for (i = 0; i < 1200; ++i) glColor3ub((unsigned char)i, 37, 91);
        glGetFloatv(0x0b00, color);   /* GL_CURRENT_COLOR */
        number("GLPYR batch", color[0] > 0.685f && color[0] < 0.687f &&
               color[1] > 0.144f && color[1] < 0.146f && color[3] == 1.0f);
        {
            U other = wglCreateContext(dc);
            U good = other != 0;
            glColor3ub(17, 29, 43);
            good &= wglMakeCurrent(dc, other) != 0;
            glColor3ub(101, 103, 107);
            good &= wglMakeCurrent(dc, context) != 0;
            glGetFloatv(0x0b00, color);
            good &= color[0] > 0.066f && color[0] < 0.067f;
            good &= wglMakeCurrent(dc, other) != 0;
            glGetFloatv(0x0b00, color);
            good &= color[0] > 0.395f && color[0] < 0.397f;
            good &= wglMakeCurrent(dc, context) != 0;
            good &= wglDeleteContext(other) != 0;
            number("GLPYR batch_contexts", good);
        }
    }
#endif

    glViewport(0, 0, 640, 480);
    glMatrixMode(0x1701);   /* GL_PROJECTION */
    glLoadIdentity();
    /* 60 degrees vertical field of view, 4:3, near 1, far 100. */
    glFrustum(-0.7698004, 0.7698004, -0.5773503, 0.5773503, 1.0, 100.0);
    glClearColor(0.0f, 0.0f, 64.0f / 255.0f, 1.0f);
    glEnable(0x0b71);   /* GL_DEPTH_TEST */
    glDepthFunc(0x0203);   /* GL_LEQUAL */
    glEnable(0x0b44);   /* GL_CULL_FACE */
    glCullFace(0x0405);   /* GL_BACK */
    glFrontFace(0x0901);   /* GL_CCW */
    glShadeModel(0x1d01);   /* GL_SMOOTH */
#ifdef TEXTURED
    {
        static const unsigned char texels[16] = {
            255, 0, 0, 255, 0, 255, 0, 255,
            0, 0, 255, 255, 255, 255, 0, 255
        };
        glMatrixMode(0x1701);   /* GL_PROJECTION */
        glLoadIdentity();
        glMatrixMode(0x1702);   /* GL_TEXTURE, applied exactly once */
        glLoadIdentity();
        glTranslatef(0.5f, 0, 0);
        glBindTexture(0x0de1, 1);   /* GL_TEXTURE_2D */
        glTexParameteri(0x0de1, 0x2801, 0x2600);   /* min: GL_NEAREST */
        glTexParameteri(0x0de1, 0x2800, 0x2600);   /* mag: GL_NEAREST */
        glTexImage2D(0x0de1, 0, 0x1908, 2, 2, 0, 0x1908, 0x1401, texels);
        glEnable(0x0de1);
    }
#endif
#ifdef MULTITEXTURE
    {
        static const unsigned char lightmap[16] = {
            128,128,128,255, 64,64,64,255, 32,32,32,255, 192,192,192,255
        };
        active_texture = (SelectTexture)wglGetProcAddress("glActiveTextureARB");
        client_texture = (SelectTexture)wglGetProcAddress("glClientActiveTextureARB");
        multi_coord = (MultiCoord)wglGetProcAddress("glMultiTexCoord2fARB");
        if (!active_texture || !client_texture || !multi_coord) fail(4);
        active_texture(0x84c1);
        glMatrixMode(0x1702); glLoadIdentity();
        glBindTexture(0x0de1, 2);
        glTexParameteri(0x0de1, 0x2801, 0x2600);
        glTexParameteri(0x0de1, 0x2800, 0x2600);
        glTexImage2D(0x0de1, 0, 0x1908, 2, 2, 0, 0x1908, 0x1401, lightmap);
        glEnable(0x0de1);
        active_texture(0x84c0);
    }
#endif
#ifdef ARRAYS
    for (i = 0; i < 12; ++i)
    {
        vertex_array[i][0] = faces[i / 3][i % 3][0];
        vertex_array[i][1] = faces[i / 3][i % 3][1];
        vertex_array[i][2] = faces[i / 3][i % 3][2];
        color_array[i][0] = colors[i / 3][0];
        color_array[i][1] = colors[i / 3][1];
        color_array[i][2] = colors[i / 3][2];
        color_array[i][3] = 255;
    }
    glEnableClientState(0x8074);   /* GL_VERTEX_ARRAY */
    glEnableClientState(0x8076);   /* GL_COLOR_ARRAY */
    glVertexPointer(3, 0x1406, 0, vertex_array);   /* GL_FLOAT */
    glColorPointer(4, 0x1401, 0, color_array);   /* GL_UNSIGNED_BYTE */
#endif

#ifdef INTERACTIVE
    angle = 0.0f;
    for (;;)
    {
        while (PeekMessageA(message, 0, 0, 0, 1))
        {
            if (message[1] == 0x0012) ExitProcess(0);   /* WM_QUIT */
            TranslateMessage(message);
            DispatchMessageA(message);
        }
        if (GetAsyncKeyState(0x1b) & 0x8000) ExitProcess(0);
        draw(angle);
        angle += 1.0f;
#ifdef SINGLE
        glFinish();
#else
        SwapBuffers(dc);
#endif
    }
#else
    for (frame = 0; frame <= 60; ++frame)
    {
        while (PeekMessageA(message, 0, 0, 0, 1))
        {
            TranslateMessage(message);
            DispatchMessageA(message);
        }
        angle = (float)frame * 0.5f;
        draw(angle);
#ifdef SINGLE
        glFinish();
#else
        SwapBuffers(dc);
#endif
    }
    number("GLPYR frames", frame - 1);
    number("GLPYR error", glGetError());
    number("GLPYR done", 1);
    while (GetMessageA(message, 0, 0, 0) > 0)
    {
        TranslateMessage(message);
        DispatchMessageA(message);
    }
    ExitProcess(0);
#endif
}
