

#include "dust/core/hash.h"
#include "dust/render/rect.h"

#include "font.h"

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC    // don't need this outside this module
#include "dust/libs/stb_truetype_min.h"

using namespace dust;

//////////////////////////////////////////////////////////
//// GLYPH CACHE - FIXME: Make this part of renderer? ////
//////////////////////////////////////////////////////////

// we could theoretically hash codepoints to glyph indexes and
// then glyph indexes to glyph data, but just skip the indexes
//
// might want to revisit this if we ever add shaping support
struct GlyphCache
{
    unsigned cp;
    Glyph * glyph;

    unsigned getKey() const { return cp; }
    bool keyEqual(unsigned _cp) const { return _cp == cp; }
    static uint64_t getHash(unsigned cp) { return hash64(cp); }
};

///////////////////////////////
//// Actual back-end logic ////
///////////////////////////////

struct FontInstanceSTB : FontInstance
{
    stbtt_fontinfo  info;
    float           scale;  // scale from font units to pixels

    Table<GlyphCache>   cache;

    FontInstanceSTB(const FontCreateParameters & p) : FontInstance(p) {}

    ~FontInstanceSTB() { clearCache(); }

    // return true if successful
    bool init()
    {
        if(!stbtt_InitFont(&info, parameters.data, 0)) return false;

        oversampleX = 1;    // oversample on X axis (set below)
        oversampleY = 1;    // oversample on Y axis (just snap?)

        float sizePx = parameters.sizePt * parameters.dpi * (1 / 72.f);

        // double the pixel size in X-direction with oversampling
        // until it's above a threshold; this improves positioning
        // of small fonts while reducing render time of large ones
        // also cap the oversampling for very small sizes just in case
        int resX = 1 + (int)sizePx;
        while(resX < 64 && oversampleX < 4)
        {
            oversampleX <<= 1; resX <<= 1;
        }

        // compute pixel size: 72 dpi gives 1pt = 1px
        scale = stbtt_ScaleForMappingEmToPixels(&info, sizePx);

        // get metrics, scale them and store them
        // we want descent as distance going down, so negate
        int ascent, descent, lineGap;
        stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);

        setMetrics(scale*ascent, -scale*descent, scale*lineGap);

        // clear cache just in case
        clearCache();

