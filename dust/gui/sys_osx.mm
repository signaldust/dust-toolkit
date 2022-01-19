
#include "window.h"
#include "app.h"


#include <CoreAudio/CoreAudio.h>
#include <AudioUnit/AudioUnit.h>

#include <Cocoa/Cocoa.h>
#include <Carbon/Carbon.h>  // this is for the dead-key workaround mess

#include <opengl/gl3.h>

#include <string>

#include "key_scancode_osx.h"

/*

Cursor movement can apparently be disabled by using:
    CGAssociateMouseAndMouseCursorPosition(bool)

We should probably have some API for this..

*/


/*

FIXME: implement CoreGraphics blit code to test performance..

 -> done: performance sucks


CGColorSpaceRef colourSpace = CGColorSpaceCreateDeviceRGB();
CGImageRef image = juce_createCoreGraphicsImage (temp, colourSpace, false);
CGColorSpaceRelease (colourSpace);
CGContextDrawImage (cg, CGRectMake (r.origin.x, r.origin.y, clipW, clipH), image);
CGImageRelease (image);

*/


using namespace dust;

// We use DUST_COCOA_PREFIX to prefix all Objective-C class names.
// This is necessary in plugin situations, thanks to global namespace.
//
// NOTE: The C-preprocessor requires two-passes to actually expand this.
#ifdef DUST_COCOA_PREFIX
#define DUST_RENAME(x)   DUST_CONCAT_EXPAND(DUST_COCOA_PREFIX, x)
// -- now we can rename:
#define DUSTWrapperView  DUST_RENAME(WrapperView)
#define DUSTAppDelegate  DUST_RENAME(AppDelegate)
#else
#error DUST_COCOA_PREFIX not defined!
#endif


//////////////////////
/// TEXT CLIPBOARD ///
//////////////////////

bool dust::clipboard::setText(const char *buf, unsigned len)
{
    NSString* str = [[NSString alloc]
                initWithBytesNoCopy:(void *)buf
                             length:(NSUInteger)len
                           encoding:NSUTF8StringEncoding
                       freeWhenDone:FALSE];
    [[NSPasteboard generalPasteboard] clearContents];
    bool success = TRUE == [[NSPasteboard generalPasteboard]
        setString:str forType:NSPasteboardTypeString];

    [str release];

    return success;
}

bool dust::clipboard::getText(std::string & out)
{
    NSString* clipString = [[NSPasteboard generalPasteboard]
        stringForType:NSPasteboardTypeString];

    if(!clipString) return false;

    out = [clipString UTF8String];

    return true;
}

//////////////
/// WINDOW ///
//////////////

// NOTE: The tear-down strategy here is a bit funky because
// Cocoa likes to keep stuff around in the auto-release pool
// if windows are created from outside the run-loop.
//
// So handle the tear-down of windows by detaching all children
// of the closed window, to make sure we don't touch them later
// when they might no longer exist.

#if DUST_USE_OPENGL
@interface DUSTWrapperView : NSOpenGLView <NSWindowDelegate>
#else
@interface DUSTWrapperView : NSView <NSWindowDelegate>
#endif
{
    // avoid encoding a typestring
    void *_sysFramePtr;
}
    // this is just used to setup the wrapper backpointer
    // avoid a typestring by passing a void* on signature
    -(void)setSystemFrame:(void*)frame;
@end // interface

#define sysFrame ((CocoaWindow*)_sysFramePtr)

struct CocoaWindow : Window
{
    DUSTWrapperView     *sysView;

    WindowDelegate &delegate;

    NSTrackingArea  *tracking;
    NSTimer         *timer;

    unsigned    scaleFactor;
    unsigned    keymods;    // Apple sucks, have to track this

    UInt32      deadKeyState = 0;

    struct TitleBar : Panel
    {
        unsigned size = 0;  // this is in "virtual low-dpi pixels"

        TitleBar()
        {
            style.rule = LayoutStyle::NORTH;
            style.visualOnly = true;
        }

        int ev_size_y(float dpi)
        {
            return int(size * getWindow()->getSystemDPI() / 96);
        }

        void render(RenderContext & rc)
        {
            rc.fill(paint::Gradient2(
                theme.bgMidColor, 0, 0,
                theme.bgColor, 0, layout.h));
        }
        
    } titleBar;

