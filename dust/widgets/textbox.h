
#pragma once

#include "dust/core/utf8.h"
#include "dust/gui/window.h"
#include "text_buffer.h"

namespace dust
{
    static const float  textBoxBorderPt = 6;

    // This is essentially a simplified version of TextArea
    // that is limited to a single line of text.
    //
    // FIXME: should really merge the two into one?
    struct TextBox : Panel
    {
        Notify  onEnter = doNothing;
        Notify  onShiftEnter = [this](){ onEnter(); };
        
        Notify  onEscape = doNothing;
        Notify  onTab = doNothing;
        
        // this is called on keypress when we reset color
        Notify  onResetColor = doNothing;

        std::string wordSeparators = " \n\t\"\'()[]{}<>=&|^~!.,:;+-*/%";

        Font    _font;

        // cursorColor is reset to default after every keydown
        ARGB    cursorColor = 0;

        TextBox()
        {
            style.rule = LayoutStyle::FILL;
        }

        // FIXME: make this use components like labels
        Font & getFont()
        {
            if(_font.valid()) return _font;

            // fall-back if no font can be found
            Window * win = getWindow();

            // default to monospace even if we can handle proportional
            _font.loadDefaultFont(9, win ? win->getDPI() : 96, true);
            recalculateSize();

            return _font;
        }

        // focus the control and select it's contents
        void focusSelectAll()
        {
            focus();
            buffer.doSelectAll();
        }

        void ev_dpi(float dpi)
        {
            Font & font = getFont();
            font.setDPI(dpi);

            borderSize = (int) ceilf(textBoxBorderPt * dpi / 72.f);
        }

        int ev_size_x(float dpi)
        {
            // request at least 2 digits worth of space, to make sure
            // that the control is at least theoretically usable
            return (int) ceilf(2*borderSize + 2*getFont()->getCharAdvanceW('0'));
        }

        int ev_size_y(float dpi)
        {
            return (int) ceilf(2*borderSize + getFont()->getLineHeight());
        }

        // append contents into vector
        void outputContents(std::vector<char> & out)
        {
            unsigned offset = out.size();
            out.reserve(offset + buffer.getSize());

            for(auto byte : buffer) out.push_back(byte);
        }

        void recalculateSize()
        {
            Font & font = getFont();
            if(!font.valid()) return;

            int lineHeight = 1+(int)(font->getLineHeight());

            float w = 0, x = 0;

            int cursorX = 0;

            unsigned bytePos = 0;

            utf8::Decoder   decoder;
            for(auto byte : buffer)
            {
                if(bytePos == buffer.getCursor())
                {
                    cursorX = int(x);
                }
                ++bytePos;

                // keep going until we have a full char
                if(!decoder.next(byte)) continue;
                auto ch = decoder.ch;

                x += font->getCharAdvanceW(decoder.ch);
                if(w < x) w = x;
            }

            // handle trailing invalid unicode
            if(decoder.state != utf8::ACCEPT)
            {
                x += font->getCharAdvanceW(decoder.ch);
                if(w < x) w = x;
            }

            if(bytePos == buffer.getCursor())
            {
                cursorX = int(x);
            }

            // we don't draw digits here, but still use
            // digit-width to add a bit of slack to the right
            float dw = font->getCharAdvanceW('0');
            contentSize = (int) (w + dw);

            scrollContent(cursorX - (int) (5*dw));
            scrollContent(cursorX + (int) (5*dw));
            scrollContent(cursorX);

            // whenever we recalculate we also want to draw
            redraw();
        }

        void scrollContent(int x)
        {
            int maxPos = contentSize + 2*borderSize - layout.w;
            if(x > maxPos) x = maxPos;
            if(x < 0) x = 0;

            contentOffset = x;

            // send a scrollToView too, in case we're inside
            // some srolling panel
            scrollToView(x - contentOffset, layout.w);
        }

        void render(RenderContext & rcFrame)
        {
            Font & font = getFont();
            if(!font.valid()) return;

            bool haveFocus = getWindow()->getFocus() == this;

            // use this for border as well
            ARGB selectionColor = theme.selColor;

            // draw a border
            {
                float b = .5f * borderSize;
                float w = layout.w - b;
                float h = layout.h - b;
                Path p;
                p.move(b, b);
                p.line(b, h);
                p.line(w, h);
                p.line(w, b);
                p.close();

                rcFrame.strokePath(p, .5f * b,
                    paint::Color(haveFocus ? cursorColor : selectionColor));
                rcFrame.fillPath(p, paint::Color(theme.bgColor));
            }

            int lineHeight = 1+(int)(font->getLineHeight());

            // try to center vertically
            int offsetY = (int)(.5f*(layout.h - lineHeight));

            // offset context for actual drawing into a content rect
            Rect    content(borderSize, borderSize,
                layout.w - 2*borderSize, layout.h - 2*borderSize);
            RenderContext   rc(rcFrame, content,
                borderSize-contentOffset, offsetY);

            ARGB cursorUseColor = cursorColor;
            if(!haveFocus) { cursorUseColor = 0; }

            float dw = font->getCharAdvanceW('0');
            float x = 0, y = lineHeight - font->getDescent();

            // when inSelection is true, selectX gives the filled
            // pixel coordinate on current line to allow pixel fills
            bool inSelection = false;
            int selectX;

            unsigned selectStart = buffer.getSelectionStart();
            unsigned selectEnd = buffer.getSelectionEnd();

            unsigned bytePos = 0;

            int cursorSize = (int)ceil(getWindow()->pt());

            utf8::Decoder   decoder;
            for(auto byte : buffer)
            {
                if(!inSelection && bytePos == selectStart)
                {
                    inSelection = true;
                    selectX = (int)(x);
                }
                if(bytePos == selectEnd) inSelection = false;

                if(buffer.getCursor() == bytePos)
                {
                    rc.fillRect(paint::Color(cursorUseColor),
                        (int)(x),
                        (int)(y - font->getAscent()),
                        cursorSize, lineHeight);

                    // if we're in selection, skip the cursor
                    // as far as selection high-light goes
                    if(inSelection) selectX += cursorSize;
                }
                ++bytePos;

                // keep going until we have a full char
                if(!decoder.next(byte)) continue;
                auto ch = decoder.ch;

                if(inSelection)
                {
                    // see TextArea for comments about this
                    int nextX = (int)ceil(x + font->getCharAdvanceW(decoder.ch));

                    rc.fillRect(paint::Color(selectionColor),
                        selectX, (int)(y - font->getAscent()),
                        nextX - selectX, lineHeight);
                    selectX = nextX;
                }

                x += rc.drawChar(font, decoder.ch,
                    paint::Color(theme.fgColor), x, y);
            }

            // handle trailing invalid unicode
            if(decoder.state != utf8::ACCEPT)
            {
                if(bytePos == selectEnd)
                {
                    int nextX = (int)(x + font->getCharAdvanceW(utf8::invalid));

                    rc.fillRect(paint::Color(selectionColor),
                        selectX, (int)(y - font->getAscent()),
                        nextX - selectX, lineHeight);
                }

                x += rc.drawChar(font, utf8::invalid,
                    paint::Color(theme.fgColor), x, y);
            }

            if(buffer.getCursor() == bytePos)
            {
                rc.fillRect(paint::Color(cursorUseColor),
                    (int)(x), (int)(y - font->getAscent()),
                    cursorSize, lineHeight);
            }
        }

