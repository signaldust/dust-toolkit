
#pragma once

#include <algorithm>

namespace dust
{
    // The point (x0,y0) is the top-left corner inside rectangle.
    // The point (x1,y1) is the bottom-right corner and just outside.
    //
    struct Rect
    {
        int x0, y0, x1, y1;

        int w() const
        {
            return (std::max)(0, x1 - x0);
        }

        int h() const
        {
            return (std::max)(0, y1 - y0);
        }

        bool isEmpty() const
        {
            return (x1 <= x0) || (y1 <= y0);
        }

        Rect() { clear(); }

        Rect(const Rect & r)
        {
            set(r);
        }

        Rect(int x, int y, int w, int h)
        {
            x0 = x; y0 = y;
            x1 = x + w; y1 = y + h;
        }

        // return true if point is within rectangle
        bool test(int x, int y) const
        {
            return x >= x0 && x < x1 && y >= y0 && y < y1;
        }

        // returns true if parameter is completely inside this rectangle
        bool contains(const Rect & other) const
        {
            return x0 <= other.x0 && x1 >= other.x1
            && y0 <= other.y0 && y1 >= other.y1;
        }

        // return true if the two rectangles overlap
        bool overlap(const Rect & other) const
        {
            return x0 < other.x1 && other.x0 < x1
            && y0 < other.y1 && other.y0 < y1;
        }

        // returns area
        int area() const { return (x1-x0)*(y1-y0); }

        // returns area of union minus sum of areas
        int unionDiff(Rect other) const
        {
            int sum = area() + other.area();
            other.extend(*this);
            return other.area() - sum;
        }

        // this sets invalid region, such that
        // calling "extend" will just set the other rect
        void clear()
        {
            x0 = y0 = 0x7fffffff;
            x1 = y1 = 0;
        }

        void set(const Rect & other)
        {
            x0 = other.x0;
            y0 = other.y0;
            x1 = other.x1;
            y1 = other.y1;
        }

        // clip this rectangle into another rectangle
        // the offsets are added to the parameter rectangle
        void clip(const Rect & other, int offsetX = 0, int offsetY = 0)
        {
            x0 = (std::max)(x0, other.x0 + offsetX);
            y0 = (std::max)(y0, other.y0 + offsetY);
            x1 = (std::min)(x1, other.x1 + offsetX);
            y1 = (std::min)(y1, other.y1 + offsetY);
        }

        void extend(const Rect & other)
        {
            x0 = (std::min)(x0, other.x0);
            y0 = (std::min)(y0, other.y0);
            x1 = (std::max)(x1, other.x1);
            y1 = (std::max)(y1, other.y1);
        }

        // like extend with zero-sized rectangle at point
        // used by path rendering code
        void extendWithPoint(int x, int y)
        {
            x0 = (std::min)(x0, x);
            y0 = (std::min)(y0, y);
            x1 = (std::max)(x1, x);
            y1 = (std::max)(y1, y);
        }

        // shift the rectangle without changing size
        void offset(int x, int y)
        {
            x0 += x; x1 += x;
            y0 += y; y1 += y;
        }
    };

};  // namespace
