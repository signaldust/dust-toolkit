
#pragma once

#include "dust/gui/window.h"

#include "button.h"
#include "scroll.h"

// HERE BE DRAGONS: this is totally "work-in-progress" and anything
// here can change without warnings

/*

    The goal of this UI inspector is to provide roughly a similar directly
    access to the panel hierarchy of another window, as would be required
    for support for UI automation for accessibility purposes.


*/

namespace dust
{
    struct PanelInspector : Panel
    {
        int         level;
        TextButton  button;

        Panel   childRoot;

        Panel * target;

        PanelInspector()
        {
            style.rule = LayoutStyle::NORTH;

            childRoot.setParent(this);
            childRoot.style.rule = LayoutStyle::SOUTH;

            button.setParent(this);
            button.style.rule = LayoutStyle::WEST;
        }

        void setTarget(Panel * target);

        void render(RenderContext & rc)
        {
            rc.clear();
        }
    };

    struct WindowInspector : Panel, WindowDelegate, DiaWindowClient
    {
        static void openForWindow(Window * _target);

        void refresh();

        void open()
        {
            if(!getWindow()) openWindow(*this, *this);
        }
        
        void close()
        {
            auto * win = getWindow();
            if(win) win->closeWindow();
        }
        
        ~WindowInspector() { close(); }

        WindowInspector()
        {
            style.rule = LayoutStyle::FILL;
            
            toolbar.setParent(this);
            toolbar.style.rule = LayoutStyle::NORTH;

            btnDump.setParent(toolbar);
            btnDump.label.setText("Refresh");
            btnDump.style.rule = LayoutStyle::WEST;
            btnDump.onClick = [this] () { refresh(); };

            scroll.setParent(this);
            scroll.style.minSizeX = 300;
            scroll.style.minSizeY = 300;
        }

        void render(RenderContext & rc)
        {
            rc.clear(theme.bgColor);

        }

        void dia_reflow(Window * win)
        {
            if(win == target) refresh();
        }

    protected:
        
        Panel       toolbar;
        TextButton  btnDump;

        ScrollPanel scroll;
    
    private:
        Window *    target;

    };
}
