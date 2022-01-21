
#include "dust/core/component.h"

#include "inspector.h"

// HERE BE DRAGONS: this is totally "work-in-progress" and anything
// here can change without warnings

using namespace dust;

static ComponentManager<ElementInspector, DiaElement>  cm_ElementInspector;
static ComponentManager<WindowInspector, Window> cm_WindowInspector;

void WindowInspector::openForWindow(Window * window)
{
    auto * inspector = cm_WindowInspector.getComponent(window);

    inspector->target = window;

    inspector->open();
    inspector->refresh();

    window->registerAutomation(inspector, dia::all);
    
}

/*

    FIXME:

    The way we are doing a full refresh from scratch every time is obviously
    total duct-tape and not how this should be done in the long run.

    Instead, what we really want to do is compare the new UI tree to the previous
    version (as represented by an existing inspector tree) and only reconstruct
    sub-trees where something has actually changed.

    In order to make this more efficient (and provide a handy method in general)
    we might want to add methods to insert another panel before/after a given
    sibling in it's parent's list of children.

*/

static void buildSubtree(DiaElement * target, PanelParent & parent)
{
    auto * c = target->dia_getChildFirst();
    while(c)
    {
        if(c->dia_isVisible())
        {
            auto * ci = cm_ElementInspector.getComponent(c);
            ci->setParent(parent);
            ci->setTarget(c);
        }
        c = c->dia_getSiblingNext();
    }
}

void WindowInspector::refresh()
{
    auto * root = scroll.getContent();
    root->removeAllChildren();
    buildSubtree(target, *root);
}

void ElementInspector::setTarget(DiaElement * _target)
{
    target = _target;
    std::string str = strf("[%p] %s", target, target->dia_getName());
        
    button.label.setText(str);

    bool hideMe = target->dia_visualOnly();

    //button.label.color = hideMe ? theme.selColor : theme.fgColor;
    setEnabled( !hideMe );

    style.padding.west = 9;
    
    if(!hideMe)
    {
        auto * invoke = target->dia_queryInvoke();
        if(invoke) button.onClick = [invoke] () { invoke->dia_doInvoke(); };
        else button.onClick = doNothing;
    }

    childRoot.removeAllChildren();
    buildSubtree(target, getEnabled() ? childRoot : *getParent());
}
