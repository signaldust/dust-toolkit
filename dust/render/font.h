
#pragma once

#include <string>   // for convenience

#include "render_color.h"

namespace dust
{
    // This contains all the data that we cache for a given glyph,
    // there is normally no need to access this data directly.
    //
    // The bitmap is normally oversampled and then pre-filtered.
    // One should getOversampleX() and getOversampleY() on the font
    // to figure out how to stride through the bitmap data.
    //
    // NOTE: lsb is (typically negative) offset from origin
    // where as rsb is (typically positive) offset to advanceW
    //
    // so the logical extent of a glyph is [lsb, advanceW - rsb]
    //
    struct Glyph
    {
        int         originX;    // bitmap origin relative to glyph
        int         originY;    // bitmap origin relative to glyph

        unsigned    bbW, bbH;   // bitmap pixel size

        // these are in non-oversampled scale:
        float       advanceW;   // advance width in pixels
        float       lsb, rsb;   // left-side/right-side bearing

        Alpha       bitmap[];   // alpha mask for the glyph: bbW x bbH
    };

    struct FontCreateParameters
    {
        const uint8_t * data;
        float sizePt;
        float dpi;
    };

    // FontInstance is a base-class for actual font implementations
    struct FontInstance
    {
        const FontCreateParameters  parameters;

        FontInstance(const FontCreateParameters & param)
        : parameters(param), __refCount(1) {}

        // increment reference count and return a pointer to object
        FontInstance * retain() { ++__refCount; return this; }
        // decrement reference count and return null-pointer
        FontInstance * release() { if(!--__refCount) delete this; return 0; }

        //////////////////////
        /// Core Interface ///
        //////////////////////

        // return font ascent (up from baseline; design metric)
        float getAscent() { return metrics.ascent; }

        // return font descent (down from baseline; design metric)
        float getDescent() { return metrics.descent; }

        // return the distance from descent to the ascent of next line
        float getLineGap() { return metrics.linegap; }

        // return line height = ascent + descent + lineGap
        float getLineHeight() { return metrics.lineheight; }

        // this tries to approximate vertical centering offset
        // that can be added to box-center to vertically align
        float getVertOffset() { return .5f * (metrics.ascent - metrics.descent); }

        // return the advance width for the character (ie. rsb - lsb)
        float getCharAdvanceW(unsigned ch) { return getGlyphForChar(ch)->advanceW; }

        // return the left-side bearing for a character
        float getCharLSB(unsigned ch) { return getGlyphForChar(ch)->lsb; }

        // get the extents of an utf-8 string, by default the advance width
        //
        // if adjustLeft/adjustRight are true, then the width will be adjusted
        // by the lsb/rsb of the first/last char for centering purposes
        //
        // NOTE: if you are not trying to center text, leave them false!
        float getTextWidth(const char * txt, unsigned txtLen,
            bool adjustLeft = false, bool adjustRight = false);

        float getTextWidth(const std::string & str,
            bool adjustLeft = false, bool adjustRight = false)
        {
            return getTextWidth
            (str.c_str(), str.size(), adjustLeft, adjustRight);
        }

        // this tries to find basic "intelligent" linebreaks
        //  widthPx0 is the maximum pixel width of the first line
        //  widthPx  is the maximum pixel width of the other lines
        //
        // on return, outBreaks stores the text-positions just after newlines
        // returns the pixel width of the last line
        float splitLines(std::vector<unsigned> & outBreaks,
            const char * txt, unsigned txtLen, float widthPx0, float widthPx);

        ///////////////////////////
        /// Advanced interfaces ///
        ///////////////////////////

        // returns a glyph for a character, see notes on Glyph
        virtual const Glyph * getGlyphForChar(unsigned ch) = 0;

        // get the oversampling factors - these are per-font
        unsigned getOversampleX() const { return oversampleX; }
        unsigned getOversampleY() const { return oversampleY; }

    protected:
        virtual ~FontInstance() { }  // only destroy by release()

        /////////////////////////////////////////////
        /// Internal Interface for implementation ///
        /////////////////////////////////////////////

        // these are needed by every font ever, so keep them here
        struct Metrics
        {
            float ascent;
            float descent;
            float linegap;
            float lineheight;   // cached from the above
        } metrics;

        unsigned    oversampleX;    // bitmap oversampling in X direction
        unsigned    oversampleY;    // bitmap oversampling in Y direction

        void setMetrics(float ascent, float descent, float linegap)
        {
            metrics.ascent = ascent;
            metrics.descent = descent;
            metrics.linegap = linegap;
            metrics.lineheight = ascent + descent + linegap;
        }

    private:
        unsigned __refCount;
    };

    // never call this directly, it's just a rebuild time optimisation
    // this keeps font.default.h from having to include anything
    const uint8_t * __getDefaultFontData(bool monospace);

    struct Font
    {
        Font() { instance = 0; }
        Font(const Font & f) { instance = f.instance->retain(); }
        ~Font() { release(); }

        void release() { if(instance) instance = instance->release(); }

        // returns true if the font is valid
        bool valid() { return 0 != instance; }

        // returns true if the font is valid and automaticaly sets
        // DPI to match the parameter if required
        bool valid(float dpi) { setDPI(dpi); return valid(); }

        void resize(float sizePt, float dpi)
        {
            if(!instance) return;

            FontCreateParameters param =
            { instance->parameters.data, sizePt, dpi };
            loadFont(param);
        }

        void setSizePt(float sizePt)
        {
            if(instance && instance->parameters.sizePt != sizePt)
                resize(sizePt, instance->parameters.dpi);
        }

        void setDPI(float dpi)
        {
            if(instance && instance->parameters.dpi != dpi)
                resize(instance->parameters.sizePt, dpi);
        }

        // add load functions for each font implementation backend
        // we could pass the length too but at least STBTT doesn't care
        void loadFont(float sizePt, float dpi, const uint8_t * fontData)
        {
            FontCreateParameters param = { fontData, sizePt, dpi };
            loadFont(param);
        }

        // default load wrappers in font.default.cpp
        void loadDefaultFont(float sizePt, float dpi, bool monospace = false)
        {
            loadFont(sizePt, dpi, __getDefaultFontData(monospace));
        }

        // convenience overloads
        void loadDefaultFont(float sizePt) { loadDefaultFont(sizePt, 96, false); }
        void loadDefaultMono(float sizePt) { loadDefaultFont(sizePt, 96, true); }

        Font & operator=(Font & f)
        { release(); instance = f.instance->retain(); return *this; }
		FontInstance * operator->() const { return instance; }

    private:
        FontInstance * instance;

        void loadFont(const FontCreateParameters & param);

    };

}; // namespace
