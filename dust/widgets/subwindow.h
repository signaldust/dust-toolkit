
#pragma once

#include "dust/gui/panel.h"
#include "button.h"

// NOTE: This is draft status and we might consider doing what we do with tabs
// and simply making the subwindows themselves internal to SubwindowArea
namespace dust
{
    // This can be used as a SubWindow container to avoid useless reflows
    //
    // It should NOT be used as a parent for anything else.
    struct SubwindowArea : Panel { void reflowChildren() { } };

    // This is an MDI-like sub-window that's contained inside it's parent
    struct Subwindow : Panel
    {
        static const unsigned   titleHeightPt = 15;
        static const unsigned   borderSizePt = 3;

        struct TitleButton : ButtonBase
        {
            ARGB color = 0xffff0000;
            
            TitleButton()
            {
                style.rule = LayoutStyle::WEST;
                style.minSizeX = titleHeightPt;
                style.minSizeY = titleHeightPt;
            }

            void render(RenderContext & rc)
            {
                Path p;
                float c = .5f * layout.w;
                p.arc(c, c, .5f*c, 0, 2*acos(-1.f), true);


                ARGB blended = color::lerp(color, theme.fgColor, 0x80);
                
                rc.fillPath(p, paint::Color(isMouseOver
                    ? blended : color::lerp(blended, theme.bgColor, 0x80)));
                    
                rc.strokePath(p, getWindow()->pt()
                    , paint::Color(isMousePressed
                    ? blended : color::lerp(blended, theme.bgColor, 0x80)));
            }
        };
    
        // this is basically how much we force to be within parent
        static const unsigned   titleMinVisiblePt = 4 * titleHeightPt;

        TitleButton             btnClose;
        Label                   title;
        
        Panel & getContent() { return contentPanel; }
        
        Subwindow()
        {
            style.rule = LayoutStyle::MANUAL;
            
            contentPanel.style.padding.north = titleHeightPt;
            contentPanel.style.padding.south = borderSizePt;
            
            contentPanel.style.padding.east = borderSizePt;
            contentPanel.style.padding.west = borderSizePt;

            // don't allow silly small windows
            contentPanel.style.minSizeX = titleMinVisiblePt;
            contentPanel.setParent(this);

            frame.setParent(this);
            titleBar.style.rule = LayoutStyle::NORTH;
            titleBar.setParent(frame);
            btnClose.setParent(titleBar);
            title.setParent(titleBar);

            monitorMouseDown.setParent(this);
            monitorMouseDown.onMouseDown =
            [this]() { setParent(getParent()); redraw(true); };
        }

        void clipPosition(float dpi)
        {
            layout.x = std::min(layout.x,
                (int) (getParent()->getLayout().w - dpi*(titleMinVisiblePt/72.f)));
            layout.x = std::max(layout.x, 0);

            layout.y = std::min(layout.y,
                (int) (getParent()->getLayout().h - dpi*(titleHeightPt/72.f)));
            layout.y = std::max(layout.y, 0);

            getParent()->updateWindowOffsets();
        }

        void ev_layout(float dpi)
        {
            layoutAsRoot(dpi);

            float pt = dpi / 72.f;
            if(getParent()) clipPosition(dpi);
        }

        bool ev_mouse(const MouseEvent & ev)
        {
            if(ev.type == MouseEvent::tScroll) return false;

            if(ev.type == MouseEvent::tDown && ev.button == 1)
            {
                dragX = ev.x;
                dragY = ev.y;
            }

            if(ev.type == MouseEvent::tMove && ev.button == 1)
            {
                redraw(true);
                
                layout.x += ev.x - dragX;
                layout.y += ev.y - dragY;

                clipPosition(getWindow()->getDPI());

                redraw(true);
            }
            
            return true;
        }

        void setTitle(const char * txt) { title.setText(txt); }

        void reflowChildren()
        {
            auto * win = getWindow();
            if(!win) return;
            ev_dpi(win->getDPI());
            reflow();
        }
        
        void ev_dpi(float dpi)
        {
            computeSize((unsigned&)layout.w, (unsigned&)layout.h, dpi);
        }

        void render(RenderContext & rc)
        {
            float pt = getWindow()->pt();

            float c = .5f * titleHeightPt * pt;

            Path p;
            float bb = .5 * borderSizePt*pt;
            p.rect(bb, bb, layout.w-bb, layout.h-bb, .5f*c);
            rc.strokePath(p, bb, paint::Color(theme.bgMidColor));
            rc.fillPath(p, paint::Color(theme.bgColor));
        }

    private:
        Panel           frame;
        Panel           titleBar;
        Panel           contentPanel;

        int             dragX, dragY;

        // We add this as a layer on top of content panel to
        // check for mouse down events -> pop window to top
        struct MonitorMouseDown : Panel
        {
            Notify      onMouseDown;
            bool ev_mouse(const MouseEvent & ev)
            {
                if(ev.type == MouseEvent::tDown) onMouseDown();
                // never consume anything
                return false;
            }
        } monitorMouseDown;
    };
}
