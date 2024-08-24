
#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>

namespace dust
{

    // represent AARGB colors as 32-bit integers
    // pack them as 0xAaRrGgBb
    typedef uint32_t ARGB;

    // alpha data is 8-bit unsigned
    typedef uint8_t  Alpha;

    // put the low-level stuff in a sub-namespace
    namespace color
    {
        // this takes blend range [0, 0xff]
        static inline ARGB blend(ARGB c, Alpha blend)
        {
            if(!blend) return 0;
            if(blend==0xff) return c;

            // slightly faster(?) 64-bit path
            if(sizeof(uintptr_t) >= 8)
            {
                uintptr_t _c = c;
                _c = (((_c&0xff00ff00u)<<24)|(_c&0x00ff00ffu)) * blend
                    + 0x0080008000800080u;
                _c &= 0xff00ff00ff00ff00u;

                return ARGB( (_c>>8) | (_c >> 32) );
            }
            else
            {
                // two components at a time
                ARGB ag = (((c>>8)& 0xff00ff)*blend) + 0x800080;
                ARGB rb = ((c     & 0xff00ff)*blend) + 0x800080;
    
                ag = ((ag + ((ag&0xff00ff00)>>8))&0xff00ff00);
                rb = ((rb + ((rb&0xff00ff00)>>8))&0xff00ff00)>>8;
                return ag|rb;
            }
        }

        // blend color, keep alpha
        static inline ARGB blendColor(ARGB c, Alpha bl)
        {
            return (blend(c, bl) & 0xffFFff) | ((0xff<<24) & c);
        }

        // multiply two colors
        static inline ARGB multiply(ARGB c1, ARGB c2)
        {
            if(!c1 || !c2) return 0;
            if(!~c1) return c2;
            if(!~c2) return c1;

            // slight faster(?) 64-bit path
            if(sizeof(uintptr_t) >= 8)
            {
                uintptr_t _c1 = c1;
                uintptr_t _c2 = c2;

                uintptr_t ag = (_c1&0xff00ff00u)*(_c2&0xff00ff00u);
                uintptr_t rb = (_c1&0x00ff00ffu)*(_c2&0x00ff00ffu);

                ag += 0x0080000000800000;
                ag &= 0xff000000ff000000;
                
                rb += 0x0000008000000080;
                rb &= 0x0000ff000000ff00;
    
                uintptr_t _c = (ag>>16)|(rb>>8);
                return ARGB(_c | (_c>>16));
    
                //return ARGB((ag>>16)|(ag>>32)|(rb>>8)|(rb>>24));
            }
            else
            {            
                // This unfortunately must be done component at a time
                ARGB a = (c1 >> 24)          * (c2 >> 24);
                ARGB r = ((c1 >> 16)&0xff)   * ((c2 >> 16)&0xff);
                ARGB g = ((c1 >> 8)&0xff)    * ((c2 >> 8)&0xff);
                ARGB b = (c1 & 0xff)         * (c2 & 0xff);
    
                // fix the [0, 254] range to [0, 255] instead
                a = (a + 0x80) & 0xff00;
                r = (r + 0x80) & 0xff00;
                g = (g + 0x80) & 0xff00;
                b = (b + 0x80) & 0xff00;
    
                return (a << 16) | (r << 8) | g | (b >> 8);
            }
        }

