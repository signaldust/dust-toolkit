
#pragma once

#include <cstdio>
#ifndef _WIN32
# include <sys/stat.h>
# include <unistd.h>
#else
# include <io.h>
#endif

#include <memory>

#include "dust/core/utf8.h"
#include "dust/gui/window.h"

#include "dust/regex/lore.h"    // for search

#include "text_buffer.h"

namespace dust
{
    // FIXME: this should eventually become C-API for plugins
    struct SyntaxParser
    {
        virtual ~SyntaxParser() {}
        
        // return characters that break "select word"
        virtual const char * wordSeparators() { return 0; }

        // reset parser and set the callback for attribute output
        virtual void start(void (*set)(void*ptr,TextAttrib*a), void*ptr) = 0;

        // parse a single unicode codepoint at a given position
        virtual void parse(unsigned pos, unsigned ch) = 0;

        // flush state at end of file; output at end of file
        virtual void flush() = 0;
    };

    // Multi-line text-area - should be placed inside a scrolling panel
    //
    // supports "bunch of stuff" to make it a more realistic editor
    struct TextArea : Panel
    {
        // called when textarea gains focus
        Notify  onFocus = doNothing;

        // called when something (even cursor position) changes
        Notify  onUpdate = doNothing;

        // called on right click
        std::function<void(MouseEvent const &)> onContextMenu = doNothing;

        Font    _font;
        
        bool    showLineNumbers = true;
        bool    autoCloseParens = false;
        
        unsigned tabStop = 4;
        unsigned wrapMark = 80;

        ARGB    cursorColor;

        ARGB    commentColor;
        ARGB    literalColor;
        ARGB    operatorColor;

        // parenColors are automatically used for ()[]{}
        // the array contents are cycled based on nesting
        std::vector<ARGB>   parenColors;

        std::unique_ptr<SyntaxParser> syntaxParser = 0;
        
        const char * wordSeparators()
        {
            const char * p = syntaxParser ? syntaxParser->wordSeparators() : 0;
            if(!p) p = " \n\t\"\'()[]{}<>=&|^~!?.,:;+-*/%$";
            return p;
        }

