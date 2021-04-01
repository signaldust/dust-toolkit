
#include "render.h"

#include "dust/core/utf8.h"

using namespace dust;

void RenderContext::clear(ARGB color)
{
    // get the underlying surface data and pitch
    ARGB * pixels = target.getPixels();
    unsigned pitch = target.getPitch();

    // explicit width computation seems necessary
    // to allow clang to auto-vectorise the inner loop
    unsigned clipW = clipRect.x1 - clipRect.x0;

    // clip-rect is already in surface coordinates so
    // all we have to do is just loop over the pixels
    for(int y = clipRect.y0; y < clipRect.y1; ++y)
    {
        ARGB * pixRow = pixels + clipRect.x0 + y*pitch;
        
        for(int x = 0; x < clipW; ++x) pixRow[x] = color;
    }

}

// this is basically a copy of getTextWidth from font.cpp
// except here we want to actuallly issue the render calls
//
// FIXME: this duplicates code, might wannt match these?
//
float RenderContext::drawTextWithPaint(Font & f, IPaintGlyph & paint,
    const char *txt, unsigned len, float x, float y, bool adjustLeft)
{
    utf8::Decoder   decoder;

    unsigned osX = f->getOversampleX();
    unsigned osY = f->getOversampleY();

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
            g = f->getGlyphForChar(decoder.ch);

            // if this is first char of a line then pad with lsb
            if(adjustLeft) { width -= g->lsb; adjustLeft = false; }

            paint.paintGlyph(g, offX + x + width, offY + y, osX, osY);

            width += g->advanceW;
        }
    }

    if(!charDone)
    {
        // add glyph advance to the width
        g = f->getGlyphForChar(utf8::invalid);

        // if this is first char of a line then pad with lsb
        if(adjustLeft) { width -= g->lsb; adjustLeft = false; }

        paint.paintGlyph(g, offX + x + width, offY + y, osX, osY);

        width += g->advanceW;
    }

    return width;

}
