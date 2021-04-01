
#include <cstdint>      // for portable int64_t
#include <algorithm>    // for min/max
#include <vector>       // use for dynamic arrays

#include "dust/core/defs.h"

#include "rect.h"
#include "render_path.h"

using namespace dust;

// none of this stuff needs to be public
namespace {

template <typename T>
inline static void heapsort_siftDown(T * a, int start, int end)
{
    int root = start;

    T rv = a[root];

    while(root*2+1 < end) {
        int child = 2*root + 1;

        if((child + 1 < end) && (a[child] < a[child+1]))
        {
            child += 1;
        }
        if(rv < a[child])
        {
            a[root] = a[child];
            root = child;
        }
        else break;
    }

    a[root] = rv;
}

template <typename T>
inline static void heapsort(T * a, int count)
{
    int start, end;

    /* heapify */
    for(start = (count-2)/2; start >=0; start--)
    {
        heapsort_siftDown(a, start, count);
    }

    /* then extract all */
    for(end=count-1; end > 0; end--)
    {
        std::swap(a[0], a[end]);

        // this could be improved by shifting children
        // up until we reach the heap, putting the end
        // node at the bottom level and shifting up
        heapsort_siftDown(a, 0, end);
    }
}

// index + key pair used for sorting
struct SortIK
{
    int key;
    unsigned index;

    bool operator<(const SortIK & other) const
    {
        return key < other.key;
    }
};

// stores point coordinates as fixed point integers
struct XPoint
{
    // the precision in no longer coupled to quality
    // but spBits must be at least _SUBPIXBITS
    static const unsigned spBits = 8;
    static const unsigned spCount = (1 << spBits);

    int fpX, fpY;

    // default constructor
    XPoint() : fpX(0), fpY(0) {}

    // copy constructor
    XPoint(const XPoint & p) : fpX(p.fpX), fpY(p.fpY) {}
    const XPoint & operator=(const XPoint & p)
    { fpX = p.fpX; fpY = p.fpY; return *this; }

    // convenience overloads.. can't use constructor
    // otherwise we'd keep "reconverting" from integers
    XPoint operator+(const XPoint & p) const
    { XPoint r = *this; r.fpX += p.fpX; r.fpY += p.fpY; return r; }
    XPoint operator-(const XPoint & p) const
    { XPoint r = *this; r.fpX -= p.fpX; r.fpY -= p.fpY; return r; }

    bool operator==(const XPoint & p) const
    { return fpX == p.fpX && fpY == p.fpY; }

    // this is only 32-bit precision.. but.. whatever
    // it specifically rounds down, so preserves sign
    int dot(const XPoint & p)
    {
        return (int64_t(fpX) * int64_t(p.fpX)
            + int64_t(fpY) * int64_t(p.fpY)) >> spBits;
    }

    void set(int x, int y)
    {
        fpX = x << spBits;
        fpY = y << spBits;
    }

    void set(float x, float y)
    {
        fpX = (int)(x*(float)spCount);
        fpY = (int)(y*(float)spCount);
    }

    void setFP(int fpX0, int fpY0)
    {
        fpX = fpX0;
        fpY = fpY0;
    }

    // integer position constructor
    XPoint(int x, int y) { set(x, y); }

    // float position constructor
    XPoint(float x, float y) { set(x, y); }

};

struct Edge
{
    XPoint  a, b;
};

typedef std::vector<Edge>   EdgeList;

// builds an edgelist from flattened path
struct EdgeListBuilder
{
    EdgeList    edges;

    Rect    bb;

    XPoint  p0, pC;
    bool    isOpen;

    float   offset;

    // offset is used to compensate for sampling position
    EdgeListBuilder(float offset) : isOpen(false), offset(offset) { }

    void move(float x, float y)
    {
        close();

        p0 = pC = XPoint(x-offset,y-offset);
    }

    void line(float x, float y)
    {
        XPoint p1(x-offset, y-offset);
        if(p1 == p0) return;    // ignore identical points

        Edge e = { p0, p1 };
        edges.push_back(e);
        p0 = p1;

        bb.extendWithPoint(p0.fpX, p0.fpY);

        isOpen = true;
    }

