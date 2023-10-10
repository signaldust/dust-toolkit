
#pragma once

#include "text_ptable.h"

namespace dust
{
    // We implement syntax highlighting with plugable parsers
    // by feeding them the contents of the file and building
    // an array of <position,attribute> tupples.
    //
    // These are defined here, because TextBuffer also uses them
    // for intelligent selection of expressions.
    struct TextAttrib
    {
        // these are mapped to actual colors by TextArea
        enum
        {
            aDefault,   // default text - use rainbow parens
            aComment,   // comment block or line, ignore parens
            aLiteral,   // string/character literal, ignore parens
            aOperator,  // for operators and similar

            aError  // should be last
        };

        unsigned    pos;
        unsigned    attrib;
    };

    // Text ptable - this provides higher-level text-editing
    // functionality over the low-level piece-table
    //
    struct TextBuffer
    {
        TextBuffer() { moveRowColumn = invalidColumn; }

        // return true if ptable contents are modified
        bool isModified() { return ptable.isModified(); }

        // set current contents as not-modified
        void setNotModified() { ptable.setNotModified(); }

        // pass-through iterators
        PieceTable::Iterator begin() { return ptable.begin(); }
        PieceTable::Iterator end() { return ptable.end(); }

        ////////////////////////
        // UNDO/REDO COMMANDS //
        ////////////////////////

        void doUndo()
        {
            moveRowColumn = invalidColumn;
            ptable.doUndo();
        }

        void doRedo()
        {
            moveRowColumn = invalidColumn;
            ptable.doRedo();
        }

        ///////////////
        // CLIPBOARD //
        ///////////////

        // copy current selection or line to the clipboard
        // if wantSelection is true, then select whatever we
        // actually copied, so that doCut can remove it
        bool setClipboardText(bool wantSelection)
        {
            // build a copy of the text
            std::vector<char>   out;

            unsigned start = ptable.cursor.pos0;
            unsigned end = ptable.cursor.pos1;
            if(start > end) std::swap(start, end);

            if(start == end)
            {
                start = getLineStart(start);
                end = getLineEnd(end);

                // if this is not the last line
                // then include the newline too
                if(end < ptable.getSize()) ++end;
            }

            out.resize(end-start);

            for(unsigned i = start; i < end; ++i)
            {
                // intentionally crash is cursors are borked
                out[i - start] = *ptable.getElementAt(i);
            }

            // do the actual clipboard action
            if(!clipboard::setText(out.data(), out.size())) return false;

            if(wantSelection) setSelection(start, end);

            return true;
        }

        void doPaste()
        {
            std::string txt;
            if(clipboard::getText(txt))
            {
                // stand-alone transaction
                Action act(ptable);
                ptable.eraseSelection();
                
                // if text ends in a newline, then treat it as "full lines"
                // this is not quite perfect, but it's better than nothing
                if(txt.back() == '\n')
                {
                    setCursor(getLineStart(getCursor()), false);
                }
                
                ptable.insert(getCursor(), txt.c_str(), txt.size());
            }
        }

        void doCopy()
        {
            setClipboardText(false);
        }

        void doCut()
        {
            if(setClipboardText(true))
            {
                Action act(ptable);
                ptable.eraseSelection();
            }
        }

        ////////////////
        // TEXT INPUT //
        ////////////////

