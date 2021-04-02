
#pragma once

#include "dust/render/render.h"
#include "dust/core/component.h"

#include "panel.h"

namespace dust
{
    // not strictly "window" specific, but might just as well be
    namespace clipboard
    {
        // put the string as text on the system clipboard
        // this explicitly takes a pointer to be more flexible
        //
        // returns true on success
        bool setText(const char * buf, unsigned len);

        // get text contents of the system clipboard
        bool getText(std::string & buf);
    };

    // Menu implements a basic popup menu for right-clicks and drop downs
    struct Menu
    {
        // add a regular item into the menu
        virtual void addItem(const char * txt, unsigned id,
            bool enabled = true, bool tick = false) = 0;

        // add a separator
        virtual void addSeparator() = 0;

        // active menu at desired position; implicitly releases the menu
        virtual void activate(int frameX, int frameY,
            bool alignRight = false) = 0;

    protected:
        virtual ~Menu() {}  // not really needed but fix clang warning
    };

    // WindowDelegate provides callbacks for window lifetime notifications.
    struct WindowDelegate
    {
        // called after a window is created
        virtual void win_created() {}

        // called when user tries to close window
        // return true to allow the window to close
        virtual bool win_closing() { return true; }

        // called when a window is closed
        // after this returns, the window will be destroyed
        virtual void win_closed() {}

        // called when window activation changes
        virtual void win_activate(bool active) {}

#ifdef __APPLE__
        // by default we create an NSWindow if parent is null
        // if this returns true, then we only create view
        virtual bool win_want_view_only() { return false; }
#endif
    };

    // Window is the base class for system windows/views and implements
    // all the platform independent logic
    //
    // NOTE: platform implementations are required to guarantee that
    // the GL context of the window is current when components are drained
    //
    struct Window : PanelParent, ComponentHost
    {
        // called by setScale()
        dust::Notify onScaleChange = dust::doNothing;
    
        // close the window
        virtual void closeWindow() = 0;

        // return a system-specific native handle
        // this is HWND on Windows and (NSView*) on OSX.
        virtual void * getSystemHandle() = 0;

        // get the current scaling for this window
        unsigned getDPI() { return (getSystemDPI() * dpiScalePercentage) / 100; }

        // this returns the physical DPI of the screen
        // application code should usually never use this
        virtual unsigned getSystemDPI() { return 96; }

        // set logical scaling as percentage of physical
        void setScale(unsigned scale)
        {
            dpiScalePercentage = scale;
            debugPrint("Logical scaling set to %d%% (%d dpi)\n",
                dpiScalePercentage, getDPI());
            broadcastDPI(getDPI());
            reflowChildren();
            onScaleChange();
        }

        unsigned getScale() { return dpiScalePercentage; }

        // normally we update about 30 times per second
        // but in some cases one might want to set it higher
        virtual void setUpdateRate(unsigned msTick) {}

        // set minimum size - default is initial size
        // this is passed directly to operating system
        virtual void setMinSize(int w, int h) = 0;

        // resize the window
        virtual void resize(int w, int h) = 0;

        // Implement window-modal save/discard/cancel dialog.
        // Calls one of the callbacks when dismissed.
        virtual void confirmClose(
            Notify saveAndClose, Notify close, Notify cancel) = 0;

        // Shows a modal save dialog.
        // If user selects a filename, modifies out and calls save.
        // If user cancels the dialog, calls cancel.
        //
        // Non-null path is set as the initial directory.
        virtual void saveAsDialog(std::string & out,
            Notify save, Notify cancel, const char * path = 0) = 0;
        
        // try to toggle maximized state
        // this mainly exists to start in maximized state
        // note that we don't require this to be supported
        virtual void toggleMaximize() {}

        // set window title
        virtual void setTitle(const char * txt) = 0;

        // create a menu
        virtual Menu * createMenu(const std::function<void(int)> & onSelect) = 0;

        // get point size in pixels for this window
        float pt() { return getDPI() * (1 / 72.f); }

        void reflowChildren() { needLayout = true; }
        void redrawRect(const Rect & r, bool allowExtraPass);