        // add two colors with saturation
        static inline ARGB clipAdd(ARGB c1, ARGB c2) {

            if(!c1) return c2;
            if(!c2) return c1;

            // slightly faster(?) 64-bit path
            if(sizeof(uintptr_t) >= 8)
            {
                uintptr_t _c1 = c1, _c2 = c2;
                _c1 = ((_c1<<24)|_c1)&0x00ff00ff00ff00ffu;
                _c2 = ((_c2<<24)|_c2)&0x00ff00ff00ff00ffu;

                uintptr_t _c = _c1 + _c2;

                uintptr_t sat = 0xff * (_c & (0x0100010001000100u));
                _c = ((sat>>8) | (_c & 0x00ff00ff00ff00ffu));
                return ARGB( _c | (_c >> 24) );
            }
            else
            {
                // two components at a time
                ARGB ag = ((c1&0xff00ff00)>>8) + ((c2&0xff00ff00)>>8);
                ARGB rb = ((c1&0x00ff00ff)     + ((c2&0x00ff00ff)));
    
                // saturate by taking the overflow, multiplying by 0xff
                // to get an OR mask, then AND out the garbage
                ARGB sat = 0xff * ((ag&0x01000100) | ((rb&0x01000100)>>8));
                ARGB argb = (sat | (rb&0x00ff00ff)) | ((ag&0x00ff00ff)<<8);
    
                return argb;
            }
        }

        // premultiplied over-composition,
        // since this can overflow, use saturated adds
        static inline ARGB AoverB(ARGB a, ARGB b) {

            // if either is zero, just return the other
            if(!a) return b;
            if(!b) return a;

            int a_alpha = (a>>24);

            // early out for completely opaque A
            if(a_alpha == 0xff) return a;

            return clipAdd(a, blend(b, (unsigned char) (255-a_alpha)));
        }

        // lerp the colors using an alpha value as parameter
        // gives c0 when a = 0, c1 when a=0xff
        static inline ARGB alphaMask(ARGB c0, ARGB c1, Alpha a) {
            return blend(c0, ~a) + blend(c1, a);
        }

        // lerp the colors using open-range 0:8 fixed point fraction
        // note that a is NOT an alpha value (it has different range!)
        // intended for image interpolation with sub-pixel positions
        //
        // NOTE: this will NEVER return c2!
        static inline ARGB lerp(ARGB c1, ARGB c2, uint8_t frac)
        {
            ARGB rb1 = c1     &0xff00ff;
            ARGB ag1 = (c1>>8)&0xff00ff;

            ARGB rb2 = c2     &0xff00ff;
            ARGB ag2 = (c2>>8)&0xff00ff;

            ARGB drb = rb2 - rb1;
            ARGB dag = ag2 - ag1;

            drb *= frac; drb >>= 8;
            dag *= frac; dag >>= 8;

            ARGB rb  =  (drb+rb1)       & 0x00ff00ff;
            ARGB ag  = ((dag+ag1) << 8) & 0xff00ff00;

            return rb | ag;
        }

        // This is mostly for computing colors once, not optimized
        // Computes c1/c2 and clips the result
        static inline ARGB divide(ARGB c1, ARGB c2)
        {
            auto a1 = 0xffu & (c1 >> 24);
            auto r1 = 0xffu & (c1 >> 16);
            auto g1 = 0xffu & (c1 >> 8);
            auto b1 = 0xffu & c1;

            auto a2 = 0xffu & (c2 >> 24);
            auto r2 = 0xffu & (c2 >> 16);
            auto g2 = 0xffu & (c2 >> 8);
            auto b2 = 0xffu & c2;

            auto a = std::min(0xffu, (a1*0xff) / std::max(1u, a2));
            auto r = std::min(0xffu, (r1*0xff) / std::max(1u, r2));
            auto g = std::min(0xffu, (g1*0xff) / std::max(1u, g2));
            auto b = std::min(0xffu, (b1*0xff) / std::max(1u, b2));

            return (a<<24)|(r<<16)|(g<<8)|b;
        }

