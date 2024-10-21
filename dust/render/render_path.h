
#pragma once

#include "dust/core/defs.h"
#include "rect.h"

#include <vector>
#include <cmath>    // we don't need this yet, but we will!


//// FIXME:
//
// Want to make some changes to the interfaces here:
//
//  - give the transform methods to the Path directly and make them
//    return a Path::Transform object that can be further transformed
//    this should look like the current TransformPath
//
namespace dust
{
    // This implements curve subdivisions for paths.
    //
    // The methods act like path commands, translating them into
    // multiple line-commands, so initial point is not output.
    //
    // There's normally no reason to use this directly
    //
    namespace subdivide
    {
        // this is the maximum distance error allowed for subdivision
        static const float tolerance = .125f;
        // the subvision logic actually needs it squared
        static const float tolerance2 = tolerance*tolerance;

        // quadratic bezier subdivision: split at middle until
        // the mid-point of the line is close to the control point
        template <typename LinePath>
        static void quad(LinePath & to,
            float x0, float y0,
            float x1, float y1,
            float x2, float y2)
        {
            // line evaluation
            float x02 = .5f * (x0 + x2), y02 = .5f * (y0 + y2);

            // calculate relative distance
            float xd = x02 - x1, yd = y02 - y1;

            // can we approximate as a line?
            if(xd*xd + yd*yd <= tolerance2)
            {
                to.line(x2, y2);
            }
            else
            {
                // curve evaluation
                float x01 = .5f * (x0 + x1), y01 = .5f * (y0 + y1);
                float x12 = .5f * (x1 + x2), y12 = .5f * (y1 + y2);
                float x012 = .5f * (x01 + x12), y012 = .5f * (y01 + y12);

                // recursively sub-divide
                quad(to, x0, y0, x01, y01, x012, y012);
                quad(to, x012, y012, x12, y12, x2, y2);
            }
        }

        // cubic bezier subdivision: split at middle until distances
        // from p1 to (p0+p2)/2 and p2 to (p1+p3)/2 are below tolerance
        // this is the same as the subdivide2 condition, checked on the
        // underlying quadratics that blossom into the actual cubic
        template <typename LinePath>
        static void cubic(LinePath & to,
            float x0, float y0,
            float x1, float y1,
            float x2, float y2,
            float x3, float y3)
        {
            // line evaluations
            float x02 = .5f * (x0 + x2), y02 = .5f * (y0 + y2);
            float x13 = .5f * (x1 + x3), y13 = .5f * (y1 + y3);

            // relative distances
            float xd1 = x02 - x1, yd1 = y02 - y1;
            float xd2 = x13 - x2, yd2 = y13 - y2;

            // can we approximate as a line?
            if(xd1*xd1 + yd1*yd1 <= tolerance2
            && xd2*xd2 + yd2*yd2 <= tolerance2)
            {
                to.line(x3, y3);
            }
            else
            {
                // split the curve at the mid-point
                float x01 = .5f * (x0 + x1), y01 = .5f * (y0 + y1);
                float x12 = .5f * (x1 + x2), y12 = .5f * (y1 + y2);
                float x23 = .5f * (x2 + x3), y23 = .5f * (y2 + y3);

                float x012 = .5f * (x01 + x12), y012 = .5f * (y01 + y12);
                float x123 = .5f * (x12 + x23), y123 = .5f * (y12 + y23);

                float x0123 = .5f * (x012 + x123), y0123 = .5f * (y012 + y123);

                cubic(to, x0, y0, x01, y01, x012, y012, x0123, y0123);
                cubic(to, x0123, y0123, x123, y123, x23, y23, x3, y3);
            }
        }
    };

    // simplify a path into line segments only
    template <typename LinePath>
    struct FlattenPath
    {
        void close() {
            x0 = xC; y0 = yC;
            lineOut.close();
        }
        void move(float x, float y)
        {
            xC = x0 = x; yC = y0 = y;
            lineOut.move(x0,y0);
        }
        void line(float x, float y)
        {
            x0 = x; y0 = y;
            lineOut.line(x0,y0);
        }

