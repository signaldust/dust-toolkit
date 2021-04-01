
#pragma once

#include <string>   // for convenience to draw string directly

#include "rect.h"
#include "render_paint.h"
#include "render_path.h"
#include "font.h"

namespace dust
{

    // RenderContext is an immutable wrapper of render state.
    //
    // To modify the state, one should construct a new context
    // with some of the parameters potentially inherited.
    //
    struct RenderContext
    {
        // construct a render context for a surface
        // sets the clipping rectangle to cover the surface
        // set the origin point to (0,0)
        RenderContext(Surface & dst)
            : target(dst)
            , clipRect(0,0,dst.getSizeX(),dst.getSizeY())
            , offX(0), offY(0)
        {
        }

        // construct a render context for a surface
        // sets the clipping rectangle to the intersection
        // of the surface bounds and the specified clip
        // if offset is true, sets origin to top-left of clip
        RenderContext(Surface & dst
            , const Rect & clip, bool offset = false)
            : target(dst), clipRect(clip)
            , offX(offset ? clip.x0 : 0)
            , offY(offset ? clip.y0 : 0)
        {
            clipRect.clip(Rect(0,0,dst.getSizeX(), dst.getSizeY()));
        }

        // construct a render context for a surface
        // sets the clipping rectangle to the intersection
        // of the surface bounds and the specified clip
        // set the origin point to (oX, oY)
        RenderContext(Surface & dst
            , const Rect & clip, int oX, int oY)
            : target(dst), clipRect(clip), offX(oX), offY(oY)
        {
            clipRect.clip(Rect(0,0, dst.getSizeX(), dst.getSizeY()));
        }

        // Construct a render context from existing context with
        // additional clipping. If offset is true, origin is set
        // to the top-left corner of clipTo region.
        //
        RenderContext(RenderContext & parent
            , const Rect & clipTo, bool offset = false)
            : target(parent.target), clipRect(parent.clipRect)
            , offX(parent.offX + (offset ? clipTo.x0 : 0))
            , offY(parent.offY + (offset ? clipTo.y0 : 0))
        {
            // clipTo is in parent coordinates so offset to surface
            clipRect.clip(clipTo, parent.offX, parent.offY);;
        }

        // Construct a render context from existing context with
        // additional clipping and specified origin offset.
        //
        // The clipping and origin are relative to parent origin.
        //
        RenderContext(RenderContext & parent
            , const Rect & clipTo, int oX, int oY)
            : target(parent.target), clipRect(parent.clipRect)
            , offX(parent.offX + oX), offY(parent.offY + oY)
        {
            // clipTo is in parent coordinates so offset to surface
            clipRect.clip(clipTo, parent.offX, parent.offY);;
        }

        // Construct a render context from existing contxt with
        // specified origin offset but without adjusting clipping
        RenderContext(RenderContext & parent, int oX, int oY)
            : target(parent.target), clipRect(parent.clipRect)
            , offX(parent.offX + oX), offY(parent.offY + oY)
        {
        }

        // returns clip rect relative to current offset
        const Rect getClipRect() const
        {
            return Rect(clipRect.x0 - offX, clipRect.y0 - offY,
                clipRect.w(), clipRect.h());
        }

        // returns clip rect relative to backing store
        const Rect getBackingClipRect() const { return clipRect; }

        // returns current x-offset relative to backing store
        const int getBackingOffsetX() const { return offX; }

        // returns current x-offset relative to backing store
        const int getBackngOffsetY() const { return offY; }

        // this returns true if context draws into the specified surface
        const bool isContextForSurface(Surface & s) { return &s == &target; }

        /////////////////////
        // DRAWING METHODS //
        /////////////////////

        // clear the target into the specified color
        void clear(ARGB color = 0);

        // fill the target with the specified paint
        template <typename Blend = blend::Over, typename PaintSource>
        void fill(const PaintSource & src)
        {
            // build paint
            Paint<PaintSource, Blend> paint(*this, src);
            // paint the whole clipping rectangle
            paint.paintRect(clipRect);
        }

        // fill the specified rectangle
        template <typename Blend = blend::Over, typename PaintSource>
        void fillRect(const PaintSource & src, int x, int y, int w, int h)
        {
            // build paint
            Paint<PaintSource, Blend> paint(*this, src);
            // paint with another rectangle
            Rect r(offX + x, offY + y, w, h);
            r.clip(clipRect);
            paint.paintRect(r);
        }

