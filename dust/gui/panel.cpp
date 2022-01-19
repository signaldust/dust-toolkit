
#include "panel.h"
#include "window.h"

namespace dust
{
    Theme theme;
    
    Panel::~Panel()
    {
        // do this here, so children get chance to
        // discard tracking before we lose our vtable
        removeAllChildren();
        
        if(parent)
        {
            setParent(0);
        }
    }

    void Panel::redraw(bool allowExtraPass)
    {
        if(!visible) return;

        Window * win = getWindow();
        if(win)
        {
            Rect r(layout.windowOffsetX, layout.windowOffsetY,
                layout.w, layout.h);

            PanelParent * up = getParent();
            while(up)
            {
                if(!up->visible) return;

                Rect ur(up->layout.windowOffsetX, up->layout.windowOffsetY,
                    up->layout.w, up->layout.h);

                r.clip(ur);

                up = up->getParent();
            }

            if(!r.isEmpty()) win->redrawRect(r, allowExtraPass);
        }
    }

    void Panel::focus()
    {
        Window * win = getWindow();
        if(win) win->setFocus(this);
    }

    void Panel::setParent(PanelParent * newParent)
    {
        // FIXME: we could preserve Window in cases
        // where the new parent is already rooted
        // in the same window and is not a child of the
        // control that we are trying to parent
        if(parent)
        {
            Window * win = getWindow();
            if(win)
            {
                broadcastDiscardWindow();
                win->discardTracking(this);
                discardWindow();
            }
            parent->removeChild(this);
        }

        parent = newParent;

        if(parent)
        {
            parent->addChild(this);
        }

        // if we have a window after update,
        // send DPI notify and request a reflow()
        Window * win = getWindow();
        if(win)
        {
            ev_dpi(win->getDPI());
            broadcastDPI(win->getDPI());

            reflow();
        }
    }

    void Panel::insertAfter(Panel * other)
    {
        // FIXME: we could preserve Window in cases
        // where the new parent is already rooted
        // in the same window and is not a child of the
        // control that we are trying to parent
        if(parent)
        {
            Window * win = getWindow();
            if(win)
            {
                broadcastDiscardWindow();
                win->discardTracking(this);
                discardWindow();
            }
            parent->removeChild(this);
        }

        if(!other) return;  // FIXME: ?

        parent = other->parent;

        if(parent)
        {
            parent->addChildAfter(this, other);
        }

        // if we have a window after update,
        // send DPI notify and request a reflow()
        Window * win = getWindow();
        if(win)
        {
            ev_dpi(win->getDPI());
            broadcastDPI(win->getDPI());

            reflow();
        }
    }
    
    void Panel::insertBefore(Panel * other)
    {
        // FIXME: we could preserve Window in cases
        // where the new parent is already rooted
        // in the same window and is not a child of the
        // control that we are trying to parent
        if(parent)
        {
            Window * win = getWindow();
            if(win)
            {
                broadcastDiscardWindow();
                win->discardTracking(this);
                discardWindow();
            }
            parent->removeChild(this);
        }

        if(!other) return;  // FIXME: ?

        parent = other->parent;

        if(parent)
        {
            // NOTE: the special case where other->siblingsPrev is null
            // is handled in addChildAfter by inserting as the first child
            parent->addChildAfter(this, other->siblingsPrev);
        }

        // if we have a window after update,
        // send DPI notify and request a reflow()
        Window * win = getWindow();
        if(win)
        {
            ev_dpi(win->getDPI());
            broadcastDPI(win->getDPI());

            reflow();
        }
    }    
};