        // press-enter and optionally indent
        void doNewline(unsigned ident = 0)
        {
            moveRowColumn = invalidColumn;

            // we can merge the newline with another insert
            // but we do a dummy default transaction afterwards
            // to break the coalesces after each line
            {
                Action act(ptable, PieceTable::TRANSACT_INSERT);
    
                // we start with erase selection, for undo reasons
                ptable.eraseSelection();
                
                // extend selection backwards over spaces
                unsigned pos = getSelectionStart();
                while(pos)
                {
                    unsigned prev = getPrevChar(pos);
                    const char * ch = ptable.getElementAt(prev);
                    if(!ch || (*ch != ' ' && *ch != '\t')) break;
                    pos = prev;
                }
    
                // extend selection forward over spaces
                unsigned end = getSelectionEnd();
                while(true)
                {
                    const char * ch = ptable.getElementAt(end);
                    if(!ch || (*ch != ' ' && *ch != '\t')) break;
                    end = getNextChar(end);
                }
                
                setSelection(pos, end);
    
                ptable.eraseSelection();
    
                unsigned p = getCursor();
                ptable.insert(p++, "\n", 1);
                while(ident--) ptable.insert(p++, " ", 1);
            }

            // this breaks coalesce after newline
            Action act(ptable, PieceTable::TRANSACT_DEFAULT);
        }

        // press backspace
        void doBackspace(unsigned shiftWidth)
        {
            moveRowColumn = invalidColumn;

            //ptable.debugSpans();
            if(!ptable.eraseSelection())
            {
                unsigned pos = getCursor();
                unsigned lineStart = getLineStart(pos);
                unsigned posPrev = getPrevChar(pos);
                
                // bail out at beginning of file
                // this also avoids problems for empty files below
                if(posPrev == pos) return;

                // figure out where the previous tab-stop would be
                unsigned shift = getColumn(lineStart, posPrev) % shiftWidth;

                // if we have a space, then scan for more
                if(shift && *ptable.getElementAt(posPrev) == ' ')
                {
                    while(shift--)
                    {
                        // try to get further
                        unsigned ppp = getPrevChar(posPrev);
                        
                        // if it's not a space, bail out
                        if(*ptable.getElementAt(ppp) != ' ') break;
                        
                        // otherwise eat it too
                        posPrev = ppp;
                    }
                }

                // regular backspace
                ptable.erase(posPrev, pos - posPrev);
            }
        }

        // press delete
        void doDelete()
        {
            moveRowColumn = invalidColumn;

            if(!ptable.eraseSelection())
            {
                // if no selection, remove the next char
                unsigned pos = getCursor();
                unsigned posNext = getNextChar(pos);
                ptable.erase(pos, posNext - pos);
            }
        }

        // insert some text into the ptable at cursor
        // this is for typed text, but works with anything
        //
        void doText(const char * text, unsigned len = ~0)
        {
            if(len == ~0) len = strlen(text);

            moveRowColumn = invalidColumn;

            if(haveSelection())
            {
                Action act(ptable, PieceTable::TRANSACT_DEFAULT);
            }

            Action act(ptable, PieceTable::TRANSACT_INSERT);
            //debugPrint("doText: '%s'\n",text);
            ptable.eraseSelection();
            ptable.insert(getCursor(), text, len);
        }

        // this is a special case routine that surrounds
        // selection with parens or really anything
        void doParens(const char * a, const char * b)
        {
            Action act(ptable, PieceTable::TRANSACT_DEFAULT);

            unsigned la = strlen(a);
            unsigned p0 = ptable.cursor.pos0 + la;
            unsigned p1 = ptable.cursor.pos1 + la;

            ptable.insert(getSelectionStart(), a, la);
            ptable.cursor.pos0 = p0;
            ptable.cursor.pos1 = p1;
            ptable.insert(getSelectionEnd(), b, strlen(b));
            ptable.cursor.pos0 = p0;
            ptable.cursor.pos1 = p1;
        }

        //////////////////////////
        // BASIC CURSOR HELPERS //
        //////////////////////////

        unsigned getSize() { return ptable.getSize(); }

        // set selection range and cursor position
        void setSelection(unsigned cursor, unsigned start)
        {
            // dummy transaction to break coalesce when moving
            Action act(ptable, PieceTable::TRANSACT_DEFAULT);
        
            ptable.cursor.pos0 = cursor;
            ptable.cursor.pos1 = start;
        }

