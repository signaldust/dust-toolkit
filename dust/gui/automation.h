
#pragma once

#include "dust/core/component.h"

#include <cstdint>

// HERE BE DRAGONS: this is totally "work-in-progress" and anything
// here can change without warnings

namespace dust
{
    // DIA is the Dust Interface Automation framework
    namespace dia
    {
        // reflow events are sent whenever the layout is redone
        // which typically (but not always) means that something
        // in the UI hierarchy has changed
        static const auto reflow        = (uint64_t(1) << 0);
        
        // only subscribe to general events (eg. window closing)
        static const auto unspecified   = (uint64_t(1) << 63);

        static const auto all           = ~uint64_t(0);
    }
    
    struct DiaWindowClient : virtual ComponentHost
    {
        // this is always sent when a registration changes
        virtual void dia_registered(Window *, uint64_t newMask) {}

        // this is always sent to any registered clients
        virtual void dia_closed(Window *) {}

        // reflow events
        virtual void dia_reflow(Window *) {}
    };

}