        void quad(
            float x1, float y1,
            float x2, float y2)
        {
            subdivide::quad(*this, x0, y0, x1, y1, x2, y2);
            x0 = x2; y0 = y2;
        }

        void cubic(
            float x1, float y1,
            float x2, float y2,
            float x3, float y3)
        {
            subdivide::cubic(*this, x0, y0, x1, y1, x2, y2, x3, y3);
            x0 = x3; y0 = y3;
        }

        void end() { lineOut.end(); }


        FlattenPath(LinePath & lineOut)
        : lineOut(lineOut)
        , x0(0), y0(0), xC(0), yC(0)
        {}

    private:
        LinePath    &lineOut;
        float       x0, y0;     // current pen position
        float       xC, yC;     // last "move" command, for close()
    };


    // Builds a stroke by convolving the path as a whole.
    template <typename LinePath>
    struct StrokePath
    {
        LinePath & to;

        void end() { finishOpen(); }
        void close() { finishClosed(); }

        void move(float x, float y)
        {
            // finish previously open stroke if any
            finishOpen();

            x0 = x;
            y0 = y;
        }

        void line(float x, float y)
        {
            // is this the first point?
            if(!stack.size())
            {
                startStroke(x, y);
            }
            else
            {
                connectStroke(x, y);
            }
        }

        void quad(
            float x1, float y1,
            float x2, float y2)
        {
            // subdivide with line output back to this path
            subdivide::quad(*this, x0, y0, x1, y1, x2, y2);
            x0 = x2; y0 = y2;
        }

        void cubic(
            float x1, float y1,
            float x2, float y2,
            float x3, float y3)
        {
            // subdivide with line output back to this path
            subdivide::cubic(*this, x0, y0, x1, y1, x2, y2, x3, y3);
            x0 = x3; y0 = y3;
        }

        // allow public construction, since one can then stroke
        // directly into a path without building an intermediate path
        StrokePath(LinePath & to, float width) : to(to)
        {
            float pi = acos(-1.f);

            // this is from a cairo-paper, except force multiple of 4
            // this way the brush is always 4-ways symmetric
            //
            // using square tolerance here is a good idea?
            float tol = subdivide::tolerance2;
            int nBrush = 4 * (int) ceil(.25f * pi/acos(1.f - 2*tol/width));
            if(nBrush < 4) nBrush = 4;

            // calculate the fraction of the circle covered by the polygon
            float polyFrac = (nBrush*sin(2*pi/nBrush))/(2*pi);

            // then calculate radius that gives the circle area
            float radius = .5f * width / sqrt(polyFrac);

            //printf("stroke w: %.2f, n: %d, r=%f (x%f)\n",
            //    width, nBrush, radius, polyFrac);

            // starting like this, we get a diamond for 4 points
            // this should look more consistent than straight square
            // for 6 points it blurs a bit sideways which is probably
            // the better thing to do?
            float x = radius;
            float y = 0;

            // double the angle for rotation
            float c = cos((float)(2*pi) / (nBrush));
            float s = sin((float)(2*pi) / (nBrush));

            brush.resize(nBrush);
            for(unsigned i = 0; i < nBrush; ++i)
            {
                brush[i].x = x;
                brush[i].y = y;
                float newX = c*x + s*y;
                float newY = c*y - s*x;
                x = newX;
                y = newY;
            }
        }

    private:

        struct Point
        {
            float x,y;

            // need to allow default construction
            Point(float x = 0, float y = 0) : x(x), y(y) {}
        };

        std::vector<Point> brush;

        // we convolve by always going the same direction around the
        // brush, so this tracks the current index
        unsigned bIndex, bIndexFirst;

        // keep a stack of points so that we can trace the other side
        // of the stroke (backwards) once the stroke is closed
        std::vector<Point> stack;

        float x0, y0;     // previous input point
        float prevDX, prevDY;   // previous line delta (for left turn checks)
        float firstDX, firstDY; // first delta for finishing correctly