    void close()
    {
        if(!isOpen) return;

        Edge e = { p0, pC };
        edges.push_back(e);
        p0 = pC;

        bb.extendWithPoint(p0.fpX, p0.fpY);
    }

    void end()
    {
        close();
    }

    void clipToBB(Rect & rect)
    {
        Rect bbPix = bb;
        bbPix.x0 >>= XPoint::spBits;
        bbPix.x1 >>= XPoint::spBits; bbPix.x1 += 1;
        bbPix.y0 >>= XPoint::spBits;
        bbPix.y1 >>= XPoint::spBits; bbPix.y1 += 1;

        rect.clip(bbPix);
    }

};


// sampleBits sets quality as 4^sampleBits samples
// note that this is power of four, eg. 4 -> 256 levels
// so the sensible range is from 0 to 4
//
// template class to compile specialized versions
template <unsigned sampleBits = 2, bool verticalScan = false>
struct PainterRenderTemplate
{
    //// constants, derived from sampleBits

    // extra precision bits
    static const unsigned extraBits = XPoint::spBits - sampleBits;
    // sample count
    static const unsigned sampleCount = (1<<sampleBits);
    // sample step
    static const unsigned sampleStep = (1<<extraBits);
    // max coverage = sampleCount ^2
    static const unsigned maxCoverage = sampleCount * sampleCount;
    // extra bits mask
    static const unsigned extraMask = sampleStep - 1;
    // sample bits mask
    static const unsigned sampleMask = ~extraMask;

    // extend sign bit to all bits
    static inline int imask(int x) { return x>>(sizeof(int)*8-1); }

    // branchless absolute value, needs imask(x)
    // can also be used to conditionally invert another value
    static inline int iabs(int x, int mask) { return (x^mask)-mask; }

    // branchless signum, needs imask(x)
    static inline int isign(int mask) { return 1+(mask<<1); }

    // active trace structure, optimized for cache size
    struct Trace
    {
        unsigned next;  // next edge index (in bucket or active list)

        int x, ymax;    // current x, max y
        int dx, dy;     // (x1-x0) and (y1-y0), signed
        int err;

        // for sorting to work
        bool operator<(const Trace & other) const
        {
            return x < other.x;
        }

        inline void step()
        {
            int dxm = imask(dx);
            int dxa = iabs(dx, dxm);
            int dya = iabs(dy, imask(dy));

            // step whole sample
            err += dxa << extraBits;

            if(err >= dya)
            {
                int step = err/dya;
                err -= dya * step;

                x += iabs(step, dxm);
            }
        }

        // initialize the edge and bump forward to clipTop if necessary
        inline void init(const XPoint & p0, const XPoint & p1, int clipTop)
        {
            int x0 = p0.fpX, y0 = p0.fpY;
            int x1 = p1.fpX, y1 = p1.fpY;

            dx = x1 - x0;
            dy = y1 - y0;

            // figure out which direction we are going
            // change the sign of dx if necessary
            int y;
            if(dy > 0) { x = x0; y = y0; ymax = y1; }
            else { x = x1; y = y1; ymax = y0; dx = -dx; }

            // perform a manual step forward
            int dxm = imask(dx);
            int dxa = iabs(dx, dxm);
            int dya = iabs(dy, imask(dy));

            // if y has any extra bits set, we want to step it forward
            int yAdjust = (int(sampleStep) - int(y & extraMask)) & extraMask;
            // alternatively, if we're clipping top then just do that
            if(clipTop > y) yAdjust = clipTop - y;

            y += yAdjust;

            // here we need 64-bit accumulator
            int64_t err64 = int64_t(dxa) * int64_t(yAdjust);
            if(err64 >= dya)
            {
                int64_t step = err64/dya;
                err64 -= dya * step;
                x += isign(dxm)*int(step);
            }
            err = int(err64);
        }

        int wdir() const
        {
            return isign(imask(dy));
        }
    };