        unsigned findMouse(int mx, int my)
        {
            // font must always be valid, so just crash if it's not!
            Font & font = getFont();

            float x = 0;

            unsigned bytePos = 0, charPos = 0, prevCharPos = 0;

            if(mx < x) return bytePos;

            utf8::Decoder   decoder;
            for(auto byte : buffer)
            {
                ++bytePos;

                // keep going until we have a full char
                if(!decoder.next(byte)) continue;
                auto ch = decoder.ch;

                prevCharPos = charPos;
                charPos = bytePos;

                // get advance width
                float cw = font->getCharAdvanceW(decoder.ch);

                // check if mouse is to the left of the glyph center
                if(mx < x + .5f*cw) { return prevCharPos; }

                x += cw;
            }

            // if we didn't find a position return the very end
            return bytePos;
        }

        void resetColor()
        {
            // reset color on focus
            if(cursorColor != theme.actColor)
            {
                cursorColor = theme.actColor;
                onResetColor();
                redraw();
            }
        }

        void ev_focus(bool gained)
        {
            resetColor();
            
            redraw();
        }

        bool ev_mouse(const MouseEvent & ev)
        {
            if(ev.type == MouseEvent::tDown && ev.button == 1)
            {
                focus();

                bool keepSel = ev.keymods & KEYMOD_SHIFT;
                buffer.setCursor(findMouse(ev.x, ev.y), keepSel);

                dragWords = ev.nClick > 1;
                if(dragWords) buffer.doSelectWords(wordSeparators.c_str());

                recalculateSize();  // really just for scrolling
                return true;
            }

            if(ev.type == MouseEvent::tMove && ev.button == 1)
            {
                buffer.setCursor(findMouse(ev.x, ev.y), true);
                
                if(dragWords) buffer.doSelectWords(wordSeparators.c_str());

                recalculateSize();  // really just for scrolling
                return true;
            }

            return false;
        }

        bool ev_key(Scancode vk, bool pressed, unsigned mods)
        {
            if(!pressed) return false;
            bool keepSel = mods & KEYMOD_SHIFT;

            if(mods & KEYMOD_CMD)
            switch(vk) // with command-shortcut key
            {
                case SCANCODE_Z: resetColor();
                    keepSel ? buffer.doRedo() : buffer.doUndo(); break;
                case SCANCODE_A:  resetColor(); buffer.doSelectAll(); break;

                case SCANCODE_X: resetColor(); buffer.doCut(); break;
                case SCANCODE_C: resetColor(); buffer.doCopy(); break;
                case SCANCODE_V: resetColor(); buffer.doPaste(); break;

                default: return false;
            }
            else
            switch(vk) // not holding command shortcut
            {
                case SCANCODE_RETURN:
                case SCANCODE_RETURN2:
                    if(keepSel) onShiftEnter(); else onEnter(); break;

                case SCANCODE_ESCAPE: onEscape(); break;

                case SCANCODE_BACKSPACE: resetColor(); buffer.doBackspace(1); break;
                case SCANCODE_DELETE: resetColor(); buffer.doDelete(); break;

                case SCANCODE_UP:   // FIXME: keep history?
                case SCANCODE_HOME:
                    buffer.setCursor(0, keepSel); break;

                case SCANCODE_DOWN: // FIXME: keep history?
                case SCANCODE_END:
                    buffer.setCursor(buffer.getSize(), keepSel); break;

                case SCANCODE_LEFT: buffer.moveBack(keepSel); break;
                case SCANCODE_RIGHT: buffer.moveForward(keepSel); break;

                case SCANCODE_TAB: onTab(); break;
                
                default: return false;
            }

            recalculateSize();
            return true;
        }

        void ev_text(const char * txt)
        {
            resetColor();
            buffer.doText(txt);
            recalculateSize();
        }


    protected:
        TextBuffer  buffer;

        // handle scrolling logic
        int borderSize;
        int contentSize;
        int contentOffset;

        bool    dragWords;
        bool    dragAll;
    };
};
