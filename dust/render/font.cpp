

#include "dust/core/utf8.h"

#include "font.h"

using namespace dust;

float FontInstance::getTextWidth(const char * txt, unsigned len,
    bool adjustLeft, bool adjustRight)
{
    utf8::Decoder   decoder;

    float width = 0;

    const Glyph * g = 0;

    // we use this to check if last char is incomplete
    bool charDone = true;
    for(unsigned i = 0; i < len; ++i)
    {
        if(!txt[i] && len == ~0) break;
        charDone = decoder.next(txt[i]);
        if(charDone)
        {
            // add glyph advance to the width
            g = getGlyphForChar(decoder.ch);
            width += g->advanceW;

            // if this is first char of a line then pad with lsb
            if(adjustLeft) { width -= g->lsb; adjustLeft = false; }
        }
    }

    if(!charDone)
    {
        // add glyph advance to the width
        g = getGlyphForChar(utf8::invalid);
        width += g->advanceW;

        // if this is first char of a line then pad with lsb
        if(adjustLeft) { width -= g->lsb; adjustLeft = false; }
    }

    // do rsb adjustment if desired; only if we have a glyph
    if(g && adjustRight) { width -= g->rsb; }

    return width;
}

float FontInstance::splitLines(std::vector<unsigned> & outBreaks,
    const char * txt, unsigned len, float widthPx0, float widthPx)
{
    utf8::Decoder   decoder;
    const Glyph * g = 0;
    
    outBreaks.clear();
    outBreaks.push_back(0); // assume we split right away
    
    float remaining = widthPx0;

    float current = 0;

    unsigned charStart = 0;

    // we use this to check if last char is incomplete
    bool charDone = true;
    for(unsigned i = 0; i < len; ++i)
    {
        if(charDone) charStart = i;
        
        if(!txt[i] && len == ~0) break;
        charDone = decoder.next(txt[i]);
        if(charDone)
        {
            // if we have explicit newline, go next line
            if(decoder.ch == '\n')
            {
                outBreaks.back() = i + 1;
                outBreaks.push_back(outBreaks.back());
                
                remaining = widthPx;
                current = 0;
                continue;
            }
            
            // add glyph advance to the width
            g = getGlyphForChar(decoder.ch);
            current += g->advanceW;
            
            // if we have a space, advance current linebreak
            if(decoder.ch == ' ')
            {
                outBreaks.back() = i + 1;
                
                remaining -= current;
                current = 0;
                continue;
            }

            // does current word put us past the line break?
            if(current > remaining)
            {
                // if we are wider than the whole line, then we already
                // broke at the previous space and we should break here
                //
                // same is also true if we haven't seen any spaces
                if(!outBreaks.back() || current > widthPx)
                {
                    outBreaks.back() = charStart;
                    current = g->advanceW;
                }
                
                outBreaks.push_back(outBreaks.back());
                remaining = widthPx;
            }
        }
    }

    if(!charDone)
    {
        // add glyph advance to the width
        g = getGlyphForChar(utf8::invalid);
        
        current += g->advanceW;

        // see above
        if(current > remaining)
        {
            if(!outBreaks.back() || current > widthPx)
            {
                outBreaks.back() = charStart;
                current = g->advanceW;
            }
            
            outBreaks.push_back(outBreaks.back());
            remaining = widthPx;
        }
    }

    // finally adjust the last linebreak
    outBreaks.back() = len;

    return widthPx - remaining;
}