        // this is called by the first lineTo
        void startStroke(float x, float y)
        {
            float dx = x - x0;
            float dy = y - y0;

            if(dx*dx + dy*dy < 1e-8f)
            {
                // special case the situation where the first line
                // is zero length: we must draw something here, so
                // just draw a point on the spot
                if(!stack.size())
                {
                    to.move( x0 + brush[0].x, y0 + brush[0].y);
                    // must loop backwards like everywhere else
                    for(unsigned i = brush.size(); i;)
                    {
                        --i;
                        to.line( x0 + brush[i].x, y0 + brush[i].y);
                    }
                    to.close();
                }

                return;
            };

            // dot the normals of the brushpoints against
            // the line and find the one that dots the most
            // this is the initial brush direction we want to use
            //
            // FIXME: this relies on the brush vectors all being
            // the same length, so only works for pseudo-circles
            float maxCross = 0;
            for(unsigned i = 0; i < brush.size(); ++i)
            {
                float cross = brush[i].x*dy - brush[i].y*dx;

                if(cross < maxCross) continue;

                maxCross = cross;
                bIndex = i;
            }

            // draw the initial outline segment
            to.move(
                x0 + brush[bIndex].x,
                y0 + brush[bIndex].y);

            to.line(
                x + brush[bIndex].x,
                y + brush[bIndex].y);

            // push the point
            stack.push_back(Point(x0,y0));
            x0 = x;
            y0 = y;

            prevDX = dx;
            prevDY = dy;

            firstDX = dx;
            firstDY = dy;

            bIndexFirst = bIndex;
        }

        // this is called by subsequent lineTo and end commands
        // force-arc is set to true to ignore line direction for end cap
        void connectStroke(float x, float y)
        {
            float dx = x - x0;
            float dy = y - y0;

            if(dx*dx + dy*dy < 1e-8f) return;

            // check if this is a left-turn
            // use the actual last delta for more consistent results
            //
            // FIXME: on M1 we need a small epsilon so we're not sensitive
            // to floating point rounding at stroke ends.. should really figure
            // out a more consistent strategy to use here
            if(prevDY*dx > prevDX*dy)
            {
                // loop brush vertices to find the max cross
                float maxCross = brush[bIndex].x*dy - brush[bIndex].y*dx;
                while(true)
                {
                    unsigned next = bIndex + 1;
                    if(next == brush.size()) next = 0;

                    float cross = brush[next].x*dy - brush[next].y*dx;
                    if(cross < maxCross) break;
                    maxCross = cross;
                    bIndex = next;
                }

                // shortcut the brush outline
                to.line(
                    x0 + brush[bIndex].x,
                    y0 + brush[bIndex].y);
            }
            else
            {
                // loop brush vertices to find the max cross product
                float maxCross = brush[bIndex].x*dy - brush[bIndex].y*dx;
                while(true)
                {
                    unsigned next = bIndex;
                    // loop backwards
                    if(!next) next = brush.size();
                    --next;

                    float cross = brush[next].x*dy - brush[next].y*dx;
                    if(cross < maxCross) break;
                    maxCross = cross;
                    bIndex = next;

                    // plot the brush outline
                    to.line(
                        x0 + brush[bIndex].x,
                        y0 + brush[bIndex].y);
                }
            }

            // draw the actual stroke outline
            to.line(x + brush[bIndex].x, y + brush[bIndex].y);

            // push and update the previous point
            stack.push_back(Point(x0,y0));
            x0 = x;
            y0 = y;

            prevDX = dx;
            prevDY = dy;

        }

        // draws an end-cap for open stroke
        void endCap()
        {
            while(bIndex != bIndexFirst)
            {
                if(!bIndex) bIndex = brush.size();
                --bIndex;

                // plot the brush outline
                to.line(
                    x0 + brush[bIndex].x,
                    y0 + brush[bIndex].y);
            }
            to.close();
        }

