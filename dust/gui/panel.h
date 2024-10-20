
#pragma once

#include "dust/core/defs.h"
#include "dust/core/component.h"

#include "dust/render/render.h"

#include "event.h"

#include <list>

namespace dust
{
    // This is for widgets to pull their colors from.
    struct Theme
    {
        // these should form a gradient
        ARGB winColor   = 0xff000000;
        ARGB bgColor    = 0xff101316;
        ARGB bgMidColor = 0xff1d1f21;
        ARGB midColor   = 0xff282a2e;

        ARGB selColor   = 0xff373b41;   // for selections
        ARGB fgMidColor = 0xff5e5f60;
        ARGB fgColor    = 0xffb7b5b4;
        ARGB maxColor   = 0xffefece9;

        // these should be rainbow colors safe for text
        ARGB errColor   = 0xffdb5a7a;   // red, for errors
        ARGB warnColor  = 0xffdbaa7a;   // yellow, for warning
        ARGB goodColor  = 0xff7adb7a;   // green, for success
        ARGB actColor   = 0xff8899dd;   // blue, active fg
    };

    extern Theme theme;
    
    // Layout "style-sheet" for Panels - on top for easy reference
    struct LayoutStyle
    {
        // the layout rule to use
        enum Rule
        {
            FILL,           // fill remaining content area (default)

            // border rules - slice area from one of the sides
            NORTH, EAST, SOUTH, WEST,

            // flow for text and other objects (FIXME: not implemented ..)
            FLOW,

            // fill whole parent - useful for overlays
            OVERLAY,

            // explicit manual layout (eg. for MDI windows)
            MANUAL,

            INVALID         // like none, but causes debug print

        } rule = FILL;

        // desired minimum size in points
        float minSizeX = 0, minSizeY = 0;

        // scrolling containers should usually not expand to fit children
        // when set, size calculation for parent uses minSize directly
        //
        // ie. this is set on the content panel that we "can scroll" and
        // it isn't necessary to fit that content into the layout
        bool canScrollX = false, canScrollY = false;

        // content padding has no direct effect on the control itself
        // but it's added around content for layout of children
        struct ContentPadding
        {
            float north = 0, east = 0, south = 0, west = 0;

            // shortcut for single value assignment to all members
            ContentPadding & operator=(float p)
            {
                north = p; east = p; south = p; west = p;
                return *this;
            }
        } padding;
    };

    struct Layout
    {
        // parent relative position and size
        int x = 0, y = 0, w = 0, h = 0;

        // window relative position (eg. for mouse event handling)
        // also useful for positioning context menus
        int windowOffsetX = 0, windowOffsetY = 0;

        // content size - need not match the actual control size
        int contentSizeX = 0, contentSizeY = 0;

        // content offset - offsets the position of any children
        // and is typically used for things like scrolling panels
        int contentOffsetX = 0, contentOffsetY = 0;

        // this is used to cache for pixel-values that
        // are calculated from LayoutStyle::padding
        struct ContentPadding
        {
            int north = 0, east = 0, south = 0, west = 0;
        } contentPadding;
    };

    ///
    // The basic UI element is Panel which is some sort of widget.
    // Their lifetime is entirely application managed.
    //
    // At any given time, each Panel has at most one parent,
    // which may be another Panel or a Window. If the parent is
    // destroyed (eg. a Window is closed) the Panels will simply
    // become parentless until attached to another parent.
    //
    // It is generally safe to destroy Panels at any time, except
    // during event-processing of sub-trees rooted on the Panel.

    struct Panel; // forward declare for PanelParent
    struct Window;  // forward declare for PanelParent

    // PanelParent contains the implementation of the Panel hierarchy,
    // with Panel itself simply adding the parenting logic.
    //
    // Window also derives from this, but cannot have a parent.
    //
    struct PanelParent : EventResponder, virtual ComponentHost
    {        
        virtual ~PanelParent();

        // return current window or nullptr
        virtual Window * getWindow() = 0;

        // return true if we have children
        bool hasChildren() { return 0 != children.first; }
        
        // remove all children (eg. used by closing windows)
        void removeAllChildren();

        // broadcast ev_update recursively to all children
        void updateAllChildren();

        // render children
        void renderChildren(RenderContext & rc);

        Panel * dispatchMouseEvent(const MouseEvent & ev);

        const Layout & getLayout() { return layout; }

