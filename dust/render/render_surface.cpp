

#include <cmath>

#include "render_paint.h"

#if defined(DUST_ARCH_X86)
# include <x86intrin.h>
#endif

using namespace dust;

// NOTES ON BLUR:
//
// for the reflection, assume that image is zero past the
// boundary, then the impulse response past the border is
// state * (a^n) and the convo weight is (1-a)*a^n
// multiply together and do a sum n = 0..inf we get
//   b / (1 + a) so reflecting state we multiply by
//   1 / (1 + a) !
//
// for two poles ..
//  if we use 1 / ( 1 - a*z^-1)^m
//
// we get bin(n+m-1, m-1) * a^n * u[n]
// where the binomial expands as:
//  (n+m-1)! / ((m-1)!*n!)
// where if m = 2 this simplifies as
//  (n+1)! / n! = (n+1)
//
// so (n+1) * a^n * u[n] and with unity gain
// normalizsation (1-a)^2 * (n+1) * a^n * u[n]
//
// Now, the responses from the pole states are
//  r1 = s1*(1-a) * (n + 1) * a^n
//  r2 = s2 * a^n
//
// the accumulation into first pole then is
//   (1-a)*a^n * (r1 + r2)
//
// the accumulation into the second pole is
//   (1-a)^2*(n+1)*a^n * (r1 + r2)
//
// so:
//  b2: 1->1 : 1 / (1 + a)^2
//  b3: 1->2 : (1 + a^2) / (1 + a)^3
//  b1: 2->1 : 1 / (1 + a)
//  b2: 2->2 : 1 / (1 + a)^2
//
// then we just need compensation for the single pixel:
//
//  b1 = a/(1+a), b2 = b1*b1, b3 = (1+a*a)*b1*b2
//

static inline void blurLine(
    unsigned *buf, unsigned count,
    const __m128 & a, const __m128 & b1,
    const __m128 & b2, const __m128 & b3)
{
    // clear states
    __m128 s1 = _mm_setzero_ps();
    __m128 s2 = _mm_setzero_ps();
    __m128 zz = _mm_setzero_ps();
    for(unsigned x = 0; x < count; ++x)
    {
        // load
        //__m128 v = _mm_cvtsi32_si128(buf[x]);
        __m128 v = _mm_load_ss((float*)&buf[x]);
        v = _mm_unpacklo_epi8(v, zz);
        v = _mm_unpacklo_epi16(v, zz);
        v = _mm_cvtepi32_ps(v);

        // state = v + coeff * (state - v);
        s1 = _mm_add_ps(v,
            _mm_mul_ps(a, _mm_sub_ps(s1, v)));

        s2 = _mm_add_ps(s1,
            _mm_mul_ps(a, _mm_sub_ps(s2, s1)));

        // store
        v = _mm_cvtps_epi32(s2);
        v = _mm_packs_epi32(v,v);
        v = _mm_packus_epi16(v,v);
        //buf[x] = _mm_cvtsi128_si32(v);
        _mm_store_ss((float*)&buf[x], v);
    }

    // boundary correction
    __m128 tmp = s1;
    s1 = _mm_add_ps( _mm_mul_ps(b2, tmp), _mm_mul_ps(b1, s2) );
    s2 = _mm_add_ps( _mm_mul_ps(b3, tmp), _mm_mul_ps(b2, s2) );

    for(unsigned x = count; x--;)
    {
        // load
        //__m128 v = _mm_cvtsi32_si128(buf[x]);
        __m128 v = _mm_load_ss((float*)&buf[x]);
        v = _mm_unpacklo_epi8(v, zz);
        v = _mm_unpacklo_epi16(v, zz);
        v = _mm_cvtepi32_ps(v);

        // state = v + coeff * (state - v);
        s1 = _mm_add_ps(v,
            _mm_mul_ps(a, _mm_sub_ps(s1, v)));

        s2 = _mm_add_ps(s1,
            _mm_mul_ps(a, _mm_sub_ps(s2, s1)));

        // store
        v = _mm_cvtps_epi32(s2);
        v = _mm_packs_epi32(v,v);
        v = _mm_packus_epi16(v,v);
        //buf[x] = _mm_cvtsi128_si32(v);
        _mm_store_ss((float*)&buf[x], v);
    }
}

// s = src, sp = src pitch, d = dst, dp = dst pitch
static inline void imageTranspose(
    unsigned *s, unsigned sp, unsigned *d, unsigned dp)
{
    // these could be parameters if necessary later
    unsigned & w = sp;
    unsigned & h = dp;

    // assume 64-byte cache lines; this means we should
    // copy 16 lines in parallel to avoid trashing the
    // write-back caches..

    // mask out the low 4 bits to get the number of
    // full 16-line blocks to run
    unsigned h16 = h & ~0xf;

    unsigned y = 0;
    while(y < h16)
    {
        unsigned y16 = y + 16;
        for(unsigned x = 0; x < w; ++x)
        {
            for(unsigned yy = y; yy < y16; ++yy)
            {
                d[yy+dp*x] = s[x+sp*yy];
            }
        }
        y = y16;
    }

    // last pass
    for(unsigned x = 0; x < w; ++x)
    {
        for(unsigned yy = y; yy < h; ++yy)
        {
            d[yy+dp*x] = s[x+sp*yy];
        }
    }
}

