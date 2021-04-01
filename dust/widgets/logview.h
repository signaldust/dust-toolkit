#pragma once

namespace dust
{
    // Simple read-only multi-line text control
    struct LogView : Panel
    {
        Font    _font;
        
        unsigned tabStop = 8;

        ARGB    fgColor;
        ARGB    bgColor;

        // called when clicking lines looking like <filename>:<num>:<num>:
        // this is what at least clang errors look like
        std::function<void(const char*,int,int)> onClickError
            = [](const char*,int,int){};

        LogView()
        {
            style.rule = LayoutStyle::FILL;

            bgColor = theme.bgColor;
            fgColor = theme.fgColor;

            sizeX = 0;
            sizeY = 0;
        }
        
        void clear()
        { 
            buffer.clear();
            stopScroll = false;
            recalculateSize(); 
        }
        
        void append(const char * txt, unsigned n)
        {
            buffer.insert(buffer.end(), txt, txt + n);
            recalculateSize(); 
        }

        // FIXME: make this use components like labels
        Font & getFont()
        {
            if(_font.valid()) return _font;

            // fall-back if no font can be found
            Window * win = getWindow();
            if(win)
            {
                // default to monospace even if we can handle proportional
                _font.loadDefaultFont(7, win->getDPI(), true);
                recalculateSize();
            }

            return _font;
        }

        void ev_dpi(float dpi)
        {
            Font & font = getFont();
            if(font->parameters.dpi != dpi)
            {
                font.setDPI(dpi);
                recalculateSize();
            }
        }

        // FIXME: can we have TextBuffer implement utf8 iterator directly?
        void recalculateSize()
        {
            Font & font = getFont();
            if(!font.valid()) return;

            int lines = 1, lineHeight = (int)ceil(font->getLineHeight());

            float w = 0, x = 0;

            // assume (for now) all digits are same size
            // this is safe for most sensible fonts
            float dw = font->getCharAdvanceW('0');

            // use space width for tabStops
            // only matters for proportional fonts
            float sw = font->getCharAdvanceW(' ');

            utf8::Decoder   decoder;
            for(auto byte : buffer)
            {
                // keep going until we have a full char
                if(!decoder.next(byte)) continue;
                auto ch = decoder.ch;

                // check newlines
                if(ch == '\n') { x = 0; ++lines; continue; }

                // check for tabs
                if(ch == '\t')
                {
                    x += tabStop*sw - fmod(x, tabStop*sw);
                    continue;
                }

                x += font->getCharAdvanceW(decoder.ch);
                if(w < x) w = x;
            }

            // handle trailing invalid unicode
            if(decoder.state != utf8::ACCEPT)
            {
                x += font->getCharAdvanceW(decoder.ch);
                if(w < x) w = x;
            }

            sizeX = (int) ceilf(w);
            sizeY = lines * lineHeight;

            reflow();   // do reflow first so we can hope to scroll

            // reset to very bottom unless manually scrolled
            if(!stopScroll) scrollToView(0, sizeY);
        }

        int ev_size_x(float dpi) { return sizeX; }
        int ev_size_y(float dpi) { return sizeY; }

        void ev_mouse_exit()
        {
            hoverLine = -1;
        }
        
        bool ev_mouse(const MouseEvent & e)
        {
            Font & font = getFont();
            if(!font.valid()) return false;

            // pass scroll to parent
            if(e.type == MouseEvent::tScroll)
            {
                stopScroll = true;
                return false;
            }
            
            if(e.type == MouseEvent::tMove && !e.button)
            {
                hoverLine = int(e.y - font->getDescent())
                    / (int)ceil(font->getLineHeight());
                redraw();
            }
        
            // try to parse for error positions
            if(e.type == MouseEvent::tDown && e.button == 1)
            {
                int line = 0, lineHeight = (int)ceil(font->getLineHeight());
                int wantLine = int(e.y - font->getDescent())
                    / (int)ceil(font->getLineHeight());

                int colons = 0;

                std::vector<uint8_t>    filename;
                int errLine = 0, errCol = 0;
                
                for(auto byte : buffer)
                {
                    // check newlines
                    if(byte == '\n') { ++line; continue; }
                    
                    // found the correct line
                    if(line == wantLine)
                    {
                        if(byte == ':') { ++colons; continue; }
                        switch(colons)
                        {
                        case 0: filename.push_back(byte); break;
                        case 1:
                            if(byte < '0' || byte > '9') return true;
                            errLine = errLine * 10 + byte - '0';
                            break;
                        case 2:
                            if(byte < '0' || byte > '9') return true;
                            errCol = errCol * 10 + byte - '0';
                            break;
                        case 3:
                            if(!filename.size()) return true;
                            filename.push_back(0);
                            onClickError(
                                (const char*)filename.data(), errLine, errCol);
                            return true;
                        }
                    }
                }
            }
            return true;
        }

        void render(RenderContext & rc)
        {
            Font & font = getFont();
            if(!font.valid()) return;

            // for drawing margins, undo parent's contentOffsetX
            // this keeps margin fixed when parent is scrolling
            // using context offset ensures stable float rounding
            RenderContext rcMargin(rc,
                -getParent()->getLayout().contentOffsetX, 0);

            rc.clear(bgColor);

            int line = 0, lineHeight = (int)ceil(font->getLineHeight());

            float dw = font->getCharAdvanceW('0');
            float sw = font->getCharAdvanceW(' ');

            float x = 0, y = lineHeight - font->getDescent();

            // don't bother with outputting characters
            // that are above or below the current view
            const Rect & clip = rc.getClipRect();

            ARGB midColor = color::lerp(bgColor, fgColor, 0x40);
            int linePx = (int) getWindow()->pt();
            if(line == hoverLine)
                rc.fillRect(paint::Color(midColor),
                    0, int(y)+2*linePx, layout.w, linePx);

            utf8::Decoder   decoder;
            for(auto byte : buffer)
            {
                // keep going until we have a full char
                if(!decoder.next(byte)) continue;

                auto ch = decoder.ch;

                int lineY = (int) (y-font->getAscent());
                bool skipHidden = (lineY > clip.y1
                    || lineY + lineHeight < clip.y0);

                
                // check newlines
                if(ch == '\n')
                {
                    x = 0; ++line;
                    y += lineHeight;
                    
                    if(line == hoverLine)
                        rc.fillRect(paint::Color(midColor),
                            0, int(y)+2*linePx, layout.w, linePx);
                            
                    continue;
                }

                // check for tabs
                if(ch == '\t')
                {
                    x += tabStop*sw - fmod(x, tabStop*sw);
                    continue;
                }

                // actual rendering, we can skip this part
                if(skipHidden) continue;

                x += rc.drawChar(font, decoder.ch,
                    paint::Color(fgColor), x, y);
                
            }

            // handle trailing invalid unicode
            if(decoder.state != utf8::ACCEPT)
            {
                x += rc.drawChar(font, utf8::invalid,
                    paint::Color(fgColor), x, y);
            }
        }

    protected:

        std::vector<char>   buffer;

        int         sizeX;
        int         sizeY;

        int         hoverLine = -1;

        bool        stopScroll = false;
    };

};