        // finish the loop for closed strokes
        void finishLoop()
        {
            // is the last turn a left-turn
            // here we can use the indexes
            if(prevDY*firstDX - prevDX*firstDY > 0)
            {
                // shortcut the inner corner
                to.line(
                    x0 + brush[bIndexFirst].x,
                    y0 + brush[bIndexFirst].y);
            }
            else
            {
                // use the end-cap logic
                while(bIndex != bIndexFirst)
                {
                    if(!bIndex) bIndex = brush.size();
                    --bIndex;

                    // plot the brush outline
                    to.line(
                        x0 + brush[bIndex].x,
                        y0 + brush[bIndex].y);
                }
            }
            to.close();
        }

        void finishOpen()
        {
            if(!stack.size()) return;

            while(stack.size())
            {
                Point & p = stack.back();
                connectStroke(p.x, p.y);
                stack.pop_back();
            }
            endCap();
        }

        void finishClosed()
        {
            // empty path
            if(!stack.size()) return;
            
            // close the stroke outline
            {
                Point & p = stack[0];
                connectStroke(p.x, p.y);
                finishLoop();
            }

            // draw the last point as first point of other side
            {
                Point & p = stack.back();
                startStroke(p.x, p.y);
                stack.pop_back();
            }

            // draw rest of the other side
            while(stack.size())
            {
                Point & p = stack.back();
                connectStroke(p.x, p.y);
                stack.pop_back();
            }

            // finish the other side with a cap
            finishLoop();
        }
    };

    // Path is a command list for defining a vector path.
    // It works the same as most path definition languages.
    //
    struct Path
    {
        // allow clearing for reuse
        // avoids some alloc when drawing multiple paths
        // that are procedurally generated on the fly
        void clear() { clist.clear(); }

        // close the current path
        Path & close()
        {
            pushC(cClose);
            return *this;
        }

        // move the pen to (x,y)
        Path & move(float x, float y)
        {
            pushC(cMove);
            pushXY(x, y);
            return *this;
        }

        // draw a line to (x,y)
        Path & line(float x, float y)
        {
            pushC(cLine);
            pushXY(x, y);
            return *this;
        }
        // draw a quadratic bezier to x2,y2 with control point x1,y1
        Path & quad(
            float x1, float y1,
            float x2, float y2)
        {
            pushC(cQuad);
            pushXY(x1, y1);
            pushXY(x2, y2);
            return *this;
        }

        // draw a cubic bezier to x3,y3 with control point x1,y1 and x2,y2
        Path & cubic(
            float x1, float y1,
            float x2, float y2,
            float x3, float y3)
        {
            pushC(cCubic);
            pushXY(x1, y1);
            pushXY(x2, y2);
            pushXY(x3, y3);
            return *this;
        }

        Path & end() { pushC(cEnd); return *this; }

        // plot() is a short-cut to either move() or line()
        // depending on whether there is already an open path
        Path & plot(float x, float y)
        {
            if(!clist.size()
            || clist.back().c == cClose
            || clist.back().c == cEnd)
            {
                return move(x,y);
            }
            else
            {
                return line(x,y);
            }
            return *this;
        }

