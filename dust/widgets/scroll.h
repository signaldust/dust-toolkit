
#pragma once

#include "dust/gui/panel.h"

namespace dust
{
    static const float  scrollbarSizePt = 6;

    template <bool horizontal>
    struct ScrollbarBase : Panel
    {
        Notify onScroll = doNothing;

        ScrollbarBase()
        {
            setScrollState(0, 1, 1);

            style.minSizeX = (horizontal ? 2 : 1) * scrollbarSizePt;
            style.minSizeY = (horizontal ? 1 : 2) * scrollbarSizePt;

            style.rule = horizontal ? LayoutStyle::SOUTH : LayoutStyle::EAST;
        }

        int getPosition() const { return position; }

        void setPosition(int _position)
        {
            if(_position < 0) _position = 0;

            int maxPos = rangeFull - rangeView;
            if(_position > maxPos) _position = maxPos;

            // if position doesn't change, bail out
            if(_position == position) return;

            // otherwise do the update
            position = _position;

            onScroll();
            redraw();
        }

        void setScrollRange(int _rangeView, int _rangeFull)
        {
            rangeView = _rangeView;
            rangeFull = _rangeFull;
            setPosition(position);
            redraw();   // always redraw
        }

        void setScrollState(int _position, int _rangeView, int _rangeFull)
        {
            position = _position;
            rangeView = _rangeView;
            rangeFull = _rangeFull;
            setPosition(position);
            redraw();   // always redraw
        }

        bool ev_mouse(const MouseEvent & e)
        {
            if(rangeView == rangeFull) return false;

            if(e.type == MouseEvent::tDown && e.button == 1)
            {
                dragPos = position;
                dragOff = horizontal ? e.x : e.y;
                return true;
            }
            if(e.type == MouseEvent::tMove && e.button == 1)
            {
                int delta = (horizontal ? e.x : e.y) - dragOff;
                int range = (horizontal ? layout.w : layout.h);
                setPosition(dragPos + (delta * rangeFull) / range);
                return true;
            }

            return false;
        }

        void render(RenderContext & rc)
        {
            if(rangeView >= rangeFull) return;

            float pt = getWindow()->pt();

            // available framesize in scroll direction
            int fSize = horizontal ? layout.w : layout.h;

            float hSize = scrollbarSizePt * pt;

            // actual available scrolling size:
            float sSize = fSize - hSize;

            // handle offset and length in pixels
            float hPos = (sSize * position) / rangeFull;
            float hLen = (sSize * rangeView) / rangeFull;

            // build a stroke path
            Path p;
            if(horizontal)
            {
                p.move(.5*hSize + hPos, .5*hSize);
                p.line(.5*hSize + hPos + hLen, .5*hSize);
            }
            else
            {
                p.move(.5*hSize, .5*hSize + hPos);
                p.line(.5*hSize, .5*hSize + hPos + hLen);
            }

            float bs = .1f * scrollbarSizePt * pt;
            rc.strokePath(p, .5f * hSize + bs, paint::Color(theme.fgMidColor));
            rc.strokePath(p, .5f * hSize, paint::Color(theme.bgColor));
        }
    private:
        int position;   // client position
        int rangeView;  // client range in view (handle scale)
        int rangeFull;  // client range maximum

        int dragPos, dragOff;


    };

    typedef ScrollbarBase<false>    ScrollbarV;
    typedef ScrollbarBase<true>     ScrollbarH;

    // Scrollpanel
    struct ScrollPanel : Panel
    {
        ScrollPanel()
        {
            content.setParent(this);
            content.style.rule = LayoutStyle::FILL;
            content.style.canScrollX = true;
            content.style.canScrollY = true;

            bottom.setParent(this);
            bottom.style.rule = LayoutStyle::SOUTH;

            spacer.setParent(&bottom);
            spacer.style.rule = LayoutStyle::EAST;
            spacer.style.minSizeX = scrollbarSizePt;
            spacer.style.minSizeY = scrollbarSizePt;

            hscroll.setParent(&bottom);
            hscroll.onScroll = [this]() { this->redraw(); };

            vscroll.setParent(this);
            vscroll.onScroll = [this]() { this->redraw(); };

            // usually makes sense to fill area
            style.rule = LayoutStyle::FILL;
        }

        // we don't actually draw anything, but we do lazy scroll-update
        // this way mouse-overscrolling is not sensitive to event report rate
        void render(RenderContext &)
        {
            int x = hscroll.getPosition(), y = vscroll.getPosition();
            auto & cl = content.getLayout();

            // early out
            if(cl.contentOffsetX == -x && cl.contentOffsetY == -y) return;

            // redraw
            cl.contentOffsetX = -x;
            cl.contentOffsetY = -y;
            content.updateWindowOffsets();
        }

