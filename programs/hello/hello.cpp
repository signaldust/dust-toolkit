
#include "dust/gui/app.h"
#include "dust/widgets/label.h"
#include "dust/widgets/button.h"

// This is the panel that holds our actual GUI
//
struct HelloWorld : dust::Panel
{
    dust::Label     helloText;
    
    dust::Button    closeButton;
    dust::Label     closeLabel;

    HelloWorld()
    {
        // In dust-toolkit any parent-child relationships are dynamic
        // and never take ownership, so we can build the whole GUI tree
        // directly in the constructor, even though we'll only attach this
        // to a window in app_startup() once the event-loop is running.

        // set the panel itself to fill the window..
        style.rule = dust::LayoutStyle::FILL;

        // The native layout system is "border layout" and the order in which
        // we add children to their parent is the order they are processed,
        // but the order of anything other than setParent() is mostly irrelevant.
        //
        // First, we place a button at the bottom of the panel:
        closeButton.setParent(this);
        closeButton.style.rule = dust::LayoutStyle::SOUTH;

        // when the button is pressed, we ask the window to close
        // we can attach functionality to buttons simply by using lambdas
        closeButton.onClick = [this](){ getWindow()->closeWindow(); };

        // standard buttons are blank, so add a child label for text
        closeLabel.setParent(closeButton);
        closeLabel.style.rule = dust::LayoutStyle::FILL;
        closeLabel.setText("close");
        // let's use a monospace font just for giggles
        closeLabel.font.loadDefaultFont(8.f, 96.f, true);
        
        // then we'll fill the rest of the window with a greeting
        helloText.setParent(this);
        helloText.setText("Hi, how are you doing?");
        helloText.font.loadDefaultFont(20.f, 96.f);
        helloText.style.rule = dust::LayoutStyle::FILL;

        // by default, the label draws with dust::theme.fgColor,
        // but we'll paint the background pretty, so draw text in black instead
        helloText.color = 0xff000000;
    }

    void render(dust::RenderContext & rc)
    {
        // we'll make the background prettier with a gradient
        rc.fill<dust::blend::None>(
            dust::paint::Gradient2(
                0xffff00ff, 0, 0,
                0xff400080, 0, layout.h));
    }
};

struct HelloApp : dust::Application
{
    HelloWorld  hello;

    // This is called inside the event-loop when we call run() in main.
    // Creating windows outside the event-loop can cause issues on some
    // platforms (eg. on macOS it leaks memory) so one should do it here.
    void app_startup()
    {
        // We'll first create a window, passing the app as the delegate.
        //
        // Since we want a top-level window (ie. we're not trying to place
        // the native window/view inside another native window/view) we'll
        // pass a null-pointer as the parent.
        //
        // In theory, we could use dust::openWindow() to automatically find
        // the minimum size that can fit our GUI, but unfortunately the label
        // widget hasn't been updated for this, so we'll make do without.
        auto * win = dust::createWindow(*this, 0, 400, 100);

        // give the window a title
        win->setTitle("Hello World!");

        // then we'll set the window as the parent of our GUI
        hello.setParent(win);
    }
    
};

#ifdef _WIN32
int WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR args, int nShowCmd)
#else
int main()
#endif
{
    HelloApp app;

    app.run();
}