    static unsigned firstScanForEdge(const Edge & e, int clipTop)
    {
        int y = (std::min)(e.a.fpY, e.b.fpY);
        // this is a bit overcomplicated, but keep it the same as
        // the init adjustment which needs the actual delta
        int yAdjust = (int(sampleStep) - int(y & extraMask)) & extraMask;
        if(clipTop > y) yAdjust = clipTop - y;
        return (y + yAdjust - clipTop) >> extraBits;
    }

    static bool render(EdgeList & edges, const Rect & clipRect, FillRule fill,
        uint8_t * maskOut, unsigned maskPitch)
    {
        //debugPrint("raster_ref: %d edges, r: %d,%d %dx%d\n",
        //    edges.size(), clipRect.x0, clipRect.y0, clipRect.w(), clipRect.h());

        // sanity check to avoid zero allocs
        if(!edges.size() || clipRect.isEmpty()) return false;

        int clipX0 = (verticalScan?clipRect.y0:clipRect.x0) << XPoint::spBits;
        int clipX1 = (verticalScan?clipRect.y1:clipRect.x1) << XPoint::spBits;
        int clipY0 = (verticalScan?clipRect.x0:clipRect.y0) << XPoint::spBits;
        int clipY1 = (verticalScan?clipRect.x1:clipRect.y1) << XPoint::spBits;

        int nScan = (clipY1 - clipY0) >> extraBits;

        // initialize startup edge list
        // we use this initially for startup edge counts
        // and then replace them with index to first trace
        std::vector<unsigned> startList(nScan);
        for(unsigned i = 0; i < nScan; ++i)
        {
            startList[i] = 0;
        }

        // go through edges and find bucket sizes
        // run backwards to throw away useless ones
        for(unsigned i = edges.size(); i--;)
        {
            Edge & e = edges[i];

            // transpose points
            if(verticalScan)
            {
                (std::swap)(e.a.fpX, e.a.fpY);
                (std::swap)(e.b.fpX, e.b.fpY);
            }

            if((e.a.fpY == e.b.fpY)
            ||((e.a.fpX >= clipX1) && (e.b.fpX >= clipX1))
            ||((e.a.fpY < clipY0) && (e.b.fpY < clipY0))
            ||((e.a.fpY+int(extraMask) >= clipY1)
            && (e.b.fpY+int(extraMask) >= clipY1)))
            {
                // replace with last one and pop_back
                e = edges.back();
                edges.pop_back();
                continue;
            }

            ++startList[firstScanForEdge(e, clipY0)];
        }

        if(!edges.size()) return false;

        // accumulate startup counts so we can then
        // subtract backwards to get start limits
        // also find the maximum bucket size
        int maxBucketSize = startList[0];
        for(unsigned i = 1; i < nScan; ++i)
        {
            if(maxBucketSize < startList[i]) maxBucketSize = startList[i];
            startList[i] += startList[i-1];
        }

        // allocate traces, then initialize from edges
        std::vector<Trace>    traces(edges.size());
        for(unsigned i = 0; i < edges.size(); ++i)
        {
            Edge & e = edges[i];

            int yMin = firstScanForEdge(e, clipY0);
            int t = --startList[yMin];
            traces[t].init(e.a, e.b, clipY0);
        }

        // sort all the bins, fix startup limits
        {
            // allocate a temporary std::vector for the purpose
            std::vector<SortIK> tsort(maxBucketSize);

            unsigned sortEnd = edges.size();
            for(unsigned i = nScan; i--;)
            {
                unsigned sortStart = startList[i];
                // while it's less than ideal to fill indexes like this
                // just for the purpose of sorting, doing lots of swaps
                // on the actual traces is not ideal either
                for(unsigned j = sortStart; j < sortEnd; ++j)
                {
                    tsort[j-sortStart].index = j;
                    tsort[j-sortStart].key = traces[j].x;
                }
                heapsort(tsort.data(), sortEnd - sortStart);

                unsigned next = ~0;  // end of list
                for(unsigned j = sortStart; j < sortEnd; ++j)
                {
                    traces[tsort[j-sortStart].index].next = next;
                    next = tsort[j-sortStart].index;
                }
                startList[i] = next;
                sortEnd = sortStart;
            }
        }

        int xLimit = (verticalScan ? clipRect.h() : clipRect.w());

        // coverage buffer for one scanline
        // extra pixels to skip some checks
        std::vector<short>    coverage(xLimit+2);
        for(int i = 0; i < 2+xLimit; ++i)
        {
            coverage[i] = 0;
        }

        //// MAINLOOP
        unsigned activeHead = ~0;

        int yStart = verticalScan ? clipRect.x0 : clipRect.y0;
        int yLimit = verticalScan ? clipRect.x1 : clipRect.y1;
        for(int y = yStart; y < yLimit; ++y)
        {
            // skip lines with no edges
            bool lineHasEdges = false;

            unsigned scanIndexPx = (y-yStart) << sampleBits;
            // sub-pixel scanlines
            for(int s = 0; s < sampleCount; ++s)
            {
                unsigned scanIndex = scanIndexPx + s;
                unsigned scanY = clipY0 + (scanIndex << extraBits);

                //// UPDATE EDGE TABLE

                // in order to sort this "almost sorted" single linked list
                // we want to pop from front and push to another list
                // then check that we have reverse order (push down if not)
                // and finally reverse the list back to the original list?
                unsigned nextPop = activeHead;
                unsigned revHead = ~0;
                while(~nextPop)
                {
                    // pop from the old list
                    unsigned t = nextPop;
                    nextPop = traces[t].next;

                    // drop dead edges before stepping and sorting
                    if(traces[t].ymax <= scanY) continue;

                    // step the edge
                    traces[t].step();

                    // scan the list from revhead to find where to
                    // place the newly popped entry
                    unsigned *nextPtr = &revHead;
                    while(~*nextPtr && traces[*nextPtr].x >= traces[t].x)
                    {
                        nextPtr = &traces[*nextPtr].next;
                    }
                    // then place it
                    traces[t].next = *nextPtr; *nextPtr = t;
                }
                // merge the list with new edges
                unsigned addHead = startList[scanIndex];
                activeHead = ~0;
                // merge the two
                while(~revHead)
                {
                    unsigned t = revHead; revHead = traces[t].next;
                    while(~addHead && traces[addHead].x >= traces[t].x)
                    {
                        unsigned a = addHead;
                        addHead = traces[addHead].next;

                        // check for "dead on arrival"
                        if(traces[a].ymax <= scanY) continue;

                        traces[a].next = activeHead;
                        activeHead = a;
                    }
                    traces[t].next = activeHead; activeHead = t;
                }
                // add any additional edges
                while(~addHead)
                {
                    unsigned a = addHead;
                    addHead = traces[addHead].next;

                    // check for "dead on arrival"
                    if(traces[a].ymax <= scanY) continue;

                    traces[a].next = activeHead;
                    activeHead = a;
                }

                // if no active edges, go to next scanline
                if(!~activeHead) continue;
                lineHasEdges = true;

                //// WINDING CALC + EDGE UPDATES
                unsigned *edgePtr = &activeHead;
                int winding = 0;
                bool inPoly = false;
                while(~*edgePtr)
                {
                    unsigned edge = *edgePtr;

                    // break out if past visible area
                    if(traces[edge].x >= clipX1) break;

                    winding += traces[edge].wdir();
                    bool inPolyAfter = 0 != (winding & fill);

                    if(inPoly != inPolyAfter)
                    {
                        inPoly = inPolyAfter;

                        // clip on the left edge
                        int x = traces[edge].x + extraMask;
                        if(x < clipX0) x = clipX0;

                        int xPix0 = ((x-clipX0) >> XPoint::spBits);
                        int xPix1 = xPix0+1;

                        int xOff1 = (x & (XPoint::spCount-1)) >> extraBits;
                        int xOff0 = sampleCount - xOff1;

                        if(inPoly)
                        {
                            coverage[xPix0] += xOff0;
                            coverage[xPix1] += xOff1;
                        }
                        else
                        {
                            coverage[xPix0] -= xOff0;
                            coverage[xPix1] -= xOff1;
                        }
                    }

                    edgePtr = &traces[edge].next;
                }
                // process any edges past visible area
                while(~*edgePtr)
                {
                    unsigned edge = *edgePtr;

                    // drop edges going further right
                    if(!imask(traces[edge].dx))
                    {
                        *edgePtr = traces[edge].next;
                        continue;
                    }

                    edgePtr = &traces[edge].next;
                }
            }

            // coverage sum and coverage to alpha
            int coverageSum = 0;
            uint8_t * alphaScan = verticalScan
                ? (maskOut + clipRect.y0 * int(maskPitch) + y)
                : (maskOut + clipRect.x0 + y * int(maskPitch));
            if(true)
            {
                for(int x = 0; x < xLimit; ++x)
                {
                    // sum and clear
                    coverageSum += coverage[x]; coverage[x] = 0;

                    alphaScan[x*(verticalScan?maskPitch:1)] = (unsigned char)
                        ((coverageSum * 255) / maxCoverage);
                }
            }
            else
            {
                for(int x = 0; x < xLimit; ++x)
                {
                    alphaScan[x*(verticalScan?maskPitch:1)] = 0;
                }
            }
        }

        return true;
    }
};

}; // anonymous namespace