void Surface::blur(Surface & src, float r)
{
    unsigned w = src.szX, h = src.szY;

    // need a temporary surface to hold transposed data
    Surface tmp(h, w);

#ifdef DUST_ARCH_X86
    // we absolutely can't have denormals!
    // get old control word, set desired bits
    unsigned int sse_control_store = _mm_getcsr();

    // bits: bits: 15 = flush to zero
    //  | 14:13 = round to zero | 6 = denormals are zero
    _mm_setcsr(sse_control_store | 0xE040);
#endif

    float a = exp(-2.f/r);
    float b1 = a / ( 1 + a);
    float b2 = b1 * b1;
    float b3 = (1+a*a) * b1 * b2;

    __m128 va = _mm_set1_ps(a);
    __m128 vb1 = _mm_set1_ps(b1);
    __m128 vb2 = _mm_set1_ps(b2);
    __m128 vb3 = _mm_set1_ps(b3);

    // first transpose from src to temp
    imageTranspose(src.pixels, w, tmp.pixels, h);
    // vertical blur pass
    for(unsigned x = 0; x < w; ++x)
    {
        blurLine(tmp.pixels+x*h, h, va, vb1, vb2, vb3);
    }

    // resize the current surface
    validate(w, h);

    // transpose from temp to this surface
    imageTranspose(tmp.pixels, h, pixels, w);
    // horizontal blur pass
    for(unsigned y = 0; y < h; ++y)
    {
        blurLine(pixels+y*w, w, va, vb1, vb2, vb3);
    }

#ifdef DUST_ARCH_X86
    _mm_setcsr(sse_control_store);
#endif
}


// FIXME: add special case handling for borders?
void Surface::emboss(float h)
{
    unsigned xmax = szX - 2;
    unsigned ymax = szY - 2;

    float scale = h / 255.f;    // height scaling

    for(unsigned y = 0; y < ymax; ++y)
    {
        for(unsigned x = 0; x < xmax; ++x)
        {
            /////// aYX
            // try to avoid register spills by fetching corners
            // then adding rest of the points into them

            int32_t a00 = pixels[(x+0)+szX*(y+0)] >> 24;
            int32_t a02 = pixels[(x+2)+szX*(y+0)] >> 24;
            int32_t a20 = pixels[(x+0)+szX*(y+2)] >> 24;
            int32_t a22 = pixels[(x+2)+szX*(y+2)] >> 24;

            int32_t a01 = pixels[(x+1)+szX*(y+0)] >> 24;
            a00 += a01; a02 += a01;

            int32_t a10 = pixels[(x+0)+szX*(y+1)] >> 24;
            a00 += a10; a20 += a10;

            int32_t a12 = pixels[(x+2)+szX*(y+1)] >> 24;
            a02 += a12; a22 += a12;

            int32_t a21 = pixels[(x+1)+szX*(y+2)] >> 24;
            a20 += a21; a22 += a21;

            int32_t a11 = pixels[(x+1)+szX*(y+1)] >> 24;
            a00 += a11; a02 += a11; a20 += a11; a22 += a11;

            // now each of the corners has sum of 4 points
            // so the kernel is
            //  [ -1 -2 -1 ]
            //  [  0  0  0 ]
            //  [ +1,+2,+1 ]

            float dx = scale * (a00 + a20 - a02 - a22);
            float dy = scale * (a20 + a22 - a00 - a02);

            // normal from the partial derivatives is
            // [ -dx, -dy, 1 ]
            //
            // normalize it:

            float z = 1.f / sqrt(dx*dx + dy*dy + 1);

            // calculate diffuse with .5 + .5*dot, using
            // simple lighting vector [0, 1, 0]
            float D = .5 + .5 * (z*dy);

            // convert to grayscale color value
            int32_t rgb = 0x10101 * (int) (255.f * D);

            // store into the pixel
            pixels[(x+1)+szX*(y+1)] = (a11<<24) | rgb;

        }
    }
}

void Surface::fadeEdges(float radius)
{
    int ymax = std::min(unsigned(ceilf(radius)), szY);
    for(int y = 0; y < ymax; ++y)
    {
        // compute smooth step
        float t = (y + .5f) / radius;
        int fade = int(0xff * ( t*t*(3-2*t) ));

        for(int x = 0; x < szX; ++x)
        {
            pixels[x+pitch*y] =
                color::blend(pixels[x+pitch*y], fade);
            pixels[x+pitch*(szY-y-1)] =
                color::blend(pixels[x+pitch*(szY-y-1)], fade);
        }
    }
    
    int xmax = std::min(unsigned(ceilf(radius)), szX);

    for(int x = 0; x < xmax; ++x)
    {
        // compute smooth step
        float t = (x + .5f) / radius;
        int fade = int(0xff * ( t*t*(3-2*t) ));

        for(int y = 0; y < szY; ++y)
        {
            pixels[x+pitch*y] =
                color::blend(pixels[x+pitch*y], fade);
            pixels[(szX-x-1)+pitch*y] =
                color::blend(pixels[(szX-x-1)+pitch*y], fade);
        }
    }
}

#define STB_IMAGE_IMPLEMENTATION
#include "dust/libs/stb_image.h"

Surface::Surface(const std::vector<char> & fileContents)
{
    int w,h,n;
    // let STBI convert to 4-channels (RGBA)
    auto image = stbi_load_from_memory(
        (const unsigned char*) fileContents.data(),
        fileContents.size(), &w, &h, &n, 4);

    if(!image) return;  // failure

    this->pitch = w;
    this->szX = w;
    this->szY = h;

    this->pixels = new unsigned[w*h];
    for(int i = 0; i < w*h; ++i)
    {
        this->pixels[i] = (image[i*4] << 16) | (image[i*4+1] << 8)
            | (image[i*4+2]) | (image[i*4+3] << 24);
    }

    stbi_image_free(image);
}

