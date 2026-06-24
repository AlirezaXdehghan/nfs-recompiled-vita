#include <lib/renderer.h>
#include <lib/window.h>
#include <lib/gamepad.h>
#include <SDL3/SDL.h>
#if defined(__vita__)
/* vitaGL supplies the GL symbols and enums; the desktop SDL_opengl/GL headers
 * would clash with it, so pull in vitaGL alone on Vita. */
# include <vitaGL.h>
#else
# include <SDL3/SDL_opengl.h>
# include <GL/gl.h>
#endif

namespace win32
{

#if !defined(__vita__)
void GLAPIENTRY errorCallback(GLenum /*source*/, GLenum type, GLuint /*id*/, GLenum severity, GLsizei /*length*/, const GLchar* message, const void* /*userParam*/)
{
    if (severity != GL_DEBUG_SEVERITY_NOTIFICATION)
    SDL_LogError(SDL_LOG_CATEGORY_RENDER, "OpenGL: %s type = 0x%x, severity = 0x%x, \"%s\"\n",
            (type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : ""),
            type, severity, message);
}
PFNGLDEBUGMESSAGECALLBACKPROC glDebugMessageCallback;
#endif

Renderer::Renderer(WinApplication* application, Window *window)
    :   m_application(application)
    ,   m_window(window)
    ,   m_renderer(
#if defined(__vita__)
            nullptr /* vitaGL doesn't use SDL GL contexts */
#else
            SDL_GL_CreateContext(m_window->m_window)
#endif
        )
    ,   m_videoMemory(new MemMap(800*600*2*2)) // double buffer 16 bits 800x600
    ,   m_currentBuffer(0)
    ,   m_depth(16)
    ,   m_colorPalette()
    ,   m_screenData(nullptr)
    ,   m_screenDataSize(0)
    ,   m_lastPresentUsec(0)
{
    setCurrent();
#if defined(__vita__)
    static bool s_vglInitDone = false;
    if (!s_vglInitDone) {
        SDL_Log("[renderer] vglInit");
        vglInit(0x800000);
        vglUseExtraMem(GL_TRUE);
        s_vglInitDone = true;
    } else {
        SDL_Log("[renderer] vglInit skipped (already done)");
    }
    /* Open the Vita pad once, here, so it drives input from the very first menu
     * even before the game opens a DirectInput device. The function-static is
     * constructed once for the process (no leak, no double-open). It becomes the
     * keyboard-synthesis source (s_gp1) that updateKeys() polls each frame; any
     * DirectInput pad the game opens later reads the same physical joystick 0
     * independently, so the two coexist. Without this, a Vita (which has no
     * physical keyboard) could boot with no usable input at all. */
    SDL_Log("[renderer] skipping Gamepad on Vita (Vita3K debugging)");
#else
    SDL_GL_SetSwapInterval(1);
    glDebugMessageCallback = (PFNGLDEBUGMESSAGECALLBACKPROC)SDL_GL_GetProcAddress("glDebugMessageCallback");
    if (glDebugMessageCallback)
    {
        glEnable(GL_DEBUG_OUTPUT);
        glDebugMessageCallback(errorCallback, 0);
    }
#endif

    SDL_Log("[renderer] before glGenTextures");
    glGenTextures(1, &m_texture);
    SDL_Log("[renderer] before glBindTexture");
    glBindTexture(GL_TEXTURE_2D, m_texture);
    SDL_Log("[renderer] before glEnable");
    glEnable(GL_TEXTURE_2D);

    clearCurrent();
    SDL_Log("[renderer] constructor done");
}

Renderer::~Renderer()
{
#if !defined(__vita__)
    SDL_GL_DestroyContext(static_cast<SDL_GLContext>(m_renderer));
#endif
    free(m_screenData);
}

void Renderer::setCurrent()
{
#if !defined(__vita__)
    SDL_GL_MakeCurrent(m_window->m_window, static_cast<SDL_GLContext>(m_renderer));
#endif
}

void Renderer::clearCurrent()
{
#if !defined(__vita__)
    SDL_GL_MakeCurrent(m_window->m_window, nullptr);
#endif
}

void Renderer::setVideoMode(x86::reg32 w, x86::reg32 h, x86::reg32 bpp)
{
    SDL_Log("[renderer] setVideoMode %ux%u bpp=%u", unsigned(w), unsigned(h), unsigned(bpp));
    m_width = w;
    m_height = h;
    m_depth = bpp;
    x86::reg32 needed = w * h * 4;
    if (needed > m_screenDataSize)
    {
        free(m_screenData);
        m_screenData = reinterpret_cast<unsigned char*>(malloc(needed));
        m_screenDataSize = needed;
    }
    setCurrent();
    glBindTexture(GL_TEXTURE_2D, m_texture);
#if defined(__vita__)
    if (m_depth == 16)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, m_width, m_height, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, nullptr);
    else
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
#else
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, m_width, m_height, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, nullptr);
#endif
    clearCurrent();
    //glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, nullptr);
}

void Renderer::updatePalette(x86::reg32 colorCount, const x86::reg32* colors)
{
    NFS2_ASSERT(colorCount <= 256);
    memcpy(m_colorPalette, colors, colorCount*4);
}

x86::reg32 Renderer::getFrontBuffer() const
{
    return m_videoMemory->getBlockStart() + m_currentBuffer * m_width * m_height * 2;
}

x86::reg32 Renderer::getBackBuffer() const
{
    return m_videoMemory->getBlockStart() + (1 - m_currentBuffer) * m_width * m_height * 2;
}