        // set cursor to a new position, optionally keep selection
        void setCursor(unsigned cursor, bool keepSelection)
        {
            // dummy transaction to break coalesce when moving
            Action act(ptable, PieceTable::TRANSACT_DEFAULT);
        
            ptable.cursor.pos0 = cursor;
            if(!keepSelection)
            {
                ptable.cursor.pos1 = cursor;
            }
        }

        unsigned getCursor() const { return ptable.cursor.pos0; }

        bool haveSelection() const
        {
            return ptable.cursor.pos0 != ptable.cursor.pos1;
        }

        unsigned getSelectionStart() const
        {
            return (std::min)(ptable.cursor.pos0, ptable.cursor.pos1);
        }

        unsigned getSelectionEnd() const
        {
            return (std::max)(ptable.cursor.pos0, ptable.cursor.pos1);
        }

        /////////////////////////
        // SELECTION SHORTCUTS //
        /////////////////////////

        void doSelectAll()
        {
            // set cursor to the end
            setSelection(ptable.getSize(), 0);
        }

        void doSelectNone()
        {
            setCursor(getCursor(), false);
        }
        
        // extend selection to word separators
        void doSelectWords(const char * separators)
        {
            moveRowColumn = invalidColumn;

            unsigned start = getSelectionStart();
            unsigned end = getSelectionEnd();
            bool cursorAtEnd = getCursor() == end;

            // extend backwards
            start = getWordBack(start, separators);

            // extend forward
            end = getWordForward(end, separators);
            
            if(cursorAtEnd)
                setSelection(end, start);
            else
                setSelection(start, end);
        }

        // this selects inside parens on the same level as cursor
        // unless the cursor is inside a comment or string, in which
        // case we select the comment or the string
        //
        // if include=true then we also select the parens themselves
        void doSelectParens(const std::vector<TextAttrib> & attrib, bool include)
        {
            moveRowColumn = invalidColumn;

            unsigned start = getCursor();

            // find an attribute before the cursor position
            auto ai = std::lower_bound(attrib.begin(), attrib.end(), start,
                [](TextAttrib const & a, unsigned v) { return a.pos < v; });
                
            if(!haveSelection() && ai != attrib.begin())
            {
                auto aj = ai-1;
                if(aj->attrib == TextAttrib::aComment
                || aj->attrib == TextAttrib::aLiteral)
                {
                    setSelection(ai->pos, aj->pos);
                    return;
                }
            }

            unsigned nParen = 1;
            // search backwards to a paren
            while(start)
            {
                unsigned prev = getPrevChar(start);
                if(!prev) break;

                // rewind attributes
                while(ai != attrib.begin() && ai->pos > prev) --ai;

                // are we in a context where we should count stuff?
                if(ai->pos > prev ||
                 ( ai->attrib != TextAttrib::aComment
                && ai->attrib != TextAttrib::aLiteral))
                {
                    const char * ch = ptable.getElementAt(prev);
                    if(!ch) break;
                    
                    if(strchr("([{", *ch))
                    {
                        if(nParen == 1) break;
                        else --nParen;
                    }
                    if(strchr(")]}", *ch)) ++nParen;

                }

                start = prev;
            }
            
            unsigned aia = TextAttrib::aDefault;

            // count parens until we reach zero
            unsigned end = start;
            while(true)
            {
                const char * ch = ptable.getElementAt(end);
                if(!ch) break;

                // forward attributes
                while(ai != attrib.end() && ai->pos <= end)
                {
                    aia = ai->attrib; ++ai;
                }

                // are we in a context where we should count?
                if(aia != TextAttrib::aComment
                && aia != TextAttrib::aLiteral)
                {
                    if(strchr("([{", *ch)) ++nParen;
                    if(strchr(")]}", *ch)) --nParen;
                    if(!nParen) break;
                }

                end = getNextChar(end);
            }
            
            // trim white-space before closing paren or expr-sep
            if(include) end = getNextChar(end);
            else while(true)
            {
                unsigned prev = getPrevChar(end);
                const char * ch = ptable.getElementAt(prev);
                
                if(!ch || !strchr(" \t\n", *ch)) break;
                end = prev;
            }

            // trim white-space after opening paren or expr-sep
            if(include) start = getPrevChar(start);
            else while(start < end)
            {
                const char * ch = ptable.getElementAt(start);
                if(!ch || !strchr(" \t\n", *ch)) break;
                start = getNextChar(start);
            }

            // cursor to beginning
            setSelection(start, end);
        }

