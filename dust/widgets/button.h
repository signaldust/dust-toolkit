
#pragma once

#include "dust/gui/panel.h"
#include "dust/widgets/label.h"

namespace dust
{
    // ButtonBase implements the functionality of a basic button
    // it tracks mouse-over state but doesn't include rendering
    //
    struct ButtonBase : Panel
    {
        Notify onClick = doNothing;

        // if set to false, don't trigger redraw on hover change
        bool    trackHover = true;

        ButtonBase()
        {
            isMouseOver = false;
            isMousePressed = false;
        }

        bool ev_mouse(const MouseEvent & ev) override
        {
            if(ev.type == MouseEvent::tMove)
            {
                Rect r(0, 0, layout.w, layout.h);
                bool mouseOver = r.test(ev.x, ev.y);

                if(mouseOver != isMouseOver)
                {
                    isMouseOver = mouseOver;
                    if(trackHover || ev.button) redraw();
                }

                return true;
            }

            if(ev.button == 1)
            {
                switch(ev.type)
                {
                case MouseEvent::tDown:
                    isMousePressed = true;
                    redraw();

                    break;

                case MouseEvent::tUp:
                    if(isMouseOver) onClick();
                    isMousePressed = false;
                    redraw();

                    break;
                default: break;
                }

                return true;
            }

            return false;
        }

        void ev_mouse_exit() override
        {
            if(isMouseOver)
            {
                isMouseOver = false;
                if(trackHover) redraw();
            }
        }

    protected:

        bool    isMouseOver;    // true if mouse is over control
        bool    isMousePressed; // true if mouse is pressed
    };

    // FIXME: these should probably be runtimme configurable?
    static const float buttonRoundingPt = 3;
    static const float buttonMarginPt = 3;

    // Basic button with basic rendering
    struct Button : ButtonBase
    {
        Button()
        {
            style.minSizeX = 2*buttonRoundingPt;
            style.minSizeY = 2*buttonRoundingPt;

            float padding = 2*buttonMarginPt;

            style.padding.north = padding;
            style.padding.south = padding;
            style.padding.east = padding + buttonRoundingPt;
            style.padding.west = padding + buttonRoundingPt;
        }

    private:
        void render(RenderContext & rc) override
        {
            float pt = getWindow()->pt();
            float pad = (buttonRoundingPt+buttonMarginPt) * pt;
            pad = (std::min)(pad, .5f*layout.w);
            pad = (std::min)(pad, .5f*layout.h);

            // put margin around the raw rectangle
            float m = pad*buttonMarginPt / (buttonMarginPt+buttonRoundingPt);
            float w = layout.w - m;
            float h = layout.h - m;

            float cr = pad - m;

            // build rounded rectangle
            Path p;
            p.move(m+cr, m);
            p.line(w-cr, m); p.quad(w, m, w, m+cr);
            p.line(w, h-cr); p.quad(w, h, w-cr, h);
            p.line(m+cr, h); p.quad(m, h, m, h-cr);
            p.line(m, m+cr); p.quad(m, m, m+cr, m);

            bool down = (isMousePressed && isMouseOver);
            bool glow = (isMouseOver || isMousePressed);

            rc.strokePath(p, 2*pt,
                paint::Gradient2(
                    glow ? theme.fgColor : theme.fgMidColor, 0, -1,
                    theme.selColor, 0, down ? 0 : layout.h));
            rc.fillPath(p, paint::Color(theme.bgMidColor));
        }
    };

    struct TextButton : Button
    {
        Label   label;

        TextButton() { label.setParent(this); }
    };
};
