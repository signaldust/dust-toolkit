
#ifdef _WIN32    // this file is only relevant on windows

// enables styles .. both lines are required
#define ISOLATION_AWARE_ENABLED 1
#pragma comment(linker,"/manifestdependency:\"type='win32' "\
    "name='Microsoft.Windows.Common-Controls' "\
    "version='6.0.0.0' processorArchitecture='*' "\
    "publicKeyToken='6595b64144ccf1df' language='*'\"")

#include <windows.h>
#include <commctrl.h>

#if DUST_USE_OPENGL
# include "GL/gl3w.h"
#endif

#include "window.h"
#include "key_scancode_win.h"

static HINSTANCE hInstance = 0;

// if we're compiling a DLL, then this sets up the correct hInstance
extern "C" DUST_EXPORT BOOL WINAPI DllMain(
    HINSTANCE hInst, DWORD dwReason, LPVOID lpReserved)
{
    if(dwReason == DLL_PROCESS_ATTACH) { hInstance = hInst; }
	return TRUE;
}

static inline unsigned getAsyncMods()
{
    unsigned mods = 0;
    if ( GetAsyncKeyState( VK_SHIFT ) & 0x8000 ) mods |= dust::KEYMOD_SHIFT;
    if ( GetAsyncKeyState( VK_RCONTROL ) & 0x8000 ) mods |= dust::KEYMOD_CTRL;
    if ( GetAsyncKeyState( VK_LMENU ) & 0x8000 ) mods |= dust::KEYMOD_ALT;
    // do not allow LCONTROL if RMENU is pressed
    // this avoids AltGR = ctrl+alt in terms of shortcuts
    if ( GetAsyncKeyState( VK_RMENU ) & 0x8000 ) mods |= dust::KEYMOD_ALT;
    else if ( GetAsyncKeyState( VK_LCONTROL ) & 0x8000 ) mods |= dust::KEYMOD_CTRL;
    return mods;
}

// ugly low-level wheel hook.. but well.. yeah it's kinda necessary
// because otherwise we never get wheel events without keyboard focus
static HHOOK hWheelHook;
volatile static unsigned mouseHookActive = 0;
static HINSTANCE mouseHookInstance = 0;
static LRESULT CALLBACK wheelHookProc(int nCode, WPARAM wParam, LPARAM lParam) {

    // for nCode<0 we must call CallNextHookEx() but actually you want that
    // every time you don't actually want to intercept something
    if(nCode == HC_ACTION && wParam == WM_MOUSEWHEEL) {
        MSLLHOOKSTRUCT  * hs = (MSLLHOOKSTRUCT  *) lParam;

        // get window under cursor
        HWND hwnd = WindowFromPoint(hs->pt);
        // if there's one, and it matches the plugin DLL hInstance, redirect
        if(hwnd && (HINSTANCE) GetWindowLongPtr(hwnd, GWLP_HINSTANCE) == ::mouseHookInstance) {
            // PostMessage a custom wheel message here
            // hs->pt gives you the mousepoint
            // hs->mousedata hiword has the usual signed scroll amount
            // below is a very quick&dirty emulation of normal message
            // just redirected
            PostMessage(hwnd, WM_MOUSEWHEEL,
                hs->mouseData & 0xffff0000, // low-word is undefined
                (hs->pt.x&0xffff) | hs->pt.y<<16);
            // finally return non-zero to skip normal processing
            return 1;
        }
    }

    return CallNextHookEx(hWheelHook, nCode, wParam, lParam);
}

struct Win32WheelHook
{
    void installHook(HWND hwnd)
    {
        // Don't install a hook if there's a debugger (freezes stuff on break)
        // and don't bother if this is a top-level window (don't need one).
        if(mouseHookActive || IsDebuggerPresent()
        || !(GetWindowLong(hwnd, GWL_STYLE) & WS_CHILD)) return;
        
        mouseHookInstance = (HINSTANCE) GetWindowLongPtr(hwnd, GWLP_HINSTANCE);
        hWheelHook = SetWindowsHookEx( WH_MOUSE_LL, 
            wheelHookProc, mouseHookInstance, 0);
        ++mouseHookActive;
    }
    
