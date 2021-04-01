
#pragma once

#include "dust/gui/control.h"

namespace dust
{
    static const float  xyHandlePt = 12;
    
    struct XYPad : Control
    {
        // called when value changes due to user actions
        Notify  onValueChange = doNothing;
        
        XYPad()
        {
            style.minSizeX = xyHandlePt * 4;
            style.minSizeY = xyHandlePt * 4;

            style.rule = LayoutStyle::FILL;
        }

        float getX() const { return valX; }
        float getY() const { return valY; }

        bool ev_mouse(const MouseEvent & ev)
        {
            if(ev.type == MouseEvent::tDown && ev.button == 1)
            {
                dragX = ev.x;
                dragY = ev.y;

                relX = valX;
                relY = valY;

                return true;
            }

            if(ev.type == MouseEvent::tMove && ev.button == 1)
            {
                float pt = getWindow()->pt();
                float m = .5 * xyHandlePt * pt;
            
                float dx = (ev.x - dragX) / (layout.w - 4*m);
                float dy = (ev.y - dragY) / (layout.h - 4*m);

                float x = relX + dx;
                float y = relY + dy;

                if(x < 0) x = 0;
                if(x > 1) x = 1;

                if(y < 0) y = 0;
                if(y > 1) y = 1;

                valX = x;
                valY = y;

                onValueChange();

                redraw();
                
                return true;
            }

            if(ev.type == MouseEvent::tUp && ev.button == 1)
            {
                return true;
            }

            return false;
        }

        void render(RenderContext & rc)
        {
            float pt = getWindow()->pt();
            float m = .5 * xyHandlePt * pt;

            Path p;

            p.rect(m, m, layout.w - m, layout.h - m, m);
            rc.fillPath(p, paint::Color(theme.bgColor));

            // don't clear path here, we'll draw a border

            p.move(.5*layout.w, m).line(.5*layout.w, layout.h-m);
            p.move(m, .5*layout.h).line(layout.w-m, .5*layout.h);

            rc.strokePath(p, .75f*pt, paint::Color(theme.midColor));
            p.clear();

            float kx = (layout.w - 4*m) * valX;
            float ky = (layout.h - 4*m) * valY;

            p.rect(m+kx, m+ky, 3*m+kx, 3*m+ky, m);
            rc.fillPath(p, paint::Color(theme.goodColor));
        }

    protected:
        float valX = .5f;
        float valY = 0.f;

        float relX, relY;
        int dragX, dragY;
    };
    
};