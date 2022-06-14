
#include "window.h"

#if DUST_USE_OPENGL
# if defined(__APPLE__)
#  include <opengl/gl3.h>
# elif defined(_WIN32)
#  include <GL/gl3w.h>
# else
#  error "FIXME: GL headers for platform"
# endif
# include "gl-shader.h"
#endif

// seems gl3.h doesn't declare these
#ifndef GL_UNPACK_CLIENT_STORAGE_APPLE
#define GL_UNPACK_CLIENT_STORAGE_APPLE 0x85B2
#endif

namespace dust
{

    Window::Window()
    {
        needLayout = true;
        focus = 0;

        mouseTrack = 0;
        dragButton = 0;

        dpiScalePercentage = 100;
    }

    Window::~Window()
    {
        // see Panel::~Panel, same thing
        // remove children before vtable is gone
        removeAllChildren();
    }

    void Window::sendMouseEvent(const MouseEvent & ev)
    {
        if(dragButton)
        {
            // send events only if we're dragging an actual control
            // this avoids weird behavior when dragging window background
            if(mouseTrack)
            {
                MouseEvent eRel = ev;
                eRel.x -= mouseTrack->getLayout().windowOffsetX;
                eRel.y -= mouseTrack->getLayout().windowOffsetY;

                mouseTrack->ev_mouse(eRel);
            }

            if(ev.type == MouseEvent::tUp && ev.button == dragButton)
            {
                dragButton = 0;
                // we synthesize a bogus mouse-move here to make sure
                // that we get proper hover-tracking when dragging ends
                MouseEvent em = ev;
                em.type = MouseEvent::tMove;
                em.button = 0;
                sendMouseEvent(em);
            }
        }
        else
        {
            // start a dragging operation?
            //
            // this needs to be done BEFORE dispatch
            // because in some cases we might want to
            // discard the dragging right away
            if(ev.type == MouseEvent::tDown)
            {
                dragButton = ev.button;
            }
            
            Panel * target = dispatchMouseEvent(ev);
            if(mouseTrack && mouseTrack != target)
            {
                mouseTrack->ev_mouse_exit();
            }

            // for scroll events, also send a move
            // this way hover-tracking works while scrolling
            //
            // now we can keep old mouseTrack even when sending scroll
            if(ev.type == MouseEvent::tScroll)
            {
                MouseEvent em = ev;
                em.type = MouseEvent::tMove;
                em.scrollX = 0;
                em.scrollY = 0;
                sendMouseEvent(em);
            }
            else
            {
                mouseTrack = target;
            }

            // if drag was cancelled, then send an exit
            if(ev.type == MouseEvent::tDown && !dragButton)
            {
                mouseTrack->ev_mouse_exit();
                mouseTrack = 0;
            }
        }
    }

    // should be called by platform window when mouse exists window
    void Window::sendMouseExit()
    {
        // Cocoa sends enter/exit messages even when we're dragging.
        // We'll generate the exit anyway once dragging ends and
        // since on Windows we'll be capturing, this should be fine.
        if(mouseTrack && !dragButton)
        {
            mouseTrack->ev_mouse_exit();
            mouseTrack = 0;
        }
    }

    void Window::setFocus(Panel * c)
    {
        if(focus == c) return;

        if(focus) focus->ev_focus(false);
        focus = c;
        if(focus) focus->ev_focus(true);
    }

    void Window::redrawRect(const Rect & r, bool allowExtraPass)
    {
        Rect mr = r;

        // see if we should merge with the default paintRect
        if(!allowExtraPass || mr.overlap(paintRect))
        {
            mr.extend(paintRect);
            allowExtraPass = false;
        }

        // find overlaping existing rectangles,
        // merge them into the current rectangle
        //
        // NOTE: the restarts make this O(n^2) worst-case
        unsigned i = 0;
        while(i < redrawRects.size())
        {
            if(mr.overlap(redrawRects[i]))
            {
                mr.extend(redrawRects[i]);
                redrawRects[i] = redrawRects.back();
                redrawRects.pop_back();

                // we might now overlap with rectangles
                // that we previously skipped, so restart
                i = 0;
            }
            else ++i;
        }

        // add the rectangle
        if(!allowExtraPass)
        {
            paintRect.set(mr);
        }
        else
        {
            // then add the merged rectangle
            redrawRects.push_back(mr);
        }
    }

#if DUST_USE_OPENGL
    struct WindowGL
    {
        GLuint  backingTexture;

