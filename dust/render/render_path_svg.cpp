

// NanoSVG wrapper

#include <stdio.h>
#include <string.h>
#include <math.h>
#define NANOSVG_ALL_COLOR_KEYWORDS	// Include full list of color keywords.
#define NANOSVG_IMPLEMENTATION		// Expands implementation
#include "dust/libs/nanosvg.h"

#include "render.h"
using namespace dust;

#ifdef _WIN32   // FIXME: this is not a robust solution
# define strdup _strdup
#endif

void SVG::load(const char * svgData)
{
    // dup, since nsvgParse modifies the string
    char * input = strdup(svgData);
    // load in points, with 96 dpi nominal pixel?
    NSVGimage* image = nsvgParse(input, "pt", 72);
    free(input);

    importNSVG(image);
}

void SVG::loadFile(const char * path)
{
    // dup, since nsvgParse modifies the string
    // load in points, with 96 dpi nominal pixel?
    NSVGimage* image = nsvgParseFromFile(path, "pt", 72);
    importNSVG(image);
}

void SVG::importNSVG(void * ptr)
{
    NSVGimage* image = (NSVGimage*) ptr;

    width = image->width;
    height = image->height;

    // convert to our internal format
    shapes.clear();
    for (auto * shape = image->shapes; shape != NULL; shape = shape->next) {
        shapes.resize(shapes.size() + 1);

        shapes.back().fColor = 0;
        if(shape->fill.type == NSVG_PAINT_COLOR)
        {
            ARGB c = shape->fill.color;
            // NanoSVG is being retarded with color channel ordering
            shapes.back().fColor = (c & 0xff00ff00) | ((c>>16)&0xff) | ((c&0xff)<<16);
        }

        shapes.back().fRule = (shape->fillRule == NSVG_FILLRULE_NONZERO)
            ? FILL_NONZERO : FILL_EVENODD;

        shapes.back().sColor = 0;
        if(shape->stroke.type == NSVG_PAINT_COLOR)
        {
            ARGB c = shape->stroke.color;
            // NanoSVG is being retarded with color channel ordering
            shapes.back().sColor = (c & 0xff00ff00) | ((c>>16)&0xff) | ((c&0xff)<<16);
        }
        
        shapes.back().sWidth = shape->strokeWidth;

        // merge all paths into one, with move for first point
    	for (auto * path = shape->paths; path != NULL; path = path->next) {
            if(!path->npts) continue;
            shapes.back().path.move(path->pts[0], path->pts[1]);
    		for (int i = 1; i < path->npts; i += 3) {
    			float* p = &path->pts[i*2];
    			shapes.back().path.cubic(p[0],p[1], p[2],p[3], p[4],p[5]);
    		}
            if(path->closed) shapes.back().path.close();
    	}
    }

    // we're done with NanoSVG image
    nsvgDelete(image);
    
}