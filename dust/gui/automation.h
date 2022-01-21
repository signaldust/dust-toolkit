
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
    //

    
    // This is like UIA IInvokeProvider
    struct DiaInvoke
    {
        virtual void dia_doInvoke() = 0;
    };

    // This is like UIA IToggleProvider
    struct DiaToggle
    {
        virtual bool dia_getToggleState() = 0;
        
        virtual void dia_doToggle() = 0;
    };

    // This is like UIA IExpandCollapseProvider, except we simplify
    // implementation by mapping expand/collapse to setExpanded
    struct DiaExpand
    {
        virtual bool dia_getExpandState() = 0;
        virtual void dia_setExpandState(bool) = 0;

        // UIA compatible actions
        void dia_doExpand() { dia_setExpandState(true); }
        void dia_doCollapse() { dia_setExpandState(false); }
    };

    // This is like UIA IRangeValueProvider
    struct DiaRanged
    {
        // FIXME: do we want to describe properties with a struct
        // or do we want to have acessor functions for each of them?
        //
        virtual bool    dia_getRangedReadOnly() = 0;
        
        virtual double  dia_getRangedMin() = 0;
        virtual double  dia_getRangedMax() = 0;

        virtual double  dia_getRangedChangeSmall() = 0;
        virtual double  dia_getRangedChangeLarge() = 0;

        virtual double  dia_getRangedValue() = 0;
        virtual void    dia_setRangedValue(double) = 0;

    };

    // This is always inherited by PanelParent
    //
    // FIXME: we probably need an explicit tree-navigation here
    // in order to allow elements that are not Panels?
    struct DiaElement : virtual ComponentHost
    {
        virtual ~DiaElement() {}

        virtual const char * dia_getName() = 0;

        virtual bool dia_isVisible() = 0;

        // if this returns true, we flatten the element from the
        // automation hierarchy; we provide a default here because
        // this flag typically only makes sense for Panels
        virtual bool dia_visualOnly() { return false; }

        // navigation: all of these return nullptr when
        // the requested element doesn't exist
        virtual DiaElement * dia_getParent() = 0;
        virtual DiaElement * dia_getChildFirst() = 0;
        virtual DiaElement * dia_getChildLast() = 0;
        virtual DiaElement * dia_getSiblingNext() = 0;
        virtual DiaElement * dia_getSiblingPrevious() = 0;

        // for each interface, we have an acessor that returns
        // either a pointer to the interface or nullptr is unsupported
        
        virtual DiaInvoke * dia_queryInvoke() { return 0; }
        virtual DiaToggle * dia_queryToggle() { return 0; }
        virtual DiaExpand * dia_queryExpand() { return 0; }
        virtual DiaRanged * dia_queryRanged() { return 0; }
    };

}

/*

 Win32 specific notes:

 the IRawElementProviderFragmentRoot interface is for HWNDs (=Window)

 the IRawElementProviderSimple and IRawElementProviderFragment should be
 implemented by anything that's supposed to be visible in the tree

 GetEmbeddedFragmentRoots is apparently only if we host another HWND
 with it's own fragment structure, so we'll ignore that for now


*/