    void removeHook()
    {
        if(mouseHookActive && !--mouseHookActive)
        {
            UnhookWindowsHookEx(hWheelHook);
        }
    }

} wheelHook;

struct Win32Callback
{
    virtual ~Win32Callback() {}
    virtual LRESULT callback(HWND, UINT, WPARAM, LPARAM) = 0;
};

static const char winClassName[] = "Win32Wrapper";
static LRESULT CALLBACK wrapperWinProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	
    auto ctl = (Win32Callback *) GetWindowLongPtrA(hwnd, GWLP_USERDATA);
	
	return ctl ? ctl->callback(hwnd, msg, wParam, lParam)
        : DefWindowProcA(hwnd, msg, wParam, lParam);
}

// automatic singleton to register/unregister windowclass on load/unload
static struct WindowClass
{
    uintptr_t winClassAtom;

    WindowClass()
    {
        winClassAtom = 0;
        hInstance = 0;
    }

    void registerClass()
    {
        if(winClassAtom) return;

        WNDCLASSEXA wc;

        ZeroMemory(&wc, sizeof(WNDCLASSEXA));

        wc.cbSize        = sizeof(WNDCLASSEXA);
        wc.style         = 0; // CS_DBLCLKS;
        wc.lpfnWndProc   = wrapperWinProc;
        wc.cbClsExtra    = 0;
        wc.cbWndExtra    = 0;
        wc.hInstance     = hInstance;
        wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = NULL;
        wc.lpszMenuName  = NULL;
        wc.lpszClassName = (LPSTR) winClassName;
        wc.hIconSm       = LoadIcon(NULL, IDI_APPLICATION);

        winClassAtom = RegisterClassExA(&wc);
        if(!winClassAtom)
        {
            MessageBoxA(NULL, "Couldn't create a wrapper windowclass.",
                "Error!", MB_ICONEXCLAMATION | MB_OK);
        }

        INITCOMMONCONTROLSEX icc;
        icc.dwSize = sizeof(icc);
        icc.dwICC = ICC_WIN95_CLASSES;
        // also init common controls here
        InitCommonControlsEx(&icc);

    }

    ~WindowClass()
    {
        if(winClassAtom)
        {
            UnregisterClassA((LPSTR) winClassAtom, hInstance);
        }

        // when unloading the class
        if(mouseHookActive) UnhookWindowsHookEx(hWheelHook);
    }

} windowClass;

#if DUST_USE_OPENGL
namespace {
    struct GLContext
    {
        HDC     hOldDC;
        HGLRC   hOldRC;
    
        GLContext(HDC hdc, HGLRC hglrc)
        {
            hOldDC = wglGetCurrentDC();
            hOldRC = wglGetCurrentContext();

            wglMakeCurrent(hdc, hglrc);
        }
    
        ~GLContext()
        {
            // always clear whatever errors we might have caused
            glGetError();
            wglMakeCurrent(hOldDC, hOldRC);
        }
    };
};

// this is not in our glext.h :(
typedef BOOL (APIENTRY * PFNGLWGLSWAPINTERVALEXTPROC) (GLint interval);

#endif

using namespace dust;

struct Win32Window : Window, Win32Callback
{
    WindowDelegate & delegate;

    HWND hwnd;
    HDC hdc;    // set in WM_PAINT (FIXME: in constructor for GL?)

#if DUST_USE_OPENGL
    HGLRC    hglrc;
#endif
    
    // size info as set by client
    unsigned    minSizeX, minSizeY;