        TextArea()
        {
            style.rule = LayoutStyle::FILL;

            cursorColor = 0x8040FFFF;

            // in before ugly coder colors
            commentColor = 0xff8899dd;
            literalColor = 0xffaabb88;
            operatorColor = 0xffaa8899;

            // we tint these below
            parenColors.push_back(0xffff0000);
            parenColors.push_back(0xffffff00);
            parenColors.push_back(0xff00ff00);
            parenColors.push_back(0xff00ffff);
            parenColors.push_back(0xff0000ff);
            parenColors.push_back(0xffff00ff);

            // fade towards fgColor
            for(auto & c : parenColors)
            {
                c = color::lerp(c, theme.fgColor, 0xB0);
            }

            sizeX = 0;
            sizeY = 0;
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

        // called by doSearch, not really useful otherwise
        void doReplaceForSelection(lore::Matcher & m, const char * replace)
        {
            std::vector<char>   subst;

            for(int i = 0; replace[i]; ++i)
            {
                // if we have a valid group escape
                if(replace[i] == '\\')
                {
                    ++i;
                    if(replace[i] >= '0' && replace[i] <= '9')
                    {
                        int g = replace[i] - '0';
                        unsigned p0 = m.getGroupStart(g);
                        unsigned p1 = m.getGroupEnd(g);

                        for(int j = p0; j < p1; ++j)
                        {
                            const char * byte = buffer.getByteAt(j);
                            if(byte) subst.push_back(*byte);
                        }
                    }
                }
                else
                {
                    subst.push_back(replace[i]);
                }
            }

            buffer.doText(subst.data(), subst.size());
            recalculateSize();
        }

        // search for a pattern with regex, return number of matches
        // sets "matchIndex" to the index of the selected match
        unsigned doSearch(lore::Regex & re, bool findPrev,
            unsigned & matchIndex, const char * replace = 0)
        {
            struct Match { unsigned p0, p1; };
            std::vector<Match> matches;

            lore::Matcher m(re);

            // get iterators for the contents
            auto itr = buffer.begin();
            auto end = buffer.end();

            // also track beginning of current line
            auto lineStart = itr;

            // position within the line and at start
            unsigned pos = 0, posLine = 0;

            // loop buffer
            m.start(pos);
            while(itr != end)
            {
                char byte = *itr;

                ++itr; ++pos;

                // if this is regular byte, do regular match
                if(byte != '\n')
                {
                    if(!m.next(byte)) continue;

                    // if we have a match, record it
                    if(m.valid())
                    {
                        unsigned p0 = m.getGroupStart(0);
                        unsigned p1 = m.getGroupEnd(0);

                        // LORE should no longer return empty matches as valid!
                        if(p0 == p1)
                        {
                            dust::debugPrint("WARNING: Empty match?!?\n");
                            return 0;
                        }
                        
                        // here we just ignore empty matches
                        if(replace
                        && p0 == buffer.getSelectionStart()
                        && p1 == buffer.getSelectionEnd())
                        {
                            doReplaceForSelection(m, replace);
                            // break and do a new search
                            // because we modified data
                            return doSearch(re, findPrev, matchIndex);
                        } else matches.push_back({ p0, p1 });

                        // if not anchored, restart after match
                        if(!re.onlyAtBeginning())
                        {
                            itr = lineStart;
                            pos = posLine;

                            // forward, but not past the end of file
                            while(pos < p1)
                            {
                                ++itr; ++pos;
                                if(itr == end) break;
                            }

                            m.start(pos);
                            continue;
                        }
                    }
                }

                // did we hit the end of line
                if(byte == '\n')
                {
                    m.end();

                    if(m.valid())
                    {
                        unsigned p0 = m.getGroupStart(0);
                        unsigned p1 = m.getGroupEnd(0);

                        // here we just ignore empty matches
                        if(p0 != p1)
                        {
                            if(replace
                            && p0 == buffer.getSelectionStart()
                            && p1 == buffer.getSelectionEnd())
                            {
                                doReplaceForSelection(m, replace);
                                // break and do a new search
                                // because we modified data
                                return doSearch(re, findPrev, matchIndex);
                            }
                            else matches.push_back({ p0, p1 });
                        }
                    }

                    // setup for next line
                    posLine = pos;
                    lineStart = itr;

                    m.start(pos);
                    continue;
                }

            }

            // check for final match after loop
            m.end();
            if(m.valid())
            {
                unsigned p0 = m.getGroupStart(0);
                unsigned p1 = m.getGroupEnd(0);

                // LORE should no longer return empty matches as valid!
                if(p0 == p1)
                {
                    dust::debugPrint("WARNING: Empty match?!?\n");
                    return 0;
                }
                
                if(replace
                && p0 == buffer.getSelectionStart()
                && p1 == buffer.getSelectionEnd())
                {
                    doReplaceForSelection(m, replace);
                    // break and do a new search
                    // because we modified data
                    return doSearch(re, findPrev, matchIndex);
                }
                else matches.push_back({ p0, p1 });
            }

            // we're done, figure out what to do with it
            if(!matches.size()) return 0;
            
            auto cursor = buffer.getCursor();

            if(!findPrev)
            {
                matchIndex = 0;
    
                // now we have all matches, find next (or previous)
                for(auto & match : matches)
                {
                    if(match.p0 < cursor) { ++matchIndex; continue; }
    
                    buffer.setSelection(match.p1, match.p0);
                    
                    recalculateSize();
                    return matches.size();
                }
    
                matchIndex = 0;
                buffer.setSelection(matches[0].p1, matches[0].p0);
                
                recalculateSize();
                return matches.size();
            }
            else
            {
                matchIndex = matches.size();
                while(matchIndex--)
                {
                    if(matches[matchIndex].p1 >= cursor) continue;

                    buffer.setSelection(
                        matches[matchIndex].p1, matches[matchIndex].p0);

                    recalculateSize();
                    return matches.size();
                }
                
                matchIndex = matches.size() - 1;
                buffer.setSelection(
                    matches[matchIndex].p1, matches[matchIndex].p0);

                recalculateSize();
                return matches.size();
            }
        }

        // FIXME: can we have TextBuffer implement utf8 iterator directly?
        void recalculateSize()
        {
            Font & font = getFont();
            if(!font.valid()) return;

            // we reparse here for now
            attribs.clear();
            if(syntaxParser) syntaxParser->start(addAttrib, this);

            int lines = 1, lineHeight = (int)ceil(font->getLineHeight());

            float w = 0, x = 0;
            int cursorX = 0, cursorY = 0;

            lineMargin = 0;

            // this is used for tabstops
            float sw = font->getCharAdvanceW(' ');

            unsigned bytePos = 0;
            unsigned charBytePos = 0;

            utf8::Decoder   decoder;
            for(auto byte : buffer)
            {
                if(bytePos == buffer.getCursor())
                {
                    cursorX = int(x);
                    cursorY = lines * lineHeight;

                }
                ++bytePos;

                // keep going until we have a full char
                if(!decoder.next(byte)) continue;
                auto ch = decoder.ch;

                if(syntaxParser) syntaxParser->parse(charBytePos, ch);
                charBytePos = bytePos;

                // check newlines
                if(ch == '\n') { x = 0; ++lines; continue; }

                // check for tabs
                if(ch == '\t')
                {
                    x += (tabStop+.5f)*sw;
                    x -= fmod(x, tabStop*sw);
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

            if(bytePos == buffer.getCursor())
            {
                cursorX = int(x);
                cursorY = lines * lineHeight;
            }

            // calculate space for line margin
            if(showLineNumbers)
            {
                // assume (for now) all digits are same size
                // this is safe for most sensible fonts
                float dw = font->getCharAdvanceW('0');
    
                lineMargin = 2*sw;

                // add space for digits
                for(unsigned ld = lines; ld; ld /= 10) { lineMargin += dw; }

                // add a bit of space after the line
                w += lineMargin + 2*dw;
            }

            sizeX = (int) ceilf(w);
            sizeY = (1 + lines) * lineHeight;

            if(syntaxParser) syntaxParser->flush();
            // simplify render by pushing an extra attrib node
            attribs.push_back({bytePos, TextAttrib::aDefault});

            reflow();   // do reflow first so we can hope to scroll

            exposePoint((int) (lineMargin + cursorX),
                cursorY - (int) font->getAscent());

            onUpdate();
        }

        // this performs scrolling of the given point into view
        // trying to also expose some surrounding area
        void exposePoint(int x, int y)
        {
            Font & font = getFont();
            float dw = font->getCharAdvanceW('0');
            int lh = (int) ceilf(font->getLineHeight());

            int dx = (int) ( 10 * dw );
            int dy = (int) ( 4 * lh );

            scrollToView(x, y - lh, dx, dy);

            redraw();
        }

        int ev_size_x(float dpi) { return sizeX; }
        int ev_size_y(float dpi) { return sizeY; }

        void drawLineMargin(RenderContext & rc, Font & font,
            int line, float y, int lineHeight, bool activeLine)
        {
            if(!showLineNumbers) return;

            // need to draw background, since we're potentially drawing
            // on top of text we've already drawn; we'll blend so that
            // the text below shows through just a little bit
            rc.fillRect<blend::Over>(
                paint::Color(color::blend(theme.bgColor, 0xdd)),
                0, (int)(y - font->getAscent()),
                (int) lineMargin, lineHeight);

            // using strf here is too slow (thanks to allocations)
            // just convert the number manually instead
            char sl[32];
            {
                int i = 0, l = line;
                while(true)
                {
                    int digit = l % 10; l /= 10;
                    sl[i++] = digit + '0';
                    if(!l) break;
                }
                // add final zero and null-termination
                sl[i] = 0;

                // reverse
                std::reverse(sl, sl + i);
            }

            float slw = font->getTextWidth(sl) + font->getCharAdvanceW(' ');
            rc.drawText(font, sl, -1,
                paint::Color(activeLine
                    ? theme.fgColor : theme.fgMidColor),
                lineMargin - slw, y);
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

            ARGB cursorUseColor = cursorColor;
            if(getWindow()->getFocus() != this) cursorUseColor = 0;

            int line = 1, lineHeight = (int)ceil(font->getLineHeight());
            int column = 1;

            float sw = font->getCharAdvanceW(' ');
            float x = 0, y = lineHeight - font->getDescent();

            rc.clear(theme.bgColor);
            // draw a word-wrap margin (eg. 80 characters)
            rc.fillRect(
                paint::Color(color::lerp(theme.bgColor, theme.bgMidColor, 0x80)),
                int(floor(lineMargin + x + wrapMark*sw)), 0, 1, layout.h);

            // when inSelection is true, selectX gives the filled
            // pixel coordinate on current line to allow pixel fills
            bool inSelection = false;
            int selectX;

            unsigned selectStart = buffer.getSelectionStart();
            unsigned selectEnd = buffer.getSelectionEnd();

            //int cursorSize = (int)ceil(getWindow()->pt());
            float cursorSize = getWindow()->pt();

            auto drawCursor = [&](float x, float y)
            {
                Path cursorPath;
                cursorPath.rect(x, y, x+cursorSize, y+lineHeight);
                rc.fillPath(cursorPath, paint::Color(cursorUseColor));
            };

            // don't bother with outputting characters
            // that are above or below the current view
            const Rect & clip = rc.getClipRect();

            // initialize to something that won't go negative
            // still keep the first color as first
            int parenNesting = 0x10000 * parenColors.size();

            unsigned bytePos = 0;
            unsigned attribPos = 0; // index into attribute table
            unsigned activeAttrib = TextAttrib::aDefault;

            bool cursorThisLine = false;
            float cursorX = 0;

            ARGB selectColor = theme.selColor;
            bool darkText = theme.fgColor < theme.bgColor;

            // adjust selection color so that with multiply or screen
            // we get (approximately) the desired color
            if(darkText)
                selectColor = color::divide(selectColor, theme.bgColor);
            else
                selectColor = (~0u) - color::divide(
                    (~0u) - selectColor, (~0u) - theme.bgColor);

            utf8::Decoder   decoder;
            for(auto byte : buffer)
            {
                if(!inSelection && bytePos == selectStart)
                {
                    inSelection = true;
                    selectX = (int)(x + lineMargin);
                }
                
                if(bytePos == selectEnd)
                {
                    if(darkText)
                        rc.fillRect<blend::Multiply>(
                            paint::Color(selectColor),
                            selectX, (int)(y-font->getAscent()),
                            (int)(x + lineMargin) - selectX, lineHeight);
                    else
                        rc.fillRect<blend::Screen>(
                            paint::Color(selectColor),
                            selectX, (int)(y-font->getAscent()),
                            (int)(x + lineMargin) - selectX, lineHeight);

                    inSelection = false;
                }

                if(buffer.getCursor() == bytePos)
                {
                    cursorX = x;
                    cursorThisLine = true;

                    cursorLine = line;
                    cursorColumn = column;
                }
                
                // process attributes
                while(attribs[attribPos].pos <= bytePos)
                {
                    activeAttrib = attribs[attribPos].attrib;
                    ++attribPos;
                }
                
                ++bytePos;

                // keep going until we have a full char
                if(!decoder.next(byte)) continue;

                ++column;

                // ok, we have a character, initialize color
                ARGB charColor = theme.fgColor;

                auto ch = decoder.ch;

                int lineY = (int) (y-font->getAscent());
                bool skipHidden = (lineY > clip.y1
                    || lineY + lineHeight < clip.y0);

                // check newlines
                if(ch == '\n')
                {
                    if(inSelection)
                    {
                        // draw the whole remaining line
                        if(darkText)
                            rc.fillRect<blend::Multiply>(
                                paint::Color(selectColor),
                                selectX, (int)(y-font->getAscent()),
                                layout.w - selectX, lineHeight);
                        else
                            rc.fillRect<blend::Screen>(
                                paint::Color(selectColor),
                                selectX, (int)(y-font->getAscent()),
                                layout.w - selectX, lineHeight);

                        selectX = (int)(lineMargin);
                    }
                    

                    // draw the line margin for the current line
                    // so it goes on top of previous drawing
                    if(!skipHidden)
                    {
                        if(cursorThisLine)
                            drawCursor(cursorX+lineMargin,
                                (int)(y - font->getAscent()));
                    
                        drawLineMargin(rcMargin, font,
                            line, y, lineHeight, cursorThisLine);
                    }

                    cursorThisLine = false;

                    x = 0; ++line; column = 1;
                    y += lineHeight;
                    continue;
                }

                // check for tabs
                if(ch == '\t')
                {
                    x += (tabStop+.5f)*sw;
                    x -= fmod(x, tabStop*sw);

                    continue;
                }

                // must be done before skips, but only for default text
                if(TextAttrib::aDefault == activeAttrib)
                {
                    if(!syntaxParser) { /* don't color parens in plain text */ }
                    else if(ch == '(' || ch == '[' || ch == '{')
                    {
                        charColor = parenColors
                            [(parenNesting++)%parenColors.size()];
                    }
                    else if(ch == ')' || ch == ']' || ch == '}')
                    {
                        charColor = parenColors
                            [(--parenNesting)%parenColors.size()];
                    }
                }
                else
                {
                    if(TextAttrib::aComment == activeAttrib)
                        charColor = commentColor;
                    if(TextAttrib::aLiteral == activeAttrib)
                        charColor = literalColor;
                    if(TextAttrib::aOperator == activeAttrib)
                        charColor = operatorColor;
                }

                // actual rendering, we can skip this part
                if(skipHidden) continue;

                x += rc.drawChar(font, decoder.ch,
                    paint::Color(charColor), lineMargin + x, y);
            }

            // handle trailing invalid unicode
            if(decoder.state != utf8::ACCEPT)
            {
                x += rc.drawChar(font, utf8::invalid,
                    paint::Color(theme.fgColor), lineMargin + x, y);
            }
            
            if(inSelection)
            {
                if(darkText)
                    rc.fillRect<blend::Multiply>(
                        paint::Color(selectColor),
                        selectX, (int)(y - font->getAscent()),
                        int(x + lineMargin) - selectX, lineHeight);
                else
                    rc.fillRect<blend::Screen>(
                        paint::Color(selectColor),
                        selectX, (int)(y - font->getAscent()),
                        int(x + lineMargin) - selectX, lineHeight);
            }
            
            if(cursorThisLine)
                drawCursor(cursorX+lineMargin,
                    (int)(y - font->getAscent()));
            else if(buffer.getCursor() == bytePos)
            {
                drawCursor(x+lineMargin, (int)(y - font->getAscent()));
                cursorThisLine = true;
            }

            // draw the line margin for the final line
            // so it goes on top of previous drawing
            drawLineMargin(rcMargin, font,
                line, y, lineHeight, cursorThisLine);
        }

        void setPosition(int line, int col)
        {
            unsigned bytePos = 0, charPos = 0, linePos = 1;
            utf8::Decoder   decoder;
            for(auto byte : buffer)
            {
                ++bytePos;
                
                // keep going until we have a full char
                if(!decoder.next(byte)) continue;
                auto ch = decoder.ch;

                ++charPos;
                if(linePos == line && charPos >= col)
                {
                    buffer.setCursor(bytePos, false);
                    recalculateSize();
                    return;
                }

                if(ch == '\n') { ++linePos; charPos = 0; }
            }
        }
        
        unsigned findMouse(int mx, int my)
        {
            // font must always be valid, so just crash if it's not!
            Font & font = getFont();

            int lineHeight = (int)ceil(font->getLineHeight());

            float sw = font->getCharAdvanceW(' ');

            float x = 0, y = lineHeight + .5f * font->getDescent();

            unsigned bytePos = 0, charPos = 0, prevCharPos = 0;

            if(mx < x && my < y) return bytePos;

            utf8::Decoder   decoder;
            for(auto byte : buffer)
            {
                ++bytePos;

                // keep going until we have a full char
                if(!decoder.next(byte)) continue;
                auto ch = decoder.ch;

                prevCharPos = charPos;
                charPos = bytePos;

                // check newlines
                if(ch == '\n')
                {
                    // if mouse is past the end of the line
                    // then return the position just before newline
                    if(my < y) return prevCharPos;

                    x = 0;
                    y += lineHeight;
                    continue;
                }

                // no need to resolve x until we are on the correct line
                if(my > y) continue;

                float oldX = x;
                if(ch == '\t')
                {
                    x += (tabStop+.5f)*sw;
                    x -= fmod(x, tabStop*sw);

                    oldX = std::max(oldX, x-sw);
                }
                else
                {
                    x += font->getCharAdvanceW(ch);
                }

                // check if mouse is to the left of the glyph center
                if(mx < .5f*(x+oldX) + lineMargin) { return prevCharPos; }
            }

            // if we didn't find a position return the very end
            return bytePos;
        }

        // start a new line and indent it based on the indent
        // of the previous non-empty line
        //
        void doNewlineIndent()
        {
            // find the previous non-empty line
            unsigned indent = 0;

            unsigned prevLine = buffer.getLineStart(buffer.getSelectionStart());
            unsigned p = prevLine;
            while(true)
            {
                const char *b = buffer.getByteAt(p);

                // if we hit end of file, newline or selection/cursor
                // then we treat this line as empty
                if(!b || *b == '\n' || p == buffer.getSelectionStart())
                {
                    // if we're at the beginning, indent is zero
                    if(!prevLine) break;
                    // otherwise try again previous line
                    indent = 0;
                    p = prevLine = buffer.getLineStart(prevLine-1);
                    continue;
                }

                // count the amount of space
                if(*b == ' ') { ++p; ++indent; continue; }
                if(*b == '\t')
                {
                    ++p; indent += tabStop - (indent % tabStop);
                    continue;
                }

                // FIXME: can we handle {} for C-like languages
                // and () for Lisp sensibly here somehow?

                // break if we didn't explicitly continue
                break;
            }

            buffer.doNewline(indent);
        }

        void doCut() { buffer.doCut(); redraw(); }
        void doCopy() { buffer.doCopy(); redraw(); }
        void doPaste() { buffer.doPaste(); redraw(); }

        void ev_focus(bool gained)
        {
            if(gained) onFocus();
            redraw();
        }

        bool ev_mouse(const MouseEvent & ev)
        {
            if(ev.type == MouseEvent::tDown && ev.button == 1)
            {
                focus();

                bool keepSel = ev.keymods & KEYMOD_SHIFT;

                // check if mouse is in the floating margin
                dragMargin = lineMargin
                > ev.x + getParent()->getLayout().contentOffsetX;

                // do line-select on tripple click
                if(ev.nClick > 2) dragMargin = true;

                unsigned mpos = findMouse(ev.x, ev.y);
                if(dragMargin)
                {
                    drag0 = buffer.getLineStart(mpos);
                    drag1 = buffer.getNextLine(mpos);

                    if(keepSel)
                    {
                        if(buffer.getSelectionStart() < drag0)
                        {
                            drag0 = buffer.getSelectionStart();
                        }
                        if(buffer.getSelectionEnd() > drag1)
                        {
                            drag1 = buffer.getSelectionEnd();
                        }
                    }

                    buffer.setSelection(drag1, drag0);
                }
                else buffer.setCursor(mpos, keepSel);
                
                dragWords = (!dragMargin && ev.nClick > 1);
                
                if(dragWords) buffer.doSelectWords(wordSeparators());

                exposePoint(ev.x, ev.y);
                return true;
            }

            if(ev.type == MouseEvent::tMove && ev.button == 1)
            {
                unsigned mpos = findMouse(ev.x, ev.y);
                if(dragMargin)
                {
                    int pos0 = buffer.getLineStart(mpos);
                    int pos1 = buffer.getNextLine(mpos);

                    if(pos0 <= drag0)
                    {
                        buffer.setSelection(pos0, drag1);
                    }
                    if(pos1 >= drag1)
                    {
                        buffer.setSelection(pos1, drag0);
                    }
                    if(pos0 > drag0 && pos1 < drag1)
                    {
                        buffer.setSelection(drag1, drag0);
                    }
                }
                else buffer.setCursor(findMouse(ev.x, ev.y), true);

                if(dragWords) buffer.doSelectWords(wordSeparators());
                
                exposePoint(ev.x, ev.y);
                return true;
            }

            if(ev.type == MouseEvent::tDown && ev.button == 2)
            {
                // ask for context menu
                onContextMenu(ev); return true;
            }

            return false;
        }

        bool ev_key(Scancode vk, bool pressed, unsigned mods)
        {
            if(!pressed) return false;
            bool keepSel = mods & KEYMOD_SHIFT;

            if((mods&~KEYMOD_SHIFT) == KEYMOD_CMD)
            switch(vk) // with command-shortcut key
            {
                case SCANCODE_Z: keepSel ? buffer.doRedo() : buffer.doUndo(); break;
                case SCANCODE_A: buffer.doSelectAll(); break;

                case SCANCODE_X: buffer.doCut(); break;
                case SCANCODE_C: buffer.doCopy(); break;
                case SCANCODE_V: buffer.doPaste(); break;
                
                case SCANCODE_J: buffer.doJoinLines(); break;
                
                case SCANCODE_E:
                    // extend to parens if holding shift, otherwise extend words
                    if(keepSel) buffer.doSelectLines();
                    else buffer.doSelectWords(wordSeparators());
                    break;

                case SCANCODE_R:
                    buffer.doSelectParens(attribs, keepSel);
                    break;
                
                case SCANCODE_LEFT: 
                    buffer.moveWordBack(wordSeparators(), keepSel);
                    break;
                case SCANCODE_RIGHT:
                    buffer.moveWordForward(wordSeparators(), keepSel);
                    break;
                case SCANCODE_UP:
                    // we move up a line, then end of that line
                    // from there startOrIndent will get us indent
                    buffer.moveLineUp(keepSel);
                    buffer.moveLineEnd(keepSel);
                    buffer.moveLineStartOrIndent(keepSel);
                    break;
                case SCANCODE_DOWN:
                    // we move down a line, then end of that line
                    // from there startOrIndent will get us indent
                    buffer.moveLineDown(keepSel);
                    buffer.moveLineEnd(keepSel);
                    buffer.moveLineStartOrIndent(keepSel);
                    break;

                case SCANCODE_KP_MINUS:
                case SCANCODE_MINUS:
                {
                    Font & font = getFont();
                    if(font->parameters.sizePt > 2)
                    {
                        font.setSizePt(font->parameters.sizePt - 1);
                    }
                } break;

                case SCANCODE_KP_PLUS:
                case SCANCODE_EQUALS:
                {
                    Font & font = getFont();
                    font.setSizePt(font->parameters.sizePt + 1);
                } break;

                default: return false;
            }
            else if(!(mods&~KEYMOD_SHIFT))
            switch(vk) // not holding command shortcut
            {
                case SCANCODE_RETURN:
                case SCANCODE_RETURN2: doNewlineIndent(); break;

                case SCANCODE_BACKSPACE:
                    buffer.doBackspace(keepSel ? tabStop : 1); break;
                case SCANCODE_DELETE: buffer.doDelete(); break;

                case SCANCODE_UP: buffer.moveLineUp(keepSel); break;
                case SCANCODE_DOWN: buffer.moveLineDown(keepSel); break;

                case SCANCODE_LEFT: buffer.moveBack(keepSel); break;
                case SCANCODE_RIGHT: buffer.moveForward(keepSel); break;

                case SCANCODE_HOME: buffer.moveLineStartOrIndent(keepSel); break;
                case SCANCODE_END: buffer.moveLineEnd(keepSel); break;

                case SCANCODE_PAGEDOWN:
                {
                    int nLines = 1 + (int)(.5f * getParent()->getLayout().h
                        / getFont()->getLineHeight());

                    for(int i = 0; i < nLines; ++i)
                    {
                        buffer.moveLineDown(keepSel);
                    }
                }
                break;
                case SCANCODE_PAGEUP:
                {
                    int nLines = 1 + (int)(.5f * getParent()->getLayout().h
                        / getFont()->getLineHeight());

                    for(int i = 0; i < nLines; ++i)
                    {
                        buffer.moveLineUp(keepSel);
                    }
                }
                break;
                case SCANCODE_TAB:
                {
                    // if holding shift, reduce indent
                    if(keepSel)
                    {
                        buffer.doReduceIndent(tabStop);
                        break;
                    }
                    // otherwise do block indent for selection
                    if(buffer.haveSelection())
                    {
                        buffer.doSoftIndent(tabStop);
                        break;
                    }

                    // otherwise insert a soft-tab
                    unsigned cursor = buffer.getCursor();
                    unsigned column = buffer.getColumn(
                        buffer.getLineStart(cursor), cursor);

                    unsigned addSpace = tabStop - (column % tabStop);
                    while(addSpace--) buffer.doText(" ", 1);
                }
                break;

                // special case bubble escape
                case SCANCODE_ESCAPE: return false;

                // not holding cmd, so don't bubble
                default: return true;
            }

            recalculateSize();
            return true;
        }

        void ev_text(const char * txt)
        {
            // opening parens ALWAYS surround selection
            if(buffer.haveSelection() && strchr("([{", txt[0]))
            {
                switch(txt[0])
                {
                case '(': buffer.doParens("(", ")"); break;
                case '[': buffer.doParens("[", "]"); break;
                case '{': buffer.doParens("{", "}"); break;
                }
                recalculateSize();
                return;
            }
            
            // we only ever autoClose if we have attributes
            if(autoCloseParens && strchr("([{}])", txt[0])
            && !txt[1] && attribs.size())
            {
                // figure out if we're in default/operator context?
                unsigned attr = TextAttrib::aDefault, c = buffer.getCursor();
                unsigned i0 = 0, i1 = attribs.size() - 1;
                while(i0 < i1)
                {
                    // for two elements, this rounds to i0
                    int i = i0 + (i1-i0)/2;
                    if(attribs[i].pos <= c)
                    {
                        // if next value is higher, we're done
                        // otherwise skip the next token
                        if(attribs[i+1].pos > c) break; else i0 = i+1;
                    }
                    else i1 = i;
                }
                switch(attribs[i0].attrib)
                {
                case TextAttrib::aDefault:
                case TextAttrib::aOperator:
                    switch(txt[0])
                    {
                    case '(': buffer.doText("()"); buffer.moveBack(false); break;
                    case '[': buffer.doText("[]"); buffer.moveBack(false); break;
                    case '{': buffer.doText("{}"); buffer.moveBack(false); break;
                    default:
                        {
                            const char * ch = buffer.getByteAt(c);
                            if(ch && *ch == txt[0])
                                buffer.moveForward(false);
                            else
                                buffer.doText(txt);
                        }
                    }
                    break;
                default: buffer.doText(txt);
                }
            }
            else buffer.doText(txt);
            
            recalculateSize();
        }

        // copy contents into vector
        void outputContents(std::vector<char> & out)
        {
            unsigned offset = out.size();
            out.reserve(offset + buffer.getSize());
            
            for(auto byte : buffer) out.push_back(byte);
        }

        void loadFile(const std::string & path)
        {
            buffer.loadFile(path);
            recalculateSize();

            // always force very top,left to be visible after load
            // avoids scroll weirdness if parent layout is not done
            scrollToView(0,0);
        }

        void saveFile(const std::string & path)
        {
            bool failed = false;
            
            // generate a temporary filename
#ifdef _WIN32
            std::wstring tmp = to_u16(path) + L".$tmp";
            while(!_waccess(tmp.c_str(), 0)) { tmp += L"$"; }
            FILE * file = _wfopen(tmp.c_str(), L"wb");
#else
            std::string tmp = path + ".$tmp";
            while(!access(tmp.c_str(), F_OK)) { tmp += "$"; }
            FILE * file = fopen(tmp.c_str(), "wb");
#endif

            if(!file) return;

            for(auto ch : buffer)
            {
                int byte = (uint8_t) ch;
                if(byte != fputc(byte, file)) { failed = true; break; }
            }
            fclose(file);

            // FIXME: should we popup some helpful error message
            if(failed)
            {
                debugPrint("Failed to save file: %s\n", path.c_str());
                return;
            }

#ifndef _WIN32
            // keep permissions for existing files
            struct stat oldStat;
            if(!stat(path.c_str(), &oldStat))
            {
                chmod(tmp.c_str(), oldStat.st_mode);
            }
            
            // we managed to save the file, now fix the name
            if(!rename(tmp.c_str(), path.c_str()))
            {
                buffer.setNotModified();
            }
#else
            // on Windows, do a rename dance
            // another temp filename
            std::wstring tmp2(tmp);
            tmp2 += L".old";
            while(!_waccess(tmp2.c_str(), 0))
            {
                tmp2 += L"$";
            }

            auto u16path = to_u16(path);
            bool oldFile = !_waccess(u16path.c_str(), 0);

            // this is less than ideal, but without atomic renames
            // there isn't necessarily any better option?
            if((!oldFile || !_wrename(u16path.c_str(), tmp2.c_str()))
            && !_wrename(tmp.c_str(), u16path.c_str()))
            {
                buffer.setNotModified();
            }
            
            // this might fail if in use, but that's fine
            if(oldFile) _wunlink(tmp2.c_str());
#endif
        }

        // show a save confirmation dialog if modified
        // returns true if document can close
        // returns false if user requested that document not be closed
        bool saveOnClose(const std::string & path)
        {
            if(!buffer.isModified()) return true;

            return false;
        }

        // return true if document is modified
        bool isModified() { return buffer.isModified(); }

        int getCursorLine() { return cursorLine; }
        int getCursorColumn() { return cursorColumn; }

    protected:
        std::vector<TextAttrib> attribs;

        // for purposes of C-API compatibility
        // we want to make syntax plugable :)
        static void addAttrib(void*ptr, TextAttrib * a)
        {
            auto ta = (TextArea*)ptr;
            ta->attribs.push_back(*a);
        }

        TextBuffer  buffer;

        float       lineMargin;

        bool        dragWords;      // set in mousedown
        bool        dragMargin;     // set in mousedown
        int         drag0, drag1;   // set if dragMargin is true

        int         sizeX;
        int         sizeY;

        int         cursorLine, cursorColumn;
    };

};
