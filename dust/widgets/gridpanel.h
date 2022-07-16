
#pragma once

#include "dust/gui/panel.h"

#include <vector>

namespace dust
{
    // Grid-layout container with fixed columns
    //
    // The basic idea is to build the layout with several
    // columns side-by-side and adjust the height of the
    // cells to agree with each other.
    //
    // The other feature we implement is optional weighting:
    //
    // Any extra space is distributed among rows/columns with
    // non-zero weights, proportional to the relative weights.
    //
    // NOTE: Ideally we'd probably like to distribute the extra
    // space in such a way that the relative sizes of the weighted
    // elements approach the weights as fast as possible, rather
    // than asymptotically like in the current implementation.
    //
    struct GridPanel : Panel
    {

        GridPanel(unsigned nColumn) : nColumn(nColumn)
        {
            columns = new Column[nColumn];
            style.rule = LayoutStyle::FILL;

            for(unsigned i = 0; i < nColumn; ++i)
            {
                columns[i].setParent(this);
                columns[i].weight = 0;
                columns[i].style.rule = LayoutStyle::MANUAL;
            }
        }

        ~GridPanel()
        {
            while(rows.size())
            {
                delete [] rows.back();
                rows.pop_back();
            }
            delete [] columns;
        }

        int ev_size_x(float dpi) override
        {
            int w = 0;
            for(int i = 0; i < nColumn; ++i)
            {
                columns[i].doSizeX(dpi);
                w += columns[i].getLayout().contentSizeX;
            }
            return w;
        }

        int ev_size_y(float dpi) override
        {
            // finish column layout and call sizeY calculation
            {
                // figure out normalized weights and the amount of
                // extra space that we actually have
                float wTotal = 0;
                int extraW = layout.w;

                for(int i = 0; i < nColumn; ++i)
                {
                    wTotal += columns[i].weight;
                    extraW -= columns[i].getLayout().contentSizeX;
                }

                int x = 0;
                for(int i = 0; i < nColumn; ++i)
                {
                    int w = columns[i].getLayout().contentSizeX;
                    if(wTotal > 0 && extraW > 0)
                    {
                        // if this seems convoluted, it's done this way
                        // in order to avoid losing pixels due to rounding
                        int addW = (int) (extraW * columns[i].weight / wTotal);
                        wTotal -= columns[i].weight;
                        extraW -= addW; w += addW;
                    }
                    columns[i].getLayout().x = x;
                    columns[i].getLayout().w = w;
                    x += w;

                    columns[i].doLayoutX(dpi);
                    columns[i].doSizeY(dpi);
                }
            }

            // calculate our minimum height
            int sizeY = 0;
            for(Cell * r : rows)
            {
                int h = 0;
                for(int i = 0; i < nColumn; ++i)
                {
                    h = (std::max)(h, r[i].getLayout().contentSizeY);

                }
                sizeY += h;
            }
            return sizeY;
        }

        // finish layout for y-axis
        void ev_layout(float dpi) override
        {
            // figure out normalized weights and the amount of
            // extra space that we actually have
            float wTotal = 0;
            int extraH = layout.h;

            // loop all cells on all rows and calculate their height
            for(int j = 0; j < rows.size(); ++j)
            {
                int h = 0;  // calculate height of the row
                for(int i = 0; i < nColumn; ++i)
                {
                    h = (std::max)(h, rows[j][i].getLayout().contentSizeY);
                }

                wTotal += weightRows[j];
                extraH -= h;

                // store the height to the first cell
                rows[j][0].getLayout().contentSizeY = h;
            }

            // do a second pass to setup the layouts
            int y = 0;
            for(int j = 0; j < rows.size(); ++j)
            {
                int h = rows[j][0].getLayout().contentSizeY;

                if(wTotal > 0 && extraH > 0)
                {
                    // see ev_layout_x for comment
                    int addH = (int) (extraH * weightRows[j] / wTotal);
                    wTotal -= weightRows[j];
                    extraH -= addH; h += addH;
                }

                // then set this as the height for all the cells
                for(int i = 0; i < nColumn; ++i)
                {
                    rows[j][i].getLayout().contentSizeY = h;
                }

                y += h;
            }

            for(int i = 0; i < nColumn; ++i)
            {
                columns[i].getLayout().y = 0;
                columns[i].getLayout().h = y;
                columns[i].doLayoutY(dpi);
            }
        }

        // add new row - returns the index
        int addRow()
        {
            Cell * row = new Cell[nColumn];
            rows.push_back(row);
            weightRows.push_back(0);

            for(int i = 0; i < nColumn; ++i)
            {
                row[i].setParent(&columns[i]);
                row[i].style.rule = LayoutStyle::NORTH;
            }

            return rows.size() - 1;
        }

        void weightRow(int row, float w)
        {
            if(row < weightRows.size()) weightRows[row] = w;
        }

        void weightColumn(int col, float w)
        {
            if(col < nColumn) columns[col].weight = w;
        }

        // insert component into a grid cell
        void insert(unsigned col, unsigned row, Panel & ctl)
        {
            ctl.setParent(getCell(col, row));
        }

        // return a pointer to a given cell or null if out of bounds
        Panel * getCell(unsigned c, unsigned r)
        {
            if(c >= nColumn) return 0;
            if(r >= rows.size()) return 0;
            return rows[r] + c;
        }

    private:
        const unsigned nColumn;

        struct Column : Panel
        {
            float weight;
            Layout & getLayout() { return layout; }

            void doSizeX(float dpi) { layout.w = 0; calculateContentSizeX(dpi); }
            void doSizeY(float dpi) { layout.h = 0; calculateContentSizeY(dpi); }

            void doLayoutX(float dpi) { calculateLayoutX(dpi); }
            void doLayoutY(float dpi) { calculateLayoutY(dpi); }
        } *columns;

        struct Cell : Panel
        {
            Layout & getLayout() { return layout; }
        };

        std::vector<Cell*> rows;
        std::vector<float> weightRows;
    };

    template <unsigned W, unsigned H = 0>
    struct Grid : GridPanel
    {
        Grid() : GridPanel(W)
        {
            for(unsigned i = 0; i < H; ++i) { addRow(); }
        }
    };

};