        // shortcut to draw a rectangle without filling
        // this is used by layout debugging visualisation
        //
        // bs is the border size to draw
        template <typename Blend = blend::Over, typename PaintSource>
        void drawRectBorder(const PaintSource & src,
            int x, int y, int w, int h, int bs = 1)
        {
            Rect r(x,y,w,h);
            // paint top and bottom
            fillRect(src, r.x0, r.y0, r.w(), bs);
            fillRect(src, r.x0, r.y1-bs, r.w(), bs);

            // paint left and right
            fillRect(src, r.x0, r.y0+bs, bs, r.h()-2*bs);
            fillRect(src, r.x1-bs, r.y0+bs, bs, r.h()-2*bs);
        }

        // shortcut to copy the whole surface at (x,y)
        // use copyRect if you want more control on what to copy
        template <typename Blend = blend::None>
        void copy(Surface & src, int x = 0, int y = 0)
        {
            paint::Image    srcPaint(src, x, y);
            fillRect<Blend>(srcPaint, x, y,
                src.getSizeX(), src.getSizeY());
        }

        // shortcut to copy a rectangle from a surface
        // (sx,sy) specifies the surface point to place at (x,y)
        template <typename Blend = blend::None>
        void copyRect(int x, int y, int w, int h,
            Surface & src, int sx = 0, int sy = 0)
        {
            // surface takes offset for it's origin
            paint::Image    srcPaint(src, x-sx, y-sy);
            // then just do a regular fillRect
            fillRect<Blend>(srcPaint, x, y, w, h);
        }

        // rasterize the path and fill it using the specified paint
        // setting vScan = true might make horizontal plots faster
        template <typename Blend = blend::Over, typename PaintSource>
        void fillPath(Path & p, const PaintSource & src,
            FillRule fill = FILL_NONZERO, int quality = 2, bool vScan = false)
        {
            // calculate frame local clipping rectangle
            Rect r(clipRect.x0-offX, clipRect.y0-offY,
                clipRect.w(), clipRect.h());

            // allocate space for alpha
            Alpha * maskData = new Alpha[r.w()*r.h()];
            int maskPitch = r.w();

            // draw the shape
            Alpha * maskPtr = maskData - r.x0 - maskPitch*r.y0;
            if(renderPathRef(p, r, fill,
                maskPtr, maskPitch, quality, vScan))
            {
                // then do the fill
                Paint<PaintSource, Blend> paint(*this, src);
                r.offset(offX, offY);
                paint.paintRectMask(r, maskPtr - offX - maskPitch * offY, maskPitch);
            }

            // release the mask
            delete [] maskData;
        }

        // rasterize a stroke for a path and fill it using the specified paint
        // setting vScan = true might make horizontal plots faster
        template <typename Blend = blend::Over, typename PaintSource>
        void strokePath(Path & p, float width, const PaintSource & src,
            int quality = 2, bool vScan = false)
        {
            // calculate frame local clipping rectangle
            Rect r(clipRect.x0-offX, clipRect.y0-offY,
                clipRect.w(), clipRect.h());

            // allocate space for alpha
            Alpha * maskData = new Alpha[r.w()*r.h()];
            int maskPitch = r.w();

            // draw the shape
            Alpha * maskPtr = maskData - r.x0 - maskPitch*r.y0;
            if(strokePathRef(p, width, r, maskPtr, maskPitch, quality, vScan))
            {
                // then do the fill
                Paint<PaintSource, Blend> paint(*this, src);
                r.offset(offX, offY);
                paint.paintRectMask(r, maskPtr - offX - maskPitch * offY, maskPitch);
            }

            // release the mask
            delete [] maskData;
        }

        // draw a glyph, position is the base-line pen position
        // osX and osY are the oversampling factors of the bitmap
        template <typename PaintSource>
        void drawGlyph(const Glyph & g, unsigned osX, unsigned osY,
            const PaintSource & src, float x, float y)
        {
            Paint<PaintSource, blend::Over> paint(*this, src);
            paint.paintGlyph(g, x + offX, y + offY, osX, osY);
        }