// Wrapper to pick a template specialization for the desired
// quality, allowing actual code to be optimized much better.
template <bool verticalScan>
static bool renderPath_Q(EdgeListBuilder & builder,
    const Rect & clip, FillRule fill,
    uint8_t * maskOut, unsigned maskPitch, int quality)
{
    // convert runtime quality to compile time constant
    switch(quality)
    {
    case 0:
        return PainterRenderTemplate<0, verticalScan>::render(
            builder.edges, clip, fill, maskOut, maskPitch);
    case 1:
        return PainterRenderTemplate<1, verticalScan>::render(
            builder.edges, clip, fill, maskOut, maskPitch);
    case 2:
        return PainterRenderTemplate<2, verticalScan>::render(
            builder.edges, clip, fill, maskOut, maskPitch);
    case 3:
        return PainterRenderTemplate<3, verticalScan>::render(
            builder.edges, clip, fill, maskOut, maskPitch);
    default:    // use quality=4 for anything more
        return PainterRenderTemplate<4, verticalScan>::render(
            builder.edges, clip, fill, maskOut, maskPitch);
    }
}

// Wrapper to pick a template specialization for vScan
static bool renderPath_Q_vScan(EdgeListBuilder & builder,
    const Rect & clip, FillRule fill,
    uint8_t * maskOut, unsigned maskPitch, int quality, bool vScan)
{
    if(vScan)
    {
        //debugPrint("raster_ref: transposed mode\n");
        return renderPath_Q<true>(
            builder, clip, fill, maskOut, maskPitch, quality);
    }
    else
    {
        //debugPrint("raster_ref: normal mode\n");
        return renderPath_Q<false>(
            builder, clip, fill, maskOut, maskPitch, quality);
    }
}

// public wrapper for path filling
bool dust::renderPathRef(Path & path, Rect & clip, FillRule fill,
    uint8_t * maskOut, unsigned maskPitch, int quality, bool vScan)
{
    // don't even build edges if cliprect is empty
    if(clip.isEmpty()) return false;

    EdgeListBuilder builder(.5f / (1<<quality));
    flattenPath(path, builder);

    builder.clipToBB(clip);

    return renderPath_Q_vScan(builder, clip, fill,
        maskOut, maskPitch, quality, vScan);
}

// public wrapper for path stroking
bool dust::strokePathRef(Path & path, float width, Rect & clip,
    uint8_t * maskOut, unsigned maskPitch, int quality, bool vScan)
{
    // don't even build edges if cliprect is empty
    if(clip.isEmpty()) return false;

    EdgeListBuilder builder(.5f / (1<<quality));
    strokePath(path, builder, width);

    builder.clipToBB(clip);

    return renderPath_Q_vScan(builder, clip, FILL_NONZERO,
        maskOut, maskPitch, quality, vScan);
}