        void reflowChildren()
        {
            // This is potentially a waste, but if we try to layout
            // locally, then we might have to do it twice anyway.
            if(!content.style.canScrollX
            || !content.style.canScrollY)
            {
                reflow();
                return;
            }
            
            // do synchronous reflow, then request redraw
            // it would be really nice to delay this, but then
            // we would also need to delay scroll requests and
            // that's really not entirely reasonable
            layoutAsRoot(getWindow()->getDPI());
            updateScrollBars();

            redraw();
        }

        // scroll point to view
        // if marginX / marginY are non-zero, we try to place the point
        // at least marginX / marginY pixels away from the viewport border
        void scrollToView(int x, int y, int dx, int dy)
        {
            if(!layout.w || !layout.h) return;

            // clip dx/dy so that we can always satisfy them
            if(dx > layout.w / 2) dx = layout.w / 2;
            if(dy > layout.h / 2) dy = layout.h / 2;
            
            // compute what region is actually visible
            int x0 = hscroll.getPosition(), x1 = x0 + layout.w;
            int y0 = vscroll.getPosition(), y1 = y0 + layout.h;

            int deltaX = 0;
            int deltaY = 0;

            if(x - dx < x0) { deltaX = x - dx - x0; }
            if(x + dx > x1) { deltaX = x + dx - x1; }
            if(deltaX) hscroll.setPosition(hscroll.getPosition() + deltaX);
            
            if(y - dy < y0) { deltaY = y - dy - y0; }
            if(y + dy > y1) { deltaY = y + dy - y1; }
            if(deltaY) vscroll.setPosition(vscroll.getPosition() + deltaY);

        }

        // set overscroll ratios [0,1] for x and y
        void setOverscroll(float xratio, float yratio)
        {
            if(xratio < 0) xratio = 0;
            content.overscrollX = xratio;

            if(yratio < 0) yratio = 0;
            content.overscrollY = yratio;
        }

        // FIXME: can this still run into infinite reflow loops?
        //
        // probably the only reliable way is to just put the content
        // under the scroll-bars and require that actual content adds
        // enough padding that things will fit properly.. or just make
        // the scrollbars always take room in the layout
        void updateScrollBars()
        {
            Layout & cl = content.getLayout();

            hscroll.setScrollRange(cl.w, cl.contentSizeX);
            vscroll.setScrollRange(cl.h, cl.contentSizeY);

            bool enableH = content.style.canScrollX; // cl.w <= cl.contentSizeX;
            bool enableV = content.style.canScrollY; // cl.h <= cl.contentSizeY;

            bottom.setEnabled(enableH);
            spacer.setEnabled(enableV);
            vscroll.setEnabled(enableV);
        }

        void ev_layout(float dpi)
        {
            // we need to do this again here for overscroll to work
            // FIXME: hard-coding for size-computation here is a bit ugly
            layoutAsRoot(dpi);
            updateScrollBars();
        }

        bool ev_mouse(const MouseEvent & e)
        {
            if(e.type == MouseEvent::tScroll)
            {
                hscroll.setPosition(hscroll.getPosition() - (int) e.scrollX);
                vscroll.setPosition(vscroll.getPosition() - (int) e.scrollY);
                return true;
            }
            // scroll with middle-button drag 
            if(e.type == MouseEvent::tDown && e.button == 3)
            {
                dragX = e.x;
                dragY = e.y;
                return true;
            }

            if(e.type == MouseEvent::tMove && e.button == 3)
            {
                int deltaX = e.x - dragX; dragX = e.x;
                int deltaY = e.y - dragY; dragY = e.y;
                
                hscroll.setPosition(hscroll.getPosition() - deltaX);
                vscroll.setPosition(vscroll.getPosition() - deltaY);

                return true;
            }
            return false;
        }

        Panel * getContent() { return &content; }

    private:
        struct Content : Panel
        {
            // overscroll relative to viewport size
            // 0 means none, 1 means one full viewport
            float   overscrollX = 0;
            float   overscrollY = 0;

            Layout & getLayout() { return layout; }

            int ev_size_x(float dpi)
            {
                if(!style.canScrollY) return 0;
                debugPrint("ev_size_x: %f", dpi);
                return layout.contentSizeX
                    + (int) ceilf(overscrollX * getParent()->getLayout().w)
                    + (int) ceilf(dpi * scrollbarSizePt / 72.f);
            }
            int ev_size_y(float dpi)
            {
                if(!style.canScrollX) return 0;
                debugPrint("ev_size_y: %f (content size: %d)", dpi, layout.contentSizeY);
                return layout.contentSizeY
                    + (int) ceilf(overscrollY * getParent()->getLayout().h)
                    + (int) ceilf(dpi * scrollbarSizePt / 72.f);
            }
        };

        Content     content;
        Panel       bottom, spacer;

        ScrollbarV  vscroll;
        ScrollbarH  hscroll;

        int         dragX, dragY;

    };
};