        // draw a unicode character - returns advance width
        template <typename PaintSource>
        float drawChar(Font & font, unsigned ch,
            const PaintSource & src, float x, float y)
        {
            Paint<PaintSource, blend::Over> paint(*this, src);

            const Glyph * g = font->getGlyphForChar(ch);
            paint.paintGlyph(g, x + offX, y + offY,
                font->getOversampleX(),
                font->getOversampleY());

            return g->advanceW;
        }

        // draw utf-8 string - returns total advance width
        //
        // if adjustLeft is true, adjust the initial position by
        // the lsb of the first character; useful only for centering
        // see drawCenteredText below which does the right thing (usually)
        template <typename PaintSource>
        float drawText(Font & font, const char * text, unsigned len,
            const PaintSource & src, float x, float y, bool adjustLeft = false)
        {
            Paint<PaintSource, blend::Over> paint(*this, src);
            return drawTextWithPaint(font, paint, text, len, x, y, adjustLeft);
        }

        template <typename PaintSource>
        float drawText(Font & font, const std::string & str,
            const PaintSource & src, float x, float y, bool adjustLeft = false)
        {
            return drawText(font, str.c_str(), str.size(), src, x, y, adjustLeft);
        }

        // draw utf-8 string horizontally centered around x
        // this attempts to take font lsb/rsb into account
        //
        template <typename PaintSource>
        void drawCenteredText(Font & font, const char *text, unsigned len,
            const PaintSource & src, float x, float y)
        {
            Paint<PaintSource, blend::Over> paint(*this, src);
            // take center of the box and adjust back by half width
            // we use the adjusted metrics here
            float w = font->getTextWidth(text, len, true, true);

            // and then we draw with adjusted left
            drawTextWithPaint(font, paint, text, len, x - .5f * w, y, true);
        }

        template <typename PaintSource>
        void drawCenteredText(Font & font, const std::string & str,
            const PaintSource & src, float x, float y)
        {
            drawCenteredText(font, str.c_str(), str.size(), src, x, y);
        }
    private:
        Surface &target;    // CPU render into a surface
        Rect    clipRect;   // clipping rect, in surface coordinates
        int     offX, offY; // origin point, relative to surface

        // this is just used internally to avoid templating drawText
        struct IPaintGlyph
        {
            virtual void paintGlyph(const Glyph * g, float x, float y,
                unsigned osX, unsigned osY) = 0;
        };

        ///////////////////////////
        // IPaint implementation //
        ///////////////////////////
        //
        // This is private, because there's no legit reason
        // to ever actually construct these except internally.
        template <typename PaintSource, typename Blend = blend::Over>
        struct Paint : Blend, IPaint, IPaintGlyph
        {
            RenderContext & rc;
            const PaintSource & src;

            Paint(RenderContext & rc, const PaintSource & src)
            : rc(rc), src(src) {}

            void paintRect(const Rect & rr)
            {
                // clip the rectangle to the render context
                Rect r = rr;

                // get source clipping rectangle and clip if necessary
                const Rect * srcClip = src.getClipRect();
                if(srcClip) { r.clip(*srcClip, rc.offX, rc.offY); }

                ARGB * dst = rc.target.getPixels();
                unsigned dstPitch = rc.target.getPitch();
                for(int y = r.y0; y < r.y1; ++y)
                {
                    for(int x = r.x0; x < r.x1; ++x)
                    {
                        ARGB & pixel = dst[x+dstPitch*y];
                        // source expects RC relative coordinates
                        pixel = Blend::blend(pixel,
                            src.color(x-rc.offX,y-rc.offY));
                    }
                }
            }

            void paintRectMask(const Rect & rr,
                Alpha * mask, unsigned maskPitch)
            {
                // clip the rectangle to the render context
                Rect r = rr;

                // get source clipping rectangle and clip if necessary
                const Rect * srcClip = src.getClipRect();
                if(srcClip) { r.clip(*srcClip, rc.offX, rc.offY); }

                ARGB * dst = rc.target.getPixels();
                unsigned dstPitch = rc.target.getPitch();

                for(int y = r.y0; y < r.y1; ++y)
                {
                    for(int x = r.x0; x < r.x1; ++x)
                    {
                        // fast path to reduce bandwidth
                        uint8_t alpha = mask[x+maskPitch*y];
                        if(!alpha) continue;

                        ARGB & pixel = dst[x+dstPitch*y];
                        // source expects RC relative coordinates
                        pixel = color::alphaMask(pixel,
                            Blend::blend(pixel,
                                src.color(x-rc.offX,y-rc.offY)),
                            alpha);
                    }
                }
            }