        // this is the "standard" HSV but we gamma correct the results
        // since this makes it easier to create sensible gradients
        static inline ARGB getHSV(float H, float S, float V)
        {
            if(H < 0) H = 1.f - fmod(-H, 1.f);
            if(H > 1) H = fmod(H, 1.f);

            unsigned char R, G, B;

            if ( S == 0 )                       //HSV from 0 to 1
            {
                R = (unsigned char) (V * 255);
                G = (unsigned char) (V * 255);
                B = (unsigned char) (V * 255);
            }
            else
            {
                float var_h = H * 6;
                if ( var_h == 6 ) var_h = 0 ;     //H must be < 1
                int var_i = (int) floor( var_h ); //Or ... var_i = floor( var_h )
                float var_1 = V * ( 1 - S );
                float var_2 = V * ( 1 - S * ( var_h - var_i ) );
                float var_3 = V * ( 1 - S * ( 1 - ( var_h - var_i ) ) );

                float var_r, var_g, var_b;

                if      ( var_i == 0 ) { var_r = V     ; var_g = var_3 ; var_b = var_1; }
                else if ( var_i == 1 ) { var_r = var_2 ; var_g = V     ; var_b = var_1; }
                else if ( var_i == 2 ) { var_r = var_1 ; var_g = V     ; var_b = var_3; }
                else if ( var_i == 3 ) { var_r = var_1 ; var_g = var_2 ; var_b = V    ; }
                else if ( var_i == 4 ) { var_r = var_3 ; var_g = var_1 ; var_b = V    ; }
                else                   { var_r = V     ; var_g = var_1 ; var_b = var_2; }

                R = (unsigned char) (sqrt(var_r) * 255);  //RGB results from 0 to 255
                G = (unsigned char) (sqrt(var_g) * 255);
                B = (unsigned char) (sqrt(var_b) * 255);
            }

            // always return opaque alpha
            return (0xff<<24)|(R<<16)|(G<<8)|B;
        }

        // this tries to bias hue in such a way that equal division
        // should give visually distinct colors most of the time
        static inline ARGB getNiceHSV(float h, float s, float v)
        {
            // the idea here to avoid lots of colors in the
            // cyan-range where they are hard to distinquish
            //
            // this is probably because the green tends to dominate
            //
            // probably because of the R-G and B-Y neuron arrangement
            // biasing the colors towards the range around red works
            // much better in practice
            //
            h = fmod(h, 1);
            // apply smooth-step, then add a bit of linear back to
            // avoid having zero derivative around red
            h = (1/4.f)*(h + 3*h*h*(3 - 2*h));
            return getHSV(h, s, v);
        }

        // this is very much "custom"
        // this tries to maintain luminosity no matter what
        // and then tries to generate nice colors elsewhere
        static inline ARGB getNiceHSL(float h, float s, float l)
        {
            // for luminosity, use sRGB^3 for better values?
            l *= l;
            
            // wrap h
            h = fmod(h, 1); if(h < 0) h += 1; h *= 3;
            
            float pr = (h < 1) ? std::max(0.f,1-h) : std::max(0.f,h-2);
            float pg = (h < 1) ? std::max(0.f,h) : std::max(0.f,2-h);
            float pb = (h < 2) ? std::max(0.f,h-1) : std::max(0.f,3-h);

            // custom primaries?
            float r = .9f * pr + .1f * pb;
            float g = .17f * pr + .5f * pg + .33f * pb;
            float b = pb;

            // compute nominal luminosity factor
            float n = l / (.299*r + .587*g + .114*b);
            r *= n; g *= n; b *= n;

            // soft-clip to gamut
            n = std::max(r, std::max(g, b));
            float np = (1 + (n*n) * (.5f + (n*n) * (3/8.f)));
            n = np / (sqrt(1 + (n*n)*(np*np)));
            r *= n; g *= n; b *= n;

            // recompute luminance
            n = (.299*r + .587*g + .114*b);
            // compute blend-to-white factor
            n = (l-n)/(1-n);

            // lerp
            r += n * (1 - r);
            g += n * (1 - g);
            b += n * (1 - b);

            // lerp again for saturation
            s = (1 - s);
            r += s * (l - r);
            g += s * (l - g);
            b += s * (l - b);

            // gamma correct and return
            unsigned R = (uint8_t) (sqrt(r) * 255);
            unsigned G = (uint8_t) (sqrt(g) * 255);
            unsigned B = (uint8_t) (sqrt(b) * 255);

            return (0xff<<24)|(R<<16)|(G<<8)|B;
        }

    };  // namespace Color

};