    CocoaWindow(WindowDelegate & delegate, void * parent, int w, int h)
        : delegate(delegate), scaleFactor(96), keymods(0)
    {
        tracking = 0;
        timer = 0;

        // we'll center the window below, so just pass zero position
        NSRect frame = NSMakeRect(0, 0, w, h );

        // adjust size to fit a custom title-bar if top-level
        if(!getenv("DUST_LOWRES"))
        {
            // compute content frame size
            NSRect contentFrame = [NSWindow contentRectForFrameRect: frame
                styleMask: NSTitledWindowMask];

            titleBar.size = int(frame.size.height - contentFrame.size.height);
        }
                
        NSWindow * window = 0;
        if(!parent && !delegate.win_want_view_only())
        {
            // bump frame up by titlesize
            frame.size.height += titleBar.size;
            
            window = [[NSWindow alloc]
                initWithContentRect:frame
                styleMask: NSTitledWindowMask
                | NSClosableWindowMask
                | NSMiniaturizableWindowMask
                | NSResizableWindowMask
                | (getenv("DUST_LOWRES") ? 0 : NSFullSizeContentViewWindowMask)
                backing: NSBackingStoreBuffered
                defer: NO
            ];
            [window center];
            [window setTitle:@""];
            
            // cocoa crashes with very small windows
            // although something like 10x10 is fine
            // either way, set initial size as minimum?
            [window setContentMinSize:NSMakeSize(frame.size.width,frame.size.height)];

            // allow fullscreen
            [window setCollectionBehavior:
                NSWindowCollectionBehaviorFullScreenPrimary];

            // this makes inactive traffic lights darker and title-text
            // white/gray, but we draw the actual titlebar background manually
            [window setAppearance:
                [NSAppearance appearanceNamed:NSAppearanceNameVibrantDark]];

            // make it actually black
            //[window setBackgroundColor: NSColor.blackColor];
            [window setTitlebarAppearsTransparent: TRUE];

            titleBar.setParent(this);

        }

#if DUST_USE_OPENGL
        NSOpenGLPixelFormatAttribute attrs[] =
        {
            NSOpenGLPFAAccelerated,
            NSOpenGLPFADoubleBuffer,

            NSOpenGLPFAOpenGLProfile,   NSOpenGLProfileVersion3_2Core,

            NSOpenGLPFAColorSize, 24,
            NSOpenGLPFAAlphaSize, 8,

            0
        };

        NSOpenGLPixelFormat *pixelFormat =
            [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
        sysView = [[DUSTWrapperView alloc]
                        initWithFrame:frame
                        pixelFormat: pixelFormat];

        [pixelFormat release];
#else
        sysView = [[DUSTWrapperView alloc] initWithFrame:frame];
#endif

        if(!getenv("DUST_LOWRES"))
        {
            [sysView setWantsBestResolutionOpenGLSurface:YES];

            // we need this to avoid artifacts in rounded corners
            //
            // don't do that for forced low-res test-mode though,
            // because the backing bounds are always in retina scale
            [sysView setWantsLayer:YES];
        }
        [sysView setSystemFrame:(void*)this];

        if(parent)
        {
            [((NSView*)parent) addSubview:sysView
                positioned:NSWindowAbove relativeTo:nil];
        }

        if(window)
        {
            [window setContentView:sysView];
            [window setDelegate:sysView];
            [window setReleasedWhenClosed:YES];
            [window makeKeyAndOrderFront:nil];
        }

        // put it on autorelease pool, so it gets released
        // this still gives AU plugin host chance to use it
        [sysView autorelease];

        // notify that we've created the window
        delegate.win_created();
    }

    virtual void resize(int w, int h)
    {
        NSSize newSize = NSMakeSize(w, h);
        [sysView setFrameSize:newSize];
    }

    // return the underlying NSView
    virtual void * getSystemHandle() { return (void*) sysView; }

    // this is called during init and when the window is moved from
    // one screen to another that might have a different scale factor
    void updateScaleFactor()
    {
        scaleFactor = (int) (96 * [[sysView window] backingScaleFactor]);
        if(getenv("DUST_LOWRES")) scaleFactor = 96;

        debugPrint("System DPI change to %d\n", scaleFactor);
        broadcastDPI(getDPI());

        reflowChildren();
    }

    ~CocoaWindow()
    {
        removeTrackingArea();
        removeTimer();
    }

    void setMinSize(int w, int h)
    {
        if([[sysView window] delegate] == sysView)
        {
            [[sysView window] setContentMinSize:NSMakeSize(w, h + titleBar.size)];
        }
    }

    void toggleMaximize()
    {
        [[sysView window] performZoom:nil];
    }

    unsigned getSystemDPI() { return scaleFactor; }

    void updateKeymods()
    {
        NSUInteger flags = [NSEvent modifierFlags];
        unsigned mods = 0;

        if(flags & NSShiftKeyMask) mods |= KEYMOD_SHIFT;
        if(flags & NSControlKeyMask) mods |= KEYMOD_CTRL;
        if(flags & NSCommandKeyMask) mods |= KEYMOD_SYS;
        if(flags & NSAlternateKeyMask) mods |= KEYMOD_ALT;

        keymods = mods;
    }

    // make layoutAndPaint accessible to our Cocoa class
    void layoutAndPaintWindow(int viewSizeX, int viewSizeY)
    {
        layoutAndPaint(viewSizeX, viewSizeY);
    }

#if !DUST_USE_OPENGL
    void platformBlit(dust::Surface & s)
    {
        auto scale = [[sysView window] backingScaleFactor];
        auto cg = (CGContextRef) [[NSGraphicsContext currentContext] CGContext];

        Surface tmp(s.getSizeX(), s.getSizeY());

        for(unsigned y = 0; y < s.getSizeY(); ++y)
        {
            for(unsigned x = 0; x < s.getSizeX(); ++x)
            {
                tmp.getPixels()[x + tmp.getPitch()*y]
                    = s.getPixels()[x + s.getPitch()*(s.getSizeY()-y-1)];
            }
        }
        
        auto data = CGDataProviderCreateWithData(
            0, tmp.getPixels(), tmp.getSizeX() * tmp.getPitch() * sizeof(ARGB), 0);
            
        CGDirectDisplayID displayID = CGMainDisplayID();  
        CGColorSpaceRef cs = [[[sysView window] colorSpace] CGColorSpace];
        
        auto image = CGImageCreate(
            tmp.getSizeX(), tmp.getSizeY(), 8, 32, tmp.getPitch() * 4,
            cs, (kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little),
            data, 0, false, kCGRenderingIntentDefault);
            
        CGDataProviderRelease(data);
        
        auto rect = CGRectMake(0, 0, tmp.getSizeX()/scale, tmp.getSizeY()/scale);
        CGContextDrawImage(cg, rect, image);
        CGImageRelease (image);
    }
#endif

    void setUpdateRate(unsigned msTick)
    {
        // remove previous timer if any
        removeTimer();

        if(!msTick) return;

        // create a timer
        timer = [NSTimer
            timerWithTimeInterval:msTick * 0.001
            target:sysView
            selector:@selector(timerInterval:)
            userInfo:nil
            repeats:YES
            ];
        // then add it all run loop common modes
        // this way it runs even when resizing window, etc
        [[NSRunLoop currentRunLoop]
            addTimer:timer forMode:NSRunLoopCommonModes];
    }

    void removeTimer()
    {
        // remove timer, so hopefully we won't get more events
        if(timer)
        {
            [timer invalidate];
            timer = 0;
        }
    }

    void doTimerUpdate()
    {
        updateWindowTimeDelta();
       	updateAllChildren();
    }

    void removeTrackingArea()
    {
        if(tracking)
        {
            [sysView removeTrackingArea:tracking];
            [tracking release];
            tracking = 0;
        }
    }

    void setTrackingArea()
    {
        // remove any previous area
        removeTrackingArea();

        // create a new one :p
        //
        // With "first responder" only we don't get exits when mouse
        // doesn't exit area, but only moves on top of another window.
        // So we use "AlwaysActive" and manually filter in mouseMoved: below.
        NSTrackingAreaOptions options = NSTrackingActiveAlways
            | NSTrackingInVisibleRect
            | NSTrackingMouseEnteredAndExited
            | NSTrackingMouseMoved;

        tracking = [[NSTrackingArea alloc] initWithRect:[sysView bounds]
            options:options
            owner:sysView
            userInfo:nil];

        [sysView addTrackingArea:tracking];
    }

    MouseEvent buildMouseEvent(NSView * view,
        NSEvent*event, uint8_t btn, uint8_t nClick)
    {
        NSPoint p = [view convertPoint:[event locationInWindow] fromView:nil];

        updateKeymods();
        return MouseEvent(MouseEvent::tMove,
            int(p.x*scaleFactor / 96),
            int(p.y*scaleFactor / 96),
            btn, nClick, keymods);
    }

    void closeWindow()
    {
        removeTrackingArea();
        removeTimer();

        // do we have a window?
        if([[sysView window] delegate] == sysView)
        {
            [[sysView window] close];
        }
        else
        {
            [sysView removeFromSuperview];
        }

        // Cocoa likes to delay the actual release (a lot) so make sure
        // that all the application frames are closed in a timely manner
        removeAllChildren();
    }

    void setTitle(const char * txt)
    {
        // only set the title if it's our top-level window
        if(sysView == [[sysView window] delegate])
        {
            [[sysView window] setTitle:[NSString stringWithUTF8String:txt]];
        }
    }

    struct CocoaMenu : public Menu
    {
        CocoaWindow             *frame;
        std::function<void(int)> onSelect;

        NSMenu  *menu;

        void addItem(const char * txt, unsigned _id, bool enabled, bool tick)
        {
            NSMenuItem * item = [menu
                addItemWithTitle:
                [NSString stringWithCString:txt encoding:NSUTF8StringEncoding]
                action:@selector(menuItemSelected:)
                keyEquivalent:@""];

            [item setTag:_id];
            [item setState:(tick?NSOnState:NSOffState)];
            [item setEnabled:enabled];
            [item setTarget:frame->sysView];
        }

        void addSeparator()
        {
            [menu addItem:[NSMenuItem separatorItem]];
        }

        void activate(int frameX, int frameY, bool alignRight)
        {
            NSPoint p;
            p.x = frameX * 96 / frame->scaleFactor;
            p.y = frameY * 96 / frame->scaleFactor;

            frame->activeMenu = this;

            // this seems to run a modal loop
            [menu popUpMenuPositioningItem:nil
                atLocation:p inView:frame->sysView];

            // activating a menu ends dragging operation
            // because we won't be getting the rest of it
            frame->cancelDrag();
            
            release();  // is this fine?
        }

        void release()
        {
            frame->activeMenu = 0;

            [menu release];
            delete this;
        }
    } * activeMenu; // we track the active menu directly?

    Menu * createMenu(const std::function<void(int)> & onSelect)
    {
        CocoaMenu * menu = new CocoaMenu;
        menu->frame = this;
        menu->onSelect = onSelect;
        menu->menu = [NSMenu new];
        [menu->menu setAutoenablesItems:NO];

        return menu;
    }

    // NSAlert is what we want for basic popups
    void confirmClose(Notify save, Notify close, Notify cancel)
    {
        NSAlert * alert = [[NSAlert alloc] init];
        [alert setMessageText:@"Save changes before closing?"];
        [alert setInformativeText:
        @"Your changes will be lost if you close without saving."];
        [alert addButtonWithTitle:@"Save"];
        [alert addButtonWithTitle:@"Cancel"];
        [alert addButtonWithTitle:@"Don't Save"];
    
        [alert beginSheetModalForWindow:[sysView window] completionHandler:
        ^(NSInteger result)
        {
            // make sure to finish animation
            // otherwise a second sheet will make a mess
            [[alert window] orderOut:nil];
            switch(result)
            {
                case NSAlertFirstButtonReturn: save(); break;
                case NSAlertThirdButtonReturn: close(); break;
                default: cancel(); break; // some sort of cancel
            }
        }];
    }
    
    // return true if success, replace out with selected path
    void saveAsDialog(std::string & path,
        Notify save, Notify cancel, const char * initPath)
    {
        NSSavePanel * panel = [NSSavePanel savePanel];

        if(initPath)
        {
            NSString * str = [NSString stringWithUTF8String:initPath];
            [panel setDirectoryURL:[NSURL fileURLWithPath:str]];
        }

        [panel beginSheetModalForWindow:[sysView window] completionHandler:
        ^(NSInteger result)
        {
            if(result == NSFileHandlingPanelOKButton)
            {
                NSURL * url = [panel URL];
        
                // we don't touch non-files
                if([url isFileURL] != YES) { cancel(); return; }
        
                // store c-string filename to output
                path = [url fileSystemRepresentation];
        
                save();
            }
            else
            {
                cancel();
            }
        }];
    }
};

Window * dust::createWindow(WindowDelegate & delegate,
    void * parent, int w, int h)
{
    return new CocoaWindow(delegate, parent, w, h);
}

@implementation DUSTWrapperView

-(void)setSystemFrame:(void*)frame
{
    _sysFramePtr = frame;
}

-(BOOL)windowShouldClose:(id)sender
{
    return sysFrame->delegate.win_closing() ? YES : NO;
}

-(void)menuItemSelected:(id)sender
{
    if(sysFrame->activeMenu)
        sysFrame->activeMenu->onSelect((unsigned)[sender tag]);
}

-(void)viewWillMoveToWindow:(NSWindow*)window
{
    if(window)
    {
        // make sure we get mouse movement messages
        [window setAcceptsMouseMovedEvents:YES];

        // setup the update timer
        sysFrame->setUpdateRate(1000 / 30);
    }
    else
    {
        // Cocoa likes to delay the actual release (a lot) so make sure
        // that all the application frames are closed in a timely manner
        sysFrame->removeAllChildren();

        // need these to clear references
        sysFrame->removeTimer();
        sysFrame->removeTrackingArea();

        sysFrame->delegate.win_closed();
        
        // Clang treats [sysFrame] as invalid Obj-C method call
        // so we need to use a temporary to capture in a C++ lambda
        {
            auto * window = (Window*)sysFrame;
            sysFrame->broadcastAutomation(dia::all,
                [window] (DiaWindowClient * c)
                {
                    c->dia_closed(window);
                });
        }
#if DUST_USE_OPENGL
        // drain components with OpenGL context active
        NSOpenGLContext * glcOld = [NSOpenGLContext currentContext];

        [[self openGLContext] makeCurrentContext];
#endif
        ComponentSystem::destroyComponents(sysFrame);

#if DUST_USE_OPENGL
        if(glcOld == nil) [NSOpenGLContext clearCurrentContext];
        else [glcOld makeCurrentContext];
#endif
    }
}

- (NSRect)window:(NSWindow *)window 
willPositionSheet:(NSWindow *)sheet 
       usingRect:(NSRect)rect
{
    // force sheets to below titlebar, even if we mess with that
    // funky, because of our flipped geometry
    rect.origin.y = [window contentLayoutRect].size.height;
    return rect;
}

-(void)windowWillClose:(NSNotification *)notification
{
    // if we get this message, we are the delegate for the window
    // clear the delegate to avoid problems once the view is removed
    [[self window] setDelegate:nil];
    [[self window] setContentView:nil];
}

-(void)dealloc
{
    [super dealloc];
}

-(void)timerInterval:(NSTimer*)timer
{
    // sanity check that we still have a frame
    // sometimes we can get timer calls after invalidation
    if(!sysFrame) return;

#if DUST_USE_OPENGL
    NSOpenGLContext * glcOld = [NSOpenGLContext currentContext];
    [[self openGLContext] makeCurrentContext];
#endif

    sysFrame->doTimerUpdate();

#if DUST_USE_OPENGL
    int e = glGetError();
    if(e) debugPrint("GL error: %d\n", e);

    if(glcOld == nil) [NSOpenGLContext clearCurrentContext];
    else [glcOld makeCurrentContext];
#endif
    
    // always check if we need to repaint
    if(sysFrame->needsRepaint())
    {
        [self setNeedsDisplay:TRUE];

        // FIXME: not sure why we had this here, leave it as comment for now
        // in case there are regressions; it seems we should take it out though
        // 'cos it leads to an extra draw as it doesn't seem to clear the flag
        //[self displayIfNeeded];
    }
}

-(void)updateTrackingAreas
{
    // sanity check stylemask
    auto style = [[self window] styleMask];
    sysFrame->titleBar.setEnabled(!(style & NSFullScreenWindowMask));
    
    sysFrame->setTrackingArea();
    [super updateTrackingAreas];
}

#if DUST_USE_OPENGL
-(void)prepareOpenGL
{
    [super prepareOpenGL];

    // this really doesn't seem to work?
    GLint swapInt = 0;
    [[self openGLContext] setValues:&swapInt forParameter:NSOpenGLCPSwapInterval];
}

-(void)clearGLContext
{
    [super clearGLContext];

    delete sysFrame;
    _sysFramePtr = 0;
}
#endif

-(void)drawRect:(NSRect)rect
{
    // do this viewport setup magic here, so sysFrame doesn't need to worry
    NSRect backingBounds = [self convertRectToBacking:[self bounds]];

    GLsizei backingPixelWidth  = (GLsizei)(backingBounds.size.width),
            backingPixelHeight = (GLsizei)(backingBounds.size.height);

#if DUST_USE_OPENGL
    NSOpenGLContext * glcOld = [NSOpenGLContext currentContext];
    [[self openGLContext] makeCurrentContext];
#endif
    sysFrame->layoutAndPaintWindow(backingPixelWidth, backingPixelHeight);
#if DUST_USE_OPENGL

    [[self openGLContext] flushBuffer];

    int e = glGetError();
    if(e) debugPrint("GL error: %d\n", e);

    if(glcOld == nil) [NSOpenGLContext clearCurrentContext];
    else [glcOld makeCurrentContext];

#endif
}

-(void)viewDidChangeBackingProperties
{
    sysFrame->updateScaleFactor();

    // work-around for 10.10 bug with layered views
    [super viewDidChangeBackingProperties];
    self.layer.contentsScale = self.window.backingScaleFactor;
}

-(void)scrollWheel:(NSEvent*)event
{
    MouseEvent ev = sysFrame->buildMouseEvent(self, event, 0, 0);
    // This is a pure heuristic
    ev.type = MouseEvent::tScroll;
    ev.scrollX = event.scrollingDeltaX * sysFrame->scaleFactor / 96.f;
    ev.scrollY = event.scrollingDeltaY * sysFrame->scaleFactor / 96.f;;
    if(event.hasPreciseScrollingDeltas == NO)
    {
    ev.scrollX *= 10;
    ev.scrollY *= 10;
    }
    sysFrame->sendMouseEvent(ev);
}

-(void)mouseEntered:(NSEvent*)event
{
    MouseEvent ev = sysFrame->buildMouseEvent(self, event, 0, 0);
    sysFrame->sendMouseEvent(ev);
}

-(void)mouseExited:(NSEvent*)event
{
    sysFrame->sendMouseExit();
}

-(void)mouseDown:(NSEvent*)event
{
    if(sysFrame->titleBar.getParent()
    && ([self convertPoint:[event locationInWindow] fromView:nil].y
        < sysFrame->titleBar.size))
    {
        if(event.clickCount >= 2) [[self window] performZoom:nil];
        // NOTE: This is 10.11+ but earlier should fail silent?
        else [[self window] performWindowDragWithEvent:event];
        return;
    }
    
    MouseEvent ev = sysFrame->buildMouseEvent(
        self, event, 1, [event clickCount]);

    ev.type = MouseEvent::tDown;
    sysFrame->sendMouseEvent(ev);
}
-(void)mouseUp:(NSEvent*)event
{
    MouseEvent ev = sysFrame->buildMouseEvent(
        self, event, 1, [event clickCount]);
    ev.type = MouseEvent::tUp;
    sysFrame->sendMouseEvent(ev);
}
-(void)mouseDragged:(NSEvent*)event
{
    MouseEvent ev = sysFrame->buildMouseEvent(
        self, event, 1, [event clickCount]);
    sysFrame->sendMouseEvent(ev);
}

-(void)rightMouseDown:(NSEvent*)event
{
    MouseEvent ev = sysFrame->buildMouseEvent(
        self, event, 2, [event clickCount]);
    ev.type = MouseEvent::tDown;
    sysFrame->sendMouseEvent(ev);
}
-(void)rightMouseUp:(NSEvent*)event
{
    MouseEvent ev = sysFrame->buildMouseEvent(
        self, event, 2, [event clickCount]);
    ev.type = MouseEvent::tUp;
    sysFrame->sendMouseEvent(ev);
}
-(void)rightMouseDragged:(NSEvent*)event
{
    MouseEvent ev = sysFrame->buildMouseEvent(
        self, event, 2, [event clickCount]);
    sysFrame->sendMouseEvent(ev);
}

-(void)mouseMoved:(NSEvent*)event
{
    // This magic checks whether there is another window on top of us.
    // We can't simply ask for "first responder" events, because then
    // we won't get the "exit" messages when moving onto another window.
    //
    if([NSWindow windowNumberAtPoint:[NSEvent mouseLocation]
        belowWindowWithWindowNumber:0] != [[self window] windowNumber])
    {
        sysFrame->sendMouseExit();
    }
    else
    {
        MouseEvent ev = sysFrame->buildMouseEvent(self, event, 0, 0);
        sysFrame->sendMouseEvent(ev);
    }
}

-(void)windowDidBecomeKey:(NSNotification *)notification
{
    sysFrame->delegate.win_activate(true);
    if(sysFrame->getFocus()) sysFrame->getFocus()->ev_focus(true);
}

-(void)windowDidResignKey:(NSNotification *)notification
{
    if(sysFrame->getFocus()) sysFrame->getFocus()->ev_focus(false);
    sysFrame->delegate.win_activate(false);
}

// we don't get regular keyevents for modifiers, so translate
- (void)flagsChanged:(NSEvent*)event
{
    // We CAN get [event keyCode] but we'll need to figure out
    // whether the key was pressed or released..
    auto keyCode = [event keyCode];
    unsigned char keyMap[16];
    GetKeys((BigEndianUInt32*) &keyMap);
    bool pressed = (0 != ((keyMap[ keyCode >> 3] >> (keyCode & 7)) & 1));
    
    if(pressed) [self keyDown:event]; else [self keyUp:event];
}

- (void)keyDown:(NSEvent *)event
{
    // map the raw keycode to a portable scancode and dispatch
    // also hide cursor (note: the OS sometimes ignores this)
    //
    // if we don't have a focus frame, then don't bother
    if(sysFrame->getFocus())
    {
        sysFrame->updateKeymods();
        auto vk = scancode_table_osx[[event keyCode]];
        if(KBGetLayoutType(LMGetKbdType()) == kKeyboardISO)
        {
            // need to swap grave and non-us-backslash
            switch(vk)
            {
            case SCANCODE_GRAVE: vk = SCANCODE_NONUSBACKSLASH; break;
            case SCANCODE_NONUSBACKSLASH: vk = SCANCODE_GRAVE; break;
            default: break;
            }
        }
        sysFrame->sendKey(vk, true, sysFrame->keymods);
        [NSCursor setHiddenUntilMouseMoves:YES];
    }

#if 1
    // if command is pressed, ignore all key input
    // not sure if this is safe for all layouts but
    // it means anything with cmd- is safe for shortcuts
    if(sysFrame->keymods & KEYMOD_CMD) return;

    // here be demons.. should really figure out how to use the
    // actual NSTextInput protocol to avoid this mess
    TISInputSourceRef currentKeyboard = TISCopyCurrentKeyboardInputSource();
    CFDataRef layoutData = (CFDataRef) TISGetInputSourceProperty(
        currentKeyboard, kTISPropertyUnicodeKeyLayoutData);
    const UCKeyboardLayout *keyboardLayout =
        (const UCKeyboardLayout *)CFDataGetBytePtr(layoutData);

    CGEventFlags flags = [event modifierFlags];
    
    // apparently one has to do this manually per key
    UInt32 modKeyState = 0;
    if (flags & NSShiftKeyMask) modKeyState |= shiftKey;
    if (flags & NSAlphaShiftKeyMask) modKeyState |= alphaLock;
    if (flags & NSAlternateKeyMask) modKeyState |= optionKey;
    modKeyState = (modKeyState >> 8) & 0xFF;

    const size_t unicodeStringLength = 16;
    UniChar unicodeString[unicodeStringLength];
    UniCharCount charCount;

    OSStatus err = UCKeyTranslate(keyboardLayout,
       [event keyCode],
       kUCKeyActionDown,
       modKeyState,
       LMGetKbdType(),
       0,
       &sysFrame->deadKeyState,
       unicodeStringLength,
       &charCount,
       unicodeString);

    CFRelease(currentKeyboard);

    if(err != noErr || !charCount) return;

    if(!sysFrame->getFocus()) return;

    NSString * nsstr = (NSString*) CFStringCreateWithCharacters(
        kCFAllocatorDefault, unicodeString, charCount);

    // send text event if it doesn't look like a control character
    const char * text = [nsstr UTF8String];

    // if string contains control characters, then ignore the whole string
    // effectively ignoring a dead-key followed by something like an arrow
    bool clean = true;
    for(auto * p = text; *p; ++p)
    {
        if(*p == 0x7f || *(uint8_t*)(p) < 0x20) clean = false;

    }
    if(clean) sysFrame->sendText(text);
    
    [nsstr release];
#else
    // after processing the raw scan-code above, go through
    // interpretKeyEvents: to get an insertText: call
    [self interpretKeyEvents:[NSArray arrayWithObject:event]];
#endif
}

- (void)keyUp:(NSEvent *)event
{
    // basically the same thing as keyDown: except release event
    sysFrame->updateKeymods();
    
    auto vk = scancode_table_osx[[event keyCode]];
    if(KBGetLayoutType(LMGetKbdType()) == kKeyboardISO)
    {
        // need to swap grave and non-us-backslash
        switch(vk)
        {
        case SCANCODE_GRAVE: vk = SCANCODE_NONUSBACKSLASH; break;
        case SCANCODE_NONUSBACKSLASH: vk = SCANCODE_GRAVE; break;
        default: break;
        }
    }
    sysFrame->sendKey(vk, false, sysFrame->keymods);
}

// this is called by [self interpretKeyEvents:] above
- (void)insertText:(id)string
{
    // if we don't have focus frame, then doing any of this is futile
    if(!sysFrame->getFocus()) return;

    // we can get NSString or NSAttributedString and we want to
    // always convert the latter back to the former
    if ([string isKindOfClass:[NSAttributedString class]])
    {
        string = [string string];
    }
    NSString * nsstr = (NSString*) string;

    // then we want to convert the NSString to raw utf-8 C-string
    const char * text = [nsstr UTF8String];

    // then check that there is at least one character
    if(*text) sysFrame->sendText(text);
}

- (NSArray *)validAttributesForMarkedText
{
    DUST_TRACE
    return [NSArray array];
}

- (void)setMarkedText:(id)string
        selectedRange:(NSRange)selRange
{
    DUST_TRACE
    [self insertText:string];
}

// this is called by NSTextInput stuff when we have some type of
// special text-input thing other than insertText: and we catch
// it for the primary purpose of not having the system beep! :)
- (void)doCommandBySelector:(SEL)selector
{
    // don't beep :)
}

// put y=0 on top, saves us manual remapping
-(BOOL)isFlipped
{
    return YES;
}

// another piece of useless Apple bullshit
-(BOOL)acceptsFirstResponder
{
    return YES;
}

// and this never ends
-(BOOL)acceptsFirstMouse:(NSEvent *)theEvent
{
    return YES;
}

@end // implementation


///////////////////
/// APPLICATION ///
///////////////////

// we need a delegate to delay activation of the application
@interface DUSTAppDelegate : NSObject<NSApplicationDelegate>
{
    // this is only used to call app_startup() after
    // we get the applicationDidFinishLaunching notify
    Application * cppApplication;
}
    -(void) setCppApplication:(Application *)app;
@end

// once we get this message, we can activate
@implementation DUSTAppDelegate

id <NSObject> _activity;

-(void)setCppApplication:(Application *)app
{
    cppApplication = app;
}
-(void)applicationDidFinishLaunching:(NSNotification *)aNotification
{
    NSActivityOptions options = NSActivityUserInitiatedAllowingIdleSystemSleep;
    NSString *reason = @"Real-time sensitive";
    _activity = [[[NSProcessInfo processInfo]
        beginActivityWithOptions:options reason:reason] retain];
    [NSApp activateIgnoringOtherApps:YES];
    cppApplication->app_startup();
}
-(void)applicationWillTerminate:(NSNotification *)notification {
    [[NSProcessInfo processInfo] endActivity:_activity];
    _activity = nil;
}
@end


struct dust::PlatformData
{
    NSAutoreleasePool   *nsPool;
    NSApplication       *nsApp;