            void paintGlyph(const Glyph * g, float x, float y,
                unsigned osX, unsigned osY)
            {
                // check if bitmap has non-zero size
                if(!g->bbW || !g->bbH) return;

                // calculate position with oversampled resolution
                int xs = (int) floor(osX*x + g->originX);
                int ys = (int) floor(osY*y + g->originY);

                // divide to get the actual rectangle screen placement
                // the sub-pixel positions then need a slight fix
                //
                // want rounding always the same way, so bump the
                // value up so it's never negative
                int big = 1<<20;    // should be large enough
                int x0 = (xs+big*osX) / osX - big; xs -= osX - 1;
                int y0 = (ys+big*osY) / osY - big; ys -= osY - 1;

                // build a screen-space rectangle
                Rect r( x0, y0, g->bbW / osX, g->bbH / osY);

                // clip the rectangle to the render context
                r.clip(rc.clipRect);

                // get source clipping rectangle and clip if necessary
                const Rect * srcClip = src.getClipRect();
                if(srcClip) { r.clip(*srcClip, rc.offX, rc.offY); }

                // oh right, loop the thing
                ARGB * dst = rc.target.getPixels();
                unsigned dstPitch = rc.target.getPitch();
                for(int y = r.y0; y < r.y1; ++y)
                {
                    for(int x = r.x0; x < r.x1; ++x)
                    {
                        int offset = (x*osX-xs)+(y*osY-ys)*g->bbW;

                        if(offset < 0 || offset >= g->bbW * g->bbH)
                        {
                            debugPrint("bad glyph read offset at (%d,%d):"
                                "x*os - xs: %d (w:%d), y*os - ys: %d (h:%d)\n",
                                x, y, x*osX-xs, g->bbW, y*osY-ys, g->bbH);
                                return;
                        }

                        Alpha mask = g->bitmap[offset];

                        ARGB & pixel = dst[x+dstPitch*y];
                        pixel = color::alphaMask(pixel,
                            Blend::blend(pixel,
                                src.color(x-rc.offX,y-rc.offY)), mask);
                    }
                }
            }
        };

        // this goes into render.cpp 'cos it's a bit longish
        float drawTextWithPaint(Font & font, IPaintGlyph & paint,
            const char *text, unsigned len,
            float x, float y, bool adjustLeft);
    };

    
    // This implements minimal SVG support, using NanoSVG for loading.
    //
    // We wrap in a custom class here, because we only support some features,
    // we want to simplify drawing and we want to scale on demand.
    //
    struct SVG
    {
        struct Shape
        {
            Path    path;   // merge all paths into one

            ARGB    fColor; // fill color
            FillRule fRule; // fill rule
            
            ARGB    sColor; // stroke color
            float   sWidth; // stroke width
        };

        std::vector<Shape>  shapes;

        float width, height;    // in points

        // NanoSVG doesn't take data length
        void load(const char * svgData);
        
        void loadFile(const char * path);

        // scale to fit the specified box, keeps aspect ratio and centers
        void renderFit(RenderContext & rc, float w, float h)
        {
            float scaleX = w / width;
            float scaleY = h / height;

            float scale = std::min(scaleX, scaleY);
            float dpi = 72.f * scale;

            float szX = scale * width;
            float szY = scale * height;

            render(rc, dpi, .5f*(w-szX), .5f*(h-szY));
        }

        // render into a specific position, with scaling by DPI
        void render(RenderContext & rc, float dpi, float x = 0, float y = 0)
        {
            float scale = dpi / 72.f;
            for(auto & s : shapes)
            {
                Path p;
                s.path.process(TransformPath<Path>(p, scale, x, y));

                if(s.fColor)
                {
                    rc.fillPath(p, paint::Color(s.fColor), s.fRule);
                }
                if(s.sColor && s.sWidth > 0)
                {
                    rc.strokePath(p, scale*s.sWidth, paint::Color(s.sColor));
                }
            }
        }

    private:
        void importNSVG(void *);

    };

};