        // draws a circular arc around a given mid-point (cx,cy)
        // with radius r and angles a0, a1 (radians, clockwise, 0 = up)
        Path & arc(float cx, float cy, float r, float a0, float a1, bool start = false)
        {
            // this is essentially a verbatim copy of what we have
            // in the stroke brush generation, so see comments there
            float pi = acos(-1.f);
            float tol = subdivide::tolerance2;
            int nDiv = 4 * (int) ceil(.25f * pi/acos(1.f - 2*tol/r));
            if(nDiv < 4) nDiv = 4;
            float polyFrac = (nDiv*sin(2*pi/nDiv))/(2*pi);
            float radius = r / sqrt(polyFrac);

            float tick = (2*pi)/nDiv;
            float c = cos(tick);
            float s = sin(tick);

            // adjust angles for relative to top
            a0 -= .5f*pi; a1 -= .5f*pi;

            // go to starting angle point
            if(start) move(cx+cos(a0)*radius, cy+sin(a0)*radius);
            else line(cx+cos(a0)*radius, cy+sin(a0)*radius);

            if(a0 < a1)
            {
                int i0 = (int) ceil(a0/tick);
                int i1 = (int) ceil(a1/tick);

                float x = radius * cos(i0*tick);
                float y = radius * sin(i0*tick);
            
                // rotate positive angle
                for(int i = i0; i < i1; ++i)
                {
                    line(cx+x, cy+y);
                    float newX = c*x - s*y;
                    float newY = c*y + s*x;
                    x = newX;
                    y = newY;
                }
            }
            else
            {
                int i0 = (int) floor(a0/tick);
                int i1 = (int) floor(a1/tick);

                float x = radius * cos(i0*tick);
                float y = radius * sin(i0*tick);
            
                // rotate negative angle
                for(int i = i1; i < i0; ++i)
                {
                    line(cx+x, cy+y);
                    float newX = c*x + s*y;
                    float newY = c*y - s*x;
                    x = newX;
                    y = newY;
                }
            }

            // finish at ending angle
            line(cx+cos(a1)*radius, cy+sin(a1)*radius);

            return *this;
        }

        // add a clockwise rectangle with optional corner rounding
        Path & rect(float x0, float y0, float x1, float y1, float rounding = 0)
        {
            if(rounding > 0)
            {
                #if 0
                move(x0, y0 + rounding); quad(x0, y0, x0 + rounding, y0);
                line(x1 - rounding, y0); quad(x1, y0, x1, y0 + rounding);
                line(x1, y1 - rounding); quad(x1, y1, x1 - rounding, y1);
                line(x0 + rounding, y1); quad(x0, y1, x0, y1 - rounding);
                #else
                // min-max optimal "90 degree arc as cubic" approximation
                // this is about 28% better than the "standard approximation"
                float r = (1-0.55191502449)*rounding;
                move(x0, y0 + rounding); cubic(x0, y0+r, x0+r, y0, x0 + rounding, y0);
                line(x1 - rounding, y0); cubic(x1-r, y0, x1, y0+r, x1, y0 + rounding);
                line(x1, y1 - rounding); cubic(x1, y1-r, x1-r, y1, x1 - rounding, y1);
                line(x0 + rounding, y1); cubic(x0+r, y1, x0, y1-r, x0, y1 - rounding);
                #endif
                close();
            }
            else
            {
                move(x0, y0);
                line(x1, y0);
                line(x1, y1);
                line(x0, y1);
                close();
            }
            return *this;
        }

        // add a stroke of the source path
        template<typename PathVisitor>
        void stroke(PathVisitor && to, float width)
        {
            StrokePath<PathVisitor> stroke(to, width);
            process(stroke);
        }

        // reproduce the original path commands to "out"
        template<typename PathVisitor>
        void process(PathVisitor && out) const
        {
            // iterate the list, commands will bump i by parameter count
            for(int i = 0; i < clist.size(); ++i)
            {
                switch(clist[i].c)
                {
                    case cMove:
                        out.move(clist[i+1].v, clist[i+2].v);
                        i += 2;
                        continue;

                    case cLine:
                        out.line(clist[i+1].v, clist[i+2].v);
                        i += 2;
                        continue;

                    case cQuad:
                        out.quad(
                            clist[i+1].v, clist[i+2].v,
                            clist[i+3].v, clist[i+4].v);
                        i += 4;
                        continue;

                    case cCubic:
                        out.cubic(
                            clist[i+1].v, clist[i+2].v,
                            clist[i+3].v, clist[i+4].v,
                            clist[i+5].v, clist[i+6].v);
                        i += 6;
                        continue;

                    case cClose:
                        out.close();
                        continue;

                    case cEnd:
                        out.end();
                        continue;

                    default:
                        debugPrint("dust::Path - internal error\n");
                        return;
                }
            }

            out.end();
        }

    private:

        enum CmdType
        {
            cMove,
            cLine,
            cQuad,
            cCubic,

