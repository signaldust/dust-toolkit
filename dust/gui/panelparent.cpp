
#include "panel.h"
#include "window.h"

namespace dust
{
    PanelParent::~PanelParent()
    {
        // this should probably be an assertion failure
        // if we have any children left..
        removeAllChildren();
    }

    void PanelParent::removeAllChildren()
    {
        while(children.first)
        {
            children.first->setParent(0);
        }
    }

    void PanelParent::updateAllChildren()
    {
        for(Panel * c : children)
        {
            if(!c->enabled) continue;

            c->ev_update();
            c->updateAllChildren();
        }
    }

    // See notes in header
    void PanelParent::addChild(Panel * c)
    {
        c->siblingsPrev = children.last;
        children.last = c;

        if(c->siblingsPrev) { c->siblingsPrev->siblingsNext = c; }
        else { children.first = c; }
    }

    // See notes in header
    void PanelParent::removeChild(Panel * c)
    {
        if(c->siblingsPrev) { c->siblingsPrev->siblingsNext = c->siblingsNext; }
        else { children.first = c->siblingsNext; }

        if(c->siblingsNext) { c->siblingsNext->siblingsPrev = c->siblingsPrev; }
        else { children.last = c->siblingsPrev; }

        c->siblingsPrev = 0;
        c->siblingsNext = 0;
    }

    void PanelParent::broadcastDPI(float dpi)
    {
        for(Panel * c : children)
        {
            c->ev_dpi(dpi);
            c->broadcastDPI(dpi);
        }
    }

    void PanelParent::broadcastDiscardWindow()
    {
        for(Panel * c : children)
        {
            getWindow()->discardTracking(c);
            c->broadcastDiscardWindow();
            c->discardWindow();
        }
    }

    void PanelParent::renderChildren(RenderContext & rcParent)
    {
        for(Panel * c : children)
        {
            if(!c->enabled || !c->visible) continue;

            int cx = c->layout.x + layout.contentOffsetX;
            int cy = c->layout.y + layout.contentOffsetY;

            Rect rChild(cx, cy, c->layout.w, c->layout.h);

            RenderContext   rc(rcParent, rChild, true);
            if(rc.getClipRect().isEmpty()) continue;

            // borrow maskData
            MaskDataBorrow  borrow(rcParent, rc);

            c->render(rc);
            c->renderChildren(rc);
#if DUST_DEBUG_LAYOUT
            rc.drawRectBorder(paint::Color(0x40402010),
                0, 0, c->layout.w, c->layout.h, 1);
#endif
        }
    }

    Panel * PanelParent::dispatchMouseEvent(const MouseEvent & ev)
    {
        // always test the topmost (last) child first
        for(Panel * c : in_reverse(children))
        {
            // check the bounds of the child
            int x = ev.x - c->layout.windowOffsetX;
            int y = ev.y - c->layout.windowOffsetY;

            // first check that we are visible and inside layout bounds
            if(!c->enabled || !c->visible
                || x < 0 || x >= c->layout.w
                || y < 0 || y >= c->layout.h) continue;

            // test the grand children first
            Panel * target = c->dispatchMouseEvent(ev);
            if(target) return target;

            MouseEvent eRel = ev; eRel.x = x; eRel.y = y;
            if(c->ev_mouse(eRel)) return c;
        }

        return 0;
    }

    void PanelParent::scrollToView(int x, int y, int dx, int dy)
    {
        PanelParent * parent = getParent();
        if(parent) parent->scrollToView(x + layout.x, y + layout.y, dx, dy);
    }

    void PanelParent::layoutAsRoot(float dpi)
    {
        calculateContentSizeX(dpi);
        calculateLayoutX(dpi);
        calculateContentSizeY(dpi);
        calculateLayoutY(dpi);

        updateWindowOffsets();
    }

    void PanelParent::updateWindowOffsets()
    {
        int contentX = layout.windowOffsetX + layout.contentOffsetX;
        int contentY = layout.windowOffsetY + layout.contentOffsetY;

        for(Panel * c : children)
        {
            if(!c->enabled) continue;

            c->layout.windowOffsetX = contentX + c->layout.x;
            c->layout.windowOffsetY = contentY + c->layout.y;
            c->updateWindowOffsets();
        }
    }

    void PanelParent::calculateContentSizeX(float dpi)
    {
        // use the initial layout.w as minimum content size
        // this is a temporary value unless this is the layout root
        int contentSize = 0;
        int reserveSize = 0;

        float unit = dpi * (1/72.f);

        for(Panel * c : children)
        {
            // don't do any layout if disabled
            if(!c->enabled || c->style.rule == LayoutStyle::MANUAL) continue;

            // calculate temporary size as desired pixel size
            c->layout.w = (int) ceil(c->style.minSizeX * unit);

            // compute padding size
            c->layout.contentPadding.west = (int)ceil(unit*c->style.padding.west);
            c->layout.contentPadding.east = (int)ceil(unit*c->style.padding.east);

            c->calculateContentSizeX(dpi);

            // call the manual size hook
            c->layout.contentSizeX = (std::max)
            (c->layout.contentSizeX, c->ev_size_x(dpi));

            // expand the temporary size to content unless we can scroll
            if(!c->style.canScrollX) c->layout.w = c->layout.contentSizeX;

            switch(c->style.rule)
            {
            case LayoutStyle::FILL:
            case LayoutStyle::NORTH:
            case LayoutStyle::SOUTH:
                contentSize = (std::max)(contentSize, reserveSize + c->layout.w);
                break;

            case LayoutStyle::EAST:
            case LayoutStyle::WEST:
                reserveSize += c->layout.w;
                contentSize = (std::max)(contentSize, reserveSize);
                break;

            case LayoutStyle::OVERLAY:
                contentSize = (std::max)(contentSize, c->layout.w);
                break;
                
            case LayoutStyle::MANUAL: break;    // don't give a warning

            default:
                debugPrint("warning: unknown style.rule!\n");
                break;
            }
        }

        // add padding
        contentSize += layout.contentPadding.west;
        contentSize += layout.contentPadding.east;

        layout.contentSizeX = (std::max)(contentSize, layout.w);
    }