        GLuint  vao, vbo;
        GLuint  shader;

        GLuint  fbo = 0, fboTex = 0;
        // current FBO texture size
        unsigned fboSzX = 0, fboSzY = 0;

        WindowGL()
        {
            glGenTextures(1, &backingTexture);
            glBindTexture(GL_TEXTURE_2D, backingTexture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

            // FBO for 
            glGenFramebuffers(1, &fbo);
            glGenTextures(1, &fboTex);
            glBindTexture(GL_TEXTURE_2D, fboTex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
            
            glGenVertexArrays(1, &vao);
            glBindVertexArray(vao);

            // single right triangle, we'll just blow this up until
            // it's large enough to cover the whole window
            float verts[] = { 0, 0, 1, 0, 0, 1 };
            
            glGenBuffers(1, &vbo);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

            // layout=0, size=2, float type, not normalized, stride=auto, offset=0
            glEnableVertexAttribArray(0);
            glVertexAttribPointer( 0, 2, GL_FLOAT, GL_FALSE, 0, (void*)0 );

            const char * text = R"SHADER(
            // GLSL shader code
            #ifdef VERTEX
                layout(location=0) in vec2 pos;
                void main() { gl_Position = vec4(4*pos-1, 0, 1); }
            #endif
            #ifdef FRAGMENT
                uniform sampler2D ts0;  // software rendering
                uniform sampler2D ts1;  // OpenGL rendering FBO texture
                layout(location=0) out vec4 outColor;
                layout(origin_upper_left) in vec4 gl_FragCoord;
                void main() {
                    vec4 c0 = texelFetch(ts0, ivec2(gl_FragCoord.xy), 0);
                    vec4 c1 = texelFetch(ts1, ivec2(gl_FragCoord.x,
                        textureSize(ts0, 0).y - gl_FragCoord.y), 0);
                    outColor = (1-c0.a)*c1 + c0;
                }
            #endif
            //)SHADER";

            auto shaderError = [](const char * text)
            {
                debugPrint("Failed to compile shader:\n%s", text);
                abort();
            };
            shader = compileShaderGL(shaderError, text);

            glUseProgram(shader);
            glUniform1i(glGetUniformLocation(shader, "ts0"), 0);
            glUniform1i(glGetUniformLocation(shader, "ts1"), 1);
        }

        ~WindowGL()
        {
            glDeleteProgram(shader);
            glDeleteBuffers(1, &vbo);
            glDeleteVertexArrays(1, &vao);
            glDeleteTextures(1, &backingTexture);
        }
    };

    static ComponentManager<WindowGL, Window> cm_WindowGL;

    bool Window::openGL(Panel & ctl)
    {
        // check that this render context is for our backing surface
        if(ctl.getWindow() != this) return false;

        WindowGL * gl = cm_WindowGL.getComponent(this);

        glBindFramebuffer(GL_FRAMEBUFFER, gl->fbo);
        
        if(gl->fboSzX != layout.w
        || gl->fboSzY != layout.h)
        {
            gl->fboSzX = layout.w;
            gl->fboSzY = layout.h;
            
            glBindTexture(GL_TEXTURE_2D, gl->fboTex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                gl->fboSzX, gl->fboSzY, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glFramebufferTexture2D(GL_FRAMEBUFFER,
                GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, gl->fboTex, 0);
            glBindTexture(GL_TEXTURE_2D, 0);
            
            // do a clear
            glDisable(GL_SCISSOR_TEST);
            glViewport(0, 0, gl->fboSzX, gl->fboSzY);
            glClear(GL_COLOR_BUFFER_BIT);
        }

        // build a rectangle
        Rect r(
            ctl.getLayout().windowOffsetX,
            ctl.getLayout().windowOffsetY,
            ctl.getLayout().w, ctl.getLayout().h);
            
        // set viewport
        glViewport(r.x0, layout.h - r.y1, r.w(), r.h());

        // clip to parent with scissors
        auto * parent = ctl.getParent();
        while(parent)
        {
            Rect pr(
                parent->getLayout().windowOffsetX,
                parent->getLayout().windowOffsetY,
                parent->getLayout().w, parent->getLayout().h);
            r.clip(pr);
            parent = parent->getParent();
        }
        glScissor(r.x0, layout.h - r.y1, r.w(), r.h());
        glEnable(GL_SCISSOR_TEST);

        return true;
    }
#endif

    void Window::layoutAndPaint(unsigned w, unsigned h)
    {

#if DUST_USE_OPENGL
        needRecomposite = false;    // clear flag

        WindowGL * gl = cm_WindowGL.getComponent(this);
#endif

        if(backingSurface.validate(w, h)
        || layout.w != w || layout.h != h)
        {
            layout.w = w;
            layout.h = h;
            needLayout = true;

#if DUST_USE_OPENGL
            glBindTexture(GL_TEXTURE_2D, gl->backingTexture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h,
                    0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, 0);
#endif
        }

        Rect wr(0,0,w,h);

        if(needLayout)
        {
            needLayout = false;
            layoutAsRoot(getDPI());

            // full repaint after layout
            //
            // we COULD skip this, if we tracked
            // previous layout for every control
            redrawRect(wr, false);
        }

        if(!paintRect.isEmpty())
        {
            redrawRects.push_back(paintRect);
            paintRect.clear();
        }

        (std::swap)(redrawRects, paintQueue);

        if(paintQueue.size())
        {

#if DUST_DEBUG_REDRAWS
            // need a second surface for debug painting
            Surface debugBacking;

            debugBacking.validate(w, h);
            RenderContext rcDebug(debugBacking);
            rcDebug.copy(backingSurface);

            rcDebug.fill(paint::Color(0x24240012));

            unsigned tStart = getTimeUs();
#endif
            while(paintQueue.size())
            {
                Rect & r = paintQueue.back();
                r.clip(wr);

                RenderContext rc(backingSurface, r);

                // always initialize to opaque black
                rc.clear(theme.winColor);

                // draw all children
                renderChildren(rc);

                // then draw a window border
                //rc.drawRectBorder(paint::Color(0x20202020), 0, 0, w, h);

#if DUST_DEBUG_REDRAWS
                // copy the rectangle into our debug backing surface
                rcDebug.copyRect(r.x0, r.y0, r.w(), r.h(),
                    backingSurface, r.x0, r.y0);

                rcDebug.drawRectBorder(paint::Color(0xFFFF007f),
                    r.x0, r.y0, r.w(), r.h());
#else
# if DUST_USE_OPENGL
                glBindTexture(GL_TEXTURE_2D, gl->backingTexture);
                glPixelStorei(GL_UNPACK_ROW_LENGTH, backingSurface.getPitch());
                glTexSubImage2D(GL_TEXTURE_2D, 0, r.x0, r.y0, r.w(), r.h(),
                    GL_BGRA, GL_UNSIGNED_BYTE, backingSurface.getPixels()
                    + r.x0+r.y0*backingSurface.getPitch());
# endif
#endif

                paintQueue.pop_back();
            }

#if DUST_DEBUG_REDRAWS
            debugPrint("render time: %.3fms\n", (getTimeUs()-tStart)*1e-3);
#endif

#if DUST_USE_OPENGL

 #if DUST_DEBUG_REDRAWS
            glBindTexture(GL_TEXTURE_2D, gl->backingTexture);
            // if we painted at least one rectangle, then
            // we need to upload the surface to a texture
            glPixelStorei(GL_UNPACK_ROW_LENGTH, backingSurface.getPitch());
            glTexImage2D(GL_TEXTURE_2D,
                0, GL_RGBA8, w, h,
                0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV,
                debugBacking.getPixels() );
# endif    

#else // OPENGL
            platformBlit(
# if DUST_DEBUG_REDRAWS
                debugBacking
# else
                backingSurface
# endif
            );
#endif // OPENGL
        }

#if DUST_USE_OPENGL
        glViewport(0, 0, w, h);
        glDisable(GL_SCISSOR_TEST);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        
        // Apple throws a debug message on M1 if we don't bind
        // a texture that we've actually properly allocated.
        //
        // Suppress the message by binding the software rendered
        // image twice instead.
        glActiveTexture(GL_TEXTURE1);
        if(gl->fboSzX && gl->fboSzY)
            glBindTexture(GL_TEXTURE_2D, gl->fboTex);
        else
            glBindTexture(GL_TEXTURE_2D, gl->backingTexture);
        
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, gl->backingTexture);

        // we'll blend in shader
        glDisable(GL_BLEND);
        glUseProgram(gl->shader);
        glBindVertexArray(gl->vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
        
        int e = glGetError();
        if(e) debugPrint("%s:%d: glError: %d\n", __FILE__, __LINE__, e);
#endif
    }
    
};
