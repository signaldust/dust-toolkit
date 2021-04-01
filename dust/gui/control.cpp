
#include "control.h"
#include "window.h"

namespace dust
{
    Theme theme;
    
    ControlParent::~ControlParent()
    {
        // this should probably be an assertion failure
        // if we have any children left..
        removeAllChildren();
    }

    void ControlParent::removeAllChildren()
    {
        while(children.first)
        {
            children.first->setParent(0);
        }
    }

    void ControlParent::updateAllChildren()
    {
        for(Control * c : children)
        {
            if(!c->enabled) continue;

            c->ev_update();
            c->updateAllChildren();
        }
    }

    // See notes in header
    void ControlParent::addChild(Control * c)
    {
        c->siblingsPrev = children.last;
        children.last = c;

        if(c->siblingsPrev) { c->siblingsPrev->siblingsNext = c; }
        else { children.first = c; }
    }

    // See notes in header
    void ControlParent::removeChild(Control * c)
    {
        if(c->siblingsPrev) { c->siblingsPrev->siblingsNext = c->siblingsNext; }
        else { children.first = c->siblingsNext; }

        if(c->siblingsNext) { c->siblingsNext->siblingsPrev = c->siblingsPrev; }
        else { children.last = c->siblingsPrev; }

        c->siblingsPrev = 0;
        c->siblingsNext = 0;
    }

    void ControlParent::broadcastDPI(float dpi)
    {
        for(Control * c : children)
        {
            c->ev_dpi(dpi);
            c->broadcastDPI(dpi);
        }
    }

    void ControlParent::broadcastDiscardWindow()
    {
        for(Control * c : children)
        {
            getWindow()->discardTracking(c);
            c->broadcastDiscardWindow();
            c->discardWindow();
        }
    }

    void ControlParent::renderChildren(RenderContext & rcParent)
    {
        for(Control * c : children)
        {
            if(!c->enabled || !c->visible) continue;

            int cx = c->layout.x + layout.contentOffsetX;
            int cy = c->layout.y + layout.contentOffsetY;

            Rect rChild(cx, cy, c->layout.w, c->layout.h);

            RenderContext   rc(rcParent, rChild, true);
            if(rc.getClipRect().isEmpty()) continue;

            c->render(rc);
            c->renderChildren(rc);
#if DUST_DEBUG_LAYOUT
            rc.drawRectBorder(paint::Color(0x40402010),
                0, 0, c->layout.w, c->layout.h, 1);
#endif
        }
    }

    Control * ControlParent::dispatchMouseEvent(const MouseEvent & ev)
    {
        // always test the topmost (last) child first
        for(Control * c : in_reverse(children))
        {
            // check the bounds of the child
            int x = ev.x - c->layout.windowOffsetX;
            int y = ev.y - c->layout.windowOffsetY;

            // first check that we are visible and inside layout bounds
            if(!c->enabled || !c->visible
                || x < 0 || x >= c->layout.w
                || y < 0 || y >= c->layout.h) continue;

            // test the grand children first
            Control * target = c->dispatchMouseEvent(ev);
            if(target) return target;

            // test the child itself
            if(c->ev_hittest(x, y))
            {
                MouseEvent eRel = ev; eRel.x = x; eRel.y = y;
                if(c->ev_mouse(eRel)) return c;
            }
        }

        return 0;
    }

    void ControlParent::scrollToView(int x, int y, int dx, int dy)
    {
        ControlParent * parent = getParent();
        if(parent) parent->scrollToView(x + layout.x, y + layout.y, dx, dy);
    }

    Control::~Control()
    {
        // do this here, so children get chance to
        // discard tracking before we lose our vtable
        removeAllChildren();
        
        if(parent)
        {
            setParent(0);
        }
    }

    void Control::redraw(bool allowExtraPass)
    {
        if(!visible) return;

        Window * win = getWindow();
        if(win)
        {
            Rect r(layout.windowOffsetX, layout.windowOffsetY,
                layout.w, layout.h);

            ControlParent * up = getParent();
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

    void Control::focus()
    {
        Window * win = getWindow();
        if(win) win->setFocus(this);
    }

    void Control::setParent(ControlParent * newParent)
    {
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
};