    void PanelParent::calculateContentSizeY(float dpi)
    {
        // see calculateContentSizeX
        int contentSize = 0;
        int reserveSize = 0;

        for(Panel * c : children)
        {
            // don't do any layout if disabled or rule is none
            if(!c->enabled || c->style.rule == LayoutStyle::MANUAL) continue;

            float unit = dpi * (1/72.f);

            // calculate temporary size as desired pixel size
            c->layout.h = (int) ceil(c->style.minSizeY * unit);

            // compute padding size
            c->layout.contentPadding.north = (int)ceil(unit*c->style.padding.north);
            c->layout.contentPadding.south = (int)ceil(unit*c->style.padding.south);

            c->calculateContentSizeY(dpi);

            // call the manual size hook
            c->layout.contentSizeY = (std::max)
            (c->layout.contentSizeY, c->ev_size_y(dpi));

            // expand the temporary size to content unless we can scroll
            if(!c->style.canScrollY) c->layout.h = c->layout.contentSizeY;

            switch(c->style.rule)
            {
            case LayoutStyle::FILL:
            case LayoutStyle::EAST:
            case LayoutStyle::WEST:
                contentSize = (std::max)(contentSize, reserveSize + c->layout.h);
                break;

            case LayoutStyle::NORTH:
            case LayoutStyle::SOUTH:
                reserveSize += c->layout.h;
                contentSize = (std::max)(contentSize, reserveSize);
                break;
                
            case LayoutStyle::OVERLAY:
                contentSize = (std::max)(contentSize, c->layout.h);
                break;
                
            case LayoutStyle::MANUAL:
            default:
                break;
            }

        }

        // add padding
        contentSize += layout.contentPadding.north;
        contentSize += layout.contentPadding.south;

        layout.contentSizeY = (std::max)(contentSize, layout.h);
    }

    void PanelParent::calculateLayoutX(float dpi)
    {
        layout.contentSizeX = (std::max)(layout.contentSizeX, layout.w);
        int box0 = layout.contentPadding.west;
        int box1 = layout.contentSizeX - layout.contentPadding.east;

        for(Panel * c : children)
        {
            if(!c->enabled || c->style.rule == LayoutStyle::MANUAL) continue;

            switch(c->style.rule)
            {
            case LayoutStyle::FILL:
            case LayoutStyle::NORTH:
            case LayoutStyle::SOUTH:
                c->layout.x = box0;
                c->layout.w = box1 - box0;
                break;

            case LayoutStyle::WEST:
                c->layout.x = box0;
                c->layout.w = c->layout.contentSizeX;
                box0 += c->layout.contentSizeX;
                break;

            case LayoutStyle::EAST:
                box1 -= c->layout.contentSizeX;
                c->layout.x = box1;
                c->layout.w = c->layout.contentSizeX;
                break;
                
            case LayoutStyle::OVERLAY:
                c->layout.x = layout.contentPadding.west;
                c->layout.w = layout.contentSizeX
                    - layout.contentPadding.east - c->layout.x;
                break;

            default:
                break;
            }

            c->calculateLayoutX(dpi);
        }

    }

    void PanelParent::calculateLayoutY(float dpi)
    {
        layout.contentSizeY = (std::max)(layout.contentSizeY, layout.h);
        int box0 = layout.contentPadding.north;
        int box1 = layout.contentSizeY - layout.contentPadding.south;

        for(Panel * c : children)
        {
            if(!c->enabled) continue;

            if(c->style.rule == LayoutStyle::MANUAL)
            {
                c->ev_layout(dpi);
                continue;
            }

            switch(c->style.rule)
            {
            case LayoutStyle::FILL:
            case LayoutStyle::EAST:
            case LayoutStyle::WEST:
                c->layout.y = box0;
                c->layout.h = box1 - box0;
                break;

            case LayoutStyle::NORTH:
                c->layout.y = box0;
                c->layout.h = c->layout.contentSizeY;
                box0 += c->layout.contentSizeY;
                break;

            case LayoutStyle::SOUTH:
                box1 -= c->layout.contentSizeY;
                c->layout.y = box1;
                c->layout.h = c->layout.contentSizeY;
                break;
                
            case LayoutStyle::OVERLAY:
                c->layout.y = layout.contentPadding.north;
                c->layout.h = layout.contentSizeY
                    - layout.contentPadding.south - c->layout.y;
                break;

            default:
                break;
            }

            c->calculateLayoutY(dpi);
            c->ev_layout(dpi);
        }
    }

};
