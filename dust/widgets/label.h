
#pragma once

#include "dust/gui/window.h"

#include <string>

namespace dust
{
    struct Label : Panel
    {
        Font    font;
        ARGB    color = 0;  // use theme().fgColor

        Label()
        {
            style.rule = LayoutStyle::WEST; // FIXME!

            sizeX = 0;
            sizeY = 0;

            font.loadDefaultFont(8, 96.f);
        }

        void recalculateSize()
        {
            auto * win = getWindow();
            if(!font.valid(win ? win->getDPI() : 96.f)) return;

            // calculate label size
            sizeX = (int) ceil(font->getTextWidth(txt));
            sizeY = (int) ceil(font->getLineHeight());

            reflow();
        }

        void setText(const char * txt)
        {
            this->txt = txt;
            recalculateSize();
        }

        void setText(const std::string & txt){ setText(txt.c_str()); }

        void render(RenderContext & rc)
        {
            if(!font.valid(getWindow()->getDPI())) return;

            // try to center vertically?
            rc.drawCenteredText(font, txt,
                paint::Color(color ? color : theme.fgColor), .5f*layout.w,
                font->getAscent() + .5f * (layout.h - font->getLineHeight()));
        }

        void ev_dpi(float dpi)
        {
            recalculateSize();
        }

        int ev_size_x(float dpi) { return sizeX; }
        int ev_size_y(float dpi) { return sizeY; }

    private:
        std::string txt;

        int sizeX, sizeY;
    };
};
