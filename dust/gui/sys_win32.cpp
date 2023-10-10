
#ifdef _WIN32    // this file is only relevant on windows

// enables styles .. both lines are required
#define ISOLATION_AWARE_ENABLED 1
#pragma comment(linker,"/manifestdependency:\"type='win32' "\
    "name='Microsoft.Windows.Common-Controls' "\
    "version='6.0.0.0' processorArchitecture='*' "\
    "publicKeyToken='6595b64144ccf1df' language='*'\"")

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

#define UNICODE
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>     // for browser select (using ugly old dialog, whatever)
#include <shobjidl.h>   // for browser select (using ugly Vista+ COM stuff)

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

std::string dust::to_u8(wchar_t const * in, size_t inLen)
{
    unsigned needSize = ::WideCharToMultiByte(CP_UTF8, 0, in, inLen, 0, 0, 0, 0);
    std::string out(needSize, 0);
    if(needSize)
    {
        ::WideCharToMultiByte(CP_UTF8, 0, in, inLen, &out[0], out.size(), 0, 0);
        if(inLen == -1 && !out.back()) out.pop_back(); // don't need the null
    }
    return out;
}

std::wstring dust::to_u16(char const * in, size_t inLen)
{
    unsigned needSize = ::MultiByteToWideChar(CP_UTF8, 0, in, inLen, 0, 0);
    std::wstring out(needSize, 0);
    if(needSize)
    {
        ::MultiByteToWideChar(CP_UTF8, 0, in, inLen, &out[0], out.size());
        if(inLen == -1 && !out.back()) out.pop_back(); // don't need the null
    }
    return out;
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

struct WinDropHandler
{
    virtual bool drag_move(int x, int y) = 0;
    virtual void drag_exit() = 0;

    virtual dust::Panel * drag_get_panel(int x, int y) = 0;
    virtual void drag_drop(dust::Panel * panel, const char * path) = 0;
    
protected:
    ~WinDropHandler() {}
};

struct WinDropTarget : public IDropTarget
{
    WinDropTarget(WinDropHandler & handler) : handler(handler) {}
    virtual ~WinDropTarget() { }
    
protected:
    // IUnknown methods
    STDMETHOD(QueryInterface)(REFIID iid, void FAR* FAR* p)
    {
       if (iid == IID_IUnknown || iid == IID_IDropTarget)
       {
           *p = this; AddRef(); return NOERROR;
       }
       *p = NULL;
       return ResultFromScode(E_NOINTERFACE);
    }
    STDMETHOD_(ULONG, AddRef)()
    {
        return ++refCount;
    }
    STDMETHOD_(ULONG, Release)()
    {
        if(!--refCount) { delete this; return 0; } else return refCount;
    }
    
    // IDropTarget methods
    STDMETHOD(DragEnter)(LPDATAOBJECT pDataObj, DWORD grfKeyState,
                                         POINTL pt, LPDWORD pdwEffect)
    {
        FORMATETC fmt = {};
        fmt.cfFormat = CF_HDROP;
        fmt.ptd      = NULL;
        fmt.dwAspect = DVASPECT_CONTENT;
        fmt.lindex   = -1;
        fmt.tymed    = TYMED_HGLOBAL;
    
        // check if we can do CF_HDROP since we don't support other stuff
        acceptFormat = (pDataObj->QueryGetData(&fmt) == NOERROR);
        
        if(acceptFormat)
        {
            *pdwEffect = handler.drag_move(pt.x, pt.y)
                ? DROPEFFECT_MOVE : DROPEFFECT_NONE;
        }

        return NOERROR;
    }
    STDMETHOD(DragOver)(DWORD grfKeyState, POINTL pt, LPDWORD pdwEffect)
    {
        if(acceptFormat)
        {
            *pdwEffect = handler.drag_move(pt.x, pt.y)
                ? DROPEFFECT_MOVE : DROPEFFECT_NONE;
        }
        return NOERROR;
    }
    STDMETHOD(DragLeave)()
    {
        acceptFormat = false;
        handler.drag_exit();
        return NOERROR;
    }
    STDMETHOD(Drop)(LPDATAOBJECT pDataObj, DWORD grfKeyState,
                    POINTL pt, LPDWORD pdwEffect)
    {
        auto * panel = handler.drag_get_panel(pt.x, pt.y);
        if(!panel) { *pdwEffect = DROPEFFECT_NONE; return NOERROR; }
        
        FORMATETC fmt = {};
        fmt.cfFormat = CF_HDROP;
        fmt.ptd = NULL;
        fmt.dwAspect = DVASPECT_CONTENT;
        fmt.lindex = -1;
        fmt.tymed = TYMED_HGLOBAL;

        // get the CF_HDROP data from drag source
        STGMEDIUM medium;
        HRESULT hr = pDataObj->GetData(&fmt, &medium);
        if(!FAILED(hr))
        {
            // grab a pointer to the data
            HDROP hDrop = (HDROP)GlobalLock(medium.hGlobal);
            
            if(panel)
            {
                std::wstring buf;
                // get number of files
                auto nFiles = DragQueryFileW(hDrop, ~0u, 0, 0);
                for(unsigned i = 0; i < nFiles; ++i)
                {
                    auto fnLen = DragQueryFileW(hDrop, i, 0, 0);
                    if(!fnLen) continue;
    
                    buf.resize(fnLen+1);
                    if(!DragQueryFileW(hDrop, i, &buf[0], buf.size())) continue;
    
                    handler.drag_drop(panel, dust::to_u8(buf).c_str());
                }
            }
            DragFinish(hDrop);
            GlobalUnlock(medium.hGlobal);
            
            // ordinarily ReleaseStgMedium(&medium), but DragFinish does that
        }
        else
        {
            *pdwEffect = DROPEFFECT_NONE;
            return hr;
        }
        return NOERROR;
    }
private:
    WinDropHandler & handler;
    unsigned refCount = 1;
    bool acceptFormat = false;
};

struct Win32Callback
{
    virtual ~Win32Callback() {}
    virtual LRESULT callback(HWND, UINT, WPARAM, LPARAM) = 0;
};

static const wchar_t winClassName[] = L"Win32Wrapper";
static LRESULT CALLBACK wrapperWinProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    
    auto ctl = (Win32Callback *) GetWindowLongPtr(hwnd, GWLP_USERDATA);
    
    return ctl ? ctl->callback(hwnd, msg, wParam, lParam)
        : DefWindowProcW(hwnd, msg, wParam, lParam);
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

        WNDCLASSEX wc = {};
        wc.cbSize        = sizeof(wc);
        wc.style         = 0; // CS_DBLCLKS;
        wc.lpfnWndProc   = wrapperWinProc;
        wc.cbClsExtra    = 0;
        wc.cbWndExtra    = 0;
        wc.hInstance     = hInstance;
        wc.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
        wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = NULL;
        wc.lpszMenuName  = NULL;
        wc.lpszClassName = winClassName;
        wc.hIconSm       = LoadIcon(NULL, IDI_APPLICATION);

        winClassAtom = RegisterClassExW(&wc);
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
            UnregisterClassW((LPWSTR) winClassAtom, hInstance);
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

struct Win32Window : Window, Win32Callback, WinDropHandler
{
    WindowDelegate & delegate;

    HWND hwnd;
    HDC hdc;    // set in WM_PAINT (FIXME: in constructor for GL?)

#if DUST_USE_OPENGL
    HGLRC    hglrc;
#endif

    LPDROPTARGET iDropTarget = 0;
    
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
        hwnd = CreateWindowExW(ex_style,
            (LPWSTR)windowClass.winClassAtom,
            L"", style, CW_USEDEFAULT, CW_USEDEFAULT, w, h,
            (HWND) parent, (HMENU) 0, hInstance, 0);

        if(!hwnd) debugBreak(); // should never fail

        SetWindowLongPtr(hwnd, GWLP_USERDATA, 
            (LONG_PTR) (Win32Callback*) this);
        delegate.win_created();

        // we need this for drag&drop but it also gets us COM for openDir
        OleInitialize(NULL);
        if(delegate.win_can_dropfiles())
        {
            iDropTarget = new WinDropTarget(*this);
            CoLockObjectExternal(iDropTarget, true, true);
            RegisterDragDrop(hwnd, iDropTarget);
        }

        // this will fix title-bar
        if(!parent) resize(w, h);
        
        ::SetTimer(hwnd, 0, 1000/60, 0);

#if DUST_USE_OPENGL

        hdc = GetDC(hwnd);

        // opengl pixel format
        PIXELFORMATDESCRIPTOR pfd = {};
        
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

        if(iDropTarget)
        {
            RevokeDragDrop(hwnd);
            iDropTarget->Release();
            CoLockObjectExternal(iDropTarget, false, true);
        }
        OleUninitialize();
        
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

    bool drag_move(int x, int y)
    {
        POINT p;
        p.x = x;
        p.y = y;
        ScreenToClient(hwnd, &p);
        MouseEvent ev(MouseEvent::tDragFiles, p.x, p.y, 0, 0, getAsyncMods());
        sendMouseEvent(ev);
        auto * panel = getMouseTrack();
        return panel && panel->ev_accept_files();
    }
    void drag_exit() { sendMouseExit(); }
    dust::Panel * drag_get_panel(int x, int y)
    {
        MouseEvent ev(MouseEvent::tDragFiles, x, y, 0, 0, getAsyncMods());
        sendMouseEvent(ev);
        auto * panel = getMouseTrack();
        sendMouseExit();
        
        if(!panel || !panel->ev_accept_files()) return 0;
        return panel;        
    }
    void drag_drop(dust::Panel * panel, const char * path)
    {
        delegate.win_drop_file(panel, path);
    }
    
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

    void toggleMaximize()
    {
        WINDOWPLACEMENT wp = {};
        wp.length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(hwnd, &wp);

        switch(wp.showCmd)
        {
            case SW_MAXIMIZE: ShowWindow(hwnd, SW_RESTORE); break;
            case SW_RESTORE:
            case SW_NORMAL: ShowWindow(hwnd, SW_MAXIMIZE); break;
            default: break;
        }
    }
    
    void setTitle(const char * txt)
    {
        SetWindowText(hwnd, to_u16(txt).c_str());
    }
    
    void confirmClose(
        Notify saveAndClose, Notify close, Notify cancel)
    {
        // need one extra for the null
        std::vector<wchar_t> title(GetWindowTextLengthW(hwnd) + 1);
        GetWindowTextW(hwnd, title.data(), title.size());
        
        int id = MessageBoxW(hwnd, L"Save changes before closing?", title.data(),
            MB_ICONWARNING | MB_YESNOCANCEL | MB_TASKMODAL);

        switch(id)
        {
        case IDYES: saveAndClose(); break;
        case IDNO: close(); break;
        default: cancel(); break;
        }
    }

    // See openDialogCom below
    bool saveAsDialogCOM(std::string & out,
        Notify save, Notify cancel, const char * path)
    {
#define CHECK_HR(code) { HRESULT _hr = (code); if(!SUCCEEDED(_hr)) return true; }
        // We check this explicitly, because we can also use GetOpenFileName
        // which is available since forever, where as IFileDialog is Vista+
        IFileSaveDialog *dialog = NULL;
        HRESULT hr = (CoCreateInstance(CLSID_FileSaveDialog, 
                          NULL, 
                          CLSCTX_INPROC_SERVER, 
                          IID_PPV_ARGS(&dialog)));
        if(!SUCCEEDED(hr)) return false;
        dust_defer( dialog->Release() );

        // we'll call cancel() if whatever bad happens
        bool didSave = false;
        dust_defer( if(!didSave) cancel(); );

        {
            DWORD dwFlags;
            // Apparently we should always get default flags..
            CHECK_HR(dialog->GetOptions(&dwFlags));
            CHECK_HR(dialog->SetOptions(dwFlags
                | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR));
        }

        // For save dialog (but not open dialog) the box appears to be there always?!
        // We could theoretically take existing extension from previous path.
        COMDLG_FILTERSPEC fileTypes[1] = {
            {L"All files (*.*)", L"*.*"}
        };
        CHECK_HR(dialog->SetFileTypes(1, fileTypes));

        if(path)
        {
            IShellItem *initPath = 0;
            // don't bail out if this fails, we'll just skip it
            if(SUCCEEDED(
                SHCreateItemFromParsingName(
                    to_u16(path).c_str(), 0, IID_PPV_ARGS(&initPath))))
            {
                dialog->SetDefaultFolder(initPath);
                initPath->Release();
            }
        }
    
        // Show the dialog - this fails if user cancels
        CHECK_HR(dialog->Show(hwnd));

        // item array
        IShellItem *result;
        CHECK_HR(dialog->GetResult(&result));
        dust_defer( result->Release() );
        DUST_TRACE

        PWSTR pszFilePath = NULL;
        CHECK_HR(result->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath));
        dust_defer( CoTaskMemFree(pszFilePath) );
        DUST_TRACE

        // utf-8 dance: FIXME: refactor into a function
        out = to_u8(pszFilePath);
        if(out.size()) { save(); didSave = true; }
        
        return true;
#undef CHECK_HR
    }
    
    void saveAsDialog(std::string & out,
        Notify save, Notify cancel, const char * path)
    {
        if(saveAsDialogCOM(out, save, cancel, path)) return;

        wchar_t filename[_MAX_PATH] = {};

        OPENFILENAME ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;

        // filename garbage
        ofn.lpstrFile = filename;
        ofn.nMaxFile = sizeof(filename);

        if(path) wcscpy(filename, to_u16(path).c_str());

        ofn.lpstrFileTitle = 0;
        ofn.nMaxFileTitle = 0;

        // filter
        ofn.lpstrFilter = L"All files (*.*)\0*.*\0";
        ofn.nFilterIndex = 1;

        ofn.Flags = OFN_PATHMUSTEXIST;
        ofn.Flags |= OFN_OVERWRITEPROMPT;
        ofn.Flags |= OFN_NOCHANGEDIR;

        // if non-zero, user clicked OK
        // FIXME: should really use Unicode, but we need a file API then
        // because we can't just send utf-8 to fopen() and friends
        if(GetSaveFileName(&ofn))
        {
            out = to_u8(filename);
            save();
        }
        else
        {
            cancel();
        }
    }

    // This tries to use IFileDialog which is Vista+
    // If we can't create one (eg. on XP?) then return false
    // so that we can try using the legacy dialogs instead
    bool openDialogCOM(std::function<void(const char*)> open,
        bool multiple, bool canDir, const char * path)
    {
#define CHECK_HR(code) { HRESULT _hr = (code); if(!SUCCEEDED(_hr)) return true; }
        // We check this explicitly, because we can also use GetOpenFileName
        // which is available since forever, where as IFileDialog is Vista+
        IFileOpenDialog *dialog = NULL;
        HRESULT hr = (CoCreateInstance(CLSID_FileOpenDialog, 
                          NULL, 
                          CLSCTX_INPROC_SERVER, 
                          IID_PPV_ARGS(&dialog)));
        if(!SUCCEEDED(hr)) return false;
        dust_defer( dialog->Release() );

        {
            DWORD dwFlags;
            // Apparently we should always get default flags..
            CHECK_HR(dialog->GetOptions(&dwFlags));
            
            if(multiple) dwFlags |= FOS_ALLOWMULTISELECT;
            if(canDir) dwFlags |= FOS_PICKFOLDERS;
            
            CHECK_HR(dialog->SetOptions(dwFlags
                | FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR));
        }
        
        if(path)
        {
            IShellItem *initPath = 0;
            // don't bail out if this fails, we'll just skip it
            if(SUCCEEDED(
                SHCreateItemFromParsingName(
                    to_u16(path).c_str(), 0, IID_PPV_ARGS(&initPath))))
            {
                dialog->SetDefaultFolder(initPath);
                initPath->Release();
            }
        }
    
        // Show the dialog
        CHECK_HR(dialog->Show(hwnd));

        // item array
        IShellItemArray *results;
        CHECK_HR(dialog->GetResults(&results));
        dust_defer( results->Release() );

        DWORD nFiles;
        CHECK_HR(results->GetCount(&nFiles));

        for(int i = 0; i < nFiles; ++i)
        {
            IShellItem *item;
            CHECK_HR(results->GetItemAt(i, &item));
            dust_defer( item->Release() );
            
            PWSTR pszFilePath = NULL;
            CHECK_HR(item->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath));
            dust_defer( CoTaskMemFree(pszFilePath) );

            open(to_u8(pszFilePath).c_str());
        }
        return true;
#undef CHECK_HR
    }
    
    void openDialog(std::function<void(const char*)> open,
        bool multiple, const char * path)
    {
        // if this returns false, IFileDialog is probably not available
        if(openDialogCOM(open, multiple, false, path)) return;
        
        // So... let's try legacy instead
        std::vector<wchar_t>   filename(32*1024);

        OPENFILENAME ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;

        // filename garbage
        ofn.lpstrFile = filename.data();
        ofn.nMaxFile = filename.size();

        if(path) wcscpy(filename.data(), to_u16(path).c_str());

        ofn.lpstrFileTitle = 0;
        ofn.nMaxFileTitle = 0;

        // filter
        ofn.lpstrFilter = L"All files (*.*)\0*.*\0";
        ofn.nFilterIndex = 1;

        ofn.Flags = OFN_PATHMUSTEXIST | OFN_EXPLORER;
        // FIXME: need to figure out how to parse multiselect
        if(multiple) ofn.Flags |= OFN_ALLOWMULTISELECT;
        ofn.Flags |= OFN_NOCHANGEDIR;

        // if non-zero, user clicked OK
        // FIXME: should really use Unicode, but we need a file API then
        // because we can't just send utf-8 to fopen() and friends
        if(GetOpenFileName(&ofn))
        {
            // With multiselect we get directory followed by null
            // followed by file strings with double null at the end
            // So.. check if character before nFileOffset is null
            if(ofn.nFileOffset && !filename[ofn.nFileOffset-1])
            {
                // add a directory separator..
                filename[ofn.nFileOffset-1] = '\\';
                // loop the files
                int offset = ofn.nFileOffset;
                while(filename[ofn.nFileOffset])
                {
                    open(to_u8(filename.data()).c_str());
                    offset += wcslen(filename.data() + offset) + 1;
                    // inplace copy to after the directory
                    wcscpy(filename.data() + ofn.nFileOffset,
                            filename.data() + offset);
                }
            }
            else open(to_u8(filename.data()).c_str());
        }
    }

    void openDirDialog(
        std::function<void(const char*)> open, const char * path)
    {
        // if this returns false, IFileDialog is probably not available
        if(openDialogCOM(open, false, true, path)) return;
        
        // So.. we need to use the horrible SHBrowseForFolder
        wchar_t filename[_MAX_PATH] = {};

        BROWSEINFO bi = {};
        bi.hwndOwner = hwnd;
        bi.pszDisplayName = filename; // assumed to be MAX_PATH long :P
        bi.lpszTitle = L"";
        bi.ulFlags = BIF_USENEWUI;

        if(SHBrowseForFolder(&bi))
        {
            open(to_u8(filename).c_str());
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
            AppendMenu(hMenu,
                MF_STRING | (!enabled ? MF_DISABLED : 0) | (tick ? MF_CHECKED : 0),
                id, to_u16(txt).c_str());
        }
    
        void addSeparator()
        {
            AppendMenu(hMenu, MF_SEPARATOR, 0, 0);
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
            if(GetCapture()) { ReleaseCapture(); }
            
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

    HICON   windowIcon = 0;
    HICON   windowIconSmall = 0;

    void setIcon(Surface & icon)
    {
        // deal with pitch != sizeX
        Surface tmp;        
        Surface * src = &icon;
        if(icon.getSizeX() != icon.getPitch())
        {
            src = &tmp;
            tmp.validate(icon.getSizeX(), icon.getSizeY(), 1);
            if(tmp.getSizeX() != tmp.getPitch())
            {
                dust::debugPrint("setIcon(): internal error");
            }

            for(int y = 0; y < icon.getSizeY(); ++y)
            {
                for(int x = 0; x < icon.getSizeX(); ++x)
                {
                    tmp.getPixels()[x + tmp.getPitch()*y]
                        = icon.getPixels()[x + icon.getPitch()*y];
                }
            }
        }

        ICONINFO ii = { };
        ii.fIcon = TRUE;

        // FIXME: deal with pitch vs. sizeX
        ii.hbmColor = CreateBitmap(
            src->getPitch(), src->getSizeY(), 1, 32, src->getPixels());
        ii.hbmMask = ii.hbmColor;
        
        HICON newIcon = CreateIconIndirect(&ii);
        SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)newIcon);
        DeleteObject(ii.hbmColor);

        // Windows scales like garbage, so do it manually
        tmp.validate(icon.getSizeX() / 2, icon.getSizeY() / 2);
        for(int y = 0; y < tmp.getSizeY(); ++y)
        {
            for(int x = 0; x < tmp.getSizeX(); ++x)
            {
                auto A = color::lerp(
                    icon.getPixels()[icon.getPitch()*2*y + 2*x],
                    icon.getPixels()[icon.getPitch()*2*y + 2*x+1], 0x80);
                auto B = color::lerp(
                    icon.getPixels()[icon.getPitch()*(2*y+1) + 2*x],
                    icon.getPixels()[icon.getPitch()*(2*y+1) + 2*x+1], 0x80);
                    
                tmp.getPixels()[x + tmp.getPitch()*y] = color::lerp(A, B, 0x80);
            }
        }
        ii.hbmColor = CreateBitmap(
            tmp.getPitch(), tmp.getSizeY(), 1, 32, tmp.getPixels());
        ii.hbmMask = ii.hbmColor;
        HICON newIconSmall = CreateIconIndirect(&ii);
        SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)newIconSmall);
        DeleteObject(ii.hbmColor);
        
        if(windowIcon) DestroyIcon(windowIcon);
        if(windowIconSmall) DestroyIcon(windowIconSmall);
        windowIcon = newIcon;
        windowIconSmall = newIconSmall;
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
            DefWindowProc(hwnd, msg, wParam, lParam);
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
        //
        // NOTE: in some situations (eg. menu just closed) we can end up
        // getting capture changed notifications "losing focus" to ourselves
        // which we need to ignore, otherwise dragging doesn't start
        if((HWND) lParam != hwnd)
        {
            cancelDrag();
            sendMouseExit();
        }
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
            TRACKMOUSEEVENT tme = {};
            
            tme.cbSize = sizeof(tme);
            tme.dwFlags = TME_LEAVE;
            tme.hwndTrack = hwnd;

            TrackMouseEvent(&tme);
        }
        break;

    case WM_LBUTTONDOWN:
        {
            SetCapture(hwnd);
            
            int x = MAKEPOINTS(lParam).x;
            int y = MAKEPOINTS(lParam).y;

            int keymods = getAsyncMods();

            MouseEvent ev(MouseEvent::tDown, x, y, 1, 
                getClickCount(1, x, y), keymods);
            sendMouseEvent(ev);

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
            SetCapture(hwnd);
            
            int x = MAKEPOINTS(lParam).x;
            int y = MAKEPOINTS(lParam).y;

            int keymods = getAsyncMods();

            MouseEvent ev(MouseEvent::tDown, x, y, 2, 
                getClickCount(2, x, y), keymods);
            sendMouseEvent(ev);
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
            SetCapture(hwnd);
            
            int x = MAKEPOINTS(lParam).x;
            int y = MAKEPOINTS(lParam).y;

            int keymods = getAsyncMods();

            MouseEvent ev(MouseEvent::tDown, x, y, 3, 
                getClickCount(2, x, y), keymods);
            sendMouseEvent(ev);
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
        auto ubuf = to_u8((WCHAR*)wbuf.data(), wbuf.size()/sizeof(WCHAR));
        if(ubuf.size())
        {
            out.clear();
            out.reserve(ubuf.size());
          
            // and then ... we do CRLF dance
            bool crlf = false;
            for(int i = 0; i < ubuf.size(); ++i)
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