        // extend selection beginning to beginning of line
        // extend selection end to the end of the line
        void doSelectLines()
        {
            moveRowColumn = invalidColumn;
            
            unsigned start = getSelectionStart();
            unsigned end = getSelectionEnd();
            bool cursorAtEnd = getCursor() == end;

            start = getLineStart(start);
            end = getLineEnd(end);

            // include the newline if we have one
            if(ptable.getElementAt(end)) end += 1;

            if(cursorAtEnd)
                setSelection(end, start);
            else
                setSelection(start, end);
        }

        //////////////////////////////////////////
        // CHARACTER AND LINE CURSOR NAVIGATION //
        //////////////////////////////////////////

        void moveForward(bool keepSelection)
        {
            moveRowColumn = invalidColumn;
            unsigned newPos = getNextChar(getCursor());
            setCursor(newPos, keepSelection);
        }

        void moveBack(bool keepSelection)
        {
            moveRowColumn = invalidColumn;
            unsigned newPos = getPrevChar(getCursor());
            setCursor(newPos, keepSelection);
        }

        // FIXME: make this also scan to next word beginning?
        // we don't want that behavior for doSelectWords though
        void moveWordForward(const char * separators, bool keepSelection)
        {
            moveRowColumn = invalidColumn;

            // move at the end of the word
            unsigned pos = getWordForward(getCursor(), separators);
            
            // if we didn't move, move over space instead
            if(pos == getCursor()) pos = getWordForward(pos, "\t ", true);

            // if that still didn't move us, move by one character
            if(pos == getCursor()) pos = getNextChar(pos);
            
            setCursor(pos, keepSelection);
        }

        // FIXME: make this also scan to previous word end?
        // we don't want that behavior for doSelectWords though
        void moveWordBack(const char * separators, bool keepSelection)
        {
            moveRowColumn = invalidColumn;

            // move at the beginning of the word
            unsigned pos = getWordBack(getCursor(), separators);
            
            // if we didn't move, move over space instead
            if(pos == getCursor()) pos = getWordBack(pos, "\t ", true);

            // if that still didn't move us, move by one character
            if(pos == getCursor()) pos = getPrevChar(pos);
            
            setCursor(pos, keepSelection);
        }

        void moveLineStart(bool keepSelection)
        {
            moveRowColumn = invalidColumn;
            setCursor(getLineStart(getCursor()), keepSelection);
        }

        void moveLineStartOrIndent(bool keepSelection)
        {
            unsigned cursor = getCursor();
            moveLineStart(keepSelection);

            // now see if there was non-whitespace before cursor?
            for(unsigned c = getCursor(); c < cursor; ++c)
            {
                char ch = *ptable.getElementAt(c);
                if(ch != ' ' && ch != '\t')
                {
                    // instead of going to the very beginning
                    // go to the beginning of indent
                    setCursor(c, keepSelection);
                    break;
                }
            }
        }

        void moveLineEnd(bool keepSelection)
        {
            moveRowColumn = invalidColumn;
            setCursor(getLineEnd(getCursor()), keepSelection);
        }

