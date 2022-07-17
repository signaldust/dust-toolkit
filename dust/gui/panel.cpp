
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
        // special case shortcut (doesn't do full reparenting)
        if(parent == newParent)
        {
            if(!parent) return;
            if(!getSiblingNext()) return;   // already last

            // these only manipulate the linked list
            // we don't need anything else in this special case
            parent->removeChild(this);
            parent->addChild(this);

            // reflow
            reflow();
            return;
        }
    
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
