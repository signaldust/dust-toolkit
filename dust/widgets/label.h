
#pragma once

#include "dust/gui/window.h"

#include <string>

namespace dust
{
    extern ComponentManager<Font, Window>  LabelFont;

    struct Label : Panel
    {
        // font specific to this label
        // when invalid, window-global label font is used
        Font    font;
        ARGB    color = 0;  // use theme().fgColor

        Label()
        {
            style.rule = LayoutStyle::WEST; // FIXME!

            sizeX = 0;
            sizeY = 0;
        }

        // return the current label font for a window
        Font & getFont(Window * win)
        {
            if(font.valid(win->getDPI())) return font;

            Font & winfont = LabelFont.getReference(win);
            if(!winfont.valid(win->getDPI()))
                winfont.loadDefaultFont(8, win->getDPI());

            return winfont;
        }

        void recalculateSize()
        {
            Window * win = getWindow();
            if(!win) return;

            Font & font = getFont(win);

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

        void setText(const std::string & txt) { setText(txt.c_str()); }

        void render(RenderContext & rc)
        {
            Font & font = getFont(getWindow());

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