        void moveLineUp(bool keepSelection)
        {
            unsigned lineStart = getLineStart(getCursor());

            if(moveRowColumn == invalidColumn)
                moveRowColumn = getColumn(lineStart, getCursor());

            // first line?
            if(!lineStart)
            {
                setCursor(0, keepSelection);
                return;
            }

            // otherwise get beginning of previous line
            unsigned prevLine = getLineStart(lineStart - 1);
            setCursor(getCharOnLine(prevLine, moveRowColumn), keepSelection);
        }

        void moveLineDown(bool keepSelection)
        {
            unsigned lineStart = getLineStart(getCursor());
            unsigned lineEnd = getLineEnd(getCursor());

            // last line? go to the end of it
            if(lineEnd == ptable.getSize())
            {
                setCursor(lineEnd, keepSelection);
                return;
            }

            if(moveRowColumn == invalidColumn)
                moveRowColumn = getColumn(lineStart, getCursor());

            // go to next line and fix column
            setCursor(getCharOnLine(lineEnd + 1, moveRowColumn), keepSelection);
        }

        //////////////////////////
        // LOW-LEVEL NAVIGATION //
        //////////////////////////

        const char * getByteAt(unsigned pos)
        {
            return ptable.getElementAt(pos);
        }

        // this returns true if the byte is either ASCII
        // or a byte starting a new UTF-8 codepoint
        bool isLeadingByte(unsigned char byte)
        {
            //return !((byte & 0x80) && !(byte &0x40));
            return (0xc0 & byte) != 0x80;
        }

        // get position one character forward from position
        // does not move past the end of the ptable
        unsigned getNextChar(unsigned pos)
        {
            unsigned size = ptable.getSize();
            unsigned newPos = pos;
            while(newPos < size)
            {
                const char * ch = ptable.getElementAt(++newPos);
                // if this is leading byte, we're done
                if(!ch || isLeadingByte(*ch)) break;
            }
            return newPos;
        }

        // get position one character backwards from position
        // does not move past the beginning of the ptable
        unsigned getPrevChar(unsigned pos)
        {
            if(!pos) return pos;
            unsigned newPos = pos;
            // scan backwards until the current byte is a valid
            while(--newPos)
            {
                const char * ch = ptable.getElementAt(newPos);
                // are we at the start of the code-point?
                if(isLeadingByte(*ch)) break;
            }
            return newPos;
        }

        // get position at the beginning of the line
        unsigned getLineStart(unsigned pos)
        {
            while(pos > 0)
            {
                // only dealing with '\n' so this is fine
                const char * ch = ptable.getElementAt(pos - 1);
                if(*ch == '\n') break;
                --pos;
            }
            return pos;
        }

        // get position at the end of the line (index of '\n')
        unsigned getLineEnd(unsigned pos)
        {
            unsigned end = ptable.getSize();
            while(pos < end)
            {
                const char * ch = ptable.getElementAt(pos);
                if(*ch == '\n') break;
                ++pos;
            }
            return pos;
        }

        // shortcut for getNextChar(getLineEnd(x))
        unsigned getNextLine(unsigned pos)
        {
            return getNextChar(getLineEnd(pos));
        }

        // get logical column from position on a line
        unsigned getColumn(unsigned lineStart, unsigned pos)
        {
            unsigned c = 0, offset = lineStart;
            while(offset < pos)
            {
                offset = getNextChar(offset);
                ++c;
            }
            return c;
        }

        // find a given column on a line, counting codepoints for now
        // if the line is too short, then returns position at end of line
        unsigned getCharOnLine(unsigned lineStart, unsigned column)
        {
            unsigned pos = lineStart;
            while(column)
            {
                // get character at new position
                const char * ch = ptable.getElementAt(pos);
                // if we hit end of file or end of line, then break
                if(!ch || *ch == '\n') break;

                // step forward
                pos = getNextChar(pos);
                // and keep count
                --column;
            }
            return pos;
        }

