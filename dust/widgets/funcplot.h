
#ifndef DUST_WIDGET_FUNCPLOT_H
#define DUST_WIDGET_FUNCPLOT_H

#include "panel.h"

#include <vector>

namespace dust
{
    // This implements basic function plotting.
    //
    struct FuncPlot : Panel
    {
        // represents a single data point
        //
        // these are assumed to be normalized to [0,1]
        // with (0,0) at the top-left corner
        struct Point { float x, y; };

        // function is a vector of points
        std::vector<Point>  data;

        ARGB    color;

        FuncPlot()
        {
            style.rule = LayoutStyle::FILL;
        }

        // NOTE: this explicitly does NOT clear background
        // such that we can put many of these on top of each other
        void render(RenderContext & rc)
        {
            if(!data.size()) return;

            float w = layout.w;
            float h = layout.h;

            Path path;

            for(auto & point : data)
            {
                path.plot(point.x*w, point.y*h);
            }

            rc.strokePath(path, 1.5f * getWindow()->pt(),
                paint::Color(color), 2, true);
        }
    };
};

#endif // guard