        Window();
        ~Window();

        Window * getWindow() { return this; }

        // dispatch a mouse event - normally only called by
        // the platform code, but can be used to synthesize events
        void sendMouseEvent(const MouseEvent & ev);

        // see sendMouseEvent
        void sendMouseExit();

        void setFocus(Panel * c);
        Panel * getFocus() { return focus; }

        // forward keyboard events to focus if any
        void sendKey(Scancode vk, bool pressed, unsigned mods)
        {
#if DUST_SCALE_SHORTCUTS
            if(pressed && vk == SCANCODE_EQUALS
                && mods == (KEYMOD_CMD | KEYMOD_ALT))
            {
                if(dpiScalePercentage < 200) setScale(dpiScalePercentage+25);
                return;
            }
            if(pressed && vk == SCANCODE_MINUS
                && mods == (KEYMOD_CMD | KEYMOD_ALT))
            {
                if(dpiScalePercentage > 50) setScale(dpiScalePercentage-25);
                return;
            }
#endif
            if(focus)
            {
                PanelParent * target = focus;
                while(target)
                {
                    if(target->ev_key(vk, pressed, mods)) break;
                    target = target->getParent();
                }
            }
        }

        // forward keyboard events to focus if any
        void sendText(const char * txt)
        {
            if(focus) focus->ev_text(txt);
        }

        // this will discard any focus or mouse tracking
        // without sending any events to the control
        //
        // used by Panel's destructor to avoid stale pointers
        // there is usually no need to call this manually
        //
        // FIXME: Deleting the control that consumes a mouse event
        // other than button release in response to the event leads
        // to a dangling mouseTrack pointer, because we only store
        // the tracking after the event processing returns.
        void discardTracking(Panel * c)
        {
            if(focus == c) focus = 0;
            if(mouseTrack == c)
            {
                mouseTrack = 0;
                dragButton = 0;
            }
        }
        
        // redirects an active drag-capture to another control
        // intended for co-operating controls like tabstrips
        void redirectDrag(Panel * c)
        {
            if(!dragButton) return;
            mouseTrack = c;
        }

        // cancel dragging (eg. because we opened a menu)
        void cancelDrag() { dragButton = 0; }

        // setup openGL state with render target and scissors
        // the control must be a child of the window
        //
        // returns true if successful
        bool openGL(Panel & ctl);

        // returns true if there's a paint request pending
        bool needsRepaint()
        {
#if DUST_USE_OPENGL
            if(needRecomposite) return true;
#endif
            return needLayout || !paintRect.isEmpty() || redrawRects.size();
        }

#if DUST_USE_OPENGL
        // we allow OpenGL drawing in ev_update()
        // where calling this function forces a recomposition
        // even if the painting rectangles are empty
        virtual void recompositeGL() { needRecomposite = true; }
#endif

    protected:
        // this should only be called by platform wrapper
        void layoutAndPaint(unsigned w, unsigned h);

#if !DUST_USE_OPENGL
        virtual void platformBlit(Surface &) = 0;
#endif

		// windows uses this to turn mouse moves into proper drags
		unsigned getDragButton() { return dragButton; }

    private:
        unsigned        dpiScalePercentage;

        bool            needLayout;

#if DUST_USE_OPENGL
        bool            needRecomposite;
#endif
        Panel           *focus;
        Panel           *mouseTrack;

        unsigned        dragButton;

        Rect            paintRect;

        Surface         backingSurface;

        // this is used when allowExtraPass is set for redrawRect
        std::vector<Rect>   redrawRects;

        // this is used to double-buffer redrawRects
        std::vector<Rect>   paintQueue;
    };

    // create a window - if parent is null, we create a top-level window
    // non-null parent is set as a superview on OSX and parent HWND on Windows
    Window * createWindow(WindowDelegate & delegate, void *parent, int w, int h);

    // this places a given control into a newly created window, with auto-size
    static void openWindow(Panel & c, WindowDelegate & delegate, void *parent = 0)
    {
        unsigned szX, szY;
        c.computeSize(szX, szY);
        c.setParent(createWindow(delegate, parent, szX, szY));
    }
};