        // returns the beginning of word at pos
        // returns pos if already at the beginning or not inside a word
        //
        // if invert is true, we treat separators as word-characters
        // effectively searching for the previous end of word
        unsigned getWordBack(unsigned pos, const char * sep, bool invert = false)
        {
            while(pos)
            {
                unsigned prev = getPrevChar(pos);
                
                const char * ch = ptable.getElementAt(prev);
                if(!ch || invert == !strchr(sep, *ch)) break;
                // always break on newline
                if(*ch == '\n') break;

                pos = prev;
            }
            return pos;
        }

        // returns the end of the word at pos
        // returns pos if already at the end or not inside a word
        //
        // if invert is true, we treat separators as word-characters
        // effectively searching for the next beginning of word
        unsigned getWordForward(unsigned pos, const char * sep, bool invert = false)
        {
            // scan forward until test for next is different
            while(true)
            {
                const char * ch = ptable.getElementAt(pos);
                if(!ch || invert == !strchr(sep, *ch)) break;

                // always break on newline
                if(*ch == '\n') break;

                pos = getNextChar(pos);
            }

            return pos;
        }

        /////////////////////
        // BLOCK INDENTING //
        /////////////////////

        // add indent to all lines in selection
        // uses space for "soft-tabs"
        void doSoftIndent(unsigned shiftWidth)
        {
            Action act(ptable); // standalone undo

            std::vector<char>   spaces(shiftWidth, ' ');

            unsigned cstart = getSelectionStart();
            unsigned cend = getSelectionEnd();

            // start from beginning of first line covered
            unsigned pos = getLineStart(cstart);

            // we need this to fix cursor/selection afterwards
            bool cursorAtEnd = getCursor() == cend;

            // do we start at newline?
            bool startNewLine = getSelectionStart() == pos;

            unsigned extend = 0;

            // keep going until line begins after end point
            // note we indent cursor line only if cursor is
            // not at the very beginning of the line
            while(pos < cend + extend)
            {
                ptable.insert(pos, spaces.data(), shiftWidth);
                extend += shiftWidth;

                // we can safely go to end of line and increment
                // since selection can't extend over buffer
                pos = getLineEnd(pos) + 1;
            }

            // selection range fixup
            if(cursorAtEnd)
            {
                setSelection(
                    cend+extend,
                    cstart+(startNewLine?0:shiftWidth));
            }
            else
            {
                setSelection(
                    cstart+(startNewLine?0:shiftWidth),
                    cend+extend);
            }

            ptable.saveRedoCursor();
        }

        // reduce indent of all lines in selection
        void doReduceIndent(unsigned shiftWidth)
        {
            Action act(ptable); // standalone undo

            unsigned cstart = getSelectionStart();
            unsigned cend = getSelectionEnd();
            
            // start from beginning of first line covered
            unsigned pos = getLineStart(cstart);
            unsigned end = getLineEnd(cend);

            // if cursor is on a zero-length line without
            // selection, then we should just bail-out
            if(pos == end) return;
            
            // we need this to fix cursor/selection afterwards
            bool cursorAtEnd = getCursor() == cend;
            bool firstLine = true;

            unsigned shrink = 0;
            
            // we never shift selection to the previous line
            unsigned shiftStart = cstart - pos;
            
            // get offset from the end of line, bytes are fine
            unsigned endOffset = end - cend;
            
            while(pos < cend - shrink)
            {
                // we need to count spaces here
                unsigned n = 0;
                for(int i = 0; i < shiftWidth; ++i)
                {
                    const char * b = getByteAt(pos+i);
                    // break on eof
                    if(!b) break;
                    // keep counting for spaces
                    if(*b == ' ') { ++n; continue; }
                    // count tab, but break out
                    if(*b == '\t') { ++n; break; }

                    // otherwise break;
                    break;
                }

                shrink += n;

                // record how much to shift first line
                if(firstLine)
                {
                    // only allow smaller shift
                    if(shiftStart > n) shiftStart = n;

                    firstLine = false;
                }
                
                ptable.erase(pos, n);
                pos = getLineEnd(pos) + 1;
            }
            
            // now find the new line-start position for the
            // end of the selection; shrink is always safe here
            unsigned lastLineStart = getLineStart(end - shrink);
        
            // check if (end - shrink) < lastLineStart
            // reorder for unsigned
            if(cend < lastLineStart + shrink)
            {
                end = lastLineStart;
            }
            else
            {
                // just shrink normally
                end = cend - shrink;
            }
            
            // check if (start-shift) < firstLineStart

            // selection range fixup
            if(cursorAtEnd)
            {
                setSelection(end, cstart-shiftStart);
            }
            else
            {
                setSelection(cstart-shiftStart, end);
            }

            ptable.saveRedoCursor();
        }
        
