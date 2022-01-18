
#pragma once

#include <cstdint>

// HERE BE DRAGONS: this is totally "work-in-progress" and anything
// here can change without warnings

namespace dust
{
    // DIA is the Dust Interface Automation framework
    namespace dia   // FIXME: rename diaEvent?
    {
        // reflow events are sent whenever the layout is redone
        // which typically (but not always) means that something
        // in the UI hierarchy has changed
        static const auto reflow        = (uint64_t(1) << 0);
        
        // only subscribe to general events (eg. window closing)
        static const auto unspecified   = (uint64_t(1) << 63);

        static const auto all           = ~uint64_t(0);
    }

    // Our interfaces should more or less mirror those that are
    // found in platform frameworks, even though these are internal.
    struct DiaInvoke
    {
        virtual void dia_invoke() = 0;
    };

    // This is always inherited by PanelParent
    struct DiaElement
    {
        virtual ~DiaElement() {}

        // for each interface, we have an acessor that returns
        // either a pointer to the interface or nullptr is unsupported
        virtual DiaInvoke * dia_queryInvoke() { return 0; }
    };

}