        // this is just to allow recursion up the parent tree
        // PanelParent itself never actually has a parent
        virtual PanelParent * getParent() { return 0; }

        // recalculate all the window offsets for the layout tree
        // this is called automatically whenever layout is updated
        void updateWindowOffsets();

        // request that a given point is scrolled into view
        // takes coordinates in control's local coordinate system
        //
        // the default implementation just sends it upstream until
        // it finds a control (eg. ScrollPanel) that catches it
        //
        // it is normal to call this a few times in a row to try
        // to align the view in a sensible way (eg. see TextArea)
        //
        // dx, dy set the desired distance from viewport borders
        virtual void scrollToView(int x, int y, int dx = 0, int dy = 0);

        // request reflow for children; this is recursive such
        // that we can catch it in scrolling containers to avoid
        // doing a full repaint just to adjust scrollbars
        virtual void reflowChildren()
        {
            PanelParent * parent = getParent();
            if(parent) parent->reflowChildren();
        }

        template <typename Visitor>
        void eachChild(Visitor & visitor)
        {
            for(auto * c : children) visitor(c);
        }

        // return first child or nullptr if none
        Panel * getChildFirst() const { return children.first; }

        // return previous sibling or nullptr if last
        Panel * getChildLast() const { return children.last; }

    protected:

        // there should never be any reason to create a PanelParent directly
        PanelParent() {}
        
        // disallow any accidental copy-construction
        PanelParent(PanelParent const &) = delete;

        // this contains layout information for the control
        // usually the automatic layout process manages this
        Layout  layout;

        bool    enabled = true;
        bool    visible = true;

        // does layout for children, recursively
        void layoutAsRoot(float dpi);

        void calculateContentSizeX(float dpi);
        void calculateContentSizeY(float dpi);
        void calculateLayoutX(float dpi);
        void calculateLayoutY(float dpi);

        // broadcast ev_dpi recursively to all children
        void broadcastDPI(float dpi);

        // recursive discard window and tracking of all children
        void broadcastDiscardWindow();

        // override this to actually draw stuff
        virtual void render(RenderContext & rc) { }

    private:
        friend Panel;

        // these are used by Panel::setParent which enforces
        // "at most one" condition, so no need to check here
        //
        void addChild(Panel * c);
        void removeChild(Panel * c);

        // NOTES: We originally used simple std::list for these
        // but intrusive is preferable for several reasons:
        //
        // - controls can only have one parent anyway
        // - reduces indirection and cache overheads with dispatch
        //
        // The travelsal cost reduction is real: on my MacBook for
        // example, ev_update dispatch energy impact is literally
        // reduced to 20% of the original std::list version!
        //
        struct Children
        {
            Panel *first = 0;
            Panel *last = 0;

            // forward iterator
            struct iteratorF
            {
                Panel *p;

                iteratorF(Panel * p) : p(p) {}
                Panel * operator*() const { return p; }
                iteratorF & operator++();
                bool operator!=(const iteratorF & i) const
                { return p != i.p; }
            };

            struct iteratorR
            {
                Panel *p;

                iteratorR(Panel * p) : p(p) {}
                Panel * operator*() const { return p; }
                iteratorR & operator++();
                bool operator!=(const iteratorR & i) const
                { return p != i.p; }
            };

            iteratorF begin() { return iteratorF(first); }
            iteratorF end() { return iteratorF(0); }

            iteratorR rbegin() { return iteratorR(last); }
            iteratorR rend() { return iteratorR(0); }

            typedef iteratorF iterator;
            typedef iteratorR reverse_iterator;
        } children;
    };

    // Base class for actual widgets
    struct Panel : PanelParent
    {
        // layout styles for this control
        LayoutStyle style;

        ~Panel();

        // request layout recalculation
        void reflow() { if(getWindow()) parent->reflowChildren(); }

        // compute minimum content size for a given DPI
        void computeSize(unsigned & szX, unsigned & szY, float dpi = 96)
        {
            float unit = dpi * (1/72.f);

            // we want to respect minimum size constraints here
            // where as layoutAsRoot() normally ignores those
            layout.w = (int) ceil(style.minSizeX * unit);
            layout.h = (int) ceil(style.minSizeY * unit);

            layoutAsRoot(dpi);

            // we need to add padding manually here
            // because this is normally done by parrent
            layout.contentSizeX += (int) ceil(style.padding.east*unit);
            layout.contentSizeX += (int) ceil(style.padding.west*unit);

            layout.contentSizeY += (int) ceil(style.padding.north*unit);
            layout.contentSizeY += (int) ceil(style.padding.south*unit);
            
            szX = layout.contentSizeX;
            szY = layout.contentSizeY;

            reflow();
        }