        //////////////////////
        // ADVANCED EDITING //
        //////////////////////
        
        // join all the lines covered by selection
        // if selection only covers a single line
        // then join this line with the next one
        void doJoinLines()
        {
            Action act(ptable); // standalone undo

            unsigned cstart = getSelectionStart();
            unsigned cend = getSelectionEnd();
            
            bool cursorAtEnd = getCursor() == cend;
            unsigned lineEnd = getLineEnd(cstart);

            // for single-line case, put cursor at end of line after join
            if(cstart == cend) cstart = cend = lineEnd;
            
            while(true)
            {
                // single line case - eat until next non-white character
                unsigned pos = lineEnd;
                while(true)
                {
                    const char * ch = ptable.getElementAt(++pos);
                    if(!ch || (*ch != ' ' && *ch != '\t'))
                    {
                        unsigned n = pos - lineEnd;
                        ptable.erase(lineEnd, n);
                        // if this is not an empty line, add space
                        if(!ch || *ch != '\n')
                        {
                            ptable.insert(lineEnd, " ", 1);
                            --n;    // remove one less character
                        }
                        
                        // sanity check that we're not adjusting
                        // if we're already past the actual selection
                        if(cend < lineEnd) n = 0;
                        else if(cend < lineEnd + n)
                        {
                            // this is the corner case where selection
                            // ends somewhere in the middle of indent
                            n = cend - lineEnd;
                        }

                        cend -= n;
                        
                        unsigned setCur = cstart;
                        unsigned setEnd = cend;
                        
                        if(cursorAtEnd) 
                        {
                            std::swap(setCur, setEnd);
                        }
                        setSelection(setCur, setEnd);
                        
                        break;
                    }
                }
                
                lineEnd = getLineEnd(lineEnd);

                // break if we're past selection or if the
                // selection ends right after the newline
                if(lineEnd + 1 >= cend) break;
            }

            ptable.saveRedoCursor();
        }

        /////////////////////
        // FILE OPERATIONS //
        /////////////////////
        void loadFile(const std::string & path)
        {
#ifdef _WIN32
            FILE * f = _wfopen(to_u16(path).c_str(), L"rb");
#else
            FILE * f = fopen(path.c_str(), "rb");
#endif
            if(!f)
            {
                debugPrint("TextBuffer: fopen() failed\n");
                return; // typically we can ignore this
            }
            std::vector<char>   bytes;
            while(true)
            {
                int ch = fgetc(f);
                if(ch == EOF) break;

                // we don't do these
                if(ch == '\r') continue;

                bytes.push_back(ch);
            }
            fclose(f);

            ptable.reset();

            doText(bytes.data(), bytes.size());
            setCursor(0, false);

            ptable.forgetHistory();
            ptable.setNotModified();

        }

    private:
        // used by row-movement logic to mark "moveRowColumn"
        // as invalid (ie. next move up/down needs to recalculate)
        static const unsigned invalidColumn = ~0;

        typedef PieceTable::RAIIAction  Action;

        PieceTable  ptable;

        // used by row-movement logic
        unsigned    moveRowColumn;


    };
};