void Renderer::present()
{
    Gamepad::updateKeys();
#if defined(__vita__)
    glUseProgram(0);
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDepthMask(GL_TRUE);
    glColor4f(1.f, 1.f, 1.f, 1.f);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
#endif
    glBindTexture(GL_TEXTURE_2D, m_texture);
    int w, h;
    SDL_GetWindowSizeInPixels(m_window->m_window, &w, &h);

    float gameAspect = float(m_width) / float(m_height);
    float windowAspect = float(w) / float(h);
    int vpX, vpY, vpW, vpH;
    if (windowAspect > gameAspect)
    {
        vpH = h;
        vpW = int(h * gameAspect + 0.5f);
        vpX = (w - vpW) / 2;
        vpY = 0;
    }
    else
    {
        vpW = w;
        vpH = int(w / gameAspect + 0.5f);
        vpX = 0;
        vpY = (h - vpH) / 2;
    }

    glViewport(0, 0, w, h);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Render the game texture into the aspect-ratio-correct viewport
    glViewport(vpX, vpY, vpW, vpH);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, m_width, m_height, 0, -1.0f, 1.0f);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glBegin(GL_TRIANGLES);
        glTexCoord2f(0.f, 0.f);
        glVertex3f(0.f, 0.f, 1.f);
        glTexCoord2f(1.f, 0.f);
        glVertex3f(GLfloat(m_width), 0.f, 1.f);
        glTexCoord2f(0.f, 1.f);
        glVertex3f(0.f, GLfloat(m_height), 1.f);

        glTexCoord2f(1.f, 0.f);
        glVertex3f(GLfloat(m_width), 0.f, 1.f);
        glTexCoord2f(1.f, 1.f);
        glVertex3f(GLfloat(m_width), GLfloat(m_height), 1.f);
        glTexCoord2f(0.f, 1.f);
        glVertex3f(0.f, GLfloat(m_height), 1.f);
    glEnd();
    glFlush();
#if defined(__vita__)
    vglSwapBuffers(GL_FALSE);
#else
    SDL_GL_SwapWindow(m_window->m_window);
#endif
    m_lastPresentUsec = SDL_GetTicksNS() / 1000;
}

#if defined(__vita__)
void Renderer::getLetterboxViewport(int& x, int& y, int& w, int& h)
{
    int winW, winH;
    SDL_GetWindowSizeInPixels(m_window->m_window, &winW, &winH);
    float gameAspect = float(m_width) / float(m_height);
    float windowAspect = float(winW) / float(winH);
    if (windowAspect > gameAspect)
    {
        h = winH;
        w = int(winH * gameAspect + 0.5f);
        x = (winW - w) / 2;
        y = 0;
    }
    else
    {
        w = winW;
        h = int(winW / gameAspect + 0.5f);
        x = 0;
        y = (winH - h) / 2;
    }
}

void Renderer::finishFrame()
{
    Gamepad::updateKeys();
    glFlush();
    vglSwapBuffers(GL_FALSE);
}
#endif

// Converts the game's software front buffer into m_texture. Upload only; the
// caller is responsible for present(). Uses a persistent buffer (no per-frame
// malloc) and 5/6-bit->8-bit lookup tables (no per-pixel divides) so the blit
// is cheap enough for the Vita CPU.
void Renderer::update()
{
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (m_depth == 16)
    {
#if defined(__vita__)
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_width, m_height, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, &m_application->getMemory<x86::reg16>(getFrontBuffer()));
#else
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_width, m_height, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, &m_application->getMemory<x86::reg16>(getFrontBuffer()));
#endif
    }
    else
    {
        x86::reg8* srcData = &m_application->getMemory<x86::reg8>(getFrontBuffer());
#if defined(__vita__)
        unsigned char* screenData = m_screenData;
        for (x86::reg32 i = 0; i < m_width*m_height; ++i)
        {
            const x86::reg32 color = m_colorPalette[srcData[i]];
            screenData[i * 4 + 0] = x86::reg8(color >> 24);
            screenData[i * 4 + 1] = x86::reg8(color >> 16);
            screenData[i * 4 + 2] = x86::reg8(color >> 8);
            screenData[i * 4 + 3] = x86::reg8(color);
        }
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_width, m_height, GL_RGBA, GL_UNSIGNED_BYTE, screenData);
#else
        x86::reg32* screenData = reinterpret_cast<x86::reg32*>(m_screenData);
        for (x86::reg32 i = 0; i < m_width*m_height; ++i)
        {
            screenData[i] = m_colorPalette[srcData[i]];
        }
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_width, m_height, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, screenData);
#endif
    }
}

x86::reg32 Renderer::lock(x86::reg32 index)
{
    return index == 0 ? getFrontBuffer() : getBackBuffer();
}

void Renderer::unlock(x86::reg32 index)
{
    if (index == 0)
    {
#if defined(__vita__)
        const uint64_t now = SDL_GetTicksNS() / 1000;
        if (m_lastPresentUsec && now - m_lastPresentUsec < 33333)
            return;
#endif
        setCurrent();
#if !defined(__vita__)
        SDL_GL_SetSwapInterval(0);
#endif
        update();
        present();
        clearCurrent();
    }
}

void Renderer::swap()
{
    setCurrent();
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());
#if !defined(__vita__)
    SDL_GL_SetSwapInterval(1);
#endif
    NFS2_USE(mode);
    m_currentBuffer = 1 - m_currentBuffer;

    // Convert + upload + present exactly once. (Previously this looped
    // refresh_rate/30 times, paying an extra vsync wait per frame.)
    update();
    present();
    clearCurrent();
}

}