    Win32Window(WindowDelegate & delegate, void *parent, int w, int h)
        : delegate(delegate)
    {
		// initial default
		minSizeX = w;
		minSizeY = h;
		
        // might need to adjust these
        DWORD ex_style = 0;
        DWORD style = WS_CLIPCHILDREN;

        if(parent) style |= WS_CHILD | WS_VISIBLE;
        else style |= WS_OVERLAPPEDWINDOW;

        windowClass.registerClass();
        hwnd = CreateWindowExA(ex_style,
            (LPSTR)windowClass.winClassAtom,
            "<unnamed-window>", style,
            CW_USEDEFAULT, CW_USEDEFAULT, w, h,
            (HWND) parent, (HMENU) 0, hInstance, 0);

        if(!hwnd) debugBreak(); // should never fail

        SetWindowLongPtrA(hwnd, GWLP_USERDATA, 
			(LONG_PTR) (Win32Callback*) this);
        delegate.win_created();

        // this will fix title-bar
		if(!parent) resize(w, h);
        
		::SetTimer(hwnd, 0, 1000/60, 0);

#if DUST_USE_OPENGL

        hdc = GetDC(hwnd);

        // opengl pixel format
        PIXELFORMATDESCRIPTOR pfd;
        ZeroMemory(&pfd, sizeof(pfd));
        pfd.nSize = sizeof(pfd);
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 24;
        pfd.cAlphaBits = 8;
        pfd.cDepthBits = 24;    // can we get stencil without depth bits?
        pfd.cStencilBits = 8;   // want stencil bits
        
        int format;
        format = ChoosePixelFormat(hdc, &pfd);
        SetPixelFormat(hdc, format, &pfd);
    
        hglrc = wglCreateContext(hdc);

        GLContext context(hdc, hglrc);
        PFNGLWGLSWAPINTERVALEXTPROC wglSwapInterval
        = (PFNGLWGLSWAPINTERVALEXTPROC) wglGetProcAddress("wglSwapIntervalEXT");
        wglSwapInterval(0);
        
        gl3wInit();
#endif
    }

    ~Win32Window()
    {
        if(activeMenu) { delete activeMenu; activeMenu = 0; }
        
        removeAllChildren();
        delegate.win_closed();

        wheelHook.removeHook();

        {
#if DUST_USE_OPENGL
            GLContext   context(hdc, hglrc);
#endif
            // drain components while we have our GL context (if any)
            ComponentSystem::destroyComponents(this);
        }
#if DUST_USE_OPENGL
        wglDeleteContext(hglrc);
        ReleaseDC(hwnd, hdc);
#endif
    }

    LRESULT callback(HWND, UINT, WPARAM, LPARAM);
	
	void closeWindow() { DestroyWindow(hwnd); }
	void * getSystemHandle() { return (void*) hwnd; }
	void setMinSize(int w, int h) { minSizeX = w; minSizeY = h; }
    
	void resize(int w, int h)
    {
		// we need to adjust for correct client size
		RECT wR, cR;
		GetWindowRect(hwnd, &wR);
		GetClientRect(hwnd, &cR);

		w += wR.right - wR.left - cR.right + cR.left;
		h += wR.bottom - wR.top - cR.bottom + cR.top;
		
		::SetWindowPos(hwnd, 0, 0, 0, w, h,
			SWP_NOMOVE | SWP_NOREPOSITION | SWP_SHOWWINDOW);
    }
	
	void setTitle(const char * txt)
	{
		SetWindowTextA(hwnd, txt);
	}
	
	void confirmClose(
		Notify saveAndClose, Notify close, Notify cancel)
	{
        // need one extra for the null
        std::vector<char> title(GetWindowTextLengthA(hwnd) + 1);
        GetWindowTextA(hwnd, title.data(), title.size());
        
        int id = MessageBoxA(hwnd, "Save changes before closing?", title.data(),
            MB_ICONWARNING | MB_YESNOCANCEL | MB_TASKMODAL);

        switch(id)
        {
        case IDYES: saveAndClose(); break;
        case IDNO: close(); break;
        default: cancel(); break;
        }
	}
	