            cClose,
            cEnd
        };

        // we mix commands and coordinates in a flat list
        // this is fine, since we only allow iterator access
        union CmdListType
        {
            CmdType c;  // command
            float   v;  // coordinate value

            CmdListType(const CmdType & c) : c(c) {}
            CmdListType(const float & v) : v(v) {}
        };

        typedef std::vector<CmdListType>  CmdList;
        CmdList clist;

        void pushC(CmdType ct) { clist.push_back(ct); }
        void pushV(float v) { clist.push_back(v); }
        void pushXY(float x, float y) { pushV(x); pushV(y); }
    };

    template <typename PathOut>
    struct TransformPath
    {
        TransformPath & close() { to.close(); return *this; }

        TransformPath & plot(float x, float y)
        {
            transform(x, y);
            to.plot(x,y);
            return *this;
        }
        
        TransformPath & move(float x, float y)
        {
            transform(x, y);
            to.move(x,y);
            return *this;
        }
        
        TransformPath & line(float x, float y)
        {
            transform(x, y);
            to.line(x,y);
            return *this;
        }

        TransformPath & quad(
            float x1, float y1,
            float x2, float y2)
        {
            transform(x1, y1);
            transform(x2, y2);
            to.quad(x1, y1, x2, y2);
            return *this;
        }

        TransformPath & cubic(
            float x1, float y1,
            float x2, float y2,
            float x3, float y3)
        {
            transform(x1, y1);
            transform(x2, y2);
            transform(x3, y3);

            to.cubic(x1, y1, x2, y2, x3, y3);
            return *this;
        }

        TransformPath & end() { to.end(); return *this; }

        // simple scale then offset
        TransformPath(PathOut & to,
            float scale, float offX = 0, float offY = 0)
            : to(to)
        {
            ax = scale;
            ay = 0;
            az = offX;

            bx = 0;
            by = scale;
            bz = offY;
        }

        // general case
        TransformPath(PathOut & to,
            float ax, float ay, float az,
            float bx, float by, float bz) : to(to)
            , ax(ax), ay(ay), az(az)
            , bx(bx), by(by), bz(bz)
        {
        }

    private:
        PathOut & to;

        // 3x3 affine matrix, with last row fixed to [0,0,1]
        float ax, ay, az;
        float bx, by, bz;

        void transform(float & x, float & y)
        {
            float xx = ax*x + ay*y + az;
            float yy = bx*x + by*y + bz;
            x = xx;
            y = yy;
        }
    };

    // helper to resolve the template
    template <typename LinePath>
    void flattenPath(const Path & p, LinePath & out)
    {
        FlattenPath<LinePath> flat(out);
        p.process(flat);
    }

    // helper to resolve the template
    template <typename LinePath>
    void strokePath(const Path & p, LinePath & to, float width)
    {
        StrokePath<LinePath> stroke(to, width);
        p.process(stroke);
    }

    // Fill-rules for rasterization
    //
    // NOTE: the bit-values are important, since these are
    // checked with a bit-wise AND against the winding number
    enum FillRule
    {
        FILL_EVENODD = 1,
        FILL_NONZERO = ~0
    };

    // reference rasterizer, see raster_ref.cpp
    // one should normally just let RenderContext call this
    //
    // NOTE: shrinks the clipping rectangle to area containing paths
    //
    // maskOut should point to (0,0) but isn't touched outside
    // the clipping rectangle, so offset pointer is fine
    //
    // vScan uses vertical scanlines (faster for horiz plots)
    //
    // returns true if something was drawn, false if not (eg. fully clipped)
    //
    bool renderPathRef(Path &p, Rect & clip, FillRule fill,
        uint8_t * maskOut, unsigned maskPitch, int quality, bool vScan);

    // this will draw strokes without explicitly storing the stroke
    bool strokePathRef(Path &p, float width, Rect & clip,
        uint8_t * maskOut, unsigned maskPitch, int quality, bool vScan);


}; // namespace