        // request repaint
        void redraw();

        // short-hand for setting keyboard focus to the control
        void focus();

        // set (or clear is newParent is null) the parent of this control
        //
        // NOTE: setParent(getParent()) doesn't actually cause reparenting
        // but simply moves the panel last after any of it's siblings
        void setParent(PanelParent * newParent);

        // since we often want to use references, make it possible
        void setParent(PanelParent & newParent) { setParent(&newParent); }

        // return the current parent of this control or null if none
        PanelParent * getParent() { return parent; }

        // return the current window if there is one at the root
        // of the UI tree this control is part of
        Window * getWindow() {
            if(!window) window = parent ? parent->getWindow() : 0;
            return window;
        }

        // discard cached pointer to a window, only useful internally
        void discardWindow() { window = 0; }

        // set whether the control should be visible
        //
        // hidden (but enabled) controls still receive layout
        // but events and redraws ignore the control and it's children
        void setVisible(bool b)
        { if(visible == b) return; visible = true; redraw(); visible = b;  }

        bool getVisible() { return visible; }

        // set whether the control should get layout and updates
        void setEnabled(bool b)
        { if(enabled == b) return; enabled = b; reflow(); }

        bool getEnabled() { return enabled; }

        // return next sibling or nullptr if first
        Panel * getSiblingNext() const { return siblingsNext; }

        // return previous sibling or nullptr if last
        Panel * getSiblingPrevious() const { return siblingsPrev; }

    private:
        Window * window = 0;    // cached window
        PanelParent * parent = 0;

        // allow access to siblings
        // could also just add accessors?
        friend PanelParent;
        friend PanelParent::Children::iteratorF;
        friend PanelParent::Children::iteratorR;

        // pointers for next and previous sibling
        Panel * siblingsNext = 0;
        Panel * siblingsPrev = 0;
    };

#if 0
    // Currently this is here for the purpose of documentation only.
    //
    struct GLPanel : Panel
    {
        void render(RenderContext & rc)
        {
            // When the toolkit is configured to use OpenGL, the normal
            // software rendered UI is composited on top of the OpenGL
            // framebuffer whenever the window needs to be redrawn.
            //
            // In order to allow actual OpenGL rendering to be visible,
            // one should first clear the relevant areas to transparent black.
            rc.clear();
        }

        void ev_update()
        {
            // OpenGL rendering, especially animated rendering, need not
            // necessarily trigger an actual (software rendering) redraw.
            //
            // Instead, it is safe to render OpenGL directly from ev_update
            // and then inform the window that the view should be recomposited.
            //
            renderGL();
            getWindow()->recompositeGL();
        }

        // This method is not part of any interface.
        void renderGL()
        {
            // The toolkit also maintains a separate FBO for OpenGL rendering
            // such that OpenGL content is not lost on normal redraws.
            //
            // Window has a helper method to do the following:
            //  - bind the backing FBO for OpenGL rendering as the framebuffer
            //  - set viewport and scissors to the bounds of the requested control
            //  - glEnable scissors testing
            getWindow()->openGL(*this);

            // let's paint the panel's region as green
            glClearColor(0,1,0,0);
            glClear(GL_COLOR_BUFFER_BIT);
        }
    };
#endif

    // out of line definition, because we need Panel defined
    inline PanelParent::Children::iteratorF &
    PanelParent::Children::iteratorF::operator++()
    { p = p->siblingsNext; return *this; }

    // out of line definition, because we need Panel defined
    inline PanelParent::Children::iteratorR &
    PanelParent::Children::iteratorR::operator++()
    { p = p->siblingsPrev; return *this; }

    // this allows range-based loops for children in reverse
    // keep this C++11 by hard-coding the reverse_iterator
    // into the type (would need C++14 for auto)
    template <typename T>
    struct reverse_adaptor
    {
        T & iterable;
        reverse_adaptor(T & t) : iterable(t) {}

        typename T::reverse_iterator begin() { return iterable.rbegin(); }
        typename T::reverse_iterator end() { return iterable.rend(); }
    };

    template <typename T>
    reverse_adaptor<T> in_reverse(T & t) { return reverse_adaptor<T>(t); }

};