	void saveAsDialog(std::string & out,
		Notify save, Notify cancel, const char * path = 0)
	{
        char    filename[_MAX_PATH];
        ZeroMemory(filename, sizeof(filename));

        OPENFILENAME ofn;
        ZeroMemory(&ofn, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;

        // filename garbage
        ofn.lpstrFile = filename;
        ofn.nMaxFile = sizeof(filename);

        if(path) strcpy(filename, path);

        ofn.lpstrFileTitle = 0;
        ofn.nMaxFileTitle = 0;

        // filter
        ofn.lpstrFilter = "All files (*.*)\0*.*\0";
        ofn.nFilterIndex = 1;

        ofn.Flags = OFN_PATHMUSTEXIST;
        ofn.Flags |= OFN_OVERWRITEPROMPT;
        ofn.Flags |= OFN_NOCHANGEDIR;

        // if non-zero, user clicked OK
        if(GetSaveFileName(&ofn))
        {
            out = filename;
            save();
        }
        else
        {
            cancel();
        }

	}
	
    struct Win32Menu : Menu
    {
        std::function<void(int)>    onSelect;
        
        Win32Window * win;
        HMENU   hMenu;

        ~Win32Menu() { DestroyMenu(hMenu); }
    
        void addItem(const char * txt, unsigned id, bool enabled, bool tick)
        {
            AppendMenuA(hMenu,
                MF_STRING | (!enabled ? MF_DISABLED : 0) | (tick ? MF_CHECKED : 0),
                id, txt);
        }
    
        void addSeparator()
        {
            AppendMenuA(hMenu, MF_SEPARATOR, 0, 0);
        }
    
        void activate(int frameX, int frameY, bool alignRight)
        {
            // if there was already a menu active, close it
            if(win->activeMenu) { delete win->activeMenu; win->activeMenu = 0; }
            
            POINT p;
            p.x = frameX;
            p.y = frameY;
            ClientToScreen(win->hwnd, &p);
            
            unsigned hAlign = alignRight ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
            unsigned vAlign = TPM_TOPALIGN;
            
            unsigned flags = vAlign | hAlign
                | TPM_VERPOSANIMATION | TPM_RIGHTBUTTON;

            win->cancelDrag();
            
            win->activeMenu = this;
            TrackPopupMenu(hMenu, flags, p.x, p.y, 0, win->hwnd, 0);
        }
    } * activeMenu = 0;
    
	Menu * createMenu(const std::function<void(int)> & onSelect)
	{
        Win32Menu * menu = new Win32Menu();
        menu->onSelect = onSelect;
        menu->win = this;
        menu->hMenu = CreatePopupMenu();
        
        return menu;
	}
	
	void platformBlit(Surface & backBuf)
	{
		// COPY TO SCREEN	
		BITMAPINFO bmi;
		memset(&bmi, 0, sizeof(BITMAPINFOHEADER));
		bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth = backBuf.getPitch();
		bmi.bmiHeader.biHeight = -int(backBuf.getSizeY());
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;

		// can't figure out how to get the update region
		// optimized copy to work, so just hope GDI clips
		SetDIBitsToDevice(hdc, 0, 0, 
			backBuf.getSizeX(), backBuf.getSizeY(),
			0, 0, 0, backBuf.getSizeY(), backBuf.getPixels(), &bmi,
			DIB_RGB_COLORS);
	}
	
	// for now, only allow one mouse button to drag at a time
	unsigned mouseButton = 0;
	
	void startSystemCapture() { SetCapture(hwnd); }
	void endSystemCapture() { if(GetCapture() == hwnd) ReleaseCapture(); }
	
	int nClickBtn = 0;
	int nClicks = 0;
	RECT clickRect;
	DWORD clickTimeMS;

	int getClickCount(int btn, int x, int y)
	{
		POINT pt = { x, y };
		DWORD time = GetMessageTime();

		if (nClickBtn != btn || !PtInRect(&clickRect, pt) 
		|| time - clickTimeMS > GetDoubleClickTime())
		{
			nClicks = 0;
			nClickBtn = btn;
		}

		++nClicks;
		clickTimeMS = time;

		SetRect(&clickRect, x, y, x, y);
		InflateRect(&clickRect,
				  GetSystemMetrics(SM_CXDOUBLECLK) / 2,
				  GetSystemMetrics(SM_CYDOUBLECLK) / 2);
				  
		return nClicks;
	}
};


Window * dust::createWindow(
    WindowDelegate & delegate, void * parent, int w, int h)
{
    return new Win32Window(delegate, parent, w, h);
}

LRESULT Win32Window::callback(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{	
    switch(msg)
    {
    case WM_ACTIVATE:
        {
            nClicks = 0;
            bool active = wParam != WA_INACTIVE;
            delegate.win_activate(active);
            if(getFocus()) getFocus()->ev_focus(active);
            // get focus if we're not losing activation
			if(active) SetFocus(hwnd);
            DefWindowProcA(hwnd, msg, wParam, lParam);
        }
        break;

    case WM_TIMER:
        updateAllChildren();
        if(needsRepaint())
        {
            RedrawWindow(hwnd, 0, 0, RDW_INVALIDATE);
        }
        break;

    case WM_COMMAND:
        {
            if(activeMenu)
            {
                activeMenu->onSelect((int) wParam);
                delete activeMenu;
                activeMenu = 0;
            }                
        }
        break;

    case WM_PAINT:
        // need to do this paintstruct stuff even with OpenGL
        // FIXME: OpenGL though..
        {
#if DUST_USE_OPENGL
            ValidateRect(hwnd, 0);
            GLContext   context(hdc, hglrc);
#else
            PAINTSTRUCT ps;
            hdc = BeginPaint(hwnd, &ps);
            BeginPaint(hwnd, &ps);
#endif
            RECT cRect;
            GetClientRect(hwnd, &cRect);
            int w = cRect.right - cRect.left;
            int h = cRect.bottom - cRect.top;
			
            layoutAndPaint(w, h);
            
#if DUST_USE_OPENGL
            SwapBuffers(hdc);
#else
            EndPaint(hwnd, &ps);
#endif
        }
        break;

    case WM_SIZE: reflowChildren(); RedrawWindow(hwnd, 0, 0, RDW_INVALIDATE); break;

    case WM_GETMINMAXINFO:
    {
        MINMAXINFO * ptr = (MINMAXINFO*) lParam;

        // first init with the desired client area
        ptr->ptMinTrackSize.x = minSizeX;
        ptr->ptMinTrackSize.y = minSizeY;

        // Now .. that's not content size, so adjust by the difference
        // between window and client rectangle
        RECT wR, cR;
        GetWindowRect(hwnd, &wR);
        GetClientRect(hwnd, &cR);

        ptr->ptMinTrackSize.x += wR.right - wR.left - cR.right + cR.left;
        ptr->ptMinTrackSize.y += wR.bottom - wR.top - cR.bottom + cR.top;

    }
    break;
	
    // this is a work-around for some VST hosts (eXT)
    // causing lag issues if this is not done
    case WM_SETCURSOR:
        if(LOWORD(lParam) == HTCLIENT)
        {
            SetCursor(LoadCursor(0, IDC_ARROW));
            return TRUE;
        }
        else return DefWindowProc(hwnd, msg, wParam, lParam);

    case WM_MOUSELEAVE:
        sendMouseExit();
        wheelHook.removeHook();
        break;
		
	case WM_CAPTURECHANGED:
		// cancel drag, then send mouse exit to notify the control
        // this is a situation that can't happen on OSX though?
        cancelDrag();
        sendMouseExit();
		break;

    // FIXME: this is essentially boilerplate for mouse messages
    // so maybe factor it all into one function that does the thing?
    case WM_MOUSEMOVE:
        {
            wheelHook.installHook(hwnd);

            int x = MAKEPOINTS(lParam).x;
            int y = MAKEPOINTS(lParam).y;

            int keymods = getAsyncMods();

            MouseEvent ev(MouseEvent::tMove, x, y, getDragButton(), 0, keymods);
            sendMouseEvent(ev);

            // track WM_MOUSELEAVE
            TRACKMOUSEEVENT tme;
            ZeroMemory(&tme, sizeof(tme));
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;

            TrackMouseEvent(&tme);
        }
        break;

    case WM_LBUTTONDOWN:
        {
            int x = MAKEPOINTS(lParam).x;
            int y = MAKEPOINTS(lParam).y;

            int keymods = getAsyncMods();

            MouseEvent ev(MouseEvent::tDown, x, y, 1, 
				getClickCount(1, x, y), keymods);
            sendMouseEvent(ev);

            SetCapture(hwnd);
        }
        break;

    case WM_LBUTTONUP:
        {
            int x = MAKEPOINTS(lParam).x;
            int y = MAKEPOINTS(lParam).y;

            int keymods = getAsyncMods();

            MouseEvent ev(MouseEvent::tUp, x, y, 1, 0, keymods);
            sendMouseEvent(ev);

            if(!getDragButton()) ReleaseCapture();
        }
        break;

    case WM_RBUTTONDOWN:
        {
            int x = MAKEPOINTS(lParam).x;
            int y = MAKEPOINTS(lParam).y;

            int keymods = getAsyncMods();

            MouseEvent ev(MouseEvent::tDown, x, y, 2, 
				getClickCount(2, x, y), keymods);
            sendMouseEvent(ev);
            
            SetCapture(hwnd);
        }
        break;

    case WM_RBUTTONUP:
        {
            int x = MAKEPOINTS(lParam).x;
            int y = MAKEPOINTS(lParam).y;

            int keymods = getAsyncMods();

            MouseEvent ev(MouseEvent::tUp, x, y, 2, 0, keymods);
            sendMouseEvent(ev);
            
            if(!getDragButton()) ReleaseCapture();
        }
        break;

    case WM_MBUTTONDOWN:
        {
            int x = MAKEPOINTS(lParam).x;
            int y = MAKEPOINTS(lParam).y;

            int keymods = getAsyncMods();

            MouseEvent ev(MouseEvent::tDown, x, y, 3, 
				getClickCount(2, x, y), keymods);
            sendMouseEvent(ev);
            
            SetCapture(hwnd);
        }
        break;

    case WM_MBUTTONUP:
        {
            int x = MAKEPOINTS(lParam).x;
            int y = MAKEPOINTS(lParam).y;

            int keymods = getAsyncMods();

            MouseEvent ev(MouseEvent::tUp, x, y, 3, 0, keymods);
            sendMouseEvent(ev);
            
            if(!getDragButton()) ReleaseCapture();
        }
        break;

    case WM_MOUSEWHEEL:
        {
            // in WM_MOUSEWHEEL (only) these are relative to SCREEN
            POINT p;
            p.x = MAKEPOINTS(lParam).x;
            p.y = MAKEPOINTS(lParam).y;

            ScreenToClient(hwnd, &p);

            // FIXME: modifier keys seem to act funny.. check here
            int keymods = getAsyncMods();
            float delta = GET_WHEEL_DELTA_WPARAM(wParam) / (float) WHEEL_DELTA;

            // in practice, we want to scroll a bit more than a pixel per tick
            delta *= 32;

            MouseEvent ev(MouseEvent::tScroll, p.x,p.y, 0, 0, keymods);
            ev.scrollY = delta;
            sendMouseEvent(ev);
        } break;

    // KEYBOARD EVENTS
    case WM_SYSKEYDOWN:
        {
            // Special handling for syskeydown
            // but only for top-level windows.
            unsigned style = GetWindowLong(hwnd,GWL_STYLE);
            if(!(style & WS_CHILD)
                && (lParam & (1<<29))) // is this really alt?
            {
                // trap alt-f4 and alt-space
                if(wParam == VK_F4)
                {
                    // send WM_CLOSE
                    SendMessage(hwnd, WM_CLOSE, 0, 0);
                    break;
                }

                if(wParam == VK_SPACE)
                {
                    // open system menu
                    // WM_SYSCOMMAND goes to defwindowproc
                    SendMessage(hwnd, WM_SYSCOMMAND,
                        SC_KEYMENU, VK_SPACE);
                    break;
                }
            }
        }   // NOTE: otherwise fall back to unified handling
    case WM_KEYDOWN:
        {
            char ks[256];
            ::GetKeyboardState((PBYTE) ks);

            const int bufSize = 64;
            WCHAR   buf[bufSize];   // probably long enough
            int nChar = (msg == WM_SYSKEYDOWN) ? 0 : ::ToUnicode
                (wParam, ::MapVirtualKey(wParam, MAPVK_VK_TO_VSC),
                (BYTE*) ks, (LPWSTR) buf, bufSize, 0);

            sendKey(decodeWindowsScancode(lParam, wParam),
                true, getAsyncMods());

            if(nChar > 0)
            {
                char bufUtf8[bufSize * 3 + 1];   // at most 3 bytes per WCHAR
                int nUtf8 = ::WideCharToMultiByte(CP_UTF8, 0, buf, nChar,
                    bufUtf8, bufSize * 3, 0, 0);

                // then we need to sanitise the input
                int in = 0, out = 0;
                while(in < nUtf8)
                {
                    // filter anything before ascii space
                    // this is all control characters
                    if(' ' > (unsigned char)bufUtf8[in])
                    {
                        ++in;
                        continue;
                    }
                    // perform a move
                    bufUtf8[out++] = bufUtf8[in++];
                }
                // null-terminate
                bufUtf8[out] = 0;
                // now we can send it to application
                if(out) sendText(bufUtf8);
            }
        }
        break;

    case WM_SYSKEYUP:
    case WM_KEYUP:
        {
            ev_key(decodeWindowsScancode(lParam, wParam),
                false, getAsyncMods());
        } break;

    case WM_GETDLGCODE: // work around for some hosts
        return DLGC_WANTALLKEYS;


    // do the final destruction in WM_NCDESTROY?
    case WM_NCDESTROY:
        SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
        delete this;
        break;

    // we could do it here too, but whatever
    case WM_DESTROY: break;

    default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

#include "app.h"

void Application::platformInit()
{
    // nop on Windows
}

void Application::platformClose()
{
    // nop on Windows
}

void Application::exit()
{
    PostQuitMessage(0);
}

void Application::run()
{
	app_startup();
	
    if(!nOpenWindow) return;
    MSG msg;
    while(GetMessage( &msg, 0, 0, 0 ))
    {
        DispatchMessage(&msg);
    }
}

bool dust::clipboard::setText(const char * buf, unsigned len)
{
    std::vector<char> tmp;
    tmp.reserve(len);
    for(int i = 0; i < len; ++i)
    {
        if(buf[i] == '\n') tmp.push_back('\r');
        tmp.push_back(buf[i]);
    }

    tmp.push_back(0);

    // supplying zero size for output makes to return required buffer
    unsigned needSize = ::MultiByteToWideChar(
        CP_UTF8, 0, tmp.data(), tmp.size(), 0, 0);

    // fail empty strings
    if(!needSize) return false;

    std::vector<WCHAR> wbuf;
    wbuf.resize(needSize);

    // call MultiByteToWideChar again to actually convert
    // should always get the same return value, for obvious reasons
    if(needSize != ::MultiByteToWideChar(
        CP_UTF8, 0, tmp.data(), tmp.size(), wbuf.data(), needSize))
    {
        return false;
    }

	if(!OpenClipboard(0)) return false;
	
	// get rid of existing contents
	bool error = !EmptyClipboard();

	if(!error)
	{
		HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, wbuf.size() * sizeof(WCHAR));
		if(!hg) error = true;
		else
		{
			void *ptr = GlobalLock(hg);
			if(!ptr)
			{
				error = true;
			}
			else
			{
				memcpy(ptr, &wbuf[0], wbuf.size()*sizeof(WCHAR));
				GlobalUnlock(hg);

				if(!SetClipboardData(CF_UNICODETEXT, hg)) error = true;
			}

			if(error) GlobalFree(hg);
		}
	}

	CloseClipboard();
	return !error;

}

bool dust::clipboard::getText(std::string & out)
{
	std::vector<char> wbuf;

	bool haveUnicode = 0 != IsClipboardFormatAvailable(CF_UNICODETEXT);
	bool haveText = haveUnicode || 0 != IsClipboardFormatAvailable(CF_TEXT);

	bool isOpen = haveText && (0 != OpenClipboard(0));
	if(isOpen)
	{
		if(haveUnicode)
		{
			HANDLE hdata = GetClipboardData(CF_UNICODETEXT);
			if(hdata)
			{
				wbuf.resize(GlobalSize(hdata));
				void * ptr = GlobalLock(hdata);
				if(ptr)
				{
					memcpy(&wbuf[0], ptr, wbuf.size());
				}
				else
				{
					haveText = false;
				}
			}
			else
			{
				haveText = false;
			}
		}
		else
		{
			// the joy of ANSI text
			std::vector<char> abuf;
			
			HANDLE hdata = GetClipboardData(CF_TEXT);
			if(hdata)
			{
				abuf.resize(GlobalSize(hdata));
				void * ptr = GlobalLock(hdata);
				if(ptr)
				{
					memcpy(&abuf[0], ptr, abuf.size());
				}
				else
				{
					haveText = false;
				}
			}
			else
			{
				haveText = false;
			}

			if(haveText)
			{
				// convert ansi to wchar 
				unsigned needSize = ::MultiByteToWideChar(CP_ACP, 0, 
					&abuf[0], abuf.size(), 0, 0);
				if(needSize)
				{
					wbuf.resize(needSize);
					::MultiByteToWideChar(CP_ACP, 0, 
						&abuf[0], abuf.size(), (WCHAR*)&wbuf[0], 
						wbuf.size() / sizeof(WCHAR));
				}
				else
				{
					haveText = false;
				}
			}
		}

		CloseClipboard();
	}

	// at this point if haveText is still true, then we should have
	// widechar string in wbuf and we need to convert to utf-8
	if(haveText)
	{
		std::vector<char> ubuf;
		unsigned needSize = ::WideCharToMultiByte(CP_UTF8, 0,
			(WCHAR*)&wbuf[0], wbuf.size() / sizeof(WCHAR), 0, 0, 0, 0);
		
		if(needSize)
		{
			ubuf.resize(needSize);
			::WideCharToMultiByte(CP_UTF8, 0,
				(WCHAR*)&wbuf[0], wbuf.size() / sizeof(WCHAR), 
				&ubuf[0], ubuf.size(), 0, 0);

			// and then ... we do CRLF dance
			bool crlf = false;
			for(int i = 0; i < needSize; ++i)
			{
				if(ubuf[i] == '\r') { crlf = true; continue; }
				if(ubuf[i] == '\n') crlf = false;
				if(crlf)
				{
					out = out + '\n';
				}
				// filter out embedded nulls and the final terminator
				if(!ubuf[i]) continue;
				out = out + ubuf[i];
			}

			return true;
		}
	}

	return false;
}	

#else
unsigned __platform_not_win32;  // dummy symbol to silences libtool
#endif
