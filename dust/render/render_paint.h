
#pragma once

#include "rect.h"
#include "render_color.h"
#include "render_surface.h"

// This implements various paint sources and blending modes.
//

namespace dust
{
    // IPaint is a low-level interface for CPU painting,
    // see the templated Paint class in RenderContext.
    //
    // There's no real need for clients to worry about the
    // details, since these are built automatically.
    //
    struct IPaint
    {
        // request that the specified rectangle be painted
        //
        // NOTE: this expects a clipped rectangle and failure to
        // do so results in undefined behavior!
        virtual void paintRect(const Rect & r) = 0;

        // like paintRect(..) but with additional alpha mask
        // mask points to value at (0,0), maskPitch is the row pitch
        //
        // NOTE: the pointer to the mask might NOT be valid outside
        // the specified rectangle (ie. it can be offset pointer)
        virtual void paintRectMask(
            const Rect & r, Alpha * mask, unsigned maskPitch) = 0;

    protected:
        ~IPaint() {}    // cannot be destroyed polymorphically!
    };

    // Paint source classes, put them in sub-namespace
    // These need to implement:
    //   ARGB color(int x, int y):
    //      return the color at x,y in target context coordinates
    //   Rect * getClipRect():
    //      returns the source clipping rectangle, or null if none
    namespace paint
    {
        // solid color
        struct Color
        {
            ARGB    argb;

            Color(ARGB argb) : argb(argb) {}

            ARGB color(int x, int y) const { return argb; }
            const Rect *getClipRect() const { return 0; }
        };

        struct Gradient2
        {
            ARGB    c0, c1;
            float   x0, y0;
            float   dx, dy;
            float   div;

            Gradient2(
                ARGB c0, float x0, float y0,
                ARGB c1, float x1, float y1)
                : c0(c0), c1(c1)
                , x0(x0), y0(y0)
                , dx(x1-x0), dy(y1-y0)
                , div(255.f / (dx*dx + dy*dy))
            {
            }

            ARGB color(int x, int y) const
            {
                float pf = (div * ((x-x0)*dx + (y-y0)*dy));
                int p = (int) pf;
                if(p < 0) p = 0;
                if(p > 255) p = 255;

                return color::alphaMask(c0, c1, p);
            }

            const Rect *getClipRect() const { return 0; }
        };

        // FIXME: should this perform tiling instead of clipping?
        // The most common use-case is probably just copying stuff
        // so it probably makes sense to keep this as light as possible?
        struct Image
        {
            Surface &surface;
            int     offsetX;
            int     offsetY;
            Rect    srcClip;

            // copy pixels from surface with the surface origin
            // placed at (originX,originY) on the render context
            Image(Surface & src, int originX = 0, int originY = 0)
                : surface(src), offsetX(originX), offsetY(originY)
                , srcClip(offsetX, offsetY, src.getSizeX(), src.getSizeY())
            {
            }

            const Rect * getClipRect() const { return &srcClip; }

            ARGB color(int x, int y) const
            {
                ARGB * pixels = surface.getPixels();
                int sx = x - offsetX;
                int sy = y - offsetY;

                return pixels[sx + sy*surface.getPitch()];
            }

        };
    };

    // Blending classes, put them in a sub-namespace
    //
    // these should implement a single function "blend()" as below
    namespace blend
    {
        // no blending, just replace dst with src
        struct None
        {
            static ARGB blend(ARGB dst, ARGB src)
            {
                return src;
            }
        };

        // add src and dst
        struct Add
        {
            static ARGB blend(ARGB dst, ARGB src)
            {
                return color::clipAdd(src, dst);
            }
        };

        // src over dst, standard blending
        struct Over
        {
            // standard per-multiplied over
            static ARGB blend(ARGB dst, ARGB src)
            {
                return color::AoverB(src, dst);
            }
        };

        // multiply src and dst
        struct Multiply
        {
            // multiply source and dest
            static ARGB blend(ARGB dst, ARGB src)
            {
                return color::multiply(src, dst);
            }
        };

        // inverse multiply src and dst (=1-(1-src)*(1-dst))
        struct Screen
        {
            static ARGB blend(ARGB dst, ARGB src)
            {
                // bitwise negation is equivalent to 1-x
                return ~color::multiply(~src, ~dst);
            }
        };

        // multiply dst with inverse src alpha (ignore src color)
        struct MaskOut
        {
            static ARGB blend(ARGB dst, ARGB src)
            {
                return color::blend(dst, (~src)>>24);
            }
        };

        // multiply dst with source alpha
        struct MaskIn
        {
            static ARGB blend(ARGB dst, ARGB src)
            {
                return color::blend(dst, src>>24);
            }
        };

        // this takes src color, multiplies by dst alpha
        // and keeps dst alpha as-is - use with blurred
        // source to implement an inner shadow effect
        struct InnerShadow
        {
            static ARGB blend(ARGB dst, ARGB src)
            {
                return color::blend((0xff<<24)|src, dst>>24);
            }
        };

        // this does "over" blending with src alpha and black color
        struct Shadow
        {
            static ARGB blend(ARGB dst, ARGB src)
            {
                return color::AoverB(src & (0xff<<24), dst);
            }
        };

        // does screen blend, but clips to dst alpha
        struct InnerGlow
        {
            static ARGB blend(ARGB dst, ARGB src)
            {
                src = color::blend(0xffffff&src, dst>>24);
                return ~color::multiply(~dst, ~src);
            }
        };

        // uses src color as light on dst, clipping to dst
        // multiplies the final result by two (so flat = original)
        struct InnerLight
        {
            static ARGB blend(ARGB dst, ARGB src)
            {
                ARGB c = color::multiply(dst, src|(0xff<<24));
                return color::clipAdd(c, c);
            }
        };

    };

}; // namespace