    struct CoreAudio
    {
        AudioComponent           out_c;
        AudioComponentInstance   out_i;
        AURenderCallbackStruct   proc;
    } audio;
};

// application wrappers
void Application::platformInit()
{
    platformData = new PlatformData;

    platformData->nsPool = [NSAutoreleasePool new];
    platformData->nsApp = [NSApplication sharedApplication];

    // make the application a proper Cocoa app, adding menu-bar etc
    DUSTAppDelegate * appDelegate = [[DUSTAppDelegate new] autorelease];
    [appDelegate setCppApplication:this];

    [platformData->nsApp setDelegate:appDelegate];
    [platformData->nsApp setActivationPolicy:NSApplicationActivationPolicyRegular];

    // create a minimal application menu, with Quit item
    id menubar = [[NSMenu new] autorelease];
    id appMenuItem = [[NSMenuItem new] autorelease];
    [menubar addItem:appMenuItem];
    [platformData->nsApp setMainMenu:menubar];
    id appMenu = [[NSMenu new] autorelease];
    id appName = [[NSProcessInfo processInfo] processName];
    id quitTitle = [@"Quit " stringByAppendingString:appName];
    id quitMenuItem = [[[NSMenuItem alloc]
                        initWithTitle:quitTitle
                        action:@selector(terminate:)
                        keyEquivalent:@""] autorelease];
    [appMenu addItem:quitMenuItem];
    [appMenuItem setSubmenu:appMenu];

    // deal with Cocoa swallowing some key combinations
    NSEvent* (^anti_keyblock)(NSEvent*) = ^ NSEvent* (NSEvent* event)
    {
        NSEventModifierFlags modifierFlags = [event modifierFlags]
            & NSDeviceIndependentModifierFlagsMask;
        if(event.keyCode == kVK_ANSI_Period
        && modifierFlags == NSCommandKeyMask)
        {
            if(event.type == NSKeyDown) [[NSApp keyWindow].contentView keyDown:event];
            if(event.type == NSKeyUp) [[NSApp keyWindow].contentView keyUp:event];
        }
        return event;
    };
    [NSEvent addLocalMonitorForEventsMatchingMask:NSKeyUpMask|NSKeyDownMask
        handler:anti_keyblock];
}

void Application::platformClose()
{
    setAudioCallback(0);

    [platformData->nsPool release];
}

void Application::exit()
{
    setAudioCallback(0);
    
    [platformData->nsApp stop:nil];
}

void Application::run()
{
    [NSApp run];
}

OSStatus wrapperAudioProc(void *inRefCon,
    AudioUnitRenderActionFlags *ioActionFlags,
    const AudioTimeStamp *timeStamp,
    UInt32 bus, UInt32 nFrames, AudioBufferList *ioData)
{
    //// This is very spammy
    //debugPrint("AUDIO: bus: %d, nFrames: %d, nBuffers: %d\n",
    //    bus, nFrames, ioData->mNumberBuffers);

    // there should only ever be one buffer, but clear any
    // just in case something really weird is going on
    for (unsigned i = 0; i < ioData->mNumberBuffers; ++i)
    {
        AudioBuffer & buf = ioData->mBuffers[i];
        memset(buf.mData, 0, buf.mDataByteSize);
    }

    // call the render callback
    AudioCallback * callback = ((Application*)inRefCon)->getAudioCallback();
    // sanity check stuff
    if(callback)
    {
        AudioBuffer & buf = ioData->mBuffers[0];
        callback->audioRender((float*) buf.mData, nFrames);
    }
    return 0;
}

void Application::platformAudioInit()
{
    auto & audio = platformData->audio;

    // First setup default audio output component
    AudioComponentDescription   desc;
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_DefaultOutput;
    desc.componentFlags = 0;
    desc.componentFlagsMask = 0;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;

    audio.out_c = AudioComponentFindNext(0, &desc);
    if(!audio.out_c)
    {
        debugPrint("Failed to find default audio output component.\n");
        return;
    }

    if(AudioComponentInstanceNew(audio.out_c, &audio.out_i))
    {
        debugPrint("Failed to create default audio output component.\n");
        return;
    }

    // Second setup audio stream description - to keep this simple
    // we only support one format which is 44100 stereo float
    AudioStreamBasicDescription asbd;
    asbd.mSampleRate = 44100;
    asbd.mFormatID = kAudioFormatLinearPCM;
    asbd.mFormatFlags = kAudioFormatFlagIsFloat;
    asbd.mFramesPerPacket = 1;
    asbd.mChannelsPerFrame = 2;
    asbd.mBitsPerChannel = 8 * sizeof(float);
    asbd.mBytesPerPacket = asbd.mChannelsPerFrame * sizeof(float);
    asbd.mBytesPerFrame = asbd.mChannelsPerFrame * sizeof(float);
    asbd.mReserved = 0;

    if(AudioUnitSetProperty(audio.out_i, kAudioUnitProperty_StreamFormat,
        kAudioUnitScope_Input, 0, &asbd, sizeof(asbd)))
    {
        debugPrint("Failed to set stream format property.\n");
        return;
    }

    audio.proc.inputProc = wrapperAudioProc;
    audio.proc.inputProcRefCon = (void*) this;

    if(AudioUnitSetProperty(audio.out_i, kAudioUnitProperty_SetRenderCallback,
        kAudioUnitScope_Input, 0, &audio.proc, sizeof(audio.proc)))
    {
        debugPrint("Failed to set audio callback.\n");
        return;
    }

    if(AudioUnitInitialize(audio.out_i))
    {
        debugPrint("Failed to initialize audio.\n");
        return;
    }

    if(AudioOutputUnitStart(audio.out_i))
    {
        debugPrint("Failed to start playback.\n");
        return;
    }

    debugPrint("AUDIO: started\n");
}

void Application::platformAudioClose()
{
    auto & audio = platformData->audio;

    AudioOutputUnitStop(audio.out_i);
    AudioComponentInstanceDispose(audio.out_i);
    debugPrint("AUDIO: stopped\n");
}


void * dust::getModuleBundleURL()
{
    NSBundle * nsb = [NSBundle bundleForClass:[DUSTWrapperView class]];
    return (void*) CFBridgingRetain([nsb bundleURL]);
}
