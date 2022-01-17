
#include "inspector.h"

// HERE BE DRAGONS: this is totally "work-in-progress" and anything
// here can change without warnings

using namespace dust;

static ComponentManager<PanelInspector, Panel>  cm_PanelInspector;
static ComponentManager<WindowInspector, Window> cm_WindowInspector;

void WindowInspector::openForWindow(Window * window)
{
    auto * inspector = cm_WindowInspector.getComponent(window);

    inspector->target = window;

    inspector->open();
    inspector->refresh();

    window->registerAutomation(inspector, dia::all);
    
}

void WindowInspector::refresh()
{
    auto * root = scroll.getContent();
    root->removeAllChildren();

    auto builder = [root](Panel * c)
    {
        auto * ci = cm_PanelInspector.getComponent(c);
        ci->setParent(root);
        ci->setTarget(c);
    };

    target->eachChild(builder);
}

void PanelInspector::setTarget(Panel * _target)
{
    target = _target;
    std::string str = strf("[%p] %s", target, target->getName());
        
    button.label.setText(str);
    button.label.color = target->style.visualOnly ? theme.selColor : theme.fgColor;

    bool hideMe = target->style.visualOnly;
    
    setEnabled( !hideMe );

    style.padding.west = 9;

    auto builder = [this](Panel * c)
    {
        auto * ci = cm_PanelInspector.getComponent(c);

        ci->setParent(getEnabled() ? childRoot : *getParent());
        ci->setTarget(c);
    };

    childRoot.removeAllChildren();
    target->eachChild(builder);
}
