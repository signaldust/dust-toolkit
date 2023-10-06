
#pragma once

#include <cstdint>

#include "key_scancode.h"

namespace dust
{

    // bitmasks for modifier keys, used by mouse/keyboard events
    static const uint8_t KEYMOD_SHIFT  = (1<<0);
    static const uint8_t KEYMOD_CTRL   = (1<<1);
    static const uint8_t KEYMOD_ALT    = (1<<2);
    static const uint8_t KEYMOD_SYS    = (1<<3);   // OSX cmd, Win-key(?)


    // the KEYMOD_CMD is mapped to the standard command modifier
    // on OSX this is the Command key, elsewhere usually Control
    // often shortcuts are otherwise identical, so this is useful
#ifdef __APPLE__
    static const uint8_t KEYMOD_CMD = KEYMOD_SYS;
#else
    static const uint8_t KEYMOD_CMD = KEYMOD_CTRL;
#endif

    // MouseEvent represents a single mouse action.
    //
    // When dragging, mouse move/up events are sent to the original
    // recipient of the mouse down event, with the drag button set.
    //
    // This avoids Windows-style capture madness.
    //
    struct MouseEvent
    {
        enum Type {

            tMove,

            tDown,
            tUp,

            tScroll,
            
            tDragFiles,

            tInvalid
        };

        struct Flags {
            // This is set on the synthetic tMove after tScroll
            static const uint8_t    hoverOnScroll = (1<<0);
        };

        Type        type;       // event type

        int         x, y;       // frame local coordinates

        uint8_t     button;     // mouse button
        uint8_t     nClick;     // 1 for click, 2 for double - can go higher

        uint8_t     keymods;    // key modifiers
        uint8_t     flags;      // flags (above)

        float       scrollX;    // x-direction scroll
        float       scrollY;    // y-direction scroll

        MouseEvent(Type type, int x, int y,
            uint8_t btn, uint8_t nClick, uint8_t mods)
            : type(type), x(x), y(y)
            , button(btn), nClick(nClick), keymods(mods)
            , flags(0), scrollX(0), scrollY(0)
        { }
    };

    // This is separated from Panel just for header readability reasons
    struct EventResponder
    {
        // called when DPI changes
        virtual void ev_dpi(float dpi) {}

        // called periodically for animation and content update
        virtual void ev_update() {}

        // called by layout when calculating size constraints
        // the return value is used as the minimum pixel size
        // if larger than the automatically calculated value
        virtual int ev_size_x(float dpi) { return 0; }

        // called by layout when calculating size constraints
        // the return value is used as the minimum pixel size
        // if larger than the automatically calculated value
        virtual int ev_size_y(float dpi) { return 0; }

        // called when control's layout has been finished
        // including processing of any children
        virtual void ev_layout(float dpi) {}

        // ev_mouse handles most mouse events
        // return false to bubble up
        virtual bool ev_mouse(const MouseEvent &) { return false; }

        // this is pure notification for mouse-over tracking
        // it is sent when mouse exists the window or when
        // the last mouse event was accepted by another frame
        virtual void ev_mouse_exit() {}

        // raw key-state events - return false to bubble up
        // this is keydown if pressed is true, otherwise keyup
        virtual bool ev_key(Scancode vk, bool pressed, unsigned mods)
        { return false; }

        // text entry (from keyboard), null-terminated UTF-8 string
        virtual void ev_text(const char * text) {}

        // notification about focus change
        // gained is true if focus gained, false if lost
        virtual void ev_focus(bool gained) {}

        // return true if we can drop files
        virtual bool ev_accept_files() { return false; }

    };

};
