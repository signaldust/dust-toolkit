
#include "control.h"

namespace dust
{

    void ControlParent::layoutAsRoot(float dpi)
    {
        calculateContentSizeX(dpi);
        calculateLayoutX();
        calculateContentSizeY(dpi);
        calculateLayoutY();

        updateWindowOffsets();
    }

    void ControlParent::updateWindowOffsets()
    {
        int contentX = layout.windowOffsetX + layout.contentOffsetX;
        int contentY = layout.windowOffsetY + layout.contentOffsetY;

        for(Control * c : children)
        {
            if(!c->enabled) continue;

            c->layout.windowOffsetX = contentX + c->layout.x;
            c->layout.windowOffsetY = contentY + c->layout.y;
            c->updateWindowOffsets();
        }
    }

    void ControlParent::calculateContentSizeX(float dpi)
    {
        // use the initial layout.w as minimum content size
        // this is a temporary value unless this is the layout root
        int contentSize = 0;
        int reserveSize = 0;

        for(Control * c : children)
        {
            // don't do any layout if disabled or rule is none
            if(!c->enabled || c->style.rule == LayoutStyle::NONE) continue;

            float unit = dpi * (1/72.f);

            // calculate temporary size as desired pixel size
            c->layout.w = (int) ceil(c->style.minSizeX * unit);

            // compute padding size
            c->layout.contentPadding.west = (int)ceil(unit*c->style.padding.west);
            c->layout.contentPadding.east = (int)ceil(unit*c->style.padding.east);

            c->calculateContentSizeX(dpi);

            // call the manual size hook
            c->layout.contentSizeX = (std::max)
            (c->layout.contentSizeX, c->ev_size_x(dpi));

            // expand the temporary size to content unless we can scroll
            if(!c->style.canScrollX) c->layout.w = c->layout.contentSizeX;

            switch(c->style.rule)
            {
            case LayoutStyle::FILL:
            case LayoutStyle::NORTH:
            case LayoutStyle::SOUTH:
                contentSize = (std::max)(contentSize, reserveSize + c->layout.w);
                break;

            case LayoutStyle::EAST:
            case LayoutStyle::WEST:
                reserveSize += c->layout.w;
                contentSize = (std::max)(contentSize, reserveSize);
                break;

            default:
                debugPrint("warning: unknown style.rule!\n");
                break;
            }
        }

        // add padding
        contentSize += layout.contentPadding.west;
        contentSize += layout.contentPadding.east;

        layout.contentSizeX = (std::max)(contentSize, layout.w);
    }

    void ControlParent::calculateContentSizeY(float dpi)
    {
        // see calculateContentSizeX
        int contentSize = 0;
        int reserveSize = 0;

        for(Control * c : children)
        {
            // don't do any layout if disabled or rule is none
            if(!c->enabled || c->style.rule == LayoutStyle::NONE) continue;

            float unit = dpi * (1/72.f);

            // calculate temporary size as desired pixel size
            c->layout.h = (int) ceil(c->style.minSizeY * unit);

            // compute padding size
            c->layout.contentPadding.north = (int)ceil(unit*c->style.padding.north);
            c->layout.contentPadding.south = (int)ceil(unit*c->style.padding.south);

            c->calculateContentSizeY(dpi);

            // call the manual size hook
            c->layout.contentSizeY = (std::max)
            (c->layout.contentSizeY, c->ev_size_y(dpi));

            // expand the temporary size to content unless we can scroll
            if(!c->style.canScrollY) c->layout.h = c->layout.contentSizeY;

            switch(c->style.rule)
            {
            case LayoutStyle::FILL:
            case LayoutStyle::EAST:
            case LayoutStyle::WEST:
                contentSize = (std::max)(contentSize, reserveSize + c->layout.h);
                break;

            case LayoutStyle::NORTH:
            case LayoutStyle::SOUTH:
                reserveSize += c->layout.h;
                contentSize = (std::max)(contentSize, reserveSize);
                break;

            default:
                break;
            }

        }

        // add padding
        contentSize += layout.contentPadding.north;
        contentSize += layout.contentPadding.south;

        layout.contentSizeY = (std::max)(contentSize, layout.h);
    }

    void ControlParent::calculateLayoutX()
    {
        layout.contentSizeX = (std::max)(layout.contentSizeX, layout.w);
        int box0 = layout.contentPadding.west;
        int box1 = layout.contentSizeX - layout.contentPadding.east;

        for(Control * c : children)
        {
            if(!c->enabled || c->style.rule == LayoutStyle::NONE) continue;

            switch(c->style.rule)
            {
            case LayoutStyle::FILL:
            case LayoutStyle::NORTH:
            case LayoutStyle::SOUTH:
                c->layout.x = box0;
                c->layout.w = box1 - box0;
                break;

            case LayoutStyle::WEST:
                c->layout.x = box0;
                c->layout.w = c->layout.contentSizeX;
                box0 += c->layout.contentSizeX;
                break;

            case LayoutStyle::EAST:
                box1 -= c->layout.contentSizeX;
                c->layout.x = box1;
                c->layout.w = c->layout.contentSizeX;
                break;

            default:
                break;
            }

            c->calculateLayoutX();
        }

    }

    void ControlParent::calculateLayoutY()
    {
        layout.contentSizeY = (std::max)(layout.contentSizeY, layout.h);
        int box0 = layout.contentPadding.north;
        int box1 = layout.contentSizeY - layout.contentPadding.south;

        for(Control * c : children)
        {
            if(!c->enabled || c->style.rule == LayoutStyle::NONE) continue;

            switch(c->style.rule)
            {
            case LayoutStyle::FILL:
            case LayoutStyle::EAST:
            case LayoutStyle::WEST:
                c->layout.y = box0;
                c->layout.h = box1 - box0;
                break;

            case LayoutStyle::NORTH:
                c->layout.y = box0;
                c->layout.h = c->layout.contentSizeY;
                box0 += c->layout.contentSizeY;
                break;

            case LayoutStyle::SOUTH:
                box1 -= c->layout.contentSizeY;
                c->layout.y = box1;
                c->layout.h = c->layout.contentSizeY;
                break;

            default:
                break;
            }

            c->calculateLayoutY();

            c->ev_layout();
        }
    }

};