        return true;
    }

    void clearCache()
    {
        cache.foreach([](GlyphCache & gc){ free(gc.glyph); });
        cache.clear();
    }

    const Glyph * getGlyphForChar(unsigned ch)
    {
        auto * gc = cache.find(ch);
        if(gc) return gc->glyph;

        //debugPrint("generating glyph for '%c' (=%d)\n", ch, ch);

        // didn't find it, create one

        // find index, unpack some metrics
        int index = stbtt_FindGlyphIndex(&info, ch);
        // fall back to "invalid char" if not found?
        if(!index) index = stbtt_FindGlyphIndex(&info, 0xFFFD);

        // Calculate actual raster-size and bitmap box
        // We COULD just calculate this from the above but whatever.
        Rect r;
        float xSize = scale * oversampleX;
        float ySize = scale * oversampleY;
        stbtt_GetGlyphBitmapBox(&info, index, xSize, ySize,
            &r.x0, &r.y0, &r.x1, &r.y1);

        // add padding if the glyph has legit bitmap
        if(r.w() && r.h())
        {
            // add padding on both sides
            r.x1 += 2*oversampleX;
            r.y1 += 2*oversampleY;
        }

        // allocate
        auto * g = (Glyph*) malloc(sizeof(Glyph) + sizeof(Alpha) * r.w() * r.h());

        // get metrics and scale; do we need lsb for anything?
        int advanceW, lsb;
        stbtt_GetGlyphHMetrics(&info, index, &advanceW, &lsb);
        g->advanceW = scale * advanceW;
        g->lsb = scale * lsb;

        // compute rsb: advance - lsb - (xMax-xMin)
        // these are design units
        int xMin, xMax, yMin, yMax;
        stbtt_GetGlyphBox(&info, index, &xMin, &yMin, &xMax, &yMax);
        g->rsb = scale * (advanceW - lsb - (xMax - xMin));

        // we offset by 1 full pixel
        g->originX = r.x0;
        g->originY = r.y0;

        // check if glyph actually has a legit bitmap (eg. not space)
        if(r.w() && r.h())
        {
            g->bbW = r.w();
            g->bbH = r.h();

            // get the vertices
            stbtt_vertex    *verts;
            int nVerts = stbtt_GetGlyphShape(&info, index, &verts);

            // bitmap descriptor in stbtt format, then rasterize
            stbtt__bitmap sbm;
            sbm.pixels = g->bitmap;
            sbm.stride = r.w();
            sbm.w = r.w();
            sbm.h = r.h();

            // prefiltering will shift the glyph by .5*(os-1)
            // so calculate inverse offsets
            float shiftX = .5*(oversampleX - 1) / oversampleX;
            float shiftY = .5*(oversampleY - 1) / oversampleY;

            // second parameter is tolerance, last two are invert and userdata
            // we always want our coordinate system with y going up, so invert
            stbtt_Rasterize(&sbm, .25f, verts, nVerts,
                xSize, ySize, shiftX, shiftY,
                r.x0-oversampleX, r.y0-oversampleY, 1, 0);

            // free the vertices
            STBTT_free(verts, 0);

            // finally do a filtering
            if(oversampleX > 1)
            {
                for(unsigned y = 0; y < g->bbH; ++y)
                {
                    for(unsigned x = 0; x < g->bbW - oversampleX; ++x)
                    {
                        int total = 0;
                        // short kernel so just run it brute-force
                        for(unsigned j = 0; j < oversampleX; ++j)
                        {
                            total += g->bitmap[(x+j) + y*g->bbW];
                        }
                        g->bitmap[x + y*g->bbW] = total / oversampleX;
                    }
                }
            }

            if(oversampleY > 1)
            {
                for(unsigned y = 0; y < g->bbH - oversampleY; ++y)
                {
                    for(unsigned x = 0; x < g->bbW; ++x)
                    {
                        int total = 0;
                        // short kernel so just run it brute-force
                        for(unsigned j = 0; j < oversampleY; ++j)
                        {
                            total += g->bitmap[x + (j+y)*g->bbW];
                        }
                        g->bitmap[x + y*g->bbW] = total / oversampleY;
                    }
                }
            }
        }
        else
        {
            g->bbW = 0;
            g->bbH = 0;
        }

        // oh right, store the glyph into the cache
        cache.insert(GlyphCache{ch, g});
        return g;
    }
};

////////////////////
//// FONT CACHE ////
////////////////////

struct FontCache
{
    FontCreateParameters    key;
    FontInstanceSTB         *value;

    FontCreateParameters const & getKey() const { return key; }
    bool keyEqual(FontCreateParameters const & other) const
    {
        return key.data == other.data
            && key.sizePt == other.sizePt
            && key.dpi == other.dpi;
    }
    static uint64_t getHash(FontCreateParameters const & key)
    {
        return stringHash64((uint8_t*)&key, sizeof(key));
    }
};

static Table<FontCache> fontCache;

struct FontInstanceSTBCached : FontInstanceSTB
{
    FontInstanceSTBCached(const FontCreateParameters & param)
    : FontInstanceSTB(param)
    {
        fontCache.insert(FontCache{parameters, this});
        //debugPrint("Fonts in cache: %d\n", fontCache.size());
    }
    ~FontInstanceSTBCached()
    {
        fontCache.remove(parameters);
        //debugPrint("Fonts in cache: %d\n", fontCache.size());
    }
};

void Font::loadFont(const FontCreateParameters & param)
{
    FontInstanceSTB * font = 0;

    auto * cached = fontCache.find(param);

    if(cached) { font = cached->value; font->retain(); }
    else
    {
        font = new FontInstanceSTBCached(param);

        // try to initialise the font
        if(!font->init()) { font->release(); return; }
    }

    // once we're done, replace any existing font
    release();
    instance = font;